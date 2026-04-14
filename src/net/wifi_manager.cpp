#include "wifi_manager.h"
#include "secrets.h"
#include <WiFi.h>
#include <Arduino.h>

static uint32_t last_attempt = 0;
static bool disabled = false;

void wifi_init() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[WiFi] Connecting to %s", WIFI_SSID);

    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
        delay(250);
        Serial.print(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] Connected: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\n[WiFi] Connection failed — will retry in background");
    }
}

bool wifi_connected() {
    return !disabled && WiFi.status() == WL_CONNECTED;
}

bool wifi_disabled() {
    return disabled;
}

void wifi_disable() {
    disabled = true;
    WiFi.disconnect(false);  // keep STA mode active for ESP-NOW
    Serial.println("[WiFi] Disabled (ESP-NOW mode)");
}

void wifi_enable() {
    disabled = false;
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.println("[WiFi] Re-enabling...");
}

void wifi_loop() {
    if (disabled) return;
    if (WiFi.status() != WL_CONNECTED && millis() - last_attempt > 15000) {
        last_attempt = millis();
        Serial.println("[WiFi] Reconnecting...");
        WiFi.reconnect();
    }
}
