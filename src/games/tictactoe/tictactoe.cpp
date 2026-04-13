#include "tictactoe.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <ArduinoJson.h>

static TicTacToe* s_self = nullptr;
static IPAddress pending_invite_ip;
static lv_obj_t* invite_msgbox = nullptr;

// ── Discovery callbacks ──

void on_invite_received(const Peer& from) {
    Serial.printf("[TTT] Invite from %s (%s), mode=%d\n",
                  from.name, from.ip.toString().c_str(),
                  s_self ? (int)s_self->mode_ : -1);
    if (!s_self) return;
    if (s_self->mode_ != TicTacToe::MODE_LOBBY) return;
    if (invite_msgbox) return;  // Already showing an invite

    pending_invite_ip = from.ip;

    // Show invite popup
    static const char* btns[] = {"Accept", "Decline", ""};
    invite_msgbox = lv_msgbox_create(NULL, "Game Invite",
        from.name, btns, false);
    lv_obj_set_size(invite_msgbox, 240, 140);
    lv_obj_center(invite_msgbox);

    // Style the msgbox
    lv_obj_set_style_bg_color(invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(invite_msgbox, UI_COLOR_TEXT, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(invite_msgbox);

        if (btn_id == 0) {  // Accept
            Serial.printf("[TTT] Accepting invite from %s\n",
                          pending_invite_ip.toString().c_str());
            discovery_send_accept(pending_invite_ip);

            s_self->peer_ip_ = pending_invite_ip;
            s_self->mode_ = TicTacToe::MODE_NETWORK;
            s_self->my_mark_ = TicTacToe::PLAYER_O;
            s_self->my_turn_ = false;
            discovery_set_game("tictactoe", "playing");

            lv_msgbox_close(invite_msgbox);
            invite_msgbox = nullptr;

            lv_obj_t* scr = s_self->create_board();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {  // Decline
            lv_msgbox_close(invite_msgbox);
            invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void on_accept_received(const Peer& from) {
    Serial.printf("[TTT] Accept from %s (%s)\n",
                  from.name, from.ip.toString().c_str());
    if (!s_self) return;
    if (s_self->mode_ != TicTacToe::MODE_LOBBY) return;

    s_self->peer_ip_ = from.ip;
    s_self->mode_ = TicTacToe::MODE_NETWORK;
    s_self->my_mark_ = TicTacToe::PLAYER_X;
    s_self->my_turn_ = true;
    discovery_set_game("tictactoe", "playing");

    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void on_game_data(const char* json) {
    Serial.printf("[TTT] Game data, s_self=%p mode=%d\n",
                  s_self, s_self ? (int)s_self->mode_ : -1);
    if (!s_self || s_self->mode_ != TicTacToe::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

// ── Mode Selection ──

void TicTacToe::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->my_mark_ = PLAYER_X;
    s_self->my_turn_ = true;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);

    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void TicTacToe::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    discovery_set_game("tictactoe", "waiting");

    // Register discovery callbacks
    discovery_on_invite(on_invite_received);
    discovery_on_accept(on_accept_received);
    discovery_on_game_data(on_game_data);

    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* TicTacToe::create_mode_select() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lv_obj_t* title = ui_create_title(scr, "Tic-Tac-Toe");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* btn_local = ui_create_btn(scr, "Local (2P)", 140, 50);
    lv_obj_align(btn_local, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn_local, mode_local_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_online = ui_create_btn(scr, "Online (2P)", 140, 50);
    lv_obj_align(btn_online, LV_ALIGN_CENTER, 0, 35);
    lv_obj_add_event_cb(btn_online, mode_online_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

// ── Lobby ──

void lobby_peer_cb(lv_event_t* e) {
    if (!s_self) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const Peer* peers = discovery_get_peers();
    int count = discovery_peer_count();
    if (idx < 0 || idx >= count) return;

    Serial.printf("[TTT] Sending invite to %s (%s)\n",
                  peers[idx].name, peers[idx].ip.toString().c_str());
    discovery_send_invite(peers[idx].ip);

    if (s_self->lobby_list_) {
        lv_obj_clean(s_self->lobby_list_);
        lv_list_add_text(s_self->lobby_list_, "Invite sent, waiting...");
    }
}

lv_obj_t* TicTacToe::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lv_obj_t* title = ui_create_title(scr, "Finding Opponents...");
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

// ── Game Board ──

void TicTacToe::reset_board() {
    for (int i = 0; i < 9; i++) board_[i] = EMPTY;
    current_ = PLAYER_X;
    game_done_ = false;
}

lv_obj_t* TicTacToe::create_board() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_RIGHT, -10, 10);

    static const int CELL_SIZE = 60;
    static const int GAP = 4;
    int grid_size = CELL_SIZE * 3 + GAP * 2;
    int ox = (320 - grid_size) / 2;
    int oy = (240 - grid_size) / 2 + 10;

    for (int i = 0; i < 9; i++) {
        int col = i % 3;
        int row = i / 3;
        lv_obj_t* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, CELL_SIZE, CELL_SIZE);
        lv_obj_set_pos(btn, ox + col * (CELL_SIZE + GAP), oy + row * (CELL_SIZE + GAP));
        lv_obj_set_style_bg_color(btn, UI_COLOR_CARD, 0);
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, LV_STATE_PRESSED);
        lv_obj_set_style_radius(btn, 6, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, cell_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        cells_[i] = btn;
    }

    reset_board();
    update_status();

    if (mode_ == MODE_NETWORK) {
        Serial.printf("[TTT] Board ready. I am %s, my_turn=%d, peer=%s\n",
                      my_mark_ == PLAYER_X ? "X (host)" : "O (guest)",
                      my_turn_, peer_ip_.toString().c_str());
    }

    return scr;
}

void TicTacToe::cell_cb(lv_event_t* e) {
    if (!s_self) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (s_self->game_done_) return;
    if (idx < 0 || idx > 8) return;
    if (s_self->board_[idx] != EMPTY) return;

    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;

    s_self->place_mark(idx);

    if (s_self->mode_ == MODE_NETWORK) {
        s_self->send_move(idx);
        s_self->my_turn_ = false;
        s_self->update_status();
    }
}

void TicTacToe::place_mark(int idx) {
    board_[idx] = current_;

    lv_obj_t* lbl = lv_obj_get_child(cells_[idx], 0);
    lv_label_set_text(lbl, current_ == PLAYER_X ? "X" : "O");
    lv_obj_set_style_text_color(lbl,
        current_ == PLAYER_X ? UI_COLOR_ACCENT : UI_COLOR_SUCCESS, 0);

    Cell winner = check_winner();
    if (winner != EMPTY) {
        game_done_ = true;
        char buf[32];
        if (mode_ == MODE_NETWORK) {
            bool i_won = (winner == my_mark_);
            snprintf(buf, sizeof(buf), "%s", i_won ? "You Win!" : "You Lose!");
        } else {
            snprintf(buf, sizeof(buf), "%s Wins!", winner == PLAYER_X ? "X" : "O");
        }
        lv_label_set_text(lbl_status_, buf);
        return;
    }

    if (board_full()) {
        game_done_ = true;
        lv_label_set_text(lbl_status_, "Draw!");
        return;
    }

    current_ = (current_ == PLAYER_X) ? PLAYER_O : PLAYER_X;
    update_status();
}

TicTacToe::Cell TicTacToe::check_winner() {
    static const int lines[8][3] = {
        {0,1,2}, {3,4,5}, {6,7,8},
        {0,3,6}, {1,4,7}, {2,5,8},
        {0,4,8}, {2,4,6}
    };
    for (auto& l : lines) {
        if (board_[l[0]] != EMPTY &&
            board_[l[0]] == board_[l[1]] &&
            board_[l[1]] == board_[l[2]]) {
            return (Cell)board_[l[0]];
        }
    }
    return EMPTY;
}

bool TicTacToe::board_full() {
    for (int i = 0; i < 9; i++) {
        if (board_[i] == EMPTY) return false;
    }
    return true;
}

void TicTacToe::update_status() {
    if (!lbl_status_) return;
    if (mode_ == MODE_LOCAL) {
        lv_label_set_text(lbl_status_,
            current_ == PLAYER_X ? "X's turn" : "O's turn");
    } else if (mode_ == MODE_NETWORK) {
        lv_label_set_text(lbl_status_,
            my_turn_ ? "Your turn" : "Waiting...");
    }
}

void TicTacToe::send_move(int idx) {
    Serial.printf("[TTT] send_move cell=%d to %s, current=%d\n",
                  idx, peer_ip_.toString().c_str(), (int)my_mark_);
    StaticJsonDocument<128> doc;
    doc["type"] = "move";
    doc["game"] = "tictactoe";
    doc["cell"] = idx;
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

// ── Lifecycle ──

lv_obj_t* TicTacToe::createScreen() {
    s_self = this;
    invite_msgbox = nullptr;
    mode_ = MODE_SELECT;
    screen_ = create_mode_select();
    return screen_;
}

void TicTacToe::update() {
    if (mode_ == MODE_LOBBY && lobby_list_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);

            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            int shown = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(peers[i].game, "tictactoe") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)",
                             peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, lobby_peer_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)i);
                    shown++;
                }
            }

            if (shown == 0) {
                lv_list_add_text(lobby_list_, "Searching...");
            }
        }
    }
}

void TicTacToe::destroy() {
    if (invite_msgbox) {
        lv_msgbox_close(invite_msgbox);
        invite_msgbox = nullptr;
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    lbl_status_ = nullptr;
    lobby_list_ = nullptr;
    for (int i = 0; i < 9; i++) cells_[i] = nullptr;
}

void TicTacToe::onPeerJoined(const char* ip_str) {
    peer_ip_.fromString(ip_str);
    mode_ = MODE_NETWORK;
    my_mark_ = PLAYER_O;
    my_turn_ = false;

    lv_obj_t* scr = create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    screen_ = scr;
}

void TicTacToe::onNetworkData(const char* json) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, json)) return;

    const char* game = doc["game"];
    if (!game || strcmp(game, "tictactoe") != 0) return;

    int cell = doc["cell"] | -1;
    Serial.printf("[TTT] onNetworkData cell=%d, current=%d\n",
                  cell, (int)current_);
    if (cell < 0 || cell > 8) return;

    place_mark(cell);
    my_turn_ = true;
    update_status();
}
