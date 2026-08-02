#include "debug_log.h"
#include <Arduino.h>
#include <stdarg.h>
#include <string.h>

#define DEBUG_LOG_CAP 2048

static char log_buf_[DEBUG_LOG_CAP];
static size_t log_len_ = 0;

void debug_log(const char* fmt, ...) {
    char line[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);

    Serial.print(line);  // still useful if USB happens to be connected

    size_t line_len = strlen(line);
    if (line_len >= DEBUG_LOG_CAP) line_len = DEBUG_LOG_CAP - 1;

    if (log_len_ + line_len > DEBUG_LOG_CAP) {
        size_t drop = (log_len_ + line_len) - DEBUG_LOG_CAP;
        if (drop > log_len_) drop = log_len_;
        memmove(log_buf_, log_buf_ + drop, log_len_ - drop);
        log_len_ -= drop;
    }
    memcpy(log_buf_ + log_len_, line, line_len);
    log_len_ += line_len;
}

void debug_log_get(char* buf, size_t len) {
    size_t n = log_len_ < len - 1 ? log_len_ : len - 1;
    memcpy(buf, log_buf_, n);
    buf[n] = '\0';
}
