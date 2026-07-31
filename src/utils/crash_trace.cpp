#include "crash_trace.h"
#include <Arduino.h>
#include <cstring>
#include <cstdio>

static const uint32_t MAGIC = 0xC0FFEE42;

RTC_NOINIT_ATTR static char s_checkpoint[40];
RTC_NOINIT_ATTR static uint32_t s_lv_free;
RTC_NOINIT_ATTR static uint32_t s_lv_free_biggest;
RTC_NOINIT_ATTR static uint32_t s_magic;

void crash_trace_checkpoint(const char* name, uint32_t lv_free, uint32_t lv_free_biggest) {
    strncpy(s_checkpoint, name, sizeof(s_checkpoint) - 1);
    s_checkpoint[sizeof(s_checkpoint) - 1] = '\0';
    s_lv_free = lv_free;
    s_lv_free_biggest = lv_free_biggest;
    s_magic = MAGIC;
}

void crash_trace_format(char* buf, size_t len) {
    if (s_magic != MAGIC) {
        snprintf(buf, len, "(none recorded yet — power-on reset or RTC memory cleared)");
        return;
    }
    snprintf(buf, len, "%s | LVGL free: %lu bytes (largest block %lu)",
             s_checkpoint, (unsigned long)s_lv_free, (unsigned long)s_lv_free_biggest);
}
