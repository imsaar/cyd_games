#pragma once
#include <stdint.h>

void audio_init();
void audio_tone(uint16_t freq_hz, uint16_t duration_ms);
void audio_stop();
