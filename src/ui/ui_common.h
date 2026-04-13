#pragma once
#include <lvgl.h>

// Colors
#define UI_COLOR_BG       lv_color_hex(0x1a1a2e)
#define UI_COLOR_CARD     lv_color_hex(0x16213e)
#define UI_COLOR_PRIMARY  lv_color_hex(0x0f3460)
#define UI_COLOR_ACCENT   lv_color_hex(0xe94560)
#define UI_COLOR_TEXT     lv_color_hex(0xeaeaea)
#define UI_COLOR_DIM      lv_color_hex(0x888888)
#define UI_COLOR_SUCCESS  lv_color_hex(0x4ecca3)
#define UI_COLOR_WARNING  lv_color_hex(0xf0a500)

// Create a styled screen with dark background
lv_obj_t* ui_create_screen();

// Create a "Back" button in top-left corner, calls screen_manager_back_to_menu
lv_obj_t* ui_create_back_btn(lv_obj_t* parent);

// Create a styled button
lv_obj_t* ui_create_btn(lv_obj_t* parent, const char* text, lv_coord_t w, lv_coord_t h);

// Create a title label
lv_obj_t* ui_create_title(lv_obj_t* parent, const char* text);
