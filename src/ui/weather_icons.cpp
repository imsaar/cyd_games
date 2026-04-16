#include "weather_icons.h"
#include "ui_common.h"
#include <math.h>

// Weather code stored as user_data on the object
static void draw_sun(lv_draw_ctx_t* ctx, int cx, int cy, int r, lv_color_t col) {
    // Filled circle center
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = col;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;
    lv_area_t ca = {(lv_coord_t)(cx - r/3), (lv_coord_t)(cy - r/3),
                    (lv_coord_t)(cx + r/3), (lv_coord_t)(cy + r/3)};
    lv_draw_rect(ctx, &rdsc, &ca);

    // 8 rays
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = col;
    ldsc.width = 2;
    for (int i = 0; i < 8; i++) {
        float angle = i * 3.14159f / 4.0f;
        int inner = r * 45 / 100;
        int outer = r * 90 / 100;
        lv_point_t p1 = {(lv_coord_t)(cx + inner * cosf(angle)),
                         (lv_coord_t)(cy + inner * sinf(angle))};
        lv_point_t p2 = {(lv_coord_t)(cx + outer * cosf(angle)),
                         (lv_coord_t)(cy + outer * sinf(angle))};
        lv_draw_line(ctx, &ldsc, &p1, &p2);
    }
}

static void draw_cloud(lv_draw_ctx_t* ctx, int cx, int cy, int r, lv_color_t col) {
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = col;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;

    int s = r * 35 / 100;
    // Three overlapping circles forming a cloud shape
    lv_area_t c1 = {(lv_coord_t)(cx - s - s/2), (lv_coord_t)(cy - s/3),
                    (lv_coord_t)(cx - s/2 + s/2), (lv_coord_t)(cy + s*2/3)};
    lv_draw_rect(ctx, &rdsc, &c1);

    lv_area_t c2 = {(lv_coord_t)(cx - s/2), (lv_coord_t)(cy - s),
                    (lv_coord_t)(cx + s/2), (lv_coord_t)(cy + s/3)};
    lv_draw_rect(ctx, &rdsc, &c2);

    lv_area_t c3 = {(lv_coord_t)(cx + s/4), (lv_coord_t)(cy - s/2),
                    (lv_coord_t)(cx + s + s/2), (lv_coord_t)(cy + s/2)};
    lv_draw_rect(ctx, &rdsc, &c3);

    // Base rectangle connecting them
    lv_area_t base = {(lv_coord_t)(cx - s - s/4), (lv_coord_t)(cy),
                      (lv_coord_t)(cx + s + s/4), (lv_coord_t)(cy + s/2)};
    rdsc.radius = 2;
    lv_draw_rect(ctx, &rdsc, &base);
}

static void draw_rain(lv_draw_ctx_t* ctx, int cx, int cy, int r, lv_color_t cloud_col, lv_color_t rain_col) {
    draw_cloud(ctx, cx, cy - r/4, r, cloud_col);
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = rain_col;
    ldsc.width = 2;
    int drops[][2] = {{-4, 2}, {2, 4}, {8, 1}};
    for (auto& d : drops) {
        lv_point_t p1 = {(lv_coord_t)(cx + d[0]), (lv_coord_t)(cy + r/4 + d[1])};
        lv_point_t p2 = {(lv_coord_t)(cx + d[0] - 1), (lv_coord_t)(cy + r*3/4 + d[1])};
        lv_draw_line(ctx, &ldsc, &p1, &p2);
    }
}

static void draw_snow(lv_draw_ctx_t* ctx, int cx, int cy, int r, lv_color_t cloud_col, lv_color_t snow_col) {
    draw_cloud(ctx, cx, cy - r/4, r, cloud_col);
    lv_draw_rect_dsc_t rdsc;
    lv_draw_rect_dsc_init(&rdsc);
    rdsc.bg_color = snow_col;
    rdsc.bg_opa = LV_OPA_COVER;
    rdsc.radius = LV_RADIUS_CIRCLE;
    rdsc.border_width = 0;
    int dots[][2] = {{-5, 3}, {3, 5}, {-2, 8}, {6, 2}};
    for (auto& d : dots) {
        lv_area_t a = {(lv_coord_t)(cx + d[0] - 1), (lv_coord_t)(cy + r/4 + d[1] - 1),
                       (lv_coord_t)(cx + d[0] + 1), (lv_coord_t)(cy + r/4 + d[1] + 1)};
        lv_draw_rect(ctx, &rdsc, &a);
    }
}

static void draw_thunder(lv_draw_ctx_t* ctx, int cx, int cy, int r, lv_color_t cloud_col, lv_color_t bolt_col) {
    draw_cloud(ctx, cx, cy - r/4, r, cloud_col);
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = bolt_col;
    ldsc.width = 2;
    // Lightning bolt zig-zag
    lv_point_t pts[] = {
        {(lv_coord_t)(cx + 1), (lv_coord_t)(cy + r/6)},
        {(lv_coord_t)(cx - 3), (lv_coord_t)(cy + r/3)},
        {(lv_coord_t)(cx + 3), (lv_coord_t)(cy + r/2)},
        {(lv_coord_t)(cx - 1), (lv_coord_t)(cy + r*3/4)}
    };
    for (int i = 0; i < 3; i++) {
        lv_draw_line(ctx, &ldsc, &pts[i], &pts[i + 1]);
    }
}

static void draw_fog(lv_draw_ctx_t* ctx, int cx, int cy, int r, lv_color_t col) {
    lv_draw_line_dsc_t ldsc;
    lv_draw_line_dsc_init(&ldsc);
    ldsc.color = col;
    ldsc.width = 2;
    ldsc.round_start = 1;
    ldsc.round_end = 1;
    for (int i = 0; i < 4; i++) {
        int y = cy - r/3 + i * r/4;
        int w = r * (60 + (i % 2) * 20) / 100;
        lv_point_t p1 = {(lv_coord_t)(cx - w), (lv_coord_t)y};
        lv_point_t p2 = {(lv_coord_t)(cx + w), (lv_coord_t)y};
        lv_draw_line(ctx, &ldsc, &p1, &p2);
    }
}

// ── Draw callback ──

static void icon_draw_cb(lv_event_t* e) {
    lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
    lv_obj_t* obj = lv_event_get_target(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    int w = a.x2 - a.x1;
    int h = a.y2 - a.y1;
    int cx = a.x1 + w / 2;
    int cy = a.y1 + h / 2;
    int r = (w < h ? w : h) / 2;

    int code = (int)(intptr_t)lv_obj_get_user_data(obj);

    lv_color_t yellow  = lv_color_hex(0xf0c000);
    lv_color_t gray    = lv_color_hex(0x99aabb);
    lv_color_t blue    = lv_color_hex(0x4488ff);
    lv_color_t white   = lv_color_hex(0xccddee);
    lv_color_t bolt_y  = lv_color_hex(0xffdd00);
    lv_color_t fog_c   = lv_color_hex(0x778899);

    if (code == 0) {
        draw_sun(ctx, cx, cy, r, yellow);
    } else if (code <= 3) {
        draw_cloud(ctx, cx, cy, r, gray);
    } else if (code <= 48) {
        draw_fog(ctx, cx, cy, r, fog_c);
    } else if (code <= 67) {
        draw_rain(ctx, cx, cy, r, gray, blue);
    } else if (code <= 77) {
        draw_snow(ctx, cx, cy, r, gray, white);
    } else if (code <= 86) {
        draw_rain(ctx, cx, cy, r, gray, blue);
    } else {
        draw_thunder(ctx, cx, cy, r, gray, bolt_y);
    }
}

// ── Public API ──

lv_obj_t* weather_icon_create(lv_obj_t* parent, int size) {
    lv_obj_t* obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_size(obj, size, size);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_user_data(obj, (void*)(intptr_t)0);
    lv_obj_add_event_cb(obj, icon_draw_cb, LV_EVENT_DRAW_POST, NULL);
    return obj;
}

void weather_icon_set_code(lv_obj_t* icon, int weather_code) {
    lv_obj_set_user_data(icon, (void*)(intptr_t)weather_code);
    lv_obj_invalidate(icon);
}
