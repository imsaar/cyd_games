#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Checkers : public GameBase {
    friend void ck_on_invite(const Peer& from);
    friend void ck_on_accept(const Peer& from);
    friend void ck_on_game_data(const char* json);
    friend void ck_lobby_peer_cb(lv_event_t* e);

public:
    enum Mode { MODE_SELECT, MODE_CPU, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Checkers"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    static const int BOARD_SIZE = 8;
    static const int CELL = 26;  // Fits 8*26=208 + margins in 240 height

    // Piece values: sign = player, abs > 1 = king
    enum Piece : int8_t {
        EMPTY_CELL = 0,
        RED_MAN    = 1,
        RED_KING   = 2,
        BLACK_MAN  = -1,
        BLACK_KING = -2
    };

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* cell_objs_[64] = {};
    lv_obj_t* piece_objs_[64] = {};
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;

    int8_t board_[64] = {};
    int selected_ = -1;           // Currently selected piece index
    bool is_red_turn_ = true;     // Red always goes first
    bool my_color_red_ = true;
    Mode mode_ = MODE_SELECT;
    bool my_turn_ = true;
    bool game_done_ = false;
    bool must_jump_ = false;      // Multi-jump in progress
    int jump_piece_ = -1;         // Piece doing multi-jump
    IPAddress peer_ip_;

    lv_obj_t* create_mode_select();
    lv_obj_t* create_board();
    lv_obj_t* create_lobby();
    void init_pieces();
    void draw_board();
    void draw_piece(int idx);
    void clear_highlights();
    void highlight_cell(int idx, lv_color_t color);
    bool is_mine(int idx);
    bool try_move(int from, int to);
    bool can_jump(int idx);
    bool has_any_move(bool red);
    void check_game_over();
    void promote_if_needed(int idx);
    void update_status();
    void send_move(int from, int to);
    void show_result(const char* text, bool is_win);

    // CPU AI
    void cpu_move();
    int eval_board();
    bool cpu_pending_ = false;
    uint32_t cpu_think_time_ = 0;

    static void cell_cb(lv_event_t* e);
    static void mode_cpu_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
};
