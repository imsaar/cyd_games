#pragma once
#include "../game_base.h"

class MemoryMatch : public GameBase {
public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;

    const char* name() const override { return "Memory"; }
    uint8_t maxPlayers() const override { return 1; }

private:
    static const int COLS = 4;
    static const int ROWS = 3;
    static const int NUM_CARDS = COLS * ROWS;

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* cards_[NUM_CARDS] = {};
    lv_obj_t* card_labels_[NUM_CARDS] = {};  // Direct label refs (no lv_obj_get_child)
    lv_obj_t* lbl_moves_ = nullptr;
    lv_obj_t* lbl_pairs_ = nullptr;

    int8_t  values_[NUM_CARDS] = {};
    bool    revealed_[NUM_CARDS] = {};
    bool    matched_[NUM_CARDS] = {};
    int     first_pick_  = -1;
    int     second_pick_ = -1;
    int     moves_ = 0;
    int     pairs_found_ = 0;
    bool    checking_ = false;
    uint32_t check_time_ = 0;

    // Symbols for card faces (6 pairs)
    static const char* const symbols[6];

    void shuffle();
    void reveal(int idx);
    void hide(int idx);
    void check_match();
    void show_win();

    static void card_cb(lv_event_t* e);
};
