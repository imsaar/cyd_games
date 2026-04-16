#include "wifi_manager.h"
#include "../hal/prefs.h"
#include <WiFi.h>
#include <Arduino.h>

#if __has_include("secrets.h")
#include "secrets.h"
#endif

static uint32_t last_attempt = 0;
static bool disabled = false;
static char current_ssid[64] = "";
static char current_pass[64] = "";

static WifiTestState test_state = WIFI_TEST_IDLE;
static uint32_t      test_start_ms = 0;
static const uint32_t TEST_TIMEOUT_MS = 12000;

static void load_credentials() {
    prefs_get_wifi_ssid(current_ssid, sizeof(current_ssid));
    prefs_get_wifi_pass(current_pass, sizeof(current_pass));
#if defined(WIFI_SSID) && defined(WIFI_PASSWORD)
    if (current_ssid[0] == '\0') {
        strncpy(current_ssid, WIFI_SSID, sizeof(current_ssid) - 1);
        current_ssid[sizeof(current_ssid) - 1] = '\0';
        strncpy(current_pass, WIFI_PASSWORD, sizeof(current_pass) - 1);
        current_pass[sizeof(current_pass) - 1] = '\0';
    }
#endif
}

bool wifi_has_credentials() {
    return current_ssid[0] != '\0';
}

void wifi_get_ssid(char* buf, size_t len) {
    if (len == 0) return;
    strncpy(buf, current_ssid, len - 1);
    buf[len - 1] = '\0';
}

void wifi_init() {
    WiFi.mode(WIFI_STA);
    load_credentials();

    if (!wifi_has_credentials()) {
        Serial.println("[WiFi] No credentials configured");
        return;
    }

    WiFi.begin(current_ssid, current_pass);
    Serial.printf("[WiFi] Connecting to %s", current_ssid);

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
    if (wifi_has_credentials()) {
        WiFi.begin(current_ssid, current_pass);
    }
    Serial.println("[WiFi] Re-enabling...");
}

void wifi_loop() {
    if (disabled) return;
    if (!wifi_has_credentials()) return;
    if (test_state == WIFI_TEST_CONNECTING) return;  // Don't fight the test
    if (WiFi.status() != WL_CONNECTED && millis() - last_attempt > 15000) {
        last_attempt = millis();
        Serial.println("[WiFi] Reconnecting...");
        WiFi.reconnect();
    }
}

void wifi_save_credentials(const char* ssid, const char* pass) {
    if (!ssid) ssid = "";
    if (!pass) pass = "";

    strncpy(current_ssid, ssid, sizeof(current_ssid) - 1);
    current_ssid[sizeof(current_ssid) - 1] = '\0';
    strncpy(current_pass, pass, sizeof(current_pass) - 1);
    current_pass[sizeof(current_pass) - 1] = '\0';

    prefs_set_wifi_ssid(current_ssid);
    prefs_set_wifi_pass(current_pass);

    disabled = false;
    test_state = WIFI_TEST_IDLE;

    WiFi.disconnect(false);
    if (current_ssid[0] != '\0') {
        WiFi.begin(current_ssid, current_pass);
    }
    Serial.printf("[WiFi] Saved credentials, reconnecting to %s\n", current_ssid);
}

void wifi_test_start(const char* ssid, const char* pass) {
    if (!ssid || ssid[0] == '\0') return;
    Serial.printf("[WiFi] Testing %s\n", ssid);
    test_state    = WIFI_TEST_CONNECTING;
    test_start_ms = millis();
    WiFi.disconnect(false);
    WiFi.begin(ssid, pass ? pass : "");
}

WifiTestState wifi_test_state() {
    return test_state;
}

void wifi_test_tick() {
    if (test_state != WIFI_TEST_CONNECTING) return;
    wl_status_t s = WiFi.status();
    if (s == WL_CONNECTED) {
        test_state = WIFI_TEST_SUCCESS;
        Serial.printf("[WiFi] Test connected: %s\n", WiFi.localIP().toString().c_str());
    } else if (millis() - test_start_ms > TEST_TIMEOUT_MS) {
        test_state = WIFI_TEST_FAILED;
        Serial.println("[WiFi] Test failed (timeout)");
    }
}

void wifi_test_cancel() {
    if (test_state == WIFI_TEST_IDLE) return;
    test_state = WIFI_TEST_IDLE;
    WiFi.disconnect(false);
    if (wifi_has_credentials() && !disabled) {
        WiFi.begin(current_ssid, current_pass);
    }
}
