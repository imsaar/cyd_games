#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

// 2-player Ludo (this codebase's games all cap at maxPlayers()=2, so the
// classic 4-color board is played with two opposite colors — Red and
// Yellow — rather than the traditional 4-player variant). No blocking
// rule (2+ stacked checkers don't wall off a square) to keep the ruleset
// approachable; captures, safe squares, exact-roll bear-in, extra turns
// on 6/capture, and the need-a-6-to-leave-yard rule are all implemented.
class Ludo : public GameBase {
    friend void ludo_on_invite(const Peer& from);
    friend void ludo_on_accept(const Peer& from);
    friend void ludo_on_game_data(const char* json);
    friend void ludo_lobby_peer_cb(lv_event_t* e);

public:
    enum Mode { MODE_SELECT, MODE_CPU, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };
    enum Player { PC_RED = 0, PC_YELLOW = 1 };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Ludo"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    struct Token { int8_t local_pos = -1; }; // -1 yard, 0-50 ring, 51-56 home col, 57 finished

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lbl_dice_ = nullptr;
    lv_obj_t* lbl_home_ = nullptr;
    lv_obj_t* board_area_ = nullptr;
    lv_obj_t* btn_roll_ = nullptr;

    Token tokens_[2][4];

    Mode mode_ = MODE_SELECT;
    Player my_color_ = PC_RED;
    Player current_ = PC_RED;
    bool my_turn_ = true;
    bool game_done_ = false;
    IPAddress peer_ip_;

    int die_ = 1;
    bool rolled_ = false;
    bool legal_token_[4] = {};

    uint32_t cpu_think_time_ = 0;
    bool cpu_pending_ = false;

    void reset_board();
    void apply_roll(int die);
    void send_move(int token_idx, int die);
    void send_pass(int die);
    void redraw_board();
    void update_status();
    void switch_turn();
    void show_result(const char* text, bool is_win);

    bool token_grid_pos(Player p, int local, int slot, int& row, int& col) const;
    bool is_safe_square(int shared) const;
    int  opp_count_at_shared(Player p, int shared) const;
    bool compute_move(Player p, int token_idx, int die, int& new_local) const;
    bool any_legal_move(Player p, int die) const;
    bool try_apply_move(Player p, int token_idx, int die, bool send);

    int  score_ludo_move(int token_idx, int new_local) const;
    int  cpu_pick_token() const;

    lv_obj_t* create_mode_select();
    lv_obj_t* create_lobby();
    lv_obj_t* create_board();

    static void mode_cpu_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
    static void roll_cb(lv_event_t* e);
    static void board_click_cb(lv_event_t* e);
    static void board_draw_cb(lv_event_t* e);
};
