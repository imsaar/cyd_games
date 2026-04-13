#include "wifi_manager.h"
#include "secrets.h"
#include <WiFi.h>
#include <Arduino.h>

static uint32_t last_attempt = 0;

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
    return WiFi.status() == WL_CONNECTED;
}

void wifi_loop() {
    if (WiFi.status() != WL_CONNECTED && millis() - last_attempt > 15000) {
        last_attempt = millis();
        Serial.println("[WiFi] Reconnecting...");
        WiFi.reconnect();
    }
}
