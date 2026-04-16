#include "anagram.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../hal/sound.h"
#include <Arduino.h>

static Anagram* s_self = nullptr;

// Word bank — common 4-6 letter words
static const char* const word_bank[] = {
    "GAME", "PLAY", "WORD", "FIRE", "MOON", "STAR", "FISH", "BIRD",
    "TREE", "RAIN", "SNOW", "WIND", "LAKE", "ROCK", "SAND", "GOLD",
    "BLUE", "DARK", "FAST", "SLOW", "WARM", "COOL", "JUMP", "SWIM",
    "APPLE", "BRAIN", "CHAIR", "DANCE", "EARTH", "FLAME", "GRAPE",
    "HORSE", "JUICE", "KNIFE", "LEMON", "MAGIC", "NIGHT", "OCEAN",
    "PIANO", "QUEEN", "RIVER", "SNAKE", "TIGER", "ULTRA", "VOICE",
    "WATER", "YOUTH", "CLOUD", "DREAM", "FROST", "GHOST", "HEART",
    "LIGHT", "MOUSE", "PEARL", "ROUND", "SPACE", "TRAIN", "WHEEL",
    "BEACH", "CANDY", "DOOR", "EAGLE", "FLUTE", "GREEN", "HOUSE",
    "IVORY", "JEWEL", "KINGS", "LUNAR", "MAPLE", "NOBLE", "OLIVE",
    "POWER", "QUEST", "ROYAL", "STORM", "TOWER", "UNITY", "VIVID",
    "WORLD", "ZEBRA", "AMBER", "BLOOM", "CORAL", "DIVER", "EMBER",
};
static const int WORD_COUNT = sizeof(word_bank) / sizeof(word_bank[0]);

void Anagram::scramble(const char* word, char* out) {
    int len = strlen(word);
    strcpy(out, word);
    // Fisher-Yates shuffle, ensure it's different from original
    int attempts = 0;
    do {
        for (int i = len - 1; i > 0; i--) {
            int j = random(0, i + 1);
            char tmp = out[i]; out[i] = out[j]; out[j] = tmp;
        }
        attempts++;
    } while (strcmp(out, word) == 0 && attempts < 10);
}

void Anagram::next_word() {
    if (round_ >= total_rounds_) {
        game_done_ = true;
        show_result();
        return;
    }

    // Pick a random word we haven't used recently
    word_idx_ = random(0, WORD_COUNT);
    strncpy(current_word_, word_bank[word_idx_], MAX_WORD);
    current_word_[MAX_WORD] = '\0';
    word_len_ = strlen(current_word_);

    scramble(current_word_, scrambled_);
    memset(answer_buf_, 0, sizeof(answer_buf_));
    answer_len_ = 0;
    round_start_ = millis();

    // Restore clickability on all letter buttons for the new word
    for (int i = 0; i < MAX_LETTERS; i++) {
        if (letter_btns_[i]) {
            lv_obj_set_style_bg_opa(letter_btns_[i], LV_OPA_COVER, 0);
            lv_obj_add_flag(letter_btns_[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }

    update_display();
}

void Anagram::update_display() {
    if (!screen_) return;

    // Update scrambled letter buttons
    for (int i = 0; i < MAX_LETTERS; i++) {
        if (letter_btns_[i]) {
            if (i < word_len_) {
                char s[2] = {scrambled_[i], '\0'};
                lv_obj_t* lbl = lv_obj_get_child(letter_btns_[i], 0);
                if (lbl) lv_label_set_text(lbl, s);
                lv_obj_clear_flag(letter_btns_[i], LV_OBJ_FLAG_HIDDEN);
                // Show used letters as dimmed
                bool used = false;
                for (int a = 0; a < answer_len_; a++) {
                    if (answer_buf_[a] == scrambled_[i]) {
                        // Check if this specific index was used
                        // Simple approach: count occurrences
                    }
                }
                lv_obj_set_style_bg_opa(letter_btns_[i], LV_OPA_COVER, 0);
            } else {
                lv_obj_add_flag(letter_btns_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // Update answer display
    if (lbl_answer_) {
        char disp[MAX_WORD * 2 + 1] = {};
        for (int i = 0; i < word_len_; i++) {
            if (i < answer_len_) {
                disp[i * 2] = answer_buf_[i];
            } else {
                disp[i * 2] = '_';
            }
            disp[i * 2 + 1] = ' ';
        }
        lv_label_set_text(lbl_answer_, disp);
    }

    // Score
    if (lbl_score_) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Score: %d  (%d/%d)", score_, round_ + 1, total_rounds_);
        lv_label_set_text(lbl_score_, buf);
    }
}

void Anagram::check_answer() {
    if (answer_len_ != word_len_) return;

    answer_buf_[answer_len_] = '\0';
    if (strcmp(answer_buf_, current_word_) == 0) {
        // Correct!
        int elapsed = (millis() - round_start_) / 1000;
        int bonus = (time_limit_ - elapsed);
        if (bonus < 1) bonus = 1;
        score_ += bonus * 10;
        round_++;
        next_word();
    } else {
        // Wrong — flash answer red briefly, clear
        if (lbl_answer_) {
            lv_obj_set_style_text_color(lbl_answer_, UI_COLOR_ACCENT, 0);
        }
        memset(answer_buf_, 0, sizeof(answer_buf_));
        answer_len_ = 0;
        // Re-enable all letter buttons
        for (int i = 0; i < word_len_; i++) {
            if (letter_btns_[i]) {
                lv_obj_set_style_bg_opa(letter_btns_[i], LV_OPA_COVER, 0);
                lv_obj_add_flag(letter_btns_[i], LV_OBJ_FLAG_CLICKABLE);
            }
        }
        update_display();
        // Reset color after brief display
        if (lbl_answer_) {
            lv_obj_set_style_text_color(lbl_answer_, UI_COLOR_TEXT, 0);
        }
    }
}

void Anagram::skip_word() {
    round_++;
    next_word();
}

void Anagram::show_result() {
    if (!screen_) return;
    if (score_ > 0) sound_win(); else sound_lose();
    lv_obj_t* ov = lv_obj_create(screen_);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, 260, 140);
    lv_obj_center(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ov, 16, 0);
    lv_obj_set_style_border_color(ov, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_border_width(ov, 3, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    char buf[48];
    snprintf(buf, sizeof(buf), "Game Over!\nScore: %d", score_);
    lv_obj_t* lbl = lv_label_create(ov);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t* btn = ui_create_btn(ov, "Menu", 100, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        screen_manager_back_to_menu();
    }, LV_EVENT_CLICKED, NULL);
}

// ── Callbacks ──

void Anagram::letter_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= s_self->word_len_) return;
    if (s_self->answer_len_ >= s_self->word_len_) return;

    // Add this letter to answer
    s_self->answer_buf_[s_self->answer_len_++] = s_self->scrambled_[idx];
    sound_move();

    // Dim the used button
    lv_obj_set_style_bg_opa(s_self->letter_btns_[idx], LV_OPA_30, 0);
    lv_obj_clear_flag(s_self->letter_btns_[idx], LV_OBJ_FLAG_CLICKABLE);

    s_self->update_display();

    // Auto-check when all letters placed
    if (s_self->answer_len_ == s_self->word_len_) {
        s_self->check_answer();
    }
}

void Anagram::clear_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    memset(s_self->answer_buf_, 0, sizeof(s_self->answer_buf_));
    s_self->answer_len_ = 0;
    // Re-enable all letter buttons
    for (int i = 0; i < s_self->word_len_; i++) {
        if (s_self->letter_btns_[i]) {
            lv_obj_set_style_bg_opa(s_self->letter_btns_[i], LV_OPA_COVER, 0);
            lv_obj_add_flag(s_self->letter_btns_[i], LV_OBJ_FLAG_CLICKABLE);
        }
    }
    s_self->update_display();
}

void Anagram::skip_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    s_self->skip_word();
}

// ── Screen ──

lv_obj_t* Anagram::createScreen() {
    s_self = this;
    screen_ = ui_create_screen();
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_back_btn(screen_);

    lbl_score_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_score_, LV_ALIGN_TOP_MID, 20, 8);

    lbl_timer_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(lbl_timer_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl_timer_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_timer_, LV_ALIGN_TOP_RIGHT, -5, 8);

    // Answer display
    lbl_answer_ = lv_label_create(screen_);
    lv_label_set_text(lbl_answer_, "");
    lv_obj_set_style_text_color(lbl_answer_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_answer_, &lv_font_montserrat_28, 0);
    lv_obj_align(lbl_answer_, LV_ALIGN_TOP_MID, 0, 35);

    // Letter buttons (will be positioned in next_word)
    int btn_w = 34, btn_h = 36, gap = 4;
    int row_y = 80;
    for (int i = 0; i < MAX_LETTERS; i++) {
        lv_obj_t* btn = lv_btn_create(screen_);
        lv_obj_set_size(btn, btn_w, btn_h);
        int total_w = MAX_LETTERS * (btn_w + gap) - gap;
        int ox = (320 - total_w) / 2;
        lv_obj_set_pos(btn, ox + i * (btn_w + gap), row_y);
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_ACCENT, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, letter_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
        letter_btns_[i] = btn;
    }

    // Clear and Skip buttons
    lv_obj_t* btn_clear = ui_create_btn(screen_, "Clear", 90, 36);
    lv_obj_set_pos(btn_clear, 60, 130);
    lv_obj_add_event_cb(btn_clear, clear_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_skip = ui_create_btn(screen_, "Skip", 90, 36);
    lv_obj_set_pos(btn_skip, 170, 130);
    lv_obj_add_event_cb(btn_skip, skip_cb, LV_EVENT_CLICKED, NULL);

    // Hint label at bottom
    lv_obj_t* hint = lv_label_create(screen_);
    lv_label_set_text(hint, "Unscramble the word!");
    lv_obj_set_style_text_color(hint, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    // Start game
    score_ = 0;
    round_ = 0;
    game_done_ = false;
    next_word();

    return screen_;
}

void Anagram::update() {
    if (game_done_) return;

    // Timer countdown
    int elapsed = (millis() - round_start_) / 1000;
    int remaining = time_limit_ - elapsed;
    if (remaining < 0) remaining = 0;

    if (lbl_timer_) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", remaining);
        lv_label_set_text(lbl_timer_, buf);
        lv_obj_set_style_text_color(lbl_timer_,
            remaining <= 5 ? UI_COLOR_ACCENT : UI_COLOR_WARNING, 0);
    }

    if (remaining <= 0) {
        // Time's up — skip to next word
        skip_word();
    }
}

void Anagram::destroy() {
    s_self = nullptr;
    screen_ = nullptr;
    lbl_scrambled_ = nullptr;
    lbl_answer_ = nullptr;
    lbl_score_ = nullptr;
    lbl_timer_ = nullptr;
    for (int i = 0; i < MAX_LETTERS; i++) letter_btns_[i] = nullptr;
}
