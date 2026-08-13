#pragma once
#include <lvgl.h>
#include <stdint.h>

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

// "Light Mode" on this unit is implemented as a full hardware panel color
// inversion (see display_set_inverted) rather than a separate light
// palette, so every pixel's RGB value gets complemented at render time.
// That's invisible for ordinary UI colors (bg/text pairs stay high-contrast
// either way), but it breaks colors that carry an absolute, mode-independent
// meaning — e.g. a chess/checkers piece belonging to "the white player"
// must always look white, in Dark Mode and Light Mode alike. Wrap such a
// color in this helper (passing the 24-bit hex it should appear as) and it
// will be pre-complemented in Light Mode so the hardware inversion cancels
// out and the intended color is what's actually seen.
lv_color_t ui_absolute_color_hex(uint32_t hex);
