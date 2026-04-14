#pragma once
#include "../game_base.h"

class CupPong : public GameBase {
public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;

    const char* name() const override { return "Cup Pong"; }
    uint8_t maxPlayers() const override { return 1; }

private:
    static const int MAX_CUPS = 10;   // 4-3-2-1 triangle
    static const int MAX_BALLS = 10;
    static const int CUP_W = 28;
    static const int CUP_H = 30;
    static const int TABLE_Y = 148;   // y position of table edge

    enum State { AIM, FLYING, BOUNCED, SINKING, RESULT };

    struct Cup {
        int cx, cy;
        bool alive;
        lv_obj_t* obj;
    };

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* ball_obj_ = nullptr;
    lv_obj_t* aim_marker_ = nullptr;
    lv_obj_t* aim_line_ = nullptr;
    lv_obj_t* throw_btn_ = nullptr;
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lbl_balls_ = nullptr;
    lv_obj_t* table_obj_ = nullptr;

    Cup cups_[MAX_CUPS] = {};
    int cups_alive_ = 0;

    // Ball physics
    float ball_x_ = 0, ball_y_ = 0;
    float ball_vx_ = 0, ball_vy_ = 0;
    int ball_r_ = 6;
    bool has_bounced_ = false;

    // Aim state
    int aim_x_ = 160, aim_y_ = 180;

    State state_ = AIM;
    int balls_left_ = 0;
    int score_ = 0;
    uint32_t state_timer_ = 0;

    void setup_cups();
    void throw_ball();
    void update_ball();
    void check_hit();
    void next_throw();
    void show_result();
    void draw_ball();
    void update_aim_visual();

    static void aim_touch_cb(lv_event_t* e);
    static void throw_cb(lv_event_t* e);
};
