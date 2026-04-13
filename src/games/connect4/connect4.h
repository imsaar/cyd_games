#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Connect4 : public GameBase {
    friend void c4_on_invite(const Peer& from);
    friend void c4_on_accept(const Peer& from);
    friend void c4_on_game_data(const char* json);
    friend void c4_lobby_peer_cb(lv_event_t* e);

public:
    enum Mode { MODE_SELECT, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Connect 4"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    static const int COLS = 7;
    static const int ROWS = 6;
    static const int CELL = 30;

    enum Cell { EMPTY = 0, RED = 1, YELLOW = 2 };

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* board_objs_[COLS * ROWS] = {};
    lv_obj_t* col_btns_[COLS] = {};
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;

    int8_t board_[COLS * ROWS] = {};
    Cell current_ = RED;
    Cell my_color_ = RED;
    Mode mode_ = MODE_SELECT;
    bool my_turn_ = true;
    bool game_done_ = false;
    IPAddress peer_ip_;

    lv_obj_t* create_mode_select();
    lv_obj_t* create_board();
    lv_obj_t* create_lobby();
    void reset_board();
    int drop_disc(int col);
    bool check_win(int row, int col);
    bool board_full();
    void update_status();
    void send_move(int col);
    void show_result(const char* text, bool is_win);

    static void col_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
};
