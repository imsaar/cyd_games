#pragma once
#include <stddef.h>

// Appends a formatted line (in Serial.printf-style) to an in-memory ring
// buffer, retrievable over the network via GET /debug — lets us see live
// log output when the board is cased/OTA-only with no USB access.
void debug_log(const char* fmt, ...);

// Copies the current ring buffer contents (oldest first) into buf.
void debug_log_get(char* buf, size_t len);
