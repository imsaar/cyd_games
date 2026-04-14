#pragma once
#include <lvgl.h>

void display_init();
void display_set_inverted(bool inverted);
lv_disp_t* display_get();
