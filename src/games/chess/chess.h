#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Chess : public GameBase {
    friend void ch_on_invite(const Peer& from);
    friend void ch_on_accept(const Peer& from);
    friend void ch_on_game_data(const char* json);
    friend void ch_lobby_peer_cb(lv_event_t* e);

public:
    enum Mode { MODE_SELECT, MODE_CPU, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };

    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Chess"; }
    uint8_t maxPlayers() const override { return 2; }

private:
    static const int CELL = 26;

    // Piece encoding: sign=color (+white, -black), value=type
    enum PieceType : int8_t {
        NONE = 0, PAWN = 1, KNIGHT = 2, BISHOP = 3, ROOK = 4, QUEEN = 5, KING = 6
    };

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* cell_objs_[64] = {};
    lv_obj_t* piece_labels_[64] = {};
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;

    int8_t board_[64] = {};
    int selected_ = -1;
    bool white_turn_ = true;
    bool my_color_white_ = true;
    Mode mode_ = MODE_SELECT;
    bool my_turn_ = true;
    bool game_done_ = false;
    IPAddress peer_ip_;

    // Castling rights
    bool w_king_moved_ = false, b_king_moved_ = false;
    bool w_rook_a_moved_ = false, w_rook_h_moved_ = false;
    bool b_rook_a_moved_ = false, b_rook_h_moved_ = false;
    int en_passant_sq_ = -1;  // Square where en passant capture is possible

    // CPU
    bool cpu_pending_ = false;
    uint32_t cpu_think_time_ = 0;

    lv_obj_t* create_mode_select();
    lv_obj_t* create_board();
    lv_obj_t* create_lobby();
    void init_pieces();
    void draw_board();
    void draw_piece(int idx);
    void clear_highlights();
    void highlight_cell(int idx, lv_color_t color);

    bool is_white(int idx) { return board_[idx] > 0; }
    bool is_black(int idx) { return board_[idx] < 0; }
    int abs_piece(int idx) { return board_[idx] < 0 ? -board_[idx] : board_[idx]; }

    bool is_valid_move(int from, int to, bool check_check = true);
    bool is_in_check(bool white);
    bool is_checkmate(bool white);
    bool is_stalemate(bool white);
    bool has_legal_move(bool white);
    void do_move(int from, int to);
    void update_status();
    void send_move(int from, int to);
    void show_result(const char* text, bool is_win);

    // CPU AI
    void cpu_move();
    int eval_board();
    int piece_value(int8_t p);

    static void cell_cb(lv_event_t* e);
    static void mode_cpu_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
};
