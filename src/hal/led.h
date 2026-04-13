#pragma once
#include <stdint.h>

void led_init();
void led_set(uint8_t r, uint8_t g, uint8_t b);  // 0-255 each
void led_off();
