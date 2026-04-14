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
