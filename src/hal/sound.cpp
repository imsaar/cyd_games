#include "sound.h"
#include "audio.h"
#include "prefs.h"
#include <Arduino.h>

// Non-blocking melody player
struct Note { uint16_t freq; uint16_t dur_ms; };

static const Note* melody_ = nullptr;
static int melody_len_ = 0;
static int melody_idx_ = 0;
static uint32_t note_start_ = 0;
static bool playing_ = false;

// ── Melodies ──

static const Note melody_startup[] = {
    {523, 80}, {659, 80}, {784, 80}, {1047, 160}
};

static const Note melody_move[] = {
    {262, 30}
};

static const Note melody_opponent[] = {
    {800, 40}
};

// Happy trombone fanfare: charge! then triumphant resolve
static const Note melody_win[] = {
    {392, 100}, {523, 100}, {659, 100}, {784, 150},
    {0, 80},    {784, 100}, {880, 100}, {1047, 300}
};

// Sad trombone: wah wah wah wahhh
static const Note melody_lose[] = {
    {494, 250}, {466, 250}, {440, 250}, {349, 500}
};

static const Note melody_gameover[] = {
    {440, 120}, {349, 120}, {262, 250}
};

static const Note melody_timer_done[] = {
    {880, 100}, {1047, 100}, {1319, 100}, {1568, 300}
};

static const Note melody_alarm[] = {
    {880, 150}, {0, 100}, {880, 150}, {0, 100},
    {1047, 150}, {0, 100}, {1047, 150}, {0, 400}
};

static bool repeating_ = false;
static bool muted_ = false;

static void play(const Note* notes, int len, bool repeat = false) {
    if (muted_ && !repeat) return;  // allow alarm even when muted (repeat=true)
    // If a move beep interrupts a longer melody, ignore it
    if (playing_ && len == 1 && melody_len_ > 1 && !repeat) return;

    melody_ = notes;
    melody_len_ = len;
    melody_idx_ = 0;
    playing_ = true;
    repeating_ = repeat;
    ledcWriteTone(6, notes[0].freq);
    note_start_ = millis();
}

// ── Public API ──

void sound_init() {
    // audio_init() already sets up LEDC channel 6
    muted_ = prefs_get_muted();
}

bool sound_get_muted() { return muted_; }
void sound_set_muted(bool muted) {
    muted_ = muted;
    prefs_set_muted(muted);
    if (muted) { ledcWriteTone(6, 0); playing_ = false; repeating_ = false; }
}

void sound_update() {
    if (!playing_) return;

    uint32_t elapsed = millis() - note_start_;
    if (elapsed < melody_[melody_idx_].dur_ms) return;

    melody_idx_++;
    if (melody_idx_ >= melody_len_) {
        if (repeating_) {
            melody_idx_ = 0;  // loop
        } else {
            ledcWriteTone(6, 0);
            playing_ = false;
            return;
        }
    }

    ledcWriteTone(6, melody_[melody_idx_].freq);
    note_start_ = millis();
}

void sound_startup() { play(melody_startup, sizeof(melody_startup) / sizeof(Note)); }
void sound_move()         { play(melody_move,     sizeof(melody_move)     / sizeof(Note)); }
void sound_opponent_move(){ play(melody_opponent,  sizeof(melody_opponent) / sizeof(Note)); }
void sound_win()          { play(melody_win,       sizeof(melody_win)      / sizeof(Note)); }
void sound_lose()    { play(melody_lose,    sizeof(melody_lose)    / sizeof(Note)); }
void sound_gameover()    { play(melody_gameover,   sizeof(melody_gameover)   / sizeof(Note)); }
void sound_timer_done()  { play(melody_timer_done, sizeof(melody_timer_done) / sizeof(Note)); }
void sound_alarm_start() { play(melody_alarm,      sizeof(melody_alarm)      / sizeof(Note), true); }
void sound_alarm_stop()  { ledcWriteTone(6, 0); playing_ = false; repeating_ = false; }
