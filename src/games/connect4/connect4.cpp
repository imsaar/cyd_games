#include "connect4.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <ArduinoJson.h>

static Connect4* s_self = nullptr;
static lv_obj_t* c4_invite_msgbox = nullptr;
static IPAddress c4_pending_ip;

// ── Discovery callbacks ──

void c4_on_invite(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    if (c4_invite_msgbox) return;

    c4_pending_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    c4_invite_msgbox = lv_msgbox_create(NULL, "Connect 4 Invite",
        from.name, btns, false);
    lv_obj_set_size(c4_invite_msgbox, 240, 140);
    lv_obj_center(c4_invite_msgbox);
    lv_obj_set_style_bg_color(c4_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(c4_invite_msgbox, UI_COLOR_TEXT, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(c4_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(c4_invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(c4_pending_ip);
            s_self->peer_ip_ = c4_pending_ip;
            s_self->mode_ = Connect4::MODE_NETWORK;
            s_self->my_color_ = Connect4::YELLOW;
            s_self->my_turn_ = false;
            discovery_set_game("connect4", "playing");
            lv_msgbox_close(c4_invite_msgbox);
            c4_invite_msgbox = nullptr;
            lv_obj_t* scr = s_self->create_board();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            lv_msgbox_close(c4_invite_msgbox);
            c4_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void c4_on_accept(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    s_self->peer_ip_ = from.ip;
    s_self->mode_ = Connect4::MODE_NETWORK;
    s_self->my_color_ = Connect4::RED;
    s_self->my_turn_ = true;
    discovery_set_game("connect4", "playing");
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void c4_on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != Connect4::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

void c4_lobby_peer_cb(lv_event_t* e) {
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

void Connect4::mode_cpu_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU;
    s_self->my_color_ = RED;
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

void Connect4::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->my_color_ = RED;
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

void Connect4::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    discovery_set_game("connect4", "waiting");
    discovery_on_invite(c4_on_invite);
    discovery_on_accept(c4_on_accept);
    discovery_on_game_data(c4_on_game_data);
    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* Connect4::create_mode_select() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Connect 4");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* b1 = ui_create_btn(scr, "vs CPU", 140, 42);
    lv_obj_align(b1, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(b1, mode_cpu_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* b2 = ui_create_btn(scr, "Local (2P)", 140, 42);
    lv_obj_align(b2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(b2, mode_local_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* b3 = ui_create_btn(scr, "Network (2P)", 140, 42);
    lv_obj_align(b3, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(b3, mode_online_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

// ── Lobby ──

lv_obj_t* Connect4::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Connect 4 - Find Opponent");
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

// ── Board ──

void Connect4::reset_board() {
    for (int i = 0; i < COLS * ROWS; i++) board_[i] = EMPTY;
    current_ = RED;
    game_done_ = false;
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

    lobby_list_ = nullptr;
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

// Score a hypothetical drop for 'who' in 'col'
int Connect4::score_col(int col, Cell who) {
    // Find where disc would land
    int row = -1;
    for (int r = ROWS - 1; r >= 0; r--) {
        if (board_[r * COLS + col] == EMPTY) { row = r; break; }
    }
    if (row < 0) return -1000;  // Column full

    // Temporarily place
    board_[row * COLS + col] = who;
    bool wins = check_win(row, col);
    board_[row * COLS + col] = EMPTY;

    if (wins) return 1000;

    // Prefer center columns
    int center_score = COLS / 2 - abs(col - COLS / 2);
    return center_score;
}

int Connect4::cpu_pick_col() {
    // 1. Can CPU win?
    for (int c = 0; c < COLS; c++) {
        if (score_col(c, YELLOW) >= 1000) return c;
    }
    // 2. Must block player win?
    for (int c = 0; c < COLS; c++) {
        if (score_col(c, RED) >= 1000) return c;
    }
    // 3. Pick best scoring column
    int best_col = -1, best_score = -9999;
    for (int c = 0; c < COLS; c++) {
        int s = score_col(c, YELLOW);
        if (s > best_score) { best_score = s; best_col = c; }
    }
    // Add some randomness among equally good columns
    int candidates[COLS];
    int n = 0;
    for (int c = 0; c < COLS; c++) {
        if (score_col(c, YELLOW) == best_score) candidates[n++] = c;
    }
    return candidates[random(0, n)];
}

void Connect4::send_move(int col) {
    StaticJsonDocument<128> doc;
    doc["type"] = "move";
    doc["game"] = "connect4";
    doc["col"] = col;
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

// ── Lifecycle ──

lv_obj_t* Connect4::createScreen() {
    s_self = this;
    c4_invite_msgbox = nullptr;
    mode_ = MODE_SELECT;
    screen_ = create_mode_select();
    return screen_;
}

void Connect4::update() {
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

    if (mode_ == MODE_LOBBY && lobby_list_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);
            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            int shown = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(peers[i].game, "connect4") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)",
                             peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, c4_lobby_peer_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) lv_list_add_text(lobby_list_, "Searching...");
        }
    }
}

void Connect4::destroy() {
    if (c4_invite_msgbox) {
        lv_msgbox_close(c4_invite_msgbox);
        c4_invite_msgbox = nullptr;
    }
    if (mode_ == MODE_NETWORK) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"connect4\",\"abandon\":true}");
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

void Connect4::onNetworkData(const char* json) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "connect4") != 0) return;
    if (doc["abandon"] | false) {
        show_result("Opponent left", false);
        return;
    }
    int col = doc["col"] | -1;
    if (col < 0 || col >= COLS) return;

    drop_disc(col);
    if (!game_done_) {
        my_turn_ = true;
        update_status();
    }
}
