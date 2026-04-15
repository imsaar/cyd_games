#include "ntp_time.h"
#include <time.h>
#include <Arduino.h>

static bool time_valid = false;
static char display_buf[20];

void ntp_init() {
    // Pacific Time: UTC-8 standard, UTC-7 daylight
    // POSIX TZ string handles DST automatically
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    setenv("TZ", "PST8PDT,M3.2.0,M11.1.0", 1);
    tzset();
    Serial.println("[NTP] Time sync started (Pacific)");
}

bool ntp_valid() {
    if (time_valid) return true;
    struct tm t;
    if (getLocalTime(&t, 0) && t.tm_year > 100) {
        time_valid = true;
        Serial.println("[NTP] Time synchronized");
    }
    return time_valid;
}

const char* ntp_get_display_str() {
    struct tm t;
    if (!getLocalTime(&t, 0)) {
        return "";
    }
    int hr = t.tm_hour % 12;
    if (hr == 0) hr = 12;
    const char* ampm = t.tm_hour >= 12 ? "PM" : "AM";
    static const char* months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    snprintf(display_buf, sizeof(display_buf), "%s %d %d:%02d %s",
             months[t.tm_mon], t.tm_mday, hr, t.tm_min, ampm);
    return display_buf;
}
