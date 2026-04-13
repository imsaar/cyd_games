/*
 * CYD Arcade — ESP32-2432S028 (Cheap Yellow Display)
 * Multi-game touchscreen gaming machine
 *
 * Games: Snake, Tic-Tac-Toe, Memory Match, Pong
 * Features: LVGL UI, OTA updates, UDP multiplayer discovery
 */

#include <Arduino.h>
#include "config.h"
#include "hal/display.h"
#include "hal/backlight.h"
#include "hal/led.h"
#include "hal/audio.h"
#include "net/wifi_manager.h"
#include "net/ota.h"
#include "net/discovery.h"
#include "ui/screen_manager.h"

void setup() {
    Serial.begin(115200);
    Serial.println("\n[CYD Arcade] Starting...");

    // Validate OTA partition early (prevents rollback on reboot)
    ota_validate_app();

    // Hardware init
    display_init();
    backlight_init();
    led_init();
    audio_init();

    // Brief LED flash to confirm boot
    led_set(0, 0, 50);

    // Network
    wifi_init();
    if (wifi_connected()) {
        ota_init();
        discovery_init();
        led_set(0, 50, 0);  // Green = connected
    } else {
        led_set(50, 0, 0);  // Red = no WiFi
    }

    // UI
    screen_manager_init();
    screen_manager_switch(SCREEN_MENU);

    delay(500);
    led_off();

    Serial.println("[CYD Arcade] Ready!");
}

void loop() {
    lv_timer_handler();
    screen_manager_update();

    if (wifi_connected()) {
        ota_loop();
        discovery_loop();
    }
    wifi_loop();

    delay(5);
}
