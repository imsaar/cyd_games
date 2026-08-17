#include "memory_match.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../net/mp_shell.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>
#include <Arduino.h>

static MemoryMatch* s_self = nullptr;
static const MpShellConfig kCfg = { "memory", "Memory Match", /*show_cpu_button=*/true, /*show_idle_peers=*/false };

const char* const MemoryMatch::symbols[6] = {
    LV_SYMBOL_HOME, LV_SYMBOL_BELL, LV_SYMBOL_EYE_OPEN,
    LV_SYMBOL_AUDIO, LV_SYMBOL_GPS, LV_SYMBOL_CHARGE
};

// ── Mode selection ──

void MemoryMatch::mode_solo_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_SOLO;
    s_self->my_turn_ = true;
    mp_shell_end(s_self->peer_ip_, false);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void MemoryMatch::mode_local_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->p1_turn_ = true;
    s_self->my_turn_ = true;
    s_self->score_p1_ = 0;
    s_self->score_p2_ = 0;
    mp_shell_end(s_self->peer_ip_, false);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void MemoryMatch::mode_online_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    s_self->score_p1_ = 0;
    s_self->score_p2_ = 0;
    lv_obj_t* scr = mp_shell_host_lobby(kCfg, on_host_ready, on_guest_ready, on_game_data, on_invite_failed);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void MemoryMatch::on_host_ready(const Peer& peer) {
    if (!s_self) return;
    Serial.println("[MM] host: accept received, creating board + sending sync");
    s_self->peer_ip_ = peer.ip;
    s_self->mode_ = MODE_NETWORK;
    s_self->is_p1_ = true;
    s_self->my_turn_ = true;
    s_self->p1_turn_ = true;
    s_self->guest_board_acked_ = false;

    // Host creates the board and sends layout to guest
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
    s_self->send_board_sync();
}

void MemoryMatch::on_guest_ready(const Peer& peer) {
    if (!s_self) return;
    Serial.println("[MM] guest: accepted, waiting for board sync");
    s_self->peer_ip_ = peer.ip;
    s_self->mode_ = MODE_NETWORK;
    s_self->is_p1_ = false;
    s_self->my_turn_ = false;
    s_self->p1_turn_ = true;
    // Don't create the board yet — wait for the host's board-sync message.
    // Show a clear waiting status instead of leaving the lobby list frozen
    // looking like the tap did nothing.
    mp_shell_set_status("Waiting for host to start...");
}

void MemoryMatch::on_game_data(const char* json) {
    if (!s_self) return;
    s_self->onNetworkData(json);
}

// Fired if the peer declines, or doesn't respond within the retry window.
// Without this, the lobby just silently resumes showing the peer list on
// its next 2s refresh with no explanation of what happened.
void MemoryMatch::on_invite_failed(const Peer&) {
    mp_shell_set_status("No response - try again");
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
    sound_move();

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
        sound_win();

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
    if (is_win) sound_win(); else sound_lose();

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

// ── Networking ──

void MemoryMatch::send_flip(int idx) {
    net_mc_++;
    StaticJsonDocument<160> doc;
    doc["type"] = "move";
    doc["game"] = "memory";
    doc["action"] = "flip";
    doc["idx"] = idx;
    doc["mc"] = net_mc_;
    serializeJson(doc, net_last_move_, sizeof(net_last_move_));
    discovery_send_game_data(peer_ip_, net_last_move_);
}

void MemoryMatch::send_board_sync() {
    // Host sends the card layout to guest so both have same board.
    // 4 root keys + a 12-element array = 16 ArduinoJson slots, which alone
    // is exactly 256 bytes (16 * sizeof(VariantSlot)) — a StaticJsonDocument<256>
    // has zero bytes left over, so this must stay comfortably above that.
    StaticJsonDocument<384> doc;
    doc["type"] = "move";
    doc["game"] = "memory";
    doc["action"] = "sync";
    JsonArray arr = doc.createNestedArray("v");
    for (int i = 0; i < NUM_CARDS; i++) arr.add(values_[i]);
    char buf[384];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

void MemoryMatch::onNetworkData(const char* json) {
    // Must be large enough for the "sync" message parsed from a const
    // char* (no zero-copy) — 4 root keys + a 12-element array already
    // costs 256 bytes in slots alone, before any string data is copied in.
    StaticJsonDocument<384> doc;
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[MM] onNetworkData: parse failed (%s): %s\n", err.c_str(), json);
        return;
    }
    const char* game = doc["game"];
    if (!game || strcmp(game, "memory") != 0) return;
    if (doc["abandon"] | false) {
        game_done_ = true;
        if (lbl_moves_) lv_label_set_text(lbl_moves_, "Opponent left");
        return;
    }

    const char* a_short = doc["a"] | "";
    if (strcmp(a_short, "hb") == 0) {
        if (is_p1_ && !guest_board_acked_) {
            // Guest is alive but never confirmed the board — resend right
            // away instead of waiting for our own heartbeat timer, so
            // recovery from a dropped sync is as fast as one guest hb tick.
            Serial.println("[MM] host: guest hb before ack, resending sync");
            send_board_sync();
            return;
        }
        uint32_t peer_mc = doc["mc"] | 0;
        if (peer_mc < net_mc_ && net_last_move_[0]) {
            discovery_send_game_data(peer_ip_, net_last_move_);
        }
        return;
    }
    if (strcmp(a_short, "sync_ack") == 0) {
        // Guest confirms it built the board — host can stop resending it.
        Serial.println("[MM] host: got sync_ack");
        guest_board_acked_ = true;
        return;
    }

    const char* action = doc["action"] | "";

    if (strcmp(action, "sync") == 0) {
        // Guest receives board layout from host. This is idempotent —
        // it may arrive more than once if the host is retrying because
        // an earlier ack got lost, so just re-apply and re-ack.
        Serial.printf("[MM] guest: got sync, have_board=%d\n", cards_[0] != nullptr);
        JsonArray arr = doc["v"];
        if (arr.size() == NUM_CARDS) {
            for (int i = 0; i < NUM_CARDS; i++) values_[i] = arr[i];
        }
        // Only create the board once (guest arrives here before board exists).
        if (!screen_ || !cards_[0]) {
            lv_obj_t* scr = create_board();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            screen_ = scr;
        }
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"memory\",\"a\":\"sync_ack\"}");
        return;
    }

    if (strcmp(action, "flip") == 0) {
        uint32_t peer_mc = doc["mc"] | 0;
        if (peer_mc != net_mc_ + 1) return;

        int idx = doc["idx"] | -1;
        if (idx < 0 || idx >= NUM_CARDS) return;
        if (matched_[idx] || revealed_[idx]) return;

        sound_opponent_move();
        reveal(idx);
        net_mc_ = peer_mc;

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

    if (mode_ == MODE_SOLO || mode_ == MODE_LOCAL || (mode_ == MODE_NETWORK && is_p1_)) {
        shuffle();
    }
    // Network guest: values_ arrive via sync from the host, don't shuffle locally

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
    net_reset_sync();

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

    update_status();
    return scr;
}

// ── Lifecycle ──

lv_obj_t* MemoryMatch::createScreen() {
    s_self = this;
    mode_ = MODE_SELECT;
    screen_ = mp_create_mode_select(kCfg, mode_solo_cb, mode_local_cb, mode_online_cb);
    return screen_;
}

void MemoryMatch::update() {
    if (mode_ == MODE_NETWORK && !game_done_) {
        if (millis() - net_last_hb_ms_ > NET_HB_INTERVAL_MS) {
            net_last_hb_ms_ = millis();
            if (is_p1_ && !guest_board_acked_) {
                // Guest hasn't confirmed it received the board layout yet
                // (its accept, or our sync, may have been dropped) — keep
                // resending the layout instead of a plain heartbeat.
                send_board_sync();
            } else {
                char hb[80];
                snprintf(hb, sizeof(hb),
                    "{\"type\":\"move\",\"game\":\"memory\",\"a\":\"hb\",\"mc\":%u}",
                    (unsigned)net_mc_);
                discovery_send_game_data(peer_ip_, hb);
            }
        }
    }

    if (mode_ == MODE_LOBBY) mp_shell_lobby_tick();

    // Check match after delay
    if (checking_ && millis() - check_time_ > 800) {
        check_match();
    }
}

void MemoryMatch::destroy() {
    mp_shell_end(peer_ip_, mode_ == MODE_NETWORK && !game_done_);
    s_self = nullptr;
    screen_ = nullptr;
    lbl_moves_ = nullptr;
    lbl_status_ = nullptr;
    for (int i = 0; i < NUM_CARDS; i++) {
        cards_[i] = nullptr;
        card_labels_[i] = nullptr;
    }
}
