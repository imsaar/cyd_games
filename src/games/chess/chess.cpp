#include "chess.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <ArduinoJson.h>

static Chess* s_self = nullptr;
static lv_obj_t* ch_invite_msgbox = nullptr;
static IPAddress ch_pending_ip;
static int ch_board_ox = 0, ch_board_oy = 0;

LV_FONT_DECLARE(chess_symbols);

// Use solid (filled) chess symbols for both sides, differentiate by text color
static const char* piece_sym(int8_t p) {
    int abs_p = p < 0 ? -p : p;
    switch (abs_p) {
        case 1: return "\xe2\x99\x9f"; // ♟ pawn
        case 2: return "\xe2\x99\x9e"; // ♞ knight
        case 3: return "\xe2\x99\x9d"; // ♝ bishop
        case 4: return "\xe2\x99\x9c"; // ♜ rook
        case 5: return "\xe2\x99\x9b"; // ♛ queen
        case 6: return "\xe2\x99\x9a"; // ♚ king
        default: return "";
    }
}

// ── Discovery callbacks ──

void ch_on_invite(const Peer& from) {
    if (!s_self || s_self->mode_ != Chess::MODE_LOBBY) return;
    if (ch_invite_msgbox) return;
    ch_pending_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    ch_invite_msgbox = lv_msgbox_create(NULL, "Chess Invite", from.name, btns, false);
    lv_obj_set_size(ch_invite_msgbox, 240, 140);
    lv_obj_center(ch_invite_msgbox);
    lv_obj_set_style_bg_color(ch_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(ch_invite_msgbox, UI_COLOR_TEXT, 0);
    lv_obj_t* btnm = lv_msgbox_get_btns(ch_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t id = lv_msgbox_get_active_btn(ch_invite_msgbox);
        if (id == 0) {
            discovery_send_accept(ch_pending_ip);
            s_self->peer_ip_ = ch_pending_ip;
            s_self->mode_ = Chess::MODE_NETWORK;
            s_self->my_color_white_ = false;
            s_self->my_turn_ = false;
            discovery_set_game("chess", "playing");
            lv_msgbox_close(ch_invite_msgbox); ch_invite_msgbox = nullptr;
            lv_obj_t* scr = s_self->create_board();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else { lv_msgbox_close(ch_invite_msgbox); ch_invite_msgbox = nullptr; }
    }, LV_EVENT_CLICKED, NULL);
}

void ch_on_accept(const Peer& from) {
    if (!s_self || s_self->mode_ != Chess::MODE_LOBBY) return;
    s_self->peer_ip_ = from.ip;
    s_self->mode_ = Chess::MODE_NETWORK;
    s_self->my_color_white_ = true;
    s_self->my_turn_ = true;
    discovery_set_game("chess", "playing");
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void ch_on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != Chess::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

void ch_lobby_peer_cb(lv_event_t* e) {
    if (!s_self) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const Peer* peers = discovery_get_peers();
    int count = discovery_peer_count();
    if (idx < 0 || idx >= count) return;
    discovery_send_invite(peers[idx].ip);
    if (s_self->lobby_list_) {
        lv_obj_clean(s_self->lobby_list_);
        lv_list_add_text(s_self->lobby_list_, "Invite sent, waiting...");
    }
}

// ── Mode selection ──

void Chess::mode_cpu_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU; s_self->my_color_white_ = true;
    s_self->my_turn_ = true; s_self->cpu_pending_ = false;
    discovery_clear_game();
    discovery_on_invite(nullptr); discovery_on_accept(nullptr); discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Chess::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL; s_self->my_color_white_ = true;
    s_self->my_turn_ = true; s_self->cpu_pending_ = false;
    discovery_clear_game();
    discovery_on_invite(nullptr); discovery_on_accept(nullptr); discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Chess::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    discovery_set_game("chess", "waiting");
    discovery_on_invite(ch_on_invite); discovery_on_accept(ch_on_accept);
    discovery_on_game_data(ch_on_game_data);
    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* Chess::create_mode_select() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* t = ui_create_title(scr, "Chess");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    lv_obj_t* b0 = ui_create_btn(scr, "vs CPU", 140, 42);
    lv_obj_align(b0, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(b0, mode_cpu_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* b1 = ui_create_btn(scr, "Local (2P)", 140, 42);
    lv_obj_align(b1, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(b1, mode_local_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* b2 = ui_create_btn(scr, "Network (2P)", 140, 42);
    lv_obj_align(b2, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(b2, mode_online_cb, LV_EVENT_CLICKED, NULL);
    return scr;
}

lv_obj_t* Chess::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* t = ui_create_title(scr, "Chess - Find Opponent");
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
    lobby_list_ = lv_list_create(scr);
    lv_obj_set_size(lobby_list_, 280, 160);
    lv_obj_align(lobby_list_, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(lobby_list_, UI_COLOR_CARD, 0);
    return scr;
}

// ── Board ──

void Chess::init_pieces() {
    for (int i = 0; i < 64; i++) board_[i] = NONE;
    // White (bottom, rows 6-7)
    int8_t back[] = {ROOK, KNIGHT, BISHOP, QUEEN, KING, BISHOP, KNIGHT, ROOK};
    for (int c = 0; c < 8; c++) {
        board_[7*8+c] = back[c];   // White back rank
        board_[6*8+c] = PAWN;      // White pawns
        board_[1*8+c] = -PAWN;     // Black pawns
        board_[0*8+c] = -back[c];  // Black back rank
    }
    white_turn_ = true; game_done_ = false; selected_ = -1;
    w_king_moved_ = b_king_moved_ = false;
    w_rook_a_moved_ = w_rook_h_moved_ = false;
    b_rook_a_moved_ = b_rook_h_moved_ = false;
    en_passant_sq_ = -1;
}

lv_obj_t* Chess::create_board() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_RIGHT, -5, 10);

    int bpx = 8 * CELL;
    ch_board_ox = (320 - bpx) / 2;
    ch_board_oy = 34;

    lv_obj_t* bg = lv_obj_create(scr);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, bpx, bpx);
    lv_obj_set_pos(bg, ch_board_ox, ch_board_oy);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, cell_cb, LV_EVENT_CLICKED, NULL);

    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int idx = r * 8 + c;
            bool light = (r + c) % 2 == 0;
            lv_obj_t* cell = lv_obj_create(bg);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, CELL, CELL);
            lv_obj_set_pos(cell, c * CELL, r * CELL);
            lv_obj_set_style_bg_color(cell,
                light ? lv_color_hex(0xeecea2) : lv_color_hex(0x8b6e4e), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            cell_objs_[idx] = cell;

            lv_obj_t* lbl = lv_label_create(cell);
            lv_label_set_text(lbl, "");
            lv_obj_set_style_text_font(lbl, &chess_symbols, 0);
            lv_obj_center(lbl);
            piece_labels_[idx] = lbl;
        }
    }

    lobby_list_ = nullptr;
    init_pieces();
    draw_board();
    update_status();
    return scr;
}

void Chess::draw_board() {
    for (int i = 0; i < 64; i++) draw_piece(i);
}

void Chess::draw_piece(int idx) {
    lv_obj_t* lbl = piece_labels_[idx];
    if (!lbl) return;
    lv_label_set_text(lbl, piece_sym(board_[idx]));
    if (board_[idx] > 0) {
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
    } else if (board_[idx] < 0) {
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x111111), 0);
    }
}

void Chess::clear_highlights() {
    for (int i = 0; i < 64; i++) {
        bool light = (i/8 + i%8) % 2 == 0;
        lv_obj_set_style_bg_color(cell_objs_[i],
            light ? lv_color_hex(0xeecea2) : lv_color_hex(0x8b6e4e), 0);
        lv_obj_set_style_border_width(cell_objs_[i], 0, 0);
    }
}

void Chess::highlight_cell(int idx, lv_color_t color) {
    // Used for selected piece — fill the cell
    lv_obj_set_style_bg_color(cell_objs_[idx], color, 0);
}

static void outline_cell(lv_obj_t* cell, lv_color_t color) {
    // Used for legal move hints — border outline only, preserve cell color
    lv_obj_set_style_border_color(cell, color, 0);
    lv_obj_set_style_border_width(cell, 2, 0);
}

// ── Move validation ──

bool Chess::is_valid_move(int from, int to, bool check_check) {
    if (from == to) return false;
    if (from < 0 || from >= 64 || to < 0 || to >= 64) return false;
    int8_t p = board_[from];
    if (p == NONE) return false;

    bool is_w = p > 0;
    int8_t target = board_[to];
    // Can't capture own piece
    if (target != NONE && ((target > 0) == is_w)) return false;

    int fr = from/8, fc = from%8, tr = to/8, tc = to%8;
    int dr = tr - fr, dc = tc - fc;
    int adr = dr < 0 ? -dr : dr, adc = dc < 0 ? -dc : dc;
    int type = p < 0 ? -p : p;

    bool valid = false;

    switch (type) {
    case PAWN: {
        int dir = is_w ? -1 : 1;
        int start_row = is_w ? 6 : 1;
        if (dc == 0 && target == NONE) {
            if (dr == dir) valid = true;
            if (dr == 2*dir && fr == start_row && board_[(fr+dir)*8+fc] == NONE) valid = true;
        }
        if (adc == 1 && dr == dir) {
            if (target != NONE) valid = true;
            if (to == en_passant_sq_) valid = true;  // En passant
        }
        break;
    }
    case KNIGHT:
        if ((adr == 2 && adc == 1) || (adr == 1 && adc == 2)) valid = true;
        break;
    case BISHOP:
        if (adr == adc && adr > 0) {
            int sr = dr > 0 ? 1 : -1, sc = dc > 0 ? 1 : -1;
            valid = true;
            for (int i = 1; i < adr; i++) {
                if (board_[(fr+i*sr)*8+(fc+i*sc)] != NONE) { valid = false; break; }
            }
        }
        break;
    case ROOK:
        if ((dr == 0 || dc == 0) && (adr + adc > 0)) {
            int sr = dr == 0 ? 0 : (dr > 0 ? 1 : -1);
            int sc = dc == 0 ? 0 : (dc > 0 ? 1 : -1);
            int steps = adr > adc ? adr : adc;
            valid = true;
            for (int i = 1; i < steps; i++) {
                if (board_[(fr+i*sr)*8+(fc+i*sc)] != NONE) { valid = false; break; }
            }
        }
        break;
    case QUEEN:
        if (adr == adc && adr > 0) {
            int sr = dr > 0 ? 1 : -1, sc = dc > 0 ? 1 : -1;
            valid = true;
            for (int i = 1; i < adr; i++) {
                if (board_[(fr+i*sr)*8+(fc+i*sc)] != NONE) { valid = false; break; }
            }
        } else if ((dr == 0 || dc == 0) && (adr + adc > 0)) {
            int sr = dr == 0 ? 0 : (dr > 0 ? 1 : -1);
            int sc = dc == 0 ? 0 : (dc > 0 ? 1 : -1);
            int steps = adr > adc ? adr : adc;
            valid = true;
            for (int i = 1; i < steps; i++) {
                if (board_[(fr+i*sr)*8+(fc+i*sc)] != NONE) { valid = false; break; }
            }
        }
        break;
    case KING:
        if (adr <= 1 && adc <= 1 && (adr + adc > 0)) valid = true;
        // Castling
        if (dr == 0 && adc == 2 && target == NONE) {
            if (is_w && !w_king_moved_ && fr == 7) {
                if (dc == 2 && !w_rook_h_moved_ && board_[7*8+5]==NONE && board_[7*8+6]==NONE && board_[7*8+7]==ROOK)
                    if (!is_in_check(true)) valid = true;
                if (dc == -2 && !w_rook_a_moved_ && board_[7*8+1]==NONE && board_[7*8+2]==NONE && board_[7*8+3]==NONE && board_[7*8+0]==ROOK)
                    if (!is_in_check(true)) valid = true;
            }
            if (!is_w && !b_king_moved_ && fr == 0) {
                if (dc == 2 && !b_rook_h_moved_ && board_[0*8+5]==NONE && board_[0*8+6]==NONE && board_[0*8+7]==-ROOK)
                    if (!is_in_check(false)) valid = true;
                if (dc == -2 && !b_rook_a_moved_ && board_[0*8+1]==NONE && board_[0*8+2]==NONE && board_[0*8+3]==NONE && board_[0*8+0]==-ROOK)
                    if (!is_in_check(false)) valid = true;
            }
        }
        break;
    }

    if (!valid) return false;

    // Check that move doesn't leave own king in check
    if (check_check) {
        int8_t saved_to = board_[to];
        int8_t saved_from = board_[from];
        board_[to] = board_[from]; board_[from] = NONE;
        // Handle en passant capture
        int8_t saved_ep = NONE; int ep_sq = -1;
        if (type == PAWN && to == en_passant_sq_) {
            ep_sq = is_w ? to + 8 : to - 8;
            saved_ep = board_[ep_sq]; board_[ep_sq] = NONE;
        }
        bool in_check = is_in_check(is_w);
        board_[from] = saved_from; board_[to] = saved_to;
        if (ep_sq >= 0) board_[ep_sq] = saved_ep;
        if (in_check) return false;
    }

    return valid;
}

bool Chess::is_in_check(bool white) {
    // Find king
    int8_t king = white ? KING : -KING;
    int king_sq = -1;
    for (int i = 0; i < 64; i++) {
        if (board_[i] == king) { king_sq = i; break; }
    }
    if (king_sq < 0) return true;

    // Check if any opponent piece attacks the king
    for (int i = 0; i < 64; i++) {
        if (board_[i] == NONE) continue;
        if ((board_[i] > 0) == white) continue;  // Skip own pieces
        if (is_valid_move(i, king_sq, false)) return true;
    }
    return false;
}

bool Chess::has_legal_move(bool white) {
    for (int from = 0; from < 64; from++) {
        if (board_[from] == NONE) continue;
        if ((board_[from] > 0) != white) continue;
        for (int to = 0; to < 64; to++) {
            if (is_valid_move(from, to, true)) return true;
        }
    }
    return false;
}

bool Chess::is_checkmate(bool white) {
    return is_in_check(white) && !has_legal_move(white);
}

bool Chess::is_stalemate(bool white) {
    return !is_in_check(white) && !has_legal_move(white);
}

void Chess::do_move(int from, int to) {
    int8_t p = board_[from];
    bool is_w = p > 0;
    int type = p < 0 ? -p : p;
    int fr = from/8, fc = from%8, tr = to/8, tc = to%8;

    // En passant capture
    if (type == PAWN && to == en_passant_sq_) {
        int cap = is_w ? to + 8 : to - 8;
        board_[cap] = NONE;
    }

    // Update en passant square
    en_passant_sq_ = -1;
    if (type == PAWN && ((fr - tr) == 2 || (tr - fr) == 2)) {
        en_passant_sq_ = (fr + tr) / 2 * 8 + fc;
    }

    // Castling - move rook
    if (type == KING && (tc - fc == 2 || fc - tc == 2)) {
        if (tc == 6) { board_[fr*8+5] = board_[fr*8+7]; board_[fr*8+7] = NONE; }  // Kingside
        if (tc == 2) { board_[fr*8+3] = board_[fr*8+0]; board_[fr*8+0] = NONE; }  // Queenside
    }

    // Update castling rights
    if (type == KING) { if (is_w) w_king_moved_ = true; else b_king_moved_ = true; }
    if (type == ROOK) {
        if (from == 7*8+0) w_rook_a_moved_ = true;
        if (from == 7*8+7) w_rook_h_moved_ = true;
        if (from == 0*8+0) b_rook_a_moved_ = true;
        if (from == 0*8+7) b_rook_h_moved_ = true;
    }

    board_[to] = board_[from];
    board_[from] = NONE;

    // Pawn promotion (auto-queen)
    if (type == PAWN && (tr == 0 || tr == 7)) {
        board_[to] = is_w ? QUEEN : -QUEEN;
    }

    white_turn_ = !white_turn_;
}

// ── Input ──

void Chess::cell_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && !s_self->white_turn_) return;

    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int col = (p.x - ch_board_ox) / CELL;
    int row = (p.y - ch_board_oy) / CELL;
    if (col < 0 || col >= 8 || row < 0 || row >= 8) return;
    int idx = row * 8 + col;

    bool is_my_piece = false;
    if (s_self->board_[idx] != NONE) {
        bool piece_white = s_self->board_[idx] > 0;
        if (s_self->mode_ == MODE_LOCAL) {
            is_my_piece = (piece_white == s_self->white_turn_);
        } else {
            is_my_piece = (piece_white == s_self->white_turn_) &&
                          (piece_white == s_self->my_color_white_ || s_self->mode_ == MODE_CPU);
        }
    }

    // Select a piece
    if (is_my_piece) {
        s_self->clear_highlights();
        s_self->selected_ = idx;
        s_self->highlight_cell(idx, UI_COLOR_WARNING);
        // Show legal move hints as outlines in the piece's color
        lv_color_t hint_color = s_self->is_white(idx)
            ? lv_color_hex(0xffffff) : lv_color_hex(0x111111);
        for (int t = 0; t < 64; t++) {
            if (s_self->is_valid_move(idx, t, true)) {
                outline_cell(s_self->cell_objs_[t], hint_color);
            }
        }
        return;
    }

    // Move selected piece
    if (s_self->selected_ >= 0) {
        int from = s_self->selected_;
        if (s_self->is_valid_move(from, idx, true)) {
            s_self->do_move(from, idx);
            s_self->clear_highlights();
            s_self->draw_board();
            s_self->selected_ = -1;

            if (s_self->mode_ == MODE_NETWORK) {
                s_self->send_move(from, idx);
            }

            // Check game end
            bool opp_white = s_self->white_turn_;
            if (s_self->is_checkmate(opp_white)) {
                s_self->game_done_ = true;
                // Highlight the checkmate move
                s_self->highlight_cell(from, lv_color_hex(0x44ff44));
                s_self->highlight_cell(idx, lv_color_hex(0x44ff44));

                static char chess_result_buf[32];
                static bool chess_result_win;
                if (s_self->mode_ == MODE_NETWORK) {
                    chess_result_win = !s_self->my_turn_;
                    snprintf(chess_result_buf, sizeof(chess_result_buf), "Checkmate!\n%s",
                             chess_result_win ? "You Win!" : "You Lose!");
                } else if (s_self->mode_ == MODE_CPU) {
                    chess_result_win = !opp_white;
                    snprintf(chess_result_buf, sizeof(chess_result_buf), "Checkmate!\n%s",
                             opp_white ? "CPU Wins!" : "You Win!");
                } else {
                    chess_result_win = true;
                    snprintf(chess_result_buf, sizeof(chess_result_buf), "Checkmate!\n%s Wins!",
                             opp_white ? "Black" : "White");
                }
                lv_timer_create([](lv_timer_t* t) {
                    lv_timer_del(t);
                    if (s_self) s_self->show_result(chess_result_buf, chess_result_win);
                }, 3000, NULL);
            } else if (s_self->is_stalemate(opp_white)) {
                s_self->game_done_ = true;
                s_self->show_result("Stalemate!\nDraw", false);
            } else {
                if (s_self->mode_ == MODE_NETWORK && !s_self->game_done_) {
                    s_self->my_turn_ = false;
                }
            }
            s_self->update_status();
        } else {
            // Deselect
            s_self->clear_highlights();
            s_self->selected_ = -1;
        }
    }
}

// ── Status ──

void Chess::update_status() {
    if (!lbl_status_) return;
    if (game_done_) return;
    bool in_chk = is_in_check(white_turn_);
    char buf[32];
    if (mode_ == MODE_CPU) {
        snprintf(buf, sizeof(buf), "%s%s", white_turn_ ? "Your turn" : "CPU...",
                 in_chk ? " CHECK" : "");
    } else if (mode_ == MODE_LOCAL) {
        snprintf(buf, sizeof(buf), "%s%s", white_turn_ ? "White" : "Black",
                 in_chk ? " CHECK" : "");
    } else {
        snprintf(buf, sizeof(buf), "%s%s", my_turn_ ? "Your turn" : "Waiting...",
                 in_chk ? " CHECK" : "");
    }
    lv_label_set_text(lbl_status_, buf);
}

void Chess::show_result(const char* text, bool is_win) {
    if (!screen_) return;
    lv_color_t color = is_win ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT;
    lv_obj_t* ov = lv_obj_create(screen_);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, 280, 140);
    lv_obj_center(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ov, 16, 0);
    lv_obj_set_style_border_color(ov, color, 0);
    lv_obj_set_style_border_width(ov, 3, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t* lbl = lv_label_create(ov);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -15);
    lv_obj_t* btn = ui_create_btn(ov, "Menu", 100, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) { screen_manager_back_to_menu(); }, LV_EVENT_CLICKED, NULL);
}

// ── Networking ──

void Chess::send_move(int from, int to) {
    StaticJsonDocument<128> doc;
    doc["type"] = "move"; doc["game"] = "chess";
    doc["from"] = from; doc["to"] = to;
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

void Chess::onNetworkData(const char* json) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, json)) return;
    const char* g = doc["game"];
    if (!g || strcmp(g, "chess") != 0) return;
    if (doc["abandon"] | false) {
        show_result("Opponent left", false);
        return;
    }
    int from = doc["from"] | -1, to = doc["to"] | -1;
    if (from < 0 || to < 0) return;

    do_move(from, to);
    clear_highlights();
    draw_board();

    if (is_checkmate(white_turn_)) {
        game_done_ = true;
        highlight_cell(from, lv_color_hex(0x44ff44));
        highlight_cell(to, lv_color_hex(0x44ff44));

        static char chess_net_buf[32];
        static bool chess_net_win;
        chess_net_win = my_turn_;
        snprintf(chess_net_buf, sizeof(chess_net_buf), "Checkmate!\n%s",
                 my_turn_ ? "You Win!" : "You Lose!");
        lv_timer_create([](lv_timer_t* t) {
            lv_timer_del(t);
            if (s_self) s_self->show_result(chess_net_buf, chess_net_win);
        }, 3000, NULL);
    } else if (is_stalemate(white_turn_)) {
        game_done_ = true;
        show_result("Stalemate!\nDraw", false);
    } else {
        my_turn_ = true;
    }
    update_status();
}

// ── CPU AI ──

int Chess::piece_value(int8_t p) {
    switch (p < 0 ? -p : p) {
        case PAWN: return 10; case KNIGHT: return 30; case BISHOP: return 30;
        case ROOK: return 50; case QUEEN: return 90; case KING: return 900;
        default: return 0;
    }
}

int Chess::eval_board() {
    int score = 0;
    for (int i = 0; i < 64; i++) {
        if (board_[i] == NONE) continue;
        int v = piece_value(board_[i]);
        score += (board_[i] > 0) ? v : -v;
    }
    return score;
}

void Chess::cpu_move() {
    // Simple 1-ply: try all moves, pick best for black (minimize score)
    struct Move { int from, to, score; };
    Move best = {-1, -1, 99999};

    for (int from = 0; from < 64; from++) {
        if (board_[from] >= 0) continue;  // Black pieces only
        for (int to = 0; to < 64; to++) {
            if (!is_valid_move(from, to, true)) continue;

            // Try move
            int8_t saved_to = board_[to], saved_from = board_[from];
            int8_t saved_ep = NONE; int ep_cap = -1;
            int type = -board_[from];
            if (type == PAWN && to == en_passant_sq_) {
                ep_cap = to - 8; saved_ep = board_[ep_cap]; board_[ep_cap] = NONE;
            }
            board_[to] = board_[from]; board_[from] = NONE;
            if (type == PAWN && to / 8 == 7) board_[to] = -QUEEN;

            int s = eval_board();
            // Bonus for checking opponent
            // Small random factor for variety
            s += random(-2, 3);

            // Undo
            board_[from] = saved_from; board_[to] = saved_to;
            if (ep_cap >= 0) board_[ep_cap] = saved_ep;

            if (s < best.score) {
                best = {from, to, s};
            }
        }
    }

    if (best.from >= 0) {
        do_move(best.from, best.to);
        clear_highlights();
        draw_board();

        if (is_checkmate(true)) {
            game_done_ = true;
            show_result("Checkmate!\nCPU Wins!", false);
        } else if (is_stalemate(true)) {
            game_done_ = true;
            show_result("Stalemate!\nDraw", false);
        }
        update_status();
    }
}

// ── Lifecycle ──

lv_obj_t* Chess::createScreen() {
    s_self = this;
    ch_invite_msgbox = nullptr;
    mode_ = MODE_SELECT;
    screen_ = create_mode_select();
    return screen_;
}

void Chess::update() {
    if (mode_ == MODE_CPU && !white_turn_ && !game_done_) {
        if (!cpu_pending_) {
            cpu_pending_ = true;
            cpu_think_time_ = millis();
        } else if (millis() - cpu_think_time_ > 700) {
            cpu_pending_ = false;
            cpu_move();
        }
    }

    if (mode_ == MODE_LOBBY && lobby_list_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);
            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            int shown = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(peers[i].game, "chess") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)", peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, ch_lobby_peer_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) lv_list_add_text(lobby_list_, "Searching...");
        }
    }
}

void Chess::destroy() {
    if (ch_invite_msgbox) { lv_msgbox_close(ch_invite_msgbox); ch_invite_msgbox = nullptr; }
    if (mode_ == MODE_NETWORK) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"chess\",\"abandon\":true}");
    }
    discovery_clear_game();
    discovery_on_invite(nullptr); discovery_on_accept(nullptr); discovery_on_game_data(nullptr);
    s_self = nullptr; screen_ = nullptr; lbl_status_ = nullptr; lobby_list_ = nullptr;
}
