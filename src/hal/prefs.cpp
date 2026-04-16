#include "prefs.h"
#include <Preferences.h>

static Preferences nvs;

void prefs_init() {
    nvs.begin("cyd", false);  // namespace "cyd", read-write
}

uint8_t prefs_get_brightness() {
    return nvs.getUChar("bright", 255);  // default full brightness
}

void prefs_set_brightness(uint8_t val) {
    nvs.putUChar("bright", val);
}

bool prefs_get_inverted() {
    return nvs.getBool("invert", false);
}

void prefs_set_inverted(bool val) {
    nvs.putBool("invert", val);
}

void prefs_get_name(char* buf, size_t len) {
    String s = nvs.getString("name", "");
    strncpy(buf, s.c_str(), len - 1);
    buf[len - 1] = '\0';
}

void prefs_set_name(const char* name) {
    nvs.putString("name", name);
}

bool prefs_get_muted()    { return nvs.getBool("muted", false); }
void prefs_set_muted(bool val) { nvs.putBool("muted", val); }

uint8_t prefs_get_alarm_hour() { return nvs.getUChar("alrm_h", 7); }
uint8_t prefs_get_alarm_min()  { return nvs.getUChar("alrm_m", 0); }
bool    prefs_get_alarm_on()   { return nvs.getBool("alrm_on", false); }
void    prefs_set_alarm(uint8_t hour, uint8_t min, bool on) {
    nvs.putUChar("alrm_h", hour);
    nvs.putUChar("alrm_m", min);
    nvs.putBool("alrm_on", on);
}
