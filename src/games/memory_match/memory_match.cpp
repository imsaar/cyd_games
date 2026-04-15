#include "memory_match.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <ArduinoJson.h>
#include <Arduino.h>

static MemoryMatch* s_self = nullptr;
static lv_obj_t* mm_invite_msgbox = nullptr;
static IPAddress mm_pending_ip;

const char* const MemoryMatch::symbols[6] = {
    LV_SYMBOL_HOME, LV_SYMBOL_BELL, LV_SYMBOL_EYE_OPEN,
    LV_SYMBOL_AUDIO, LV_SYMBOL_GPS, LV_SYMBOL_CHARGE
};

// ── Discovery callbacks ──

void mm_on_invite(const Peer& from) {
    if (!s_self || s_self->mode_ != MemoryMatch::MODE_LOBBY) return;
    if (mm_invite_msgbox) return;
    mm_pending_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    mm_invite_msgbox = lv_msgbox_create(NULL, "Memory Invite",
        from.name, btns, false);
    lv_obj_set_size(mm_invite_msgbox, 240, 140);
    lv_obj_center(mm_invite_msgbox);
    lv_obj_set_style_bg_color(mm_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(mm_invite_msgbox, UI_COLOR_TEXT, 0);
    lv_obj_t* btnm = lv_msgbox_get_btns(mm_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(mm_invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(mm_pending_ip);
            s_self->peer_ip_ = mm_pending_ip;
            s_self->mode_ = MemoryMatch::MODE_NETWORK;
            s_self->is_p1_ = false;
            s_self->my_turn_ = false;
            s_self->p1_turn_ = true;
            discovery_set_game("memory", "playing");
            lv_msgbox_close(mm_invite_msgbox);
            mm_invite_msgbox = nullptr;
            // Don't create board yet — wait for host to send board sync
        } else {
            lv_msgbox_close(mm_invite_msgbox);
            mm_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void mm_on_accept(const Peer& from) {
    if (!s_self || s_self->mode_ != MemoryMatch::MODE_LOBBY) return;
    s_self->peer_ip_ = from.ip;
    s_self->mode_ = MemoryMatch::MODE_NETWORK;
    s_self->is_p1_ = true;
    s_self->my_turn_ = true;
    s_self->p1_turn_ = true;
    discovery_set_game("memory", "playing");

    // Host creates the board and sends layout to guest
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
    s_self->send_board_sync();
}

void mm_on_game_data(const char* json) {
    if (!s_self) return;
    s_self->onNetworkData(json);
}

void mm_lobby_peer_cb(lv_event_t* e) {
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

void MemoryMatch::mode_solo_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_SOLO;
    s_self->my_turn_ = true;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void MemoryMatch::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->p1_turn_ = true;
    s_self->my_turn_ = true;
    s_self->score_p1_ = 0;
    s_self->score_p2_ = 0;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void MemoryMatch::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    s_self->score_p1_ = 0;
    s_self->score_p2_ = 0;
    discovery_set_game("memory", "waiting");
    discovery_on_invite(mm_on_invite);
    discovery_on_accept(mm_on_accept);
    discovery_on_game_data(mm_on_game_data);
    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* MemoryMatch::create_mode_select() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Memory Match");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* b1 = ui_create_btn(scr, "Solo", 140, 42);
    lv_obj_align(b1, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(b1, mode_solo_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* b2 = ui_create_btn(scr, "Local (2P)", 140, 42);
    lv_obj_align(b2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(b2, mode_local_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* b3 = ui_create_btn(scr, "Network (2P)", 140, 42);
    lv_obj_align(b3, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(b3, mode_online_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

lv_obj_t* MemoryMatch::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Memory - Find Opponent");
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

void MemoryMatch::shuffle() {
    for (int i = 0; i < NUM_CARDS; i++) {
        values_[i] = i / 2;
        revealed_[i] = false;
        matched_[i] = false;
    }
    for (int i = NUM_CARDS - 1; i > 0; i--) {
        int j = random(0, i + 1);
        int tmp = values_[i];
        values_[i] = values_[j];
        values_[j] = tmp;
    }
    first_pick_ = -1;
    second_pick_ = -1;
    moves_ = 0;
    pairs_found_ = 0;
    checking_ = false;
    game_done_ = false;
    score_p1_ = 0;
    score_p2_ = 0;
}

void MemoryMatch::reveal(int idx) {
    if (!cards_[idx] || !card_labels_[idx]) return;
    revealed_[idx] = true;
    lv_label_set_text(card_labels_[idx], symbols[values_[idx]]);
    lv_obj_set_style_text_color(card_labels_[idx], UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_color(cards_[idx], UI_COLOR_CARD, 0);
}

void MemoryMatch::hide(int idx) {
    if (!cards_[idx] || !card_labels_[idx]) return;
    revealed_[idx] = false;
    lv_label_set_text(card_labels_[idx], "?");
    lv_obj_set_style_text_color(card_labels_[idx], UI_COLOR_DIM, 0);
    lv_obj_set_style_bg_color(cards_[idx], UI_COLOR_PRIMARY, 0);
}

void MemoryMatch::card_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (s_self->checking_) return;
    if (idx < 0 || idx >= NUM_CARDS) return;
    if (s_self->matched_[idx]) return;
    if (s_self->revealed_[idx]) return;

    // Network: only flip if it's my turn
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;

    s_self->reveal(idx);

    // Send flip to peer
    if (s_self->mode_ == MODE_NETWORK) {
        s_self->send_flip(idx);
    }

    if (s_self->first_pick_ == -1) {
        s_self->first_pick_ = idx;
    } else {
        s_self->second_pick_ = idx;
        s_self->moves_++;
        s_self->checking_ = true;
        s_self->check_time_ = millis();
    }
}

void MemoryMatch::check_match() {
    bool match = (values_[first_pick_] == values_[second_pick_]);

    if (match) {
        matched_[first_pick_] = true;
        matched_[second_pick_] = true;
        pairs_found_++;

        lv_obj_set_style_bg_color(cards_[first_pick_], lv_color_hex(0x1a4d2e), 0);
        lv_obj_set_style_bg_color(cards_[second_pick_], lv_color_hex(0x1a4d2e), 0);

        // Score for current player in 2P modes
        if (mode_ != MODE_SOLO) {
            if (p1_turn_) score_p1_++;
            else score_p2_++;
        }

        if (pairs_found_ >= 6) {
            game_done_ = true;
            show_result();
            first_pick_ = -1;
            second_pick_ = -1;
            checking_ = false;
            return;
        }
        // Same player goes again on a match (no turn switch)
    } else {
        hide(first_pick_);
        hide(second_pick_);

        // Switch turns in 2P modes
        if (mode_ != MODE_SOLO) {
            p1_turn_ = !p1_turn_;
            if (mode_ == MODE_NETWORK) {
                my_turn_ = (is_p1_ == p1_turn_);
            }
        }
    }

    first_pick_ = -1;
    second_pick_ = -1;
    checking_ = false;
    update_status();
}

void MemoryMatch::update_status() {
    if (!lbl_moves_) return;
    char buf[48];
    if (mode_ == MODE_SOLO) {
        snprintf(buf, sizeof(buf), "Moves: %d  Pairs: %d/6", moves_, pairs_found_);
    } else if (mode_ == MODE_LOCAL) {
        snprintf(buf, sizeof(buf), "P1:%d P2:%d  %s",
                 score_p1_, score_p2_, p1_turn_ ? "P1's turn" : "P2's turn");
    } else if (mode_ == MODE_NETWORK) {
        snprintf(buf, sizeof(buf), "You:%d Opp:%d  %s",
                 is_p1_ ? score_p1_ : score_p2_,
                 is_p1_ ? score_p2_ : score_p1_,
                 my_turn_ ? "Your turn" : "Waiting...");
    }
    lv_label_set_text(lbl_moves_, buf);
}

void MemoryMatch::show_result() {
    if (!screen_) return;
    const char* text;
    bool is_win;

    if (mode_ == MODE_SOLO) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Done!\n%d moves", moves_);
        // Solo always "wins"
        text = buf;
        is_win = true;

        lv_color_t color = UI_COLOR_SUCCESS;
        lv_obj_t* overlay = lv_obj_create(screen_);
        lv_obj_remove_style_all(overlay);
        lv_obj_set_size(overlay, 240, 120);
        lv_obj_center(overlay);
        lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0e0e1a), 0);
        lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(overlay, 16, 0);
        lv_obj_set_style_border_color(overlay, color, 0);
        lv_obj_set_style_border_width(overlay, 3, 0);
        lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_t* lbl = lv_label_create(overlay);
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_color(lbl, color, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_center(lbl);
        return;
    }

    // 2-player result
    if (mode_ == MODE_NETWORK) {
        int my_score = is_p1_ ? score_p1_ : score_p2_;
        int opp_score = is_p1_ ? score_p2_ : score_p1_;
        if (my_score > opp_score) { text = "You Win!"; is_win = true; }
        else if (my_score < opp_score) { text = "You Lose!"; is_win = false; }
        else { text = "Draw!"; is_win = false; }
    } else {
        if (score_p1_ > score_p2_) { text = "P1 Wins!"; is_win = true; }
        else if (score_p2_ > score_p1_) { text = "P2 Wins!"; is_win = true; }
        else { text = "Draw!"; is_win = false; }
    }

    lv_color_t color = is_win ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT;
    lv_obj_t* overlay = lv_obj_create(screen_);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 260, 140);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(overlay, 16, 0);
    lv_obj_set_style_border_color(overlay, color, 0);
    lv_obj_set_style_border_width(overlay, 3, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    char buf[32];
    snprintf(buf, sizeof(buf), "%s\n%d - %d", text, score_p1_, score_p2_);
    lv_obj_t* lbl = lv_label_create(overlay);
    lv_label_set_text(lbl, buf);
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

// ── Networking ──

void MemoryMatch::send_flip(int idx) {
    StaticJsonDocument<128> doc;
    doc["type"] = "move";
    doc["game"] = "memory";
    doc["action"] = "flip";
    doc["idx"] = idx;
    char buf[128];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

void MemoryMatch::send_board_sync() {
    // Host sends the card layout to guest so both have same board
    StaticJsonDocument<256> doc;
    doc["type"] = "move";
    doc["game"] = "memory";
    doc["action"] = "sync";
    JsonArray arr = doc.createNestedArray("v");
    for (int i = 0; i < NUM_CARDS; i++) arr.add(values_[i]);
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

void MemoryMatch::onNetworkData(const char* json) {
    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "memory") != 0) return;
    if (doc["abandon"] | false) {
        game_done_ = true;
        if (lbl_moves_) lv_label_set_text(lbl_moves_, "Opponent left");
        return;
    }

    const char* action = doc["action"] | "";

    if (strcmp(action, "sync") == 0) {
        // Guest receives board layout from host
        JsonArray arr = doc["v"];
        if (arr.size() == NUM_CARDS) {
            for (int i = 0; i < NUM_CARDS; i++) values_[i] = arr[i];
        }
        // Now create the board
        lv_obj_t* scr = create_board();
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
        screen_ = scr;
        return;
    }

    if (strcmp(action, "flip") == 0) {
        int idx = doc["idx"] | -1;
        if (idx < 0 || idx >= NUM_CARDS) return;
        if (matched_[idx] || revealed_[idx]) return;

        reveal(idx);

        if (first_pick_ == -1) {
            first_pick_ = idx;
        } else {
            second_pick_ = idx;
            moves_++;
            checking_ = true;
            check_time_ = millis();
        }
    }
}

// ── Board creation ──

lv_obj_t* MemoryMatch::create_board() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lbl_moves_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_moves_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_moves_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_moves_, LV_ALIGN_TOP_MID, 20, 10);

    if (mode_ == MODE_SOLO || mode_ == MODE_LOCAL) {
        shuffle();
    }
    // For network mode, values_ are set by host or received via sync

    // Reset per-game state (but not values_ for network guest)
    for (int i = 0; i < NUM_CARDS; i++) {
        revealed_[i] = false;
        matched_[i] = false;
    }
    first_pick_ = -1;
    second_pick_ = -1;
    moves_ = 0;
    pairs_found_ = 0;
    checking_ = false;
    game_done_ = false;
    if (mode_ != MODE_NETWORK) {
        score_p1_ = 0;
        score_p2_ = 0;
    }

    static const int CARD_W = 68;
    static const int CARD_H = 55;
    static const int GAP = 6;
    int grid_w = COLS * CARD_W + (COLS - 1) * GAP;
    int grid_h = ROWS * CARD_H + (ROWS - 1) * GAP;
    int ox = (320 - grid_w) / 2;
    int oy = (240 - grid_h) / 2 + 12;

    for (int i = 0; i < NUM_CARDS; i++) {
        int col = i % COLS;
        int row = i / COLS;

        lv_obj_t* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, CARD_W, CARD_H);
        lv_obj_set_pos(btn, ox + col * (CARD_W + GAP), oy + row * (CARD_H + GAP));
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_radius(btn, 8, 0);

        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, "?");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl, UI_COLOR_DIM, 0);
        lv_obj_center(lbl);

        lv_obj_add_event_cb(btn, card_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)i);
        cards_[i] = btn;
        card_labels_[i] = lbl;
    }

    lobby_list_ = nullptr;
    update_status();
    return scr;
}

// ── Lifecycle ──

lv_obj_t* MemoryMatch::createScreen() {
    s_self = this;
    mm_invite_msgbox = nullptr;
    mode_ = MODE_SELECT;
    screen_ = create_mode_select();
    return screen_;
}

void MemoryMatch::update() {
    // Lobby refresh
    if (mode_ == MODE_LOBBY && lobby_list_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);
            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            int shown = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(peers[i].game, "memory") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)",
                             peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, mm_lobby_peer_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) lv_list_add_text(lobby_list_, "Searching...");
        }
    }

    // Check match after delay
    if (checking_ && millis() - check_time_ > 800) {
        check_match();
    }
}

void MemoryMatch::destroy() {
    if (mm_invite_msgbox) {
        lv_msgbox_close(mm_invite_msgbox);
        mm_invite_msgbox = nullptr;
    }
    if (mode_ == MODE_NETWORK) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"memory\",\"abandon\":true}");
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    lbl_moves_ = nullptr;
    lbl_status_ = nullptr;
    lobby_list_ = nullptr;
    for (int i = 0; i < NUM_CARDS; i++) {
        cards_[i] = nullptr;
        card_labels_[i] = nullptr;
    }
}
