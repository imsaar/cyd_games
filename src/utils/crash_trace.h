#pragma once
#include <stdint.h>
#include <stddef.h>

// Records a named checkpoint + LVGL heap stats into RTC memory, which
// survives panics/watchdog resets (cleared only on power-on). Lets us see
// what the firmware was doing right before a crash without a USB connection.
void crash_trace_checkpoint(const char* name, uint32_t lv_free = 0, uint32_t lv_free_biggest = 0);

// Formats the last recorded checkpoint (from the reset before this boot) into buf.
void crash_trace_format(char* buf, size_t len);
