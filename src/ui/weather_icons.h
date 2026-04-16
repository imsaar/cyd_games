#pragma once
#include <lvgl.h>

// Create a weather icon widget (draws sun/cloud/rain/snow/thunder)
// Returns an lv_obj_t* of the given size. Set weather code to update.
lv_obj_t* weather_icon_create(lv_obj_t* parent, int size);
void      weather_icon_set_code(lv_obj_t* icon, int weather_code);
