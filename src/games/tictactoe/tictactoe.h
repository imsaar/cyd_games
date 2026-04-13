#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class TicTacToe : public GameBase {
    friend void on_invite_received(const Peer& from);
    friend void on_accept_received(const Peer& from);
    friend void on_game_data(const char* json);
    friend void lobby_peer_cb(lv_event_t* e);

public:
    enum Mode { MODE_SELECT, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };
    enum Cell { EMPTY = 0, PLAYER_X = 1, PLAYER_O = 2 };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onPeerJoined(const char* peer_ip) override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Tic-Tac-Toe"; }
    uint8_t maxPlayers() const override { return 2; }

private:

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* cells_[9] = {};
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;

    int8_t  board_[9] = {};
    Cell    current_  = PLAYER_X;
    Cell    my_mark_  = PLAYER_X;
    Mode    mode_     = MODE_SELECT;
    bool    my_turn_  = true;
    bool    game_done_ = false;
    IPAddress peer_ip_;

    lv_obj_t* create_mode_select();
    lv_obj_t* create_board();
    lv_obj_t* create_lobby();
    void reset_board();
    void place_mark(int idx);
    Cell check_winner();
    bool board_full();
    void update_status();
    void send_move(int idx);
    void show_result(const char* text, bool is_win);

    static void cell_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
    static void restart_cb(lv_event_t* e);
};
