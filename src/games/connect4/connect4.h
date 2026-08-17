#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Connect4 : public GameBase {
public:
    enum Mode { MODE_SELECT, MODE_CPU, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };

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

    int8_t board_[COLS * ROWS] = {};
    Cell current_ = RED;
    Cell my_color_ = RED;
    Mode mode_ = MODE_SELECT;
    bool my_turn_ = true;
    bool game_done_ = false;
    IPAddress peer_ip_;

    lv_obj_t* create_board();
    void reset_board();
    int drop_disc(int col);
    bool check_win(int row, int col);
    int  win_cells_[4] = {-1,-1,-1,-1};  // board indices of winning 4
    bool board_full();
    void update_status();
    void send_move(int col);
    void show_result(const char* text, bool is_win);
    int cpu_pick_col();
    int minimax(int depth, int alpha, int beta, bool maximizing);
    int evaluate_board();
    uint32_t cpu_think_time_ = 0;
    bool cpu_pending_ = false;

    static void col_cb(lv_event_t* e);
    static void mode_cpu_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
    static void on_host_ready(const Peer& peer);   // our invite was accepted -> we go first
    static void on_guest_ready(const Peer& peer);  // we accepted someone's invite -> we go second
    static void on_game_data(const char* json);
};
