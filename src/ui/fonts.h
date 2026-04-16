#pragma once
#include <lvgl.h>

// Custom large digit fonts (0-9 : . space only)
// Fallback to montserrat_48 for any missing glyphs
LV_FONT_DECLARE(font_digit_72);
LV_FONT_DECLARE(font_digit_96);
