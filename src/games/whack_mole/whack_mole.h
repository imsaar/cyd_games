#pragma once
#include "../game_base.h"

class WhackMole : public GameBase {
public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;

    const char* name() const override { return "Whack-a-Mole"; }
    uint8_t maxPlayers() const override { return 1; }

private:
    static const int COLS = 4;
    static const int ROWS = 3;
    static const int HOLES = COLS * ROWS;
    static const int TOTAL_ROUNDS = 30;

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* lbl_score_ = nullptr;
    lv_obj_t* lbl_timer_ = nullptr;
    lv_obj_t* lbl_round_ = nullptr;
    lv_obj_t* hole_btns_[HOLES] = {};
    lv_obj_t* hole_lbls_[HOLES] = {};

    // Game state
    bool mole_up_[HOLES] = {};       // true if mole is showing
    bool mole_bomb_[HOLES] = {};     // true if this is a bomb (penalty)
    uint32_t mole_time_[HOLES] = {}; // when mole appeared
    int score_ = 0;
    int round_ = 0;
    int misses_ = 0;
    bool game_done_ = false;
    uint32_t game_start_ = 0;
    int game_time_ = 30;             // seconds

    // Spawn timing
    uint32_t last_spawn_ = 0;
    int spawn_interval_ = 800;       // ms between spawns, decreases
    int mole_duration_ = 1200;       // ms mole stays up, decreases

    void spawn_mole();
    void hide_mole(int idx);
    void whack(int idx);
    void update_display();
    void show_result();

    static void hole_cb(lv_event_t* e);
};
