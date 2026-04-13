#pragma once
#include "../game_base.h"

class Anagram : public GameBase {
public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;

    const char* name() const override { return "Anagram"; }
    uint8_t maxPlayers() const override { return 1; }

private:
    static const int MAX_WORD = 8;
    static const int MAX_LETTERS = 8;
    static const int WORDS_PER_ROUND = 5;

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* lbl_scrambled_ = nullptr;
    lv_obj_t* lbl_answer_ = nullptr;
    lv_obj_t* lbl_score_ = nullptr;
    lv_obj_t* lbl_timer_ = nullptr;
    lv_obj_t* letter_btns_[MAX_LETTERS] = {};
    lv_obj_t* answer_slots_[MAX_LETTERS] = {};

    // Word list index
    int word_idx_ = 0;
    char current_word_[MAX_WORD + 1] = {};
    char scrambled_[MAX_WORD + 1] = {};
    char answer_buf_[MAX_WORD + 1] = {};
    int answer_len_ = 0;
    int word_len_ = 0;

    int score_ = 0;
    int round_ = 0;
    int total_rounds_ = 10;
    bool game_done_ = false;
    uint32_t round_start_ = 0;
    int time_limit_ = 20;  // seconds per word

    void next_word();
    void scramble(const char* word, char* out);
    void update_display();
    void check_answer();
    void skip_word();
    void show_result();

    static void letter_cb(lv_event_t* e);
    static void clear_cb(lv_event_t* e);
    static void skip_cb(lv_event_t* e);
};
