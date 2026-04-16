#pragma once
#include <stddef.h>

void wifi_init();
bool wifi_connected();
void wifi_loop();  // Handle reconnection
void wifi_disable();  // Disconnect and stop reconnection (not persisted)
bool wifi_disabled();
void wifi_enable();   // Re-enable and reconnect

// Credential management
bool wifi_has_credentials();
void wifi_get_ssid(char* buf, size_t len);
void wifi_save_credentials(const char* ssid, const char* pass);  // Persist + reconnect

// Non-blocking connection test with candidate credentials.
// Kicks off a fresh connection attempt without persisting.
enum WifiTestState {
    WIFI_TEST_IDLE,
    WIFI_TEST_CONNECTING,
    WIFI_TEST_SUCCESS,
    WIFI_TEST_FAILED
};
void         wifi_test_start(const char* ssid, const char* pass);
WifiTestState wifi_test_state();
void         wifi_test_tick();     // Call periodically to advance the state machine
void         wifi_test_cancel();   // Restore saved creds
