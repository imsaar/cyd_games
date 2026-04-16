#pragma once
#include <stdint.h>

void     alert_state_init();
void     alert_state_check();

// Timer
void     alert_state_set_timer(uint32_t seconds);
void     alert_state_cancel_timer();
bool     alert_state_timer_running();
uint32_t alert_state_timer_remaining_ms();
bool     alert_state_timer_just_fired();

// Alarm
void     alert_state_set_alarm(uint8_t hour24, uint8_t minute);
void     alert_state_enable_alarm(bool en);
void     alert_state_cancel_alarm();
bool     alert_state_alarm_enabled();
uint8_t  alert_state_alarm_hour();
uint8_t  alert_state_alarm_minute();
bool     alert_state_alarm_just_fired();
void     alert_state_snooze_alarm();
