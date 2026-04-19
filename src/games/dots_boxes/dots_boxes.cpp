#include "dots_boxes.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>

static DotsBoxes* s_self = nullptr;
static lv_obj_t* db_invite_msgbox = nullptr;
static IPAddress db_pending_ip;

// ── Discovery callbacks ──

void db_on_invite(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    if (db_invite_msgbox) return;
    db_pending_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    db_invite_msgbox = lv_msgbox_create(NULL, "Dots & Boxes Invite",
        from.name, btns, false);
    lv_obj_set_size(db_invite_msgbox, 240, 140);
    lv_obj_center(db_invite_msgbox);
    lv_obj_set_style_bg_color(db_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(db_invite_msgbox, UI_COLOR_TEXT, 0);
    lv_obj_t* btnm = lv_msgbox_get_btns(db_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(db_invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(db_pending_ip);
            s_self->peer_ip_ = db_pending_ip;
            s_self->mode_ = DotsBoxes::MODE_NETWORK;
            s_self->my_player_ = DotsBoxes::P2;
            s_self->my_turn_ = false;
            discovery_set_game("dotsboxes", "playing");
            lv_msgbox_close(db_invite_msgbox);
            db_invite_msgbox = nullptr;
            lv_obj_t* scr = s_self->create_board();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            lv_msgbox_close(db_invite_msgbox);
            db_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void db_on_accept(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    s_self->peer_ip_ = from.ip;
    s_self->mode_ = DotsBoxes::MODE_NETWORK;
    s_self->my_player_ = DotsBoxes::P1;
    s_self->my_turn_ = true;
    discovery_set_game("dotsboxes", "playing");
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void db_on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != DotsBoxes::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

void db_lobby_peer_cb(lv_event_t* e) {
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

void DotsBoxes::mode_cpu_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU;
    s_self->my_player_ = P1;
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

void DotsBoxes::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->my_player_ = P1;
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

void DotsBoxes::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    discovery_set_game("dotsboxes", "waiting");
    discovery_on_invite(db_on_invite);
    discovery_on_accept(db_on_accept);
    discovery_on_game_data(db_on_game_data);
    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* DotsBoxes::create_mode_select() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Dots & Boxes");
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

lv_obj_t* DotsBoxes::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Dots & Boxes - Find Opponent");
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

void DotsBoxes::reset_board() {
    for (int i = 0; i < TOTAL_LINES; i++) lines_[i] = false;
    for (int i = 0; i < BOXES * BOXES; i++) boxes_[i] = NONE;
    current_ = P1;
    game_done_ = false;
    score_p1_ = 0;
    score_p2_ = 0;
    net_reset_sync();
}

lv_obj_t* DotsBoxes::create_board() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_MID, 30, 10);

    lbl_score_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_score_, LV_ALIGN_TOP_RIGHT, -10, 10);

    int ox = (320 - (DOTS - 1) * GAP) / 2;
    int oy = 45;

    // Draw dots
    for (int r = 0; r < DOTS; r++) {
        for (int c = 0; c < DOTS; c++) {
            lv_obj_t* dot = lv_obj_create(scr);
            lv_obj_remove_style_all(dot);
            lv_obj_set_size(dot, DOT_R * 2, DOT_R * 2);
            lv_obj_set_pos(dot, ox + c * GAP - DOT_R, oy + r * GAP - DOT_R);
            lv_obj_set_style_bg_color(dot, UI_COLOR_TEXT, 0);
            lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(dot, DOT_R, 0);
            lv_obj_clear_flag(dot, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        }
    }

    // Horizontal line buttons: index 0..H_LINES-1
    // Row r, col c -> index = r * BOXES + c  (but we have DOTS rows of BOXES h-lines)
    // Actually: r ranges 0..DOTS-1, c ranges 0..BOXES-1
    // H-line(r,c) = r * BOXES + c, total = DOTS * BOXES = 12
    int line_idx = 0;
    for (int r = 0; r < DOTS; r++) {
        for (int c = 0; c < BOXES; c++) {
            lv_obj_t* btn = lv_btn_create(scr);
            lv_obj_set_size(btn, GAP - DOT_R * 2 - 2, DOT_R * 3);
            lv_obj_set_pos(btn, ox + c * GAP + DOT_R + 1, oy + r * GAP - DOT_R - 2);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
            lv_obj_set_style_radius(btn, 2, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_add_event_cb(btn, line_cb, LV_EVENT_CLICKED, (void*)(intptr_t)line_idx);
            line_btns_[line_idx] = btn;
            line_idx++;
        }
    }

    // Vertical line buttons: index H_LINES..TOTAL_LINES-1
    for (int r = 0; r < BOXES; r++) {
        for (int c = 0; c < DOTS; c++) {
            lv_obj_t* btn = lv_btn_create(scr);
            lv_obj_set_size(btn, DOT_R * 3, GAP - DOT_R * 2 - 2);
            lv_obj_set_pos(btn, ox + c * GAP - DOT_R - 2, oy + r * GAP + DOT_R + 1);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
            lv_obj_set_style_bg_opa(btn, LV_OPA_60, 0);
            lv_obj_set_style_radius(btn, 2, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_pad_all(btn, 0, 0);
            lv_obj_add_event_cb(btn, line_cb, LV_EVENT_CLICKED, (void*)(intptr_t)line_idx);
            line_btns_[line_idx] = btn;
            line_idx++;
        }
    }

    // Box labels (centered in each box)
    for (int r = 0; r < BOXES; r++) {
        for (int c = 0; c < BOXES; c++) {
            lv_obj_t* lbl = lv_label_create(scr);
            lv_label_set_text(lbl, "");
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_pos(lbl, ox + c * GAP + GAP / 2 - 5, oy + r * GAP + GAP / 2 - 8);
            box_labels_[r * BOXES + c] = lbl;
        }
    }

    lobby_list_ = nullptr;
    reset_board();
    update_status();
    update_scores();
    return scr;
}

void DotsBoxes::line_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= TOTAL_LINES) return;
    if (s_self->lines_[idx]) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && s_self->current_ == P2) return;

    bool completed = s_self->place_line(idx);

    if (s_self->mode_ == MODE_NETWORK) {
        s_self->send_move(idx);
        if (!s_self->game_done_ && !completed) {
            s_self->my_turn_ = false;
            s_self->update_status();
        }
    }
}

bool DotsBoxes::place_line(int idx) {
    if (lines_[idx]) return false;
    lines_[idx] = true;
    sound_move();

    // Color the line
    lv_color_t color = (current_ == P1) ? UI_COLOR_ACCENT : UI_COLOR_SUCCESS;
    lv_obj_set_style_bg_color(line_btns_[idx], color, 0);
    lv_obj_set_style_bg_opa(line_btns_[idx], LV_OPA_COVER, 0);

    int completed = check_boxes(idx);

    if (completed > 0) {
        update_scores();
        // Check if all boxes filled
        int total = 0;
        for (int i = 0; i < BOXES * BOXES; i++) {
            if (boxes_[i] != NONE) total++;
        }
        if (total == BOXES * BOXES) {
            game_done_ = true;
            show_result();
            return true;
        }
        // Player gets another turn when completing a box
        return true;
    }

    // No box completed — switch turns
    current_ = (current_ == P1) ? P2 : P1;
    update_status();
    return false;
}

// Check which boxes (if any) are completed by placing line idx
int DotsBoxes::check_boxes(int line_idx) {
    int completed = 0;

    // For each box, check if all 4 sides are filled
    for (int r = 0; r < BOXES; r++) {
        for (int c = 0; c < BOXES; c++) {
            if (boxes_[r * BOXES + c] != NONE) continue;

            // H-line indices for box(r,c):
            //   top    = r * BOXES + c           (h-line row r, col c)
            //   bottom = (r+1) * BOXES + c       (h-line row r+1, col c)
            // V-line indices (offset by H_LINES):
            //   left   = H_LINES + r * DOTS + c
            //   right  = H_LINES + r * DOTS + (c+1)
            int top    = r * BOXES + c;
            int bottom = (r + 1) * BOXES + c;
            int left   = H_LINES + r * DOTS + c;
            int right  = H_LINES + r * DOTS + (c + 1);

            if (lines_[top] && lines_[bottom] && lines_[left] && lines_[right]) {
                boxes_[r * BOXES + c] = current_;
                // Show owner in box
                lv_label_set_text(box_labels_[r * BOXES + c],
                    current_ == P1 ? "R" : "B");
                lv_obj_set_style_text_color(box_labels_[r * BOXES + c],
                    current_ == P1 ? UI_COLOR_ACCENT : UI_COLOR_SUCCESS, 0);
                completed++;
            }
        }
    }
    return completed;
}

void DotsBoxes::update_status() {
    if (!lbl_status_) return;
    if (mode_ == MODE_CPU) {
        lv_label_set_text(lbl_status_, current_ == P1 ? "Your turn" : "CPU...");
    } else if (mode_ == MODE_LOCAL) {
        lv_label_set_text(lbl_status_, current_ == P1 ? "Red's turn" : "Blue's turn");
    } else {
        lv_label_set_text(lbl_status_, my_turn_ ? "Your turn" : "Waiting...");
    }
}

void DotsBoxes::update_scores() {
    if (!lbl_score_) return;
    score_p1_ = 0;
    score_p2_ = 0;
    for (int i = 0; i < BOXES * BOXES; i++) {
        if (boxes_[i] == P1) score_p1_++;
        else if (boxes_[i] == P2) score_p2_++;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "R:%d B:%d", score_p1_, score_p2_);
    lv_label_set_text(lbl_score_, buf);
}

void DotsBoxes::show_result() {
    if (!screen_) return;
    const char* text;
    bool is_win;

    if (mode_ == MODE_NETWORK) {
        bool i_won = (my_player_ == P1 && score_p1_ > score_p2_) ||
                     (my_player_ == P2 && score_p2_ > score_p1_);
        bool draw = (score_p1_ == score_p2_);
        text = draw ? "Draw!" : (i_won ? "You Win!" : "You Lose!");
        is_win = i_won;
    } else if (mode_ == MODE_CPU) {
        if (score_p1_ == score_p2_) { text = "Draw!"; is_win = false; }
        else if (score_p1_ > score_p2_) { text = "You Win!"; is_win = true; }
        else { text = "CPU Wins!"; is_win = false; }
    } else {
        if (score_p1_ == score_p2_) { text = "Draw!"; is_win = false; }
        else if (score_p1_ > score_p2_) { text = "Red Wins!"; is_win = true; }
        else { text = "Blue Wins!"; is_win = true; }
    }
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

// ── CPU AI ──

// Count how many sides of each adjacent box are already filled if this line is placed
int DotsBoxes::score_line(int idx) {
    if (lines_[idx]) return -1000;  // Already placed

    // Temporarily place the line
    lines_[idx] = true;

    int completes = 0;  // Boxes this would complete (4 sides)
    int gives = 0;      // Boxes this would give 3rd side to (opponent can then complete)

    for (int r = 0; r < BOXES; r++) {
        for (int c = 0; c < BOXES; c++) {
            if (boxes_[r * BOXES + c] != NONE) continue;
            int top    = r * BOXES + c;
            int bottom = (r + 1) * BOXES + c;
            int left   = H_LINES + r * DOTS + c;
            int right  = H_LINES + r * DOTS + (c + 1);
            int count = (lines_[top] ? 1 : 0) + (lines_[bottom] ? 1 : 0) +
                        (lines_[left] ? 1 : 0) + (lines_[right] ? 1 : 0);
            if (count == 4) completes++;
            else if (count == 3) gives++;
        }
    }

    lines_[idx] = false;  // Undo

    // Completing a box is great (+100 per box)
    if (completes > 0) return 100 * completes;
    // Giving opponent a 3-sided box is bad (-50 per box)
    if (gives > 0) return -50 * gives;
    // Neutral move
    return 0;
}

void DotsBoxes::cpu_move() {
    // Collect all available lines
    int available[TOTAL_LINES];
    int scores[TOTAL_LINES];
    int n = 0;

    for (int i = 0; i < TOTAL_LINES; i++) {
        if (!lines_[i]) {
            available[n] = i;
            scores[n] = score_line(i);
            n++;
        }
    }
    if (n == 0) return;

    // Find best score
    int best = scores[0];
    for (int i = 1; i < n; i++) {
        if (scores[i] > best) best = scores[i];
    }

    // Pick randomly among best
    int candidates[TOTAL_LINES];
    int nc = 0;
    for (int i = 0; i < n; i++) {
        if (scores[i] == best) candidates[nc++] = available[i];
    }

    int pick = candidates[random(0, nc)];
    bool completed = place_line(pick);

    // If completed a box, CPU gets another turn immediately
    if (completed && !game_done_) {
        cpu_pending_ = true;
        cpu_think_time_ = millis();
    }
}

void DotsBoxes::send_move(int idx) {
    net_mc_++;
    Serial.printf("[DB] send_move line=%d to %s, current=%d, mc=%u\n",
                  idx, peer_ip_.toString().c_str(), (int)current_, (unsigned)net_mc_);
    StaticJsonDocument<160> doc;
    doc["type"] = "move";
    doc["game"] = "dotsboxes";
    doc["line"] = idx;
    doc["mc"] = net_mc_;
    serializeJson(doc, net_last_move_, sizeof(net_last_move_));
    discovery_send_game_data(peer_ip_, net_last_move_);
}

// ── Lifecycle ──

lv_obj_t* DotsBoxes::createScreen() {
    s_self = this;
    db_invite_msgbox = nullptr;
    mode_ = MODE_SELECT;
    screen_ = create_mode_select();
    return screen_;
}

void DotsBoxes::update() {
    if (mode_ == MODE_NETWORK && !game_done_) {
        if (millis() - net_last_hb_ms_ > NET_HB_INTERVAL_MS) {
            net_last_hb_ms_ = millis();
            char hb[80];
            snprintf(hb, sizeof(hb),
                "{\"type\":\"move\",\"game\":\"dotsboxes\",\"a\":\"hb\",\"mc\":%u}",
                (unsigned)net_mc_);
            discovery_send_game_data(peer_ip_, hb);
        }
    }

    // CPU move with delay
    if (mode_ == MODE_CPU && current_ == P2 && !game_done_) {
        if (!cpu_pending_) {
            cpu_pending_ = true;
            cpu_think_time_ = millis();
        } else if (millis() - cpu_think_time_ > 500) {
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
                if (strcmp(peers[i].game, "dotsboxes") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)",
                             peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, db_lobby_peer_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) lv_list_add_text(lobby_list_, "Searching...");
        }
    }
}

void DotsBoxes::destroy() {
    if (db_invite_msgbox) {
        lv_msgbox_close(db_invite_msgbox);
        db_invite_msgbox = nullptr;
    }
    if (mode_ == MODE_NETWORK) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"dotsboxes\",\"abandon\":true}");
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    lbl_status_ = nullptr;
    lbl_score_ = nullptr;
    lobby_list_ = nullptr;
}

void DotsBoxes::onNetworkData(const char* json) {
    Serial.printf("[DB] onNetworkData: %s\n", json);
    StaticJsonDocument<160> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "dotsboxes") != 0) return;
    if (doc["abandon"] | false) {
        game_done_ = true;
        if (lbl_status_) lv_label_set_text(lbl_status_, "Opponent left");
        return;
    }

    const char* action = doc["a"] | "";
    uint32_t peer_mc = doc["mc"] | 0;
    if (strcmp(action, "hb") == 0) {
        if (peer_mc < net_mc_ && net_last_move_[0]) {
            discovery_send_game_data(peer_ip_, net_last_move_);
        }
        return;
    }
    if (peer_mc != net_mc_ + 1) return;

    int line = doc["line"] | -1;
    Serial.printf("[DB] received line=%d, current=%d, mc=%u\n",
                  line, (int)current_, (unsigned)peer_mc);
    if (line < 0 || line >= TOTAL_LINES) return;

    sound_opponent_move();
    bool completed = place_line(line);
    net_mc_ = peer_mc;
    if (!game_done_) {
        if (!completed) {
            my_turn_ = true;
        }
        // If completed, opponent gets another turn — my_turn_ stays false
        update_status();
    }
}
