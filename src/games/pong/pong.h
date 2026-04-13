#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Pong : public GameBase {
    friend void pong_on_invite(const Peer& from);
    friend void pong_on_accept(const Peer& from);
    friend void pong_on_game_data(const char* json);
    friend void pong_lobby_peer_cb(lv_event_t* e);

public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onPeerJoined(const char* peer_ip) override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Pong"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    static const int COURT_W = 320;
    static const int COURT_H = 200;
    static const int COURT_Y = 40;
    static const int PADDLE_W = 8;
    static const int PADDLE_H = 40;
    static const int BALL_SIZE = 8;
    static const int WIN_SCORE = 10;

    lv_obj_t* screen_     = nullptr;
    lv_obj_t* court_      = nullptr;
    lv_obj_t* paddle_l_   = nullptr;
    lv_obj_t* paddle_r_   = nullptr;
    lv_obj_t* ball_       = nullptr;
    lv_obj_t* lbl_score_  = nullptr;
    lv_obj_t* lobby_list_ = nullptr;
    lv_obj_t* center_line_[17] = {};

    float ball_x_, ball_y_;
    float ball_dx_, ball_dy_;
    float paddle_l_y_, paddle_r_y_;
    int score_l_, score_r_;
    bool is_host_ = true;
    bool playing_ = false;
    bool is_local_ = true;
    IPAddress peer_ip_;
    uint32_t last_frame_ = 0;
    uint32_t last_net_send_ = 0;

    void reset_ball();
    void reset_game();
    void step();
    void draw();
    void update_score_label();
    void send_state();
    void show_winner(bool left_won);

    lv_obj_t* create_game_screen();
    lv_obj_t* create_lobby();

    static void touch_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
};
