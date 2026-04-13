#pragma once
#include "../game_base.h"

class Snake : public GameBase {
public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;

    const char* name() const override { return "Snake"; }
    uint8_t maxPlayers() const override { return 1; }

private:
    static const int GRID_W = 12;
    static const int GRID_H = 10;
    static const int TILE   = 20;
    static const int TOP_BAR = 40;

    enum Dir { UP, DOWN, LEFT, RIGHT };
    struct Pos { int x, y; };

    lv_obj_t* screen_    = nullptr;
    lv_obj_t* game_area_ = nullptr;
    lv_obj_t* lbl_score_ = nullptr;
    lv_obj_t* overlay_   = nullptr;
    lv_obj_t* food_obj_  = nullptr;

    lv_obj_t* body_objs_[120] = {};
    int body_obj_count_ = 0;

    Pos snake_[120];
    int snake_len_    = 0;
    Dir dir_          = RIGHT;
    Dir next_dir_     = RIGHT;
    Pos food_         = {0, 0};
    int score_        = 0;
    bool game_over_   = false;
    uint32_t last_step_ = 0;
    uint32_t step_interval_ = 350;

    void reset();
    void spawn_food();
    void step();
    void draw();
    void show_game_over();

    static void dir_btn_cb(lv_event_t* e);
    static void restart_cb(lv_event_t* e);
};
