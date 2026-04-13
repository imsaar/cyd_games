#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class MemoryMatch : public GameBase {
    friend void mm_on_invite(const Peer& from);
    friend void mm_on_accept(const Peer& from);
    friend void mm_on_game_data(const char* json);
    friend void mm_lobby_peer_cb(lv_event_t* e);

public:
    enum Mode { MODE_SELECT, MODE_SOLO, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Memory"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    static const int COLS = 4;
    static const int ROWS = 3;
    static const int NUM_CARDS = COLS * ROWS;

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* cards_[NUM_CARDS] = {};
    lv_obj_t* card_labels_[NUM_CARDS] = {};
    lv_obj_t* lbl_moves_ = nullptr;
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;

    int8_t  values_[NUM_CARDS] = {};
    bool    revealed_[NUM_CARDS] = {};
    bool    matched_[NUM_CARDS] = {};
    int     first_pick_  = -1;
    int     second_pick_ = -1;
    int     moves_ = 0;
    int     pairs_found_ = 0;
    bool    checking_ = false;
    uint32_t check_time_ = 0;

    // 2-player state
    Mode    mode_ = MODE_SELECT;
    bool    is_p1_ = true;       // Am I player 1?
    bool    p1_turn_ = true;     // Is it player 1's turn?
    bool    my_turn_ = true;
    int     score_p1_ = 0;
    int     score_p2_ = 0;
    bool    game_done_ = false;
    IPAddress peer_ip_;

    static const char* const symbols[6];

    lv_obj_t* create_mode_select();
    lv_obj_t* create_board();
    lv_obj_t* create_lobby();
    void shuffle();
    void reveal(int idx);
    void hide(int idx);
    void check_match();
    void update_status();
    void show_result();
    void send_flip(int idx);
    void send_board_sync();

    static void card_cb(lv_event_t* e);
    static void mode_solo_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
};
