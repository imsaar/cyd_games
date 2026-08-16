#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Backgammon : public GameBase {
public:
    enum Mode { MODE_SELECT, MODE_CPU, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };
    enum Player { WHITE = 0, BLACK = 1 };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Backgammon"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lbl_dice_ = nullptr;
    lv_obj_t* lbl_off_ = nullptr;
    lv_obj_t* board_area_ = nullptr;
    lv_obj_t* btn_roll_ = nullptr;
    lv_obj_t* btn_bear_off_ = nullptr;

    // +N = N White checkers on point, -N = N Black checkers. Index = point-1.
    int8_t points_[24] = {};
    int8_t bar_white_ = 0, bar_black_ = 0;
    int8_t off_white_ = 0, off_black_ = 0;

    Mode mode_ = MODE_SELECT;
    Player my_color_ = WHITE;
    Player current_ = WHITE;
    bool my_turn_ = true;
    bool game_done_ = false;
    IPAddress peer_ip_;

    int dice_[4] = {};
    bool dice_used_[4] = {};
    int dice_count_ = 0;
    bool rolled_ = false;

    int selected_from_ = -2;        // -2 = none selected, -1 = bar, 0..23 = point index
    bool legal_dest_[24] = {};
    bool can_bear_off_ = false;

    uint32_t cpu_think_time_ = 0;
    bool cpu_pending_ = false;

    void reset_board();
    void roll_dice();
    void apply_dice(int d1, int d2);
    void send_roll();
    void redraw_board();
    void update_status();
    void select_origin(int from);
    void clear_selection();
    void end_turn_if_no_moves();
    void switch_turn();
    void send_move(int from, int die);
    void show_result(const char* text, bool is_win);

    int  bar_count(Player p) const { return p == WHITE ? bar_white_ : bar_black_; }
    int  dist_of(Player p, int idx) const { return p == WHITE ? idx + 1 : 24 - idx; }
    int  idx_of_dist(Player p, int d) const { return p == WHITE ? d - 1 : 24 - d; }
    int  checker_count_at(Player p, int idx) const;
    int  opp_count_at(Player p, int idx) const;
    bool all_in_home(Player p) const;
    bool has_checker_farther(Player p, int from_dist) const;
    bool compute_dest(Player p, int from, int die, int& to_idx, bool& is_bear_off) const;
    bool any_legal_move(Player p) const;
    bool try_apply_move(int from, int die, bool send);

    // Simple greedy heuristic AI (bear off > hit > make a point > avoid blots).
    int  score_move(int to_idx, bool bear_off) const;
    bool cpu_pick_move(int& from_out, int& die_out) const;

    lv_obj_t* create_board();

    static void mode_cpu_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
    static void roll_cb(lv_event_t* e);
    static void bear_off_cb(lv_event_t* e);
    static void board_click_cb(lv_event_t* e);
    static void board_draw_cb(lv_event_t* e);
    static void on_host_ready(const Peer& peer);
    static void on_guest_ready(const Peer& peer);
    static void on_game_data(const char* json);
};
