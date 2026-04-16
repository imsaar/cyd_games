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
#include "hal/prefs.h"
#include "hal/led.h"
#include "hal/audio.h"
#include "hal/sound.h"
#include "utils/alert_state.h"
#include "utils/alert_overlay.h"
#include "net/wifi_manager.h"
#include "net/ota.h"
#include "net/discovery.h"
#include "net/ntp_time.h"
#include "net/weather.h"
#include "ui/screen_manager.h"

void setup() {
    Serial.begin(115200);
    Serial.println("\n[CYD Arcade] Starting...");

    // Validate OTA partition early (prevents rollback on reboot)
    ota_validate_app();

    // Hardware init
    display_init();
    backlight_init();
    prefs_init();
    led_init();
    audio_init();
    sound_init();
    alert_state_init();

    // Restore saved display preferences
    backlight_set(prefs_get_brightness());
    display_set_inverted(prefs_get_inverted());

    // Brief LED flash to confirm boot
    led_set(0, 0, 50);

    // Network
    wifi_init();
    if (wifi_connected()) {
        ota_init();
        ntp_init();
        led_set(0, 50, 0);  // Green = WiFi connected
    } else {
        led_set(50, 50, 0); // Yellow = ESP-NOW mode
    }
    discovery_init();  // Works in both UDP and ESP-NOW modes
    weather_init();

    // UI
    screen_manager_init();
    screen_manager_switch(SCREEN_MENU);

    sound_startup();

    delay(500);
    led_off();

    Serial.println("[CYD Arcade] Ready!");
}

void loop() {
    lv_timer_handler();
    screen_manager_update();

    if (wifi_connected()) {
        ota_loop();
    }
    discovery_loop();  // Always run — works in both UDP and ESP-NOW
    weather_update();
    if (!wifi_disabled()) {
        wifi_loop();
    }

    sound_update();

    // Global alert checks (timer/alarm fire even when in other apps)
    alert_state_check();
    if (!alert_overlay_active()) {
        if (alert_state_timer_just_fired()) alert_overlay_show_timer();
        if (alert_state_alarm_just_fired()) alert_overlay_show_alarm();
    }

    delay(5);
}
