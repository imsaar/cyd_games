#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Pictionary : public GameBase {
    friend void pict_on_invite(const Peer& from);
    friend void pict_on_accept(const Peer& from);
    friend void pict_on_game_data(const char* json);
    friend void pict_lobby_peer_cb(lv_event_t* e);
    friend lv_obj_t* create_draw_area(lv_obj_t* parent, Pictionary* self);

public:
    static const int NUM_COLORS = 6;

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Pictionary"; }
    uint8_t maxPlayers() const override { return 2; }

    // Stroke point (public for hex encode/decode helpers)
    struct Pt { int16_t x, y; uint8_t color; };

private:
    static const int MAX_PTS = 600;
    Pt pts_[MAX_PTS];
    int pt_count_ = 0;
    bool pen_down_ = false;
    uint8_t cur_color_ = 0;

    // Game state
    enum Phase {
        PHASE_MENU, PHASE_MODE_SELECT, PHASE_LOBBY,
        PHASE_REVEAL, PHASE_DRAW,
        PHASE_WATCH,       // network guesser: see strokes live + can guess early
        PHASE_GUESS,       // local guesser picks answer
        PHASE_WAIT_GUESS,  // network drawer: waiting for remote guess
        PHASE_RESULT, PHASE_GAMEOVER
    };
    Phase phase_ = PHASE_MENU;
    bool network_mode_ = false;
    bool is_host_ = false;     // host = first drawer
    bool is_drawer_ = false;   // am I drawing this round?
    int round_ = 0;
    static const int TOTAL_ROUNDS = 6;
    int score_[2] = {};        // [0]=host/P1, [1]=guest/P2
    int drawer_ = 0;           // 0 or 1
    int word_idx_ = 0;
    int choices_[4] = {};
    int correct_choice_ = 0;
    uint32_t draw_start_ = 0;
    static const int DRAW_TIME = 30;
    bool guessed_early_ = false;  // guesser guessed before Done

    // Network
    IPAddress peer_ip_;
    int last_sent_pt_ = 0;        // index of last sent stroke point
    uint32_t last_send_time_ = 0;

    // Word bank
    static const char* const words_[];
    static const int WORD_COUNT;

    // LVGL objects
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* draw_area_ = nullptr;
    lv_obj_t* lbl_info_ = nullptr;
    lv_obj_t* lbl_timer_ = nullptr;
    lv_obj_t* lbl_score_ = nullptr;
    lv_obj_t* btn_panel_ = nullptr;
    lv_obj_t* choice_btns_[4] = {};
    lv_obj_t* lobby_list_ = nullptr;

    void show_menu();
    void show_mode_select();
    void show_lobby();
    void start_round();
    void show_reveal();
    void start_drawing();
    void start_watching();      // network guesser
    void start_guessing();
    void show_wait_guess();     // network drawer waits
    void show_result(bool correct);
    void show_gameover();
    void clear_ui();
    void pick_word();
    void send_strokes();
    void send_full_sync();
    void send_json(const char* buf);

    static void draw_cb(lv_event_t* e);
    static void start_cb(lv_event_t* e);
    static void reveal_cb(lv_event_t* e);
    static void done_cb(lv_event_t* e);
    static void clear_strokes_cb(lv_event_t* e);
    static void color_cb(lv_event_t* e);
    static void choice_cb(lv_event_t* e);
    static void next_cb(lv_event_t* e);
    static void play_again_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_network_cb(lv_event_t* e);
};
