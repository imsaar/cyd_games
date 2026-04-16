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
bool     prefs_get_muted();
void     prefs_set_muted(bool val);
uint8_t  prefs_get_alarm_hour();
uint8_t  prefs_get_alarm_min();
bool     prefs_get_alarm_on();
void     prefs_set_alarm(uint8_t hour, uint8_t min, bool on);
void     prefs_get_wifi_ssid(char* buf, size_t len);
void     prefs_set_wifi_ssid(const char* ssid);
void     prefs_get_wifi_pass(char* buf, size_t len);
void     prefs_set_wifi_pass(const char* pass);
bool     prefs_wifi_configured();
