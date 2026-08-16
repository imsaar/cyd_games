#include "color_fusion.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <new>

// ── Geometry ──
// Canvas buffer is w*h*2 bytes (RGB565). Kept modest — this device has no
// PSRAM and a ~200x170 buffer (68KB) is a much safer single allocation
// than the full draw-area footprint would be, on top of whatever WiFi/LVGL
// have already claimed from the heap.
static const int CANVAS_X = 0;
static const int CANVAS_Y = 0;
static const int CANVAS_W = 200;
static const int CANVAS_H = 170;
static const int BRUSH_R = 1;

// ── Palette (shared by both the stroke and fill pickers) ──
static const int NUM_COLORS = 6;
static const uint32_t palette_hex[NUM_COLORS] = {
    0x000000, 0xFF4444, 0x44CC44, 0x4488FF, 0xFFDD00, 0xFFFFFF
};
static lv_color_t palette_color(int idx) {
    return lv_color_hex(palette_hex[idx % NUM_COLORS]);
}

// A white (or near-white) swatch needs a dark selection ring instead of the
// usual white one, or the ring disappears against its own fill.
static lv_color_t swatch_border_color(int idx) {
    uint32_t hex = palette_hex[idx % NUM_COLORS];
    uint32_t r = (hex >> 16) & 0xFF, g = (hex >> 8) & 0xFF, b = hex & 0xFF;
    uint32_t luma = (r * 299 + g * 587 + b * 114) / 1000;
    return (luma > 180) ? lv_color_black() : lv_color_white();
}

enum Tool { TOOL_PEN = 0, TOOL_FILL = 1 };

static lv_obj_t*  screen_ = nullptr;
static lv_obj_t*  canvas_ = nullptr;
static lv_color_t* buf_ = nullptr;
static lv_obj_t*  btn_pen_ = nullptr;
static lv_obj_t*  btn_fill_ = nullptr;
static lv_obj_t*  btn_undo_ = nullptr;
static lv_obj_t*  btn_redo_ = nullptr;
static lv_obj_t*  stroke_swatches_[NUM_COLORS] = {};
static lv_obj_t*  fill_swatches_[NUM_COLORS] = {};

static Tool tool_ = TOOL_PEN;
static int  cur_stroke_ = 0;
static int  cur_fill_ = NUM_COLORS - 1;
static bool pen_down_ = false;
static int  last_x_ = -1;
static int  last_y_ = -1;

// ── Undo/redo history ──
// A full canvas snapshot per undo step would be ~68KB each — far too
// costly on a no-PSRAM ESP32 (see the crash this app already had from one
// such allocation). Instead, record each stroke/fill as a small replayable
// action; undo/redo just re-renders from a blank canvas through the active
// action count. Cheap (tens of bytes per action) and correct, since fills
// are replayed in original order against the same prior state each time.
static const int MAX_ACTIONS = 30;
static const int MAX_HIST_POINTS = 4000;

struct HistPoint { int16_t x, y; };
// Heap-allocated (like buf_) rather than static: a static array this size
// blew through the ESP32's fixed DRAM/BSS segment limit at link time — a
// much smaller, harder ceiling than the general free-heap budget.
static HistPoint* hist_points_ = nullptr;
static int hist_point_count_ = 0;

enum ActType { ACT_STROKE, ACT_FILL };
struct HistAction {
    ActType type;
    uint8_t color_idx;
    int16_t seed_x, seed_y; // fill only
    int pt_start, pt_count; // stroke only
};
static HistAction* actions_ = nullptr;
static int action_count_ = 0;   // applied/visible actions
static int action_stored_ = 0;  // total stored (>= action_count_ while redo is available)
static bool stroke_active_ = false;
static int  stroke_pt_start_ = 0;

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

static bool flood_fill(int sx, int sy, lv_color_t fill_color) {
    if (!buf_ || sx < 0 || sx >= CANVAS_W || sy < 0 || sy >= CANVAS_H) return false;
    lv_color_t target = buf_[sy * CANVAS_W + sx];
    if (target.full == fill_color.full) return false;

    const int MAX_STACK = 4096;
    FillSeed* stack = new (std::nothrow) FillSeed[MAX_STACK];
    if (!stack) return false;
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
    return true;
}

// ── Undo/redo helpers ──

static void hist_replay() {
    if (!buf_ || !actions_ || !hist_points_) return;
    lv_canvas_fill_bg(canvas_, lv_color_white(), LV_OPA_COVER);
    for (int a = 0; a < action_count_; a++) {
        const HistAction& act = actions_[a];
        if (act.type == ACT_STROKE) {
            lv_color_t c = palette_color(act.color_idx);
            for (int p = act.pt_start; p < act.pt_start + act.pt_count; p++) {
                if (p == act.pt_start) {
                    draw_dot(hist_points_[p].x, hist_points_[p].y, BRUSH_R, c);
                } else {
                    draw_stroke_line(hist_points_[p - 1].x, hist_points_[p - 1].y,
                                      hist_points_[p].x, hist_points_[p].y, BRUSH_R, c);
                }
            }
        } else {
            flood_fill(act.seed_x, act.seed_y, palette_color(act.color_idx));
        }
    }
    if (canvas_) lv_obj_invalidate(canvas_);
}

static void update_undo_redo_buttons() {
    if (btn_undo_) {
        if (action_count_ > 0) lv_obj_clear_state(btn_undo_, LV_STATE_DISABLED);
        else lv_obj_add_state(btn_undo_, LV_STATE_DISABLED);
    }
    if (btn_redo_) {
        if (action_count_ < action_stored_) lv_obj_clear_state(btn_redo_, LV_STATE_DISABLED);
        else lv_obj_add_state(btn_redo_, LV_STATE_DISABLED);
    }
}

// Discards any redo tail and rewinds the shared point cursor to match —
// call right before recording a brand-new action (standard undo/redo
// semantics: drawing something new after an undo drops the redo history).
static void hist_truncate_redo() {
    action_stored_ = action_count_;
    hist_point_count_ = (action_count_ == 0) ? 0
        : (actions_[action_count_ - 1].pt_start + actions_[action_count_ - 1].pt_count);
}

static void hist_add_point(int x, int y) {
    if (!hist_points_ || hist_point_count_ >= MAX_HIST_POINTS) return; // history unavailable/full; live drawing is unaffected
    hist_points_[hist_point_count_].x = (int16_t)x;
    hist_points_[hist_point_count_].y = (int16_t)y;
    hist_point_count_++;
}

static void hist_end_stroke(int color_idx, int pt_start) {
    if (!actions_) return;
    int pt_count = hist_point_count_ - pt_start;
    if (pt_count <= 0 || action_count_ >= MAX_ACTIONS) return;
    actions_[action_count_].type = ACT_STROKE;
    actions_[action_count_].color_idx = (uint8_t)color_idx;
    actions_[action_count_].pt_start = pt_start;
    actions_[action_count_].pt_count = pt_count;
    action_count_++;
    action_stored_ = action_count_;
    update_undo_redo_buttons();
}

static void hist_record_fill(int x, int y, int color_idx) {
    if (!actions_ || action_count_ >= MAX_ACTIONS) return;
    actions_[action_count_].type = ACT_FILL;
    actions_[action_count_].color_idx = (uint8_t)color_idx;
    actions_[action_count_].seed_x = (int16_t)x;
    actions_[action_count_].seed_y = (int16_t)y;
    actions_[action_count_].pt_start = 0;
    actions_[action_count_].pt_count = 0;
    action_count_++;
    action_stored_ = action_count_;
    update_undo_redo_buttons();
}

static void hist_reset() {
    action_count_ = 0;
    action_stored_ = 0;
    hist_point_count_ = 0;
    stroke_active_ = false;
    update_undo_redo_buttons();
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
    if (stroke_active_) {
        hist_end_stroke(cur_stroke_, stroke_pt_start_);
        stroke_active_ = false;
    }
    tool_ = (Tool)(intptr_t)lv_event_get_user_data(e);
    pen_down_ = false;
    update_tool_highlight();
}

static void undo_cb(lv_event_t*) {
    if (action_count_ <= 0) return;
    action_count_--;
    hist_replay();
    update_undo_redo_buttons();
}

static void redo_cb(lv_event_t*) {
    if (action_count_ >= action_stored_) return;
    action_count_++;
    hist_replay();
    update_undo_redo_buttons();
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
    hist_reset();
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
    hist_points_ = new (std::nothrow) HistPoint[MAX_HIST_POINTS];
    actions_ = new (std::nothrow) HistAction[MAX_ACTIONS];
    hist_reset();

    canvas_ = lv_canvas_create(screen_);
    lv_obj_set_pos(canvas_, CANVAS_X, CANVAS_Y);
    buf_ = new (std::nothrow) lv_color_t[CANVAS_W * CANVAS_H];
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
        lv_obj_set_style_border_color(sw, swatch_border_color(i), 0);
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
        lv_obj_set_style_border_color(sw, swatch_border_color(i), 0);
        lv_obj_set_style_border_width(sw, (i == cur_fill_) ? 2 : 0, 0);
        lv_obj_add_event_cb(sw, fill_color_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        fill_swatches_[i] = sw;
    }
    y += 46;

    lv_obj_t* clr = ui_create_btn(screen_, "Clear", 72, 24);
    lv_obj_set_pos(clr, tb_x, y);
    lv_obj_add_event_cb(clr, clear_cb, LV_EVENT_CLICKED, NULL);
    y += 28;

    btn_undo_ = ui_create_btn(screen_, LV_SYMBOL_LEFT, 34, 24);
    lv_obj_set_pos(btn_undo_, tb_x, y);
    lv_obj_add_event_cb(btn_undo_, undo_cb, LV_EVENT_CLICKED, NULL);

    btn_redo_ = ui_create_btn(screen_, LV_SYMBOL_RIGHT, 34, 24);
    lv_obj_set_pos(btn_redo_, tb_x + 38, y);
    lv_obj_add_event_cb(btn_redo_, redo_cb, LV_EVENT_CLICKED, NULL);

    update_undo_redo_buttons();

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
                hist_truncate_redo();
                stroke_active_ = true;
                stroke_pt_start_ = hist_point_count_;
                hist_add_point(lx, ly);
                draw_dot(lx, ly, BRUSH_R, c);
            } else {
                hist_add_point(lx, ly);
                draw_stroke_line(last_x_, last_y_, lx, ly, BRUSH_R, c);
            }
            last_x_ = lx;
            last_y_ = ly;
            pen_down_ = true;
            lv_obj_invalidate(canvas_);
        } else {
            if (!pen_down_) {
                if (flood_fill(lx, ly, palette_color(cur_fill_))) {
                    hist_truncate_redo();
                    hist_record_fill(lx, ly, cur_fill_);
                }
                lv_obj_invalidate(canvas_);
            }
            pen_down_ = true;
        }
    } else {
        if (stroke_active_) {
            hist_end_stroke(cur_stroke_, stroke_pt_start_);
            stroke_active_ = false;
        }
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
    if (hist_points_) {
        delete[] hist_points_;
        hist_points_ = nullptr;
    }
    if (actions_) {
        delete[] actions_;
        actions_ = nullptr;
    }
    canvas_ = nullptr;
    screen_ = nullptr;
    btn_pen_ = nullptr;
    btn_fill_ = nullptr;
    btn_undo_ = nullptr;
    btn_redo_ = nullptr;
    for (int i = 0; i < NUM_COLORS; i++) {
        stroke_swatches_[i] = nullptr;
        fill_swatches_[i] = nullptr;
    }
}
