#pragma once
#include <stdint.h>
#include <stddef.h>

void     prefs_init();
uint8_t  prefs_get_brightness();
void     prefs_set_brightness(uint8_t val);
bool     prefs_get_inverted();
void     prefs_set_inverted(bool val);
void     prefs_get_name(char* buf, size_t len);  // returns saved name or ""
void     prefs_set_name(const char* name);
uint8_t  prefs_get_alarm_hour();
uint8_t  prefs_get_alarm_min();
bool     prefs_get_alarm_on();
void     prefs_set_alarm(uint8_t hour, uint8_t min, bool on);
