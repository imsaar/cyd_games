#include "color_fusion.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"

// ── Geometry ──
static const int CANVAS_X = 0;
static const int CANVAS_Y = 0;
static const int CANVAS_W = 240;
static const int CANVAS_H = 200;
static const int BRUSH_R = 1;

// ── Palette (shared by both the stroke and fill pickers) ──
static const int NUM_COLORS = 6;
static const uint32_t palette_hex[NUM_COLORS] = {
    0x000000, 0xFF4444, 0x44CC44, 0x4488FF, 0xFFDD00, 0xFFFFFF
};
static lv_color_t palette_color(int idx) {
    return lv_color_hex(palette_hex[idx % NUM_COLORS]);
}

enum Tool { TOOL_PEN = 0, TOOL_FILL = 1 };

static lv_obj_t*  screen_ = nullptr;
static lv_obj_t*  canvas_ = nullptr;
static lv_color_t* buf_ = nullptr;
static lv_obj_t*  btn_pen_ = nullptr;
static lv_obj_t*  btn_fill_ = nullptr;
static lv_obj_t*  stroke_swatches_[NUM_COLORS] = {};
static lv_obj_t*  fill_swatches_[NUM_COLORS] = {};

static Tool tool_ = TOOL_PEN;
static int  cur_stroke_ = 0;
static int  cur_fill_ = NUM_COLORS - 1;
static bool pen_down_ = false;
static int  last_x_ = -1;
static int  last_y_ = -1;

// ── Pixel-buffer drawing helpers ──

static inline void put_px(int x, int y, lv_color_t c) {
    if (!buf_ || x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) return;
    buf_[y * CANVAS_W + x] = c;
}

static void draw_dot(int cx, int cy, int r, lv_color_t c) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            put_px(cx + dx, cy + dy, c);
        }
    }
}

static void draw_stroke_line(int x0, int y0, int x1, int y1, int r, lv_color_t c) {
    int ddx = x1 - x0; if (ddx < 0) ddx = -ddx;
    int ddy = y1 - y0; if (ddy < 0) ddy = -ddy;
    int steps = ddx > ddy ? ddx : ddy;
    if (steps == 0) { draw_dot(x0, y0, r, c); return; }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        draw_dot(x, y, r, c);
    }
}

// Row-span flood fill. Filling from a point inside a closed stroke fills
// only the interior; filling from a point outside fills everything up to
// the stroke's outer edge — "inside or outside" falls out of this for free
// as long as the drawn boundary has no gaps.
struct FillSeed { int16_t x, y; };

static void flood_fill(int sx, int sy, lv_color_t fill_color) {
    if (!buf_ || sx < 0 || sx >= CANVAS_W || sy < 0 || sy >= CANVAS_H) return;
    lv_color_t target = buf_[sy * CANVAS_W + sx];
    if (target.full == fill_color.full) return;

    const int MAX_STACK = 4096;
    FillSeed* stack = new FillSeed[MAX_STACK];
    if (!stack) return;
    int sp = 0;
    stack[sp++] = { (int16_t)sx, (int16_t)sy };

    while (sp > 0) {
        FillSeed s = stack[--sp];
        int x = s.x, y = s.y;
        if (x < 0 || x >= CANVAS_W || y < 0 || y >= CANVAS_H) continue;
        if (buf_[y * CANVAS_W + x].full != target.full) continue;

        int xl = x;
        while (xl > 0 && buf_[y * CANVAS_W + (xl - 1)].full == target.full) xl--;
        int xr = x;
        while (xr < CANVAS_W - 1 && buf_[y * CANVAS_W + (xr + 1)].full == target.full) xr++;

        for (int i = xl; i <= xr; i++) buf_[y * CANVAS_W + i] = fill_color;

        int neighbor_rows[2] = { y - 1, y + 1 };
        for (int n = 0; n < 2; n++) {
            int ny = neighbor_rows[n];
            if (ny < 0 || ny >= CANVAS_H) continue;
            int i = xl;
            while (i <= xr) {
                if (buf_[ny * CANVAS_W + i].full == target.full) {
                    if (sp < MAX_STACK) stack[sp++] = { (int16_t)i, (int16_t)ny };
                    while (i <= xr && buf_[ny * CANVAS_W + i].full == target.full) i++;
                } else {
                    i++;
                }
            }
        }
    }
    delete[] stack;
}

// ── UI callbacks ──

static void update_tool_highlight() {
    if (btn_pen_) {
        lv_obj_set_style_border_width(btn_pen_, tool_ == TOOL_PEN ? 2 : 0, 0);
        lv_obj_set_style_border_color(btn_pen_, UI_COLOR_ACCENT, 0);
    }
    if (btn_fill_) {
        lv_obj_set_style_border_width(btn_fill_, tool_ == TOOL_FILL ? 2 : 0, 0);
        lv_obj_set_style_border_color(btn_fill_, UI_COLOR_ACCENT, 0);
    }
}

static void tool_cb(lv_event_t* e) {
    tool_ = (Tool)(intptr_t)lv_event_get_user_data(e);
    pen_down_ = false;
    update_tool_highlight();
}

static void stroke_color_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    cur_stroke_ = idx;
    for (int i = 0; i < NUM_COLORS; i++) {
        if (stroke_swatches_[i]) lv_obj_set_style_border_width(stroke_swatches_[i], (i == idx) ? 2 : 0, 0);
    }
}

static void fill_color_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    cur_fill_ = idx;
    for (int i = 0; i < NUM_COLORS; i++) {
        if (fill_swatches_[i]) lv_obj_set_style_border_width(fill_swatches_[i], (i == idx) ? 2 : 0, 0);
    }
}

static void clear_cb(lv_event_t*) {
    if (canvas_) {
        lv_canvas_fill_bg(canvas_, lv_color_white(), LV_OPA_COVER);
        lv_obj_invalidate(canvas_);
    }
    pen_down_ = false;
}

static void back_cb(lv_event_t*) {
    screen_manager_back_to_menu();
}

// ── Lifecycle ──

lv_obj_t* color_fusion_create() {
    screen_ = ui_create_screen();
    lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);

    tool_ = TOOL_PEN;
    cur_stroke_ = 0;
    cur_fill_ = NUM_COLORS - 1;
    pen_down_ = false;
    last_x_ = -1;
    last_y_ = -1;

    canvas_ = lv_canvas_create(screen_);
    lv_obj_set_pos(canvas_, CANVAS_X, CANVAS_Y);
    buf_ = new lv_color_t[CANVAS_W * CANVAS_H];
    if (buf_) {
        lv_canvas_set_buffer(canvas_, buf_, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
        lv_canvas_fill_bg(canvas_, lv_color_white(), LV_OPA_COVER);
    } else {
        lv_obj_t* err = lv_label_create(screen_);
        lv_label_set_text(err, "Out of memory");
        lv_obj_set_style_text_color(err, UI_COLOR_ACCENT, 0);
        lv_obj_center(err);
    }

    int tb_x = CANVAS_X + CANVAS_W + 4;
    int y = 4;

    // Back button
    {
        lv_obj_t* btn = lv_btn_create(screen_);
        lv_obj_set_size(btn, 64, 24);
        lv_obj_set_pos(btn, tb_x, y);
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    }
    y += 30;

    // Tool toggle: Pen / Fill (bucket)
    btn_pen_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_pen_, 32, 22);
    lv_obj_set_pos(btn_pen_, tb_x, y);
    lv_obj_set_style_bg_color(btn_pen_, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(btn_pen_, 5, 0);
    {
        lv_obj_t* lbl = lv_label_create(btn_pen_);
        lv_label_set_text(lbl, LV_SYMBOL_EDIT);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_pen_, tool_cb, LV_EVENT_CLICKED, (void*)(intptr_t)TOOL_PEN);

    btn_fill_ = lv_btn_create(screen_);
    lv_obj_set_size(btn_fill_, 36, 22);
    lv_obj_set_pos(btn_fill_, tb_x + 36, y);
    lv_obj_set_style_bg_color(btn_fill_, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_radius(btn_fill_, 5, 0);
    {
        lv_obj_t* lbl = lv_label_create(btn_fill_);
        lv_label_set_text(lbl, LV_SYMBOL_TINT);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_center(lbl);
    }
    lv_obj_add_event_cb(btn_fill_, tool_cb, LV_EVENT_CLICKED, (void*)(intptr_t)TOOL_FILL);
    update_tool_highlight();
    y += 28;

    // Stroke color picker
    lv_obj_t* lbl_stroke = lv_label_create(screen_);
    lv_label_set_text(lbl_stroke, "Stroke");
    lv_obj_set_style_text_color(lbl_stroke, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_stroke, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_stroke, tb_x, y);
    y += 14;

    for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % 3, row = i / 3;
        lv_obj_t* sw = lv_btn_create(screen_);
        lv_obj_set_size(sw, 22, 18);
        lv_obj_set_pos(sw, tb_x + col * 25, y + row * 21);
        lv_obj_set_style_bg_color(sw, palette_color(i), 0);
        lv_obj_set_style_radius(sw, 3, 0);
        lv_obj_set_style_shadow_width(sw, 0, 0);
        lv_obj_set_style_border_color(sw, lv_color_white(), 0);
        lv_obj_set_style_border_width(sw, (i == cur_stroke_) ? 2 : 0, 0);
        lv_obj_add_event_cb(sw, stroke_color_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        stroke_swatches_[i] = sw;
    }
    y += 46;

    // Fill color picker
    lv_obj_t* lbl_fill = lv_label_create(screen_);
    lv_label_set_text(lbl_fill, "Fill");
    lv_obj_set_style_text_color(lbl_fill, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_fill, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_fill, tb_x, y);
    y += 14;

    for (int i = 0; i < NUM_COLORS; i++) {
        int col = i % 3, row = i / 3;
        lv_obj_t* sw = lv_btn_create(screen_);
        lv_obj_set_size(sw, 22, 18);
        lv_obj_set_pos(sw, tb_x + col * 25, y + row * 21);
        lv_obj_set_style_bg_color(sw, palette_color(i), 0);
        lv_obj_set_style_radius(sw, 3, 0);
        lv_obj_set_style_shadow_width(sw, 0, 0);
        lv_obj_set_style_border_color(sw, lv_color_white(), 0);
        lv_obj_set_style_border_width(sw, (i == cur_fill_) ? 2 : 0, 0);
        lv_obj_add_event_cb(sw, fill_color_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        fill_swatches_[i] = sw;
    }
    y += 46;

    lv_obj_t* clr = ui_create_btn(screen_, "Clear", 72, 24);
    lv_obj_set_pos(clr, tb_x, y);
    lv_obj_add_event_cb(clr, clear_cb, LV_EVENT_CLICKED, NULL);

    return screen_;
}

void color_fusion_update() {
    if (!canvas_ || !buf_) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) {
        indev = lv_indev_get_next(NULL);
        while (indev && lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER) {
            indev = lv_indev_get_next(indev);
        }
    }
    if (!indev) return;

    bool pressed = (indev->proc.state == LV_INDEV_STATE_PRESSED);
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int lx = p.x - CANVAS_X;
    int ly = p.y - CANVAS_Y;
    bool in_bounds = (lx >= 0 && lx < CANVAS_W && ly >= 0 && ly < CANVAS_H);

    if (pressed && in_bounds) {
        if (tool_ == TOOL_PEN) {
            lv_color_t c = palette_color(cur_stroke_);
            if (!pen_down_) {
                draw_dot(lx, ly, BRUSH_R, c);
            } else {
                draw_stroke_line(last_x_, last_y_, lx, ly, BRUSH_R, c);
            }
            last_x_ = lx;
            last_y_ = ly;
            pen_down_ = true;
            lv_obj_invalidate(canvas_);
        } else {
            if (!pen_down_) {
                flood_fill(lx, ly, palette_color(cur_fill_));
                lv_obj_invalidate(canvas_);
            }
            pen_down_ = true;
        }
    } else {
        pen_down_ = false;
        last_x_ = -1;
        last_y_ = -1;
    }
}

void color_fusion_destroy() {
    if (buf_) {
        delete[] buf_;
        buf_ = nullptr;
    }
    canvas_ = nullptr;
    screen_ = nullptr;
    btn_pen_ = nullptr;
    btn_fill_ = nullptr;
    for (int i = 0; i < NUM_COLORS; i++) {
        stroke_swatches_[i] = nullptr;
        fill_swatches_[i] = nullptr;
    }
}
