#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class DotsBoxes : public GameBase {
    friend void db_on_invite(const Peer& from);
    friend void db_on_accept(const Peer& from);
    friend void db_on_game_data(const char* json);
    friend void db_lobby_peer_cb(lv_event_t* e);

public:
    enum Mode { MODE_SELECT, MODE_CPU, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Dots & Boxes"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    // 4x4 dots = 3x3 boxes
    static const int DOTS = 4;
    static const int BOXES = DOTS - 1;  // 3
    static const int GAP = 50;   // Distance between dots
    static const int DOT_R = 4;

    // Lines: horizontal lines = DOTS * (DOTS-1), vertical = same
    // Total lines = 2 * DOTS * BOXES = 24
    static const int H_LINES = DOTS * BOXES;   // 12 horizontal
    static const int V_LINES = BOXES * DOTS;   // 12 vertical
    static const int TOTAL_LINES = H_LINES + V_LINES;  // 24

    enum Player { NONE = 0, P1 = 1, P2 = 2 };

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* line_btns_[TOTAL_LINES] = {};
    lv_obj_t* box_labels_[BOXES * BOXES] = {};
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lbl_score_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;

    bool lines_[TOTAL_LINES] = {};
    int8_t boxes_[BOXES * BOXES] = {};
    Player current_ = P1;
    Player my_player_ = P1;
    Mode mode_ = MODE_SELECT;
    bool my_turn_ = true;
    bool game_done_ = false;
    int score_p1_ = 0;
    int score_p2_ = 0;
    IPAddress peer_ip_;

    lv_obj_t* create_mode_select();
    lv_obj_t* create_board();
    lv_obj_t* create_lobby();
    void reset_board();
    bool place_line(int idx);
    int check_boxes(int line_idx);
    void update_status();
    void update_scores();
    void send_move(int idx);
    void show_result();

    // CPU AI
    void cpu_move();
    int score_line(int idx);
    bool cpu_pending_ = false;
    uint32_t cpu_think_time_ = 0;

    static void line_cb(lv_event_t* e);
    static void mode_cpu_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
};
