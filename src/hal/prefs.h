#pragma once
#include <stdint.h>

void     prefs_init();
uint8_t  prefs_get_brightness();
void     prefs_set_brightness(uint8_t val);
bool     prefs_get_inverted();
void     prefs_set_inverted(bool val);
