#include "led.h"
#include "config.h"
#include <Arduino.h>

void led_init() {
    pinMode(LED_R_PIN, OUTPUT);
    pinMode(LED_G_PIN, OUTPUT);
    pinMode(LED_B_PIN, OUTPUT);
    led_off();
}

void led_set(uint8_t r, uint8_t g, uint8_t b) {
    // Active LOW: 255 = off, 0 = full brightness
    analogWrite(LED_R_PIN, 255 - r);
    analogWrite(LED_G_PIN, 255 - g);
    analogWrite(LED_B_PIN, 255 - b);
}

void led_off() {
    digitalWrite(LED_R_PIN, HIGH);
    digitalWrite(LED_G_PIN, HIGH);
    digitalWrite(LED_B_PIN, HIGH);
}
