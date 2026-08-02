#pragma once
#include <stdint.h>

enum AudioOutput { AUDIO_OUT_BUZZER = 0, AUDIO_OUT_SPEAKER = 1 };

void audio_init();
void audio_tone(uint16_t freq_hz, uint16_t duration_ms);
void audio_stop();

void audio_set_output(AudioOutput out);
AudioOutput audio_get_output();
uint8_t audio_active_channel();  // LEDC channel currently routed to the selected output
