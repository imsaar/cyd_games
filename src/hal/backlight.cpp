#include "backlight.h"
#include "config.h"
#include <Arduino.h>

#define BL_CH  7  // LEDC channel for backlight

void backlight_init() {
    ledcSetup(BL_CH, 20000, 8);
    ledcAttachPin(TFT_BL_PIN, BL_CH);
    ledcWrite(BL_CH, 255);  // Full brightness
}

void backlight_set(uint8_t brightness) {
    ledcWrite(BL_CH, brightness);
}
