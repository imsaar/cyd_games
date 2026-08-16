#include "connect4.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../net/mp_shell.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>

static Connect4* s_self = nullptr;
static const MpShellConfig kCfg = { "connect4", "Connect 4", /*show_cpu_button=*/true, /*show_idle_peers=*/false };

// ── Mode selection ──

void Connect4::mode_cpu_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU;
    s_self->my_color_ = RED;
    s_self->my_turn_ = true;
    s_self->cpu_pending_ = false;
    mp_shell_end(s_self->peer_ip_, false);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Connect4::mode_local_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->my_color_ = RED;
    s_self->my_turn_ = true;
    s_self->cpu_pending_ = false;
    mp_shell_end(s_self->peer_ip_, false);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Connect4::mode_online_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    lv_obj_t* scr = mp_shell_host_lobby(kCfg, on_host_ready, on_guest_ready, on_game_data, nullptr);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Connect4::on_host_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->mode_ = MODE_NETWORK;
    s_self->my_color_ = RED;
    s_self->my_turn_ = true;
    s_self->peer_ip_ = peer.ip;
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Connect4::on_guest_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->mode_ = MODE_NETWORK;
    s_self->my_color_ = YELLOW;
    s_self->my_turn_ = false;
    s_self->peer_ip_ = peer.ip;
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Connect4::on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != Connect4::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

// ── Board ──

void Connect4::reset_board() {
    for (int i = 0; i < COLS * ROWS; i++) board_[i] = EMPTY;
    current_ = RED;
    game_done_ = false;
    net_reset_sync();
}

// Board origin stored for tap-to-column calculation
static int board_ox_ = 0;

lv_obj_t* Connect4::create_board() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_RIGHT, -10, 10);

    // Board background — tap it to drop a disc
    int board_w = COLS * CELL + 2;
    int board_h = ROWS * CELL + 2;
    int ox = (320 - board_w) / 2;
    int oy = 38;
    board_ox_ = ox;

    lv_obj_t* bg = lv_obj_create(scr);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, board_w, board_h);
    lv_obj_set_pos(bg, ox, oy);
    lv_obj_set_style_bg_color(bg, UI_COLOR_PRIMARY, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, 6, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(bg, LV_OBJ_FLAG_CLICKABLE);

    // Tap on the board → determine column from X position
    lv_obj_add_event_cb(bg, col_cb, LV_EVENT_CLICKED, NULL);

    // Grid cells (visual only)
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int idx = r * COLS + c;
            lv_obj_t* cell = lv_obj_create(bg);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, CELL - 4, CELL - 4);
            lv_obj_set_pos(cell, c * CELL + 3, r * CELL + 3);
            lv_obj_set_style_bg_color(cell, UI_COLOR_BG, 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(cell, (CELL - 4) / 2, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            board_objs_[idx] = cell;
        }
    }

    reset_board();
    update_status();
    return scr;
}

void Connect4::col_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && s_self->current_ == YELLOW) return;  // CPU's turn

    // Determine column from tap X position
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int col = (p.x - board_ox_) / CELL;
    if (col < 0 || col >= COLS) return;

    int row = s_self->drop_disc(col);
    if (row < 0) return;  // Column full

    if (s_self->mode_ == MODE_NETWORK) {
        s_self->send_move(col);
        if (!s_self->game_done_) {
            s_self->my_turn_ = false;
            s_self->update_status();
        }
    }
}

int Connect4::drop_disc(int col) {
    // Find lowest empty row in column
    int row = -1;
    for (int r = ROWS - 1; r >= 0; r--) {
        if (board_[r * COLS + col] == EMPTY) {
            row = r;
            break;
        }
    }
    if (row < 0) return -1;

    int idx = row * COLS + col;
    board_[idx] = current_;
    sound_move();

    // Update visual
    lv_color_t color = (current_ == RED) ? lv_color_hex(0xff0000) : lv_color_hex(0xffdd00);
    lv_obj_set_style_bg_color(board_objs_[idx], color, 0);

    if (check_win(row, col)) {
        game_done_ = true;

        // Highlight winning 4 cells
        for (int i = 0; i < 4; i++) {
            if (win_cells_[i] >= 0) {
                lv_obj_set_style_bg_color(board_objs_[win_cells_[i]],
                    lv_color_hex(0x44ff44), 0);
            }
        }

        static char result_buf[32];
        static bool result_is_win;
        if (mode_ == MODE_NETWORK) {
            result_is_win = (current_ == my_color_);
            snprintf(result_buf, sizeof(result_buf), "%s",
                     result_is_win ? "You Win!" : "You Lose!");
        } else if (mode_ == MODE_CPU) {
            result_is_win = (current_ == RED);
            snprintf(result_buf, sizeof(result_buf), "%s",
                     current_ == RED ? "You Win!" : "CPU Wins!");
        } else {
            result_is_win = true;
            snprintf(result_buf, sizeof(result_buf), "%s Wins!",
                     current_ == RED ? "Red" : "Yellow");
        }

        lv_timer_create([](lv_timer_t* t) {
            lv_timer_del(t);
            if (s_self) s_self->show_result(result_buf, result_is_win);
        }, 3000, NULL);
        return row;
    }

    if (board_full()) {
        game_done_ = true;
        show_result("Draw!", false);
        return row;
    }

    current_ = (current_ == RED) ? YELLOW : RED;
    update_status();
    return row;
}

bool Connect4::check_win(int row, int col) {
    Cell c = (Cell)board_[row * COLS + col];
    static const int dx[] = {1, 0, 1, 1};
    static const int dy[] = {0, 1, 1, -1};

    for (int d = 0; d < 4; d++) {
        // Collect all connected cells in this direction
        int cells[7]; // max possible connected
        int n = 0;
        // Backward
        for (int i = 3; i >= 1; i--) {
            int nr = row - dy[d] * i;
            int nc = col - dx[d] * i;
            if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) continue;
            if (board_[nr * COLS + nc] != c) { n = 0; continue; }
            cells[n++] = nr * COLS + nc;
        }
        cells[n++] = row * COLS + col; // center
        // Forward
        for (int i = 1; i <= 3; i++) {
            int nr = row + dy[d] * i;
            int nc = col + dx[d] * i;
            if (nr < 0 || nr >= ROWS || nc < 0 || nc >= COLS) break;
            if (board_[nr * COLS + nc] != c) break;
            cells[n++] = nr * COLS + nc;
        }
        // Find a run of 4 within collected cells
        if (n >= 4) {
            // Take the last 4 connected that include the placed cell
            // Simple: scan for 4 consecutive
            int run = 1;
            for (int i = 1; i < n; i++) {
                // Check adjacency (cells should be sequential)
                run++;
                if (run >= 4) {
                    for (int j = 0; j < 4; j++) win_cells_[j] = cells[i - 3 + j];
                    return true;
                }
            }
        }
    }
    win_cells_[0] = -1;
    return false;
}

bool Connect4::board_full() {
    for (int c = 0; c < COLS; c++) {
        if (board_[c] == EMPTY) return false;  // Top row
    }
    return true;
}

void Connect4::update_status() {
    if (!lbl_status_) return;
    if (mode_ == MODE_CPU) {
        lv_label_set_text(lbl_status_, current_ == RED ? "Your turn" : "CPU thinking...");
    } else if (mode_ == MODE_LOCAL) {
        lv_label_set_text(lbl_status_, current_ == RED ? "Red's turn" : "Yellow's turn");
    } else {
        lv_label_set_text(lbl_status_, my_turn_ ? "Your turn" : "Waiting...");
    }
}

void Connect4::show_result(const char* text, bool is_win) {
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

    lv_obj_t* again_btn = ui_create_btn(overlay, "Play Again", 120, 36);
    lv_obj_align(again_btn, LV_ALIGN_BOTTOM_MID, -65, -15);
    lv_obj_add_event_cb(again_btn, [](lv_event_t* e) {
        screen_manager_switch(screen_manager_current());
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn = ui_create_btn(overlay, "Menu", 90, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 70, -15);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        screen_manager_back_to_menu();
    }, LV_EVENT_CLICKED, NULL);
}

// ── CPU AI (minimax with alpha-beta pruning) ──

static const int C4_WIN_SCORE = 1000000;
static const int C4_SEARCH_DEPTH = 6;

// Column visit order, center-out, gives alpha-beta far more early cutoffs.
static void c4_col_order(int order[], int cols) {
    int mid = cols / 2;
    int oi = 0;
    order[oi++] = mid;
    for (int d = 1; d <= mid; d++) {
        if (mid - d >= 0) order[oi++] = mid - d;
        if (mid + d < cols) order[oi++] = mid + d;
    }
}

// Heuristic score of the current board from YELLOW's (CPU's) perspective.
int Connect4::evaluate_board() {
    int score = 0;

    // Center-column control tends to open the most future lines.
    for (int r = 0; r < ROWS; r++) {
        Cell c = (Cell)board_[r * COLS + COLS / 2];
        if (c == YELLOW) score += 3;
        else if (c == RED) score -= 3;
    }

    static const int dx[] = {1, 0, 1, 1};
    static const int dy[] = {0, 1, 1, -1};
    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            for (int d = 0; d < 4; d++) {
                int r2 = r + dy[d] * 3;
                int c2 = c + dx[d] * 3;
                if (r2 < 0 || r2 >= ROWS || c2 < 0 || c2 >= COLS) continue;

                int yellow = 0, red = 0, empty = 0;
                for (int k = 0; k < 4; k++) {
                    int rr = r + dy[d] * k;
                    int cc = c + dx[d] * k;
                    Cell v = (Cell)board_[rr * COLS + cc];
                    if (v == YELLOW) yellow++;
                    else if (v == RED) red++;
                    else empty++;
                }
                if (yellow > 0 && red > 0) continue;  // blocked window

                if (yellow == 3 && empty == 1) score += 50;
                else if (yellow == 2 && empty == 2) score += 10;
                else if (yellow == 1 && empty == 3) score += 1;

                if (red == 3 && empty == 1) score -= 50;
                else if (red == 2 && empty == 2) score -= 10;
                else if (red == 1 && empty == 3) score -= 1;
            }
        }
    }
    return score;
}

int Connect4::minimax(int depth, int alpha, int beta, bool maximizing) {
    if (board_full()) return 0;
    if (depth == 0) return evaluate_board();

    int order[COLS];
    c4_col_order(order, COLS);
    Cell who = maximizing ? YELLOW : RED;

    int best = maximizing ? -C4_WIN_SCORE * 2 : C4_WIN_SCORE * 2;
    for (int i = 0; i < COLS; i++) {
        int col = order[i];
        int row = -1;
        for (int r = ROWS - 1; r >= 0; r--) {
            if (board_[r * COLS + col] == EMPTY) { row = r; break; }
        }
        if (row < 0) continue;  // column full

        board_[row * COLS + col] = who;
        int score;
        if (check_win(row, col)) {
            score = maximizing ? (C4_WIN_SCORE + depth) : -(C4_WIN_SCORE + depth);
        } else {
            score = minimax(depth - 1, alpha, beta, !maximizing);
        }
        board_[row * COLS + col] = EMPTY;

        if (maximizing) {
            if (score > best) best = score;
            if (best > alpha) alpha = best;
        } else {
            if (score < best) best = score;
            if (best < beta) beta = best;
        }
        if (alpha >= beta) break;  // prune
    }
    return best;
}

int Connect4::cpu_pick_col() {
    int order[COLS];
    c4_col_order(order, COLS);

    int best_col = -1;
    int best_score = -C4_WIN_SCORE * 2;
    int alpha = -C4_WIN_SCORE * 2, beta = C4_WIN_SCORE * 2;

    for (int i = 0; i < COLS; i++) {
        int col = order[i];
        int row = -1;
        for (int r = ROWS - 1; r >= 0; r--) {
            if (board_[r * COLS + col] == EMPTY) { row = r; break; }
        }
        if (row < 0) continue;

        board_[row * COLS + col] = YELLOW;
        int score;
        if (check_win(row, col)) {
            score = C4_WIN_SCORE + C4_SEARCH_DEPTH;
        } else {
            score = minimax(C4_SEARCH_DEPTH - 1, alpha, beta, false);
        }
        board_[row * COLS + col] = EMPTY;

        if (score > best_score) {
            best_score = score;
            best_col = col;
        }
        if (best_score > alpha) alpha = best_score;
    }
    return best_col;
}

void Connect4::send_move(int col) {
    net_mc_++;
    StaticJsonDocument<160> doc;
    doc["type"] = "move";
    doc["game"] = "connect4";
    doc["col"] = col;
    doc["mc"] = net_mc_;
    serializeJson(doc, net_last_move_, sizeof(net_last_move_));
    discovery_send_game_data(peer_ip_, net_last_move_);
}

// ── Lifecycle ──

lv_obj_t* Connect4::createScreen() {
    s_self = this;
    mode_ = MODE_SELECT;
    screen_ = mp_create_mode_select(kCfg, mode_cpu_cb, mode_local_cb, mode_online_cb);
    return screen_;
}

void Connect4::update() {
    // Heartbeat for network resync.
    if (mode_ == MODE_NETWORK && !game_done_) {
        if (millis() - net_last_hb_ms_ > NET_HB_INTERVAL_MS) {
            net_last_hb_ms_ = millis();
            char hb[80];
            snprintf(hb, sizeof(hb),
                "{\"type\":\"move\",\"game\":\"connect4\",\"a\":\"hb\",\"mc\":%u}",
                (unsigned)net_mc_);
            discovery_send_game_data(peer_ip_, hb);
        }
    }

    // CPU move with a short delay for "thinking" feel
    if (mode_ == MODE_CPU && current_ == YELLOW && !game_done_) {
        if (!cpu_pending_) {
            cpu_pending_ = true;
            cpu_think_time_ = millis();
        } else if (millis() - cpu_think_time_ > 500) {
            cpu_pending_ = false;
            int col = cpu_pick_col();
            if (col >= 0) drop_disc(col);
        }
    }

    if (mode_ == MODE_LOBBY) mp_shell_lobby_tick();
}

void Connect4::destroy() {
    mp_shell_end(peer_ip_, mode_ == MODE_NETWORK && !game_done_);
    s_self = nullptr;
    screen_ = nullptr;
    lbl_status_ = nullptr;
}

void Connect4::onNetworkData(const char* json) {
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "connect4") != 0) return;
    if (doc["abandon"] | false) {
        show_result("Opponent left", false);
        return;
    }

    const char* action = doc["a"] | "";
    uint32_t peer_mc = doc["mc"] | 0;

    if (strcmp(action, "hb") == 0) {
        // Heartbeat: if peer is behind our state, resend our last move.
        if (peer_mc < net_mc_ && net_last_move_[0]) {
            discovery_send_game_data(peer_ip_, net_last_move_);
        }
        return;
    }

    // Strict +1 dedupe: only apply if this is the next expected move.
    // Duplicates (peer_mc <= net_mc_) are dropped; out-of-order (gap) moves
    // are also dropped and will be resent by the peer on its next heartbeat.
    if (peer_mc != net_mc_ + 1) return;

    int col = doc["col"] | -1;
    if (col < 0 || col >= COLS) return;

    sound_opponent_move();
    drop_disc(col);
    net_mc_ = peer_mc;
    if (!game_done_) {
        my_turn_ = true;
        update_status();
    }
}
