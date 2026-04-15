#pragma once
#include <Arduino.h>

void ntp_init();
bool ntp_valid();
// Returns formatted date/time string like "Apr 15 2:30 PM"
const char* ntp_get_display_str();
