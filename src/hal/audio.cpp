#include "audio.h"
#include "config.h"
#include <Arduino.h>

#define AUDIO_CH  6  // LEDC channel for speaker

void audio_init() {
    ledcSetup(AUDIO_CH, 2000, 8);
    ledcAttachPin(SPEAKER_PIN, AUDIO_CH);
    ledcWriteTone(AUDIO_CH, 0);
}

void audio_tone(uint16_t freq_hz, uint16_t duration_ms) {
    ledcWriteTone(AUDIO_CH, freq_hz);
    if (duration_ms > 0) {
        delay(duration_ms);
        ledcWriteTone(AUDIO_CH, 0);
    }
}

void audio_stop() {
    ledcWriteTone(AUDIO_CH, 0);
}
