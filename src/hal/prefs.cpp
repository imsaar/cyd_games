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
