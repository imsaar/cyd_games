#include "alert_state.h"
#include <Arduino.h>
#include <time.h>

static bool     timer_running_ = false;
static uint32_t timer_end_ms_ = 0;
static bool     timer_fired_ = false;

static bool     alarm_enabled_ = false;
static uint8_t  alarm_hour_ = 7;
static uint8_t  alarm_minute_ = 0;
static bool     alarm_fired_ = false;
static uint32_t alarm_snooze_until_ = 0;
static time_t   last_alarm_epoch_ = 0;

void alert_state_init() {
    timer_running_ = false;
    timer_fired_ = false;
    alarm_enabled_ = false;
    alarm_fired_ = false;
    alarm_snooze_until_ = 0;
    last_alarm_epoch_ = 0;
}

void alert_state_check() {
    // Timer check
    if (timer_running_ && millis() >= timer_end_ms_) {
        timer_running_ = false;
        timer_fired_ = true;
    }

    // Alarm check
    if (!alarm_enabled_ || alarm_fired_) return;
    if (alarm_snooze_until_ > 0 && millis() < alarm_snooze_until_) return;
    alarm_snooze_until_ = 0;

    struct tm t;
    if (!getLocalTime(&t, 0)) return;
    if (t.tm_hour == alarm_hour_ && t.tm_min == alarm_minute_) {
        time_t now = time(nullptr);
        if (now - last_alarm_epoch_ < 120) return;  // don't re-fire within 2 min
        last_alarm_epoch_ = now;
        alarm_fired_ = true;
    }
}

// Timer
void alert_state_set_timer(uint32_t seconds) {
    timer_end_ms_ = millis() + seconds * 1000;
    timer_running_ = true;
    timer_fired_ = false;
}
void alert_state_cancel_timer() { timer_running_ = false; timer_fired_ = false; }
bool alert_state_timer_running() { return timer_running_; }
uint32_t alert_state_timer_remaining_ms() {
    if (!timer_running_) return 0;
    uint32_t now = millis();
    return (now < timer_end_ms_) ? (timer_end_ms_ - now) : 0;
}
bool alert_state_timer_just_fired() {
    if (timer_fired_) { timer_fired_ = false; return true; }
    return false;
}

// Alarm
void alert_state_set_alarm(uint8_t h, uint8_t m) { alarm_hour_ = h; alarm_minute_ = m; }
void alert_state_enable_alarm(bool en) {
    alarm_enabled_ = en;
    alarm_fired_ = false;
    alarm_snooze_until_ = 0;
    if (!en) last_alarm_epoch_ = 0;
}
void alert_state_cancel_alarm() { alert_state_enable_alarm(false); }
bool alert_state_alarm_enabled() { return alarm_enabled_; }
uint8_t alert_state_alarm_hour() { return alarm_hour_; }
uint8_t alert_state_alarm_minute() { return alarm_minute_; }
bool alert_state_alarm_just_fired() {
    if (alarm_fired_) { alarm_fired_ = false; return true; }
    return false;
}
void alert_state_snooze_alarm() {
    alarm_fired_ = false;
    alarm_snooze_until_ = millis() + 5UL * 60 * 1000;
}
