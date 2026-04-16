#include "checkers.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>

static Checkers* s_self = nullptr;
static lv_obj_t* ck_invite_msgbox = nullptr;
static IPAddress ck_pending_ip;
static int board_ox_ = 0;
static int board_oy_ = 0;

// ── Discovery callbacks ──

void ck_on_invite(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    if (ck_invite_msgbox) return;
    ck_pending_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    ck_invite_msgbox = lv_msgbox_create(NULL, "Checkers Invite",
        from.name, btns, false);
    lv_obj_set_size(ck_invite_msgbox, 240, 140);
    lv_obj_center(ck_invite_msgbox);
    lv_obj_set_style_bg_color(ck_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(ck_invite_msgbox, UI_COLOR_TEXT, 0);
    lv_obj_t* btnm = lv_msgbox_get_btns(ck_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(ck_invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(ck_pending_ip);
            s_self->peer_ip_ = ck_pending_ip;
            s_self->mode_ = Checkers::MODE_NETWORK;
            s_self->my_color_red_ = false;
            s_self->my_turn_ = false;
            discovery_set_game("checkers", "playing");
            lv_msgbox_close(ck_invite_msgbox);
            ck_invite_msgbox = nullptr;
            lv_obj_t* scr = s_self->create_board();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            lv_msgbox_close(ck_invite_msgbox);
            ck_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void ck_on_accept(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    s_self->peer_ip_ = from.ip;
    s_self->mode_ = Checkers::MODE_NETWORK;
    s_self->my_color_red_ = true;
    s_self->my_turn_ = true;
    discovery_set_game("checkers", "playing");
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void ck_on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != Checkers::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

void ck_lobby_peer_cb(lv_event_t* e) {
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

void Checkers::mode_cpu_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU;
    s_self->my_color_red_ = true;
    s_self->my_turn_ = true;
    s_self->cpu_pending_ = false;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Checkers::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->my_color_red_ = true;
    s_self->my_turn_ = true;
    s_self->cpu_pending_ = false;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Checkers::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    discovery_set_game("checkers", "waiting");
    discovery_on_invite(ck_on_invite);
    discovery_on_accept(ck_on_accept);
    discovery_on_game_data(ck_on_game_data);
    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* Checkers::create_mode_select() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Checkers");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
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

lv_obj_t* Checkers::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Checkers - Find Opponent");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lobby_list_ = lv_list_create(scr);
    lv_obj_set_size(lobby_list_, 280, 160);
    lv_obj_align(lobby_list_, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(lobby_list_, UI_COLOR_CARD, 0);
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap a peer to invite");
    lv_obj_set_style_text_color(hint, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
    return scr;
}

// ── Board setup ──

void Checkers::init_pieces() {
    for (int i = 0; i < 64; i++) board_[i] = EMPTY_CELL;
    // Black pieces on top 3 rows (rows 0-2), on dark squares
    for (int r = 0; r < 3; r++) {
        for (int c = 0; c < 8; c++) {
            if ((r + c) % 2 == 1) board_[r * 8 + c] = BLACK_MAN;
        }
    }
    // Red pieces on bottom 3 rows (rows 5-7)
    for (int r = 5; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            if ((r + c) % 2 == 1) board_[r * 8 + c] = RED_MAN;
        }
    }
    is_red_turn_ = true;
    game_done_ = false;
    selected_ = -1;
    must_jump_ = false;
    jump_piece_ = -1;
}

lv_obj_t* Checkers::create_board() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_RIGHT, -5, 10);

    int board_px = BOARD_SIZE * CELL;
    board_ox_ = (320 - board_px) / 2;
    board_oy_ = 36;

    // Board container
    lv_obj_t* bg = lv_obj_create(scr);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, board_px, board_px);
    lv_obj_set_pos(bg, board_ox_, board_oy_);
    lv_obj_set_style_bg_color(bg, lv_color_hex(0x222222), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bg, cell_cb, LV_EVENT_CLICKED, NULL);

    // Draw cells
    for (int r = 0; r < 8; r++) {
        for (int c = 0; c < 8; c++) {
            int idx = r * 8 + c;
            bool dark = (r + c) % 2 == 1;
            lv_obj_t* cell = lv_obj_create(bg);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, CELL, CELL);
            lv_obj_set_pos(cell, c * CELL, r * CELL);
            lv_obj_set_style_bg_color(cell,
                dark ? lv_color_hex(0xcc2222) : lv_color_hex(0x111111), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            cell_objs_[idx] = cell;

            // Piece (circle on top of cell)
            lv_obj_t* piece = lv_obj_create(cell);
            lv_obj_remove_style_all(piece);
            lv_obj_set_size(piece, CELL - 6, CELL - 6);
            lv_obj_center(piece);
            lv_obj_set_style_radius(piece, (CELL - 6) / 2, 0);
            lv_obj_set_style_bg_opa(piece, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(piece, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            piece_objs_[idx] = piece;
        }
    }

    lobby_list_ = nullptr;
    init_pieces();
    draw_board();
    update_status();
    return scr;
}

void Checkers::draw_board() {
    for (int i = 0; i < 64; i++) draw_piece(i);
}

void Checkers::draw_piece(int idx) {
    lv_obj_t* p = piece_objs_[idx];
    if (!p) return;
    int8_t val = board_[idx];
    if (val == EMPTY_CELL) {
        lv_obj_set_style_bg_opa(p, LV_OPA_TRANSP, 0);
        return;
    }
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    bool is_red = val > 0;
    bool is_king = (val == RED_KING || val == BLACK_KING);
    lv_obj_set_style_bg_color(p,
        is_red ? lv_color_hex(0xeeeeee) : lv_color_hex(0x222222), 0);
    if (is_king) {
        lv_obj_set_style_border_color(p, UI_COLOR_WARNING, 0);
        lv_obj_set_style_border_width(p, 2, 0);
    } else {
        lv_obj_set_style_border_width(p, 0, 0);
    }
}

void Checkers::clear_highlights() {
    for (int i = 0; i < 64; i++) {
        bool dark = (i / 8 + i % 8) % 2 == 1;
        lv_obj_set_style_bg_color(cell_objs_[i],
            dark ? lv_color_hex(0xcc2222) : lv_color_hex(0x111111), 0);
    }
}

void Checkers::highlight_cell(int idx, lv_color_t color) {
    lv_obj_set_style_bg_color(cell_objs_[idx], color, 0);
}

bool Checkers::is_mine(int idx) {
    if (mode_ == MODE_LOCAL) {
        return (is_red_turn_ && board_[idx] > 0) || (!is_red_turn_ && board_[idx] < 0);
    }
    // Network: check if piece belongs to me
    if (my_color_red_) return board_[idx] > 0;
    return board_[idx] < 0;
}

bool Checkers::can_jump(int idx) {
    int8_t val = board_[idx];
    if (val == EMPTY_CELL) return false;
    int r = idx / 8, c = idx % 8;
    bool is_red = val > 0;
    bool is_king = (val == RED_KING || val == BLACK_KING);

    // Direction: red moves up (-1), black moves down (+1), kings both
    int dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
    int start = 0, end = 4;
    if (!is_king) {
        if (is_red) { start = 0; end = 2; }   // Up only
        else        { start = 2; end = 4; }    // Down only
    }

    for (int d = start; d < end; d++) {
        int mr = r + dirs[d][0], mc = c + dirs[d][1];
        int jr = r + 2*dirs[d][0], jc = c + 2*dirs[d][1];
        if (jr < 0 || jr >= 8 || jc < 0 || jc >= 8) continue;
        int mid = mr * 8 + mc;
        int dest = jr * 8 + jc;
        // Can jump if: middle has opponent piece, destination empty
        if (board_[dest] != EMPTY_CELL) continue;
        if (is_red && board_[mid] < 0) return true;
        if (!is_red && board_[mid] > 0) return true;
    }
    return false;
}

bool Checkers::try_move(int from, int to) {
    int fr = from / 8, fc = from % 8;
    int tr = to / 8, tc = to % 8;
    int8_t val = board_[from];
    bool is_red = val > 0;
    bool is_king = (val == RED_KING || val == BLACK_KING);

    int dr = tr - fr, dc = tc - fc;

    // Must be diagonal
    if (abs(dr) != abs(dc)) return false;
    if (board_[to] != EMPTY_CELL) return false;

    // Check direction for non-kings
    if (!is_king) {
        if (is_red && dr > 0) return false;   // Red can only move up
        if (!is_red && dr < 0) return false;   // Black can only move down
    }

    // Simple move (1 diagonal)
    if (abs(dr) == 1) {
        if (must_jump_) return false;  // Must jump if available
        // Check if any jump is available for current player
        for (int i = 0; i < 64; i++) {
            if ((is_red && board_[i] > 0) || (!is_red && board_[i] < 0)) {
                if (can_jump(i)) return false;  // Forced jump
            }
        }
        board_[to] = board_[from];
        board_[from] = EMPTY_CELL;
        promote_if_needed(to);
        sound_move();
        return true;
    }

    // Jump (2 diagonal)
    if (abs(dr) == 2) {
        int mr = (fr + tr) / 2, mc = (fc + tc) / 2;
        int mid = mr * 8 + mc;
        // Must jump over opponent
        if (is_red && board_[mid] >= 0) return false;
        if (!is_red && board_[mid] <= 0) return false;

        board_[to] = board_[from];
        board_[from] = EMPTY_CELL;
        board_[mid] = EMPTY_CELL;
        promote_if_needed(to);
        sound_move();

        // Check for multi-jump
        if (can_jump(to)) {
            must_jump_ = true;
            jump_piece_ = to;
            return true;
        }
        must_jump_ = false;
        jump_piece_ = -1;
        return true;
    }

    return false;
}

void Checkers::promote_if_needed(int idx) {
    int r = idx / 8;
    if (board_[idx] == RED_MAN && r == 0) board_[idx] = RED_KING;
    if (board_[idx] == BLACK_MAN && r == 7) board_[idx] = BLACK_KING;
}

bool Checkers::has_any_move(bool red) {
    for (int i = 0; i < 64; i++) {
        int8_t val = board_[i];
        if (red && val <= 0) continue;
        if (!red && val >= 0) continue;
        bool is_king = (val == RED_KING || val == BLACK_KING);
        int r = i / 8, c = i % 8;

        int dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
        int start = 0, end = 4;
        if (!is_king) {
            if (red) { start = 0; end = 2; }
            else     { start = 2; end = 4; }
        }
        for (int d = start; d < end; d++) {
            int nr = r + dirs[d][0], nc = c + dirs[d][1];
            if (nr >= 0 && nr < 8 && nc >= 0 && nc < 8 && board_[nr*8+nc] == EMPTY_CELL) return true;
            // Check jump
            int jr = r + 2*dirs[d][0], jc = c + 2*dirs[d][1];
            int mr = r + dirs[d][0], mc = c + dirs[d][1];
            if (jr >= 0 && jr < 8 && jc >= 0 && jc < 8 && board_[jr*8+jc] == EMPTY_CELL) {
                int mid = mr*8+mc;
                if (red && board_[mid] < 0) return true;
                if (!red && board_[mid] > 0) return true;
            }
        }
    }
    return false;
}

void Checkers::check_game_over() {
    int red_count = 0, black_count = 0;
    for (int i = 0; i < 64; i++) {
        if (board_[i] > 0) red_count++;
        if (board_[i] < 0) black_count++;
    }

    bool over = false;
    bool red_wins = false;

    if (red_count == 0) { over = true; red_wins = false; }
    else if (black_count == 0) { over = true; red_wins = true; }
    else if (!has_any_move(is_red_turn_)) {
        over = true;
        red_wins = !is_red_turn_;  // Current player loses
    }

    if (over) {
        game_done_ = true;

        // Highlight the last move
        if (selected_ >= 0) highlight_cell(selected_, lv_color_hex(0x44ff44));

        static char chk_result_buf[32];
        static bool chk_result_win;
        if (mode_ == MODE_NETWORK) {
            chk_result_win = (my_color_red_ == red_wins);
            snprintf(chk_result_buf, sizeof(chk_result_buf), "%s",
                     chk_result_win ? "You Win!" : "You Lose!");
        } else if (mode_ == MODE_CPU) {
            chk_result_win = red_wins;
            snprintf(chk_result_buf, sizeof(chk_result_buf), "%s",
                     red_wins ? "You Win!" : "CPU Wins!");
        } else {
            chk_result_win = true;
            snprintf(chk_result_buf, sizeof(chk_result_buf), "%s Wins!",
                     red_wins ? "Red" : "Black");
        }
        lv_timer_create([](lv_timer_t* t) {
            lv_timer_del(t);
            if (s_self) s_self->show_result(chk_result_buf, chk_result_win);
        }, 3000, NULL);
    }
}

void Checkers::cell_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && !s_self->is_red_turn_) return;

    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    int col = (p.x - board_ox_) / CELL;
    int row = (p.y - board_oy_) / CELL;
    if (col < 0 || col >= 8 || row < 0 || row >= 8) return;
    int idx = row * 8 + col;

    // If in multi-jump, only allow moving the jumping piece
    if (s_self->must_jump_ && s_self->selected_ != -1 && idx != s_self->selected_) {
        // Tap is a destination for the jump
        if (s_self->try_move(s_self->selected_, idx)) {
            int from = s_self->selected_;
            s_self->clear_highlights();
            s_self->draw_board();

            if (s_self->must_jump_) {
                // Multi-jump continues
                s_self->selected_ = s_self->jump_piece_;
                s_self->highlight_cell(s_self->selected_, UI_COLOR_WARNING);
                if (s_self->mode_ == MODE_NETWORK) s_self->send_move(from, idx);
                return;
            }

            s_self->selected_ = -1;
            if (s_self->mode_ == MODE_NETWORK) {
                s_self->send_move(from, idx);
            }
            s_self->is_red_turn_ = !s_self->is_red_turn_;
            if (s_self->mode_ == MODE_NETWORK && !s_self->game_done_) {
                s_self->my_turn_ = false;
            }
            s_self->check_game_over();
            s_self->update_status();
            return;
        }
        return;
    }

    // Selecting own piece
    if (s_self->board_[idx] != Checkers::EMPTY_CELL && s_self->is_mine(idx)) {
        // In local mode, check it's the right player's piece
        if (s_self->mode_ == MODE_LOCAL) {
            bool piece_is_red = s_self->board_[idx] > 0;
            if (piece_is_red != s_self->is_red_turn_) return;
        }
        s_self->clear_highlights();
        s_self->selected_ = idx;
        s_self->highlight_cell(idx, UI_COLOR_WARNING);
        return;
    }

    // Moving selected piece
    if (s_self->selected_ >= 0 && s_self->board_[idx] == Checkers::EMPTY_CELL) {
        int from = s_self->selected_;
        if (s_self->try_move(from, idx)) {
            s_self->clear_highlights();
            s_self->draw_board();

            if (s_self->must_jump_) {
                s_self->selected_ = s_self->jump_piece_;
                s_self->highlight_cell(s_self->selected_, UI_COLOR_WARNING);
                if (s_self->mode_ == MODE_NETWORK) s_self->send_move(from, idx);
                return;
            }

            s_self->selected_ = -1;
            if (s_self->mode_ == MODE_NETWORK) {
                s_self->send_move(from, idx);
            }
            s_self->is_red_turn_ = !s_self->is_red_turn_;
            if (s_self->mode_ == MODE_NETWORK && !s_self->game_done_) {
                s_self->my_turn_ = false;
            }
            s_self->check_game_over();
            s_self->update_status();
        }
    }
}

void Checkers::update_status() {
    if (!lbl_status_) return;
    if (mode_ == MODE_CPU) {
        lv_label_set_text(lbl_status_, is_red_turn_ ? "Your turn" : "CPU thinking...");
    } else if (mode_ == MODE_LOCAL) {
        lv_label_set_text(lbl_status_, is_red_turn_ ? "Red's turn" : "Black's turn");
    } else {
        lv_label_set_text(lbl_status_, my_turn_ ? "Your turn" : "Waiting...");
    }
}

void Checkers::show_result(const char* text, bool is_win) {
    if (!screen_) return;
    if (is_win) sound_win(); else sound_lose();
    lv_color_t color = is_win ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT;
    lv_obj_t* overlay = lv_obj_create(screen_);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 280, 140);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(overlay, 16, 0);
    lv_obj_set_style_border_color(overlay, color, 0);
    lv_obj_set_style_border_width(overlay, 3, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(overlay);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t* btn = ui_create_btn(overlay, "Menu", 100, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        screen_manager_back_to_menu();
    }, LV_EVENT_CLICKED, NULL);
}

// ── CPU AI ──

int Checkers::eval_board() {
    int score = 0;
    for (int i = 0; i < 64; i++) {
        switch (board_[i]) {
            case RED_MAN:    score += 3; break;
            case RED_KING:   score += 5; break;
            case BLACK_MAN:  score -= 3; break;
            case BLACK_KING: score -= 5; break;
            default: break;
        }
    }
    return score;
}

void Checkers::cpu_move() {
    // Collect all legal moves for black (CPU)
    struct Move { int from; int to; bool is_jump; int score; };
    Move moves[64];
    int n_moves = 0;

    // Check if any jump is available (forced)
    bool any_jump = false;
    for (int i = 0; i < 64; i++) {
        if (board_[i] >= 0) continue;  // Skip non-black
        if (can_jump(i)) { any_jump = true; break; }
    }

    for (int i = 0; i < 64; i++) {
        if (board_[i] >= 0) continue;
        int r = i / 8, c = i % 8;
        bool is_king = (board_[i] == BLACK_KING);

        int dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
        int start = is_king ? 0 : 2;  // Black moves down (rows 2,3)
        int end = is_king ? 4 : 4;

        for (int d = start; d < end; d++) {
            if (any_jump) {
                // Only collect jumps
                int jr = r + 2*dirs[d][0], jc = c + 2*dirs[d][1];
                int mr = r + dirs[d][0], mc = c + dirs[d][1];
                if (jr < 0 || jr >= 8 || jc < 0 || jc >= 8) continue;
                int mid = mr*8+mc, dest = jr*8+jc;
                if (board_[dest] != EMPTY_CELL) continue;
                if (board_[mid] <= 0) continue;  // Must jump over red
                if (n_moves < 64) {
                    // Evaluate: try move, score, undo
                    int8_t saved_from = board_[i], saved_mid = board_[mid];
                    board_[dest] = board_[i]; board_[i] = EMPTY_CELL; board_[mid] = EMPTY_CELL;
                    if (dest / 8 == 7 && board_[dest] == BLACK_MAN) board_[dest] = BLACK_KING;
                    int s = -eval_board();  // CPU wants low score (black is negative)
                    board_[i] = saved_from; board_[mid] = saved_mid; board_[dest] = EMPTY_CELL;
                    moves[n_moves++] = {i, dest, true, s};
                }
            } else {
                // Simple moves
                int nr = r + dirs[d][0], nc = c + dirs[d][1];
                if (nr < 0 || nr >= 8 || nc < 0 || nc >= 8) continue;
                int dest = nr*8+nc;
                if (board_[dest] != EMPTY_CELL) continue;
                if (n_moves < 64) {
                    int8_t saved = board_[i];
                    board_[dest] = board_[i]; board_[i] = EMPTY_CELL;
                    if (dest / 8 == 7 && board_[dest] == BLACK_MAN) board_[dest] = BLACK_KING;
                    int s = -eval_board();
                    board_[i] = saved; board_[dest] = EMPTY_CELL;
                    moves[n_moves++] = {i, dest, false, s};
                }
            }
        }
    }

    if (n_moves == 0) return;

    // Find best score
    int best = moves[0].score;
    for (int i = 1; i < n_moves; i++) {
        if (moves[i].score > best) best = moves[i].score;
    }

    // Collect all moves with best score, pick random
    Move candidates[64];
    int n_cand = 0;
    for (int i = 0; i < n_moves; i++) {
        if (moves[i].score == best) candidates[n_cand++] = moves[i];
    }
    Move& pick = candidates[random(0, n_cand)];

    try_move(pick.from, pick.to);
    clear_highlights();
    draw_board();

    // Handle multi-jump
    while (must_jump_ && jump_piece_ >= 0) {
        // Find a jump for the jumping piece
        bool found = false;
        int idx = jump_piece_;
        int r = idx / 8, c = idx % 8;
        bool is_king = (board_[idx] == BLACK_KING);
        int dirs[4][2] = {{-1,-1},{-1,1},{1,-1},{1,1}};
        int ds = is_king ? 0 : 2, de = 4;
        for (int d = ds; d < de; d++) {
            int jr = r + 2*dirs[d][0], jc = c + 2*dirs[d][1];
            int mr = r + dirs[d][0], mc = c + dirs[d][1];
            if (jr < 0 || jr >= 8 || jc < 0 || jc >= 8) continue;
            if (board_[jr*8+jc] != EMPTY_CELL) continue;
            if (board_[mr*8+mc] <= 0) continue;
            try_move(idx, jr*8+jc);
            clear_highlights();
            draw_board();
            found = true;
            break;
        }
        if (!found) break;
    }

    is_red_turn_ = true;
    must_jump_ = false;
    jump_piece_ = -1;
    check_game_over();
    update_status();
}

void Checkers::send_move(int from, int to) {
    StaticJsonDocument<128> doc;
    doc["type"] = "move";
    doc["game"] = "checkers";
    doc["from"] = from;
    doc["to"] = to;
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

// ── Lifecycle ──

lv_obj_t* Checkers::createScreen() {
    s_self = this;
    ck_invite_msgbox = nullptr;
    mode_ = MODE_SELECT;
    screen_ = create_mode_select();
    return screen_;
}

void Checkers::update() {
    // CPU move with delay
    if (mode_ == MODE_CPU && !is_red_turn_ && !game_done_) {
        if (!cpu_pending_) {
            cpu_pending_ = true;
            cpu_think_time_ = millis();
        } else if (millis() - cpu_think_time_ > 600) {
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
                if (strcmp(peers[i].game, "checkers") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)",
                             peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, ck_lobby_peer_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) lv_list_add_text(lobby_list_, "Searching...");
        }
    }
}

void Checkers::destroy() {
    if (ck_invite_msgbox) {
        lv_msgbox_close(ck_invite_msgbox);
        ck_invite_msgbox = nullptr;
    }
    if (mode_ == MODE_NETWORK) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"checkers\",\"abandon\":true}");
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    lbl_status_ = nullptr;
    lobby_list_ = nullptr;
}

void Checkers::onNetworkData(const char* json) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "checkers") != 0) return;
    if (doc["abandon"] | false) {
        show_result("Opponent left", false);
        return;
    }
    int from = doc["from"] | -1;
    int to = doc["to"] | -1;
    if (from < 0 || from >= 64 || to < 0 || to >= 64) return;

    sound_opponent_move();
    try_move(from, to);
    clear_highlights();
    draw_board();

    if (must_jump_) {
        // Opponent is multi-jumping, stay as their turn
        highlight_cell(jump_piece_, UI_COLOR_WARNING);
        return;
    }

    is_red_turn_ = !is_red_turn_;
    if (!game_done_) {
        my_turn_ = true;
        check_game_over();
    }
    update_status();
}
