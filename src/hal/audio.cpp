#include "audio.h"
#include "config.h"
#include "prefs.h"
#include "../utils/debug_log.h"
#include <Arduino.h>

// Keep both audio channels in the 0-7 "high-speed" LEDC group (hardware-timer
// driven) — channels 8-15 are software-triggered and audibly worse for
// ledcWriteTone. Backlight owns channel 7; stay clear of it here.
#define BUZZER_CH   6  // LEDC channel for piezo buzzer (GPIO22)
#define SPEAKER_CH  5  // LEDC channel for amp-driven speaker (GPIO26)

static AudioOutput active_output_ = AUDIO_OUT_BUZZER;

uint8_t audio_active_channel() {
    return active_output_ == AUDIO_OUT_SPEAKER ? SPEAKER_CH : BUZZER_CH;
}

void audio_init() {
    uint32_t buzzer_freq = ledcSetup(BUZZER_CH, 2000, 8);
    ledcAttachPin(BUZZER_PIN, BUZZER_CH);
    ledcWriteTone(BUZZER_CH, 0);

    uint32_t speaker_freq = ledcSetup(SPEAKER_CH, 2000, 8);
    ledcAttachPin(SPEAKER_PIN, SPEAKER_CH);
    ledcWriteTone(SPEAKER_CH, 0);

    active_output_ = (AudioOutput)prefs_get_audio_output();

    debug_log("[audio] buzzer: pin=%d ch=%d ledcSetup_freq=%lu\n", BUZZER_PIN, BUZZER_CH, (unsigned long)buzzer_freq);
    debug_log("[audio] speaker: pin=%d ch=%d ledcSetup_freq=%lu\n", SPEAKER_PIN, SPEAKER_CH, (unsigned long)speaker_freq);
    debug_log("[audio] active output at boot = %s\n", active_output_ == AUDIO_OUT_SPEAKER ? "SPEAKER" : "BUZZER");
}

void audio_tone(uint16_t freq_hz, uint16_t duration_ms) {
    uint8_t ch = audio_active_channel();
    ledcWriteTone(ch, freq_hz);
    if (duration_ms > 0) {
        delay(duration_ms);
        ledcWriteTone(ch, 0);
    }
}

void audio_stop() {
    ledcWriteTone(audio_active_channel(), 0);
}

void audio_set_output(AudioOutput out) {
    if (out == active_output_) return;
    ledcWriteTone(audio_active_channel(), 0);  // silence the previously active channel
    active_output_ = out;
    prefs_set_audio_output((uint8_t)out);
    debug_log("[audio] output switched to %s (ch=%d, pin=%d)\n",
        out == AUDIO_OUT_SPEAKER ? "SPEAKER" : "BUZZER",
        audio_active_channel(),
        out == AUDIO_OUT_SPEAKER ? SPEAKER_PIN : BUZZER_PIN);
}

AudioOutput audio_get_output() {
    return active_output_;
}
