#include "pong.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>
#include <math.h>

static Pong* s_self = nullptr;
static lv_obj_t* pong_invite_msgbox = nullptr;
static IPAddress pong_pending_ip;

// ── Discovery callbacks ──

void pong_on_invite(const Peer& from) {
    if (!s_self) return;
    if (!s_self->lobby_list_) return;  // Not in lobby
    if (pong_invite_msgbox) return;

    pong_pending_ip = from.ip;

    static const char* btns[] = {"Accept", "Decline", ""};
    pong_invite_msgbox = lv_msgbox_create(NULL, "Pong Invite",
        from.name, btns, false);
    lv_obj_set_size(pong_invite_msgbox, 240, 140);
    lv_obj_center(pong_invite_msgbox);
    lv_obj_set_style_bg_color(pong_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(pong_invite_msgbox, UI_COLOR_TEXT, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(pong_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(pong_invite_msgbox);
        if (btn_id == 0) {  // Accept
            discovery_send_accept(pong_pending_ip);
            s_self->peer_ip_ = pong_pending_ip;
            s_self->is_local_ = false;
            s_self->is_host_ = false;
            discovery_set_game("pong", "playing");

            lv_msgbox_close(pong_invite_msgbox);
            pong_invite_msgbox = nullptr;

            lv_obj_t* scr = s_self->create_game_screen();
            s_self->screen_ = scr;
            s_self->reset_game();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
        } else {
            lv_msgbox_close(pong_invite_msgbox);
            pong_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void pong_on_accept(const Peer& from) {
    if (!s_self) return;
    if (!s_self->lobby_list_) return;

    s_self->peer_ip_ = from.ip;
    s_self->is_local_ = false;
    s_self->is_host_ = true;
    discovery_set_game("pong", "playing");

    lv_obj_t* scr = s_self->create_game_screen();
    s_self->screen_ = scr;
    s_self->reset_game();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
}

void pong_on_game_data(const char* json) {
    if (!s_self || !s_self->playing_) return;
    s_self->onNetworkData(json);
}

// ── Lobby ──

void pong_lobby_peer_cb(lv_event_t* e) {
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

lv_obj_t* Pong::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lv_obj_t* title = ui_create_title(scr, "Pong - Find Opponent");
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

// ── Game logic ──

void Pong::reset_ball() {
    ball_x_ = COURT_W / 2;
    ball_y_ = COURT_H / 2;
    ball_dx_ = (random(0, 2) == 0) ? 3.0f : -3.0f;
    ball_dy_ = ((float)random(-20, 20)) / 10.0f;
}

void Pong::reset_game() {
    paddle_l_y_ = COURT_H / 2 - PADDLE_H / 2;
    paddle_r_y_ = COURT_H / 2 - PADDLE_H / 2;
    score_l_ = 0;
    score_r_ = 0;
    playing_ = true;
    last_net_send_ = 0;
    reset_ball();
}

void Pong::step() {
    if (!playing_) return;

    // AI for right paddle in local mode
    if (is_local_) {
        float target = ball_y_ - PADDLE_H / 2;
        float speed = 2.5f;
        if (paddle_r_y_ < target) paddle_r_y_ += speed;
        else if (paddle_r_y_ > target) paddle_r_y_ -= speed;
        if (paddle_r_y_ < 0) paddle_r_y_ = 0;
        if (paddle_r_y_ > COURT_H - PADDLE_H) paddle_r_y_ = COURT_H - PADDLE_H;
    }

    // Only host runs ball physics in network mode
    if (!is_local_ && !is_host_) return;

    ball_x_ += ball_dx_;
    ball_y_ += ball_dy_;

    // Top/bottom bounce
    if (ball_y_ <= 0) { ball_y_ = 0; ball_dy_ = -ball_dy_; }
    if (ball_y_ >= COURT_H - BALL_SIZE) { ball_y_ = COURT_H - BALL_SIZE; ball_dy_ = -ball_dy_; }

    // Left paddle collision
    if (ball_x_ <= PADDLE_W + 4 && ball_x_ >= 4 &&
        ball_y_ + BALL_SIZE >= paddle_l_y_ && ball_y_ <= paddle_l_y_ + PADDLE_H) {
        ball_dx_ = fabsf(ball_dx_) * 1.05f;
        float hit = (ball_y_ + BALL_SIZE / 2 - paddle_l_y_ - PADDLE_H / 2) / (PADDLE_H / 2);
        ball_dy_ = hit * 4.0f;
        sound_move();
    }

    // Right paddle collision
    if (ball_x_ >= COURT_W - PADDLE_W - 4 - BALL_SIZE && ball_x_ <= COURT_W - 4 - BALL_SIZE &&
        ball_y_ + BALL_SIZE >= paddle_r_y_ && ball_y_ <= paddle_r_y_ + PADDLE_H) {
        ball_dx_ = -fabsf(ball_dx_) * 1.05f;
        float hit = (ball_y_ + BALL_SIZE / 2 - paddle_r_y_ - PADDLE_H / 2) / (PADDLE_H / 2);
        ball_dy_ = hit * 4.0f;
        sound_move();
    }

    // Score
    if (ball_x_ < 0) {
        score_r_++;
        update_score_label();
        if (score_r_ >= WIN_SCORE) { show_winner(false); return; }
        reset_ball();
    }
    if (ball_x_ > COURT_W) {
        score_l_++;
        update_score_label();
        if (score_l_ >= WIN_SCORE) { show_winner(true); return; }
        reset_ball();
    }

    // Clamp ball speed
    if (fabsf(ball_dx_) > 6.0f) ball_dx_ = (ball_dx_ > 0) ? 6.0f : -6.0f;
}

void Pong::draw() {
    if (!court_) return;
    if (paddle_l_) lv_obj_set_y(paddle_l_, (int)paddle_l_y_);
    if (paddle_r_) lv_obj_set_y(paddle_r_, (int)paddle_r_y_);
    if (ball_) lv_obj_set_pos(ball_, (int)ball_x_, (int)ball_y_);
}

void Pong::update_score_label() {
    if (!lbl_score_) return;
    char buf[32];
    if (is_local_) {
        snprintf(buf, sizeof(buf), "YOU %d  -  %d CPU", score_l_, score_r_);
    } else {
        if (is_host_) {
            snprintf(buf, sizeof(buf), "YOU %d  -  %d OPP", score_l_, score_r_);
        } else {
            snprintf(buf, sizeof(buf), "OPP %d  -  %d YOU", score_l_, score_r_);
        }
    }
    lv_label_set_text(lbl_score_, buf);
}

void Pong::show_winner(bool left_won) {
    playing_ = false;

    const char* text;
    bool is_win;

    if (is_local_) {
        text = left_won ? "You Win!" : "CPU Wins!";
        is_win = left_won;
    } else {
        if (is_host_) {
            text = left_won ? "You Win!" : "You Lose!";
            is_win = left_won;
        } else {
            text = left_won ? "You Lose!" : "You Win!";
            is_win = !left_won;
        }
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
    snprintf(buf, sizeof(buf), "%s\n%d - %d", text, score_l_, score_r_);
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

// Network: host sends ball + scores + left paddle; guest sends right paddle
void Pong::send_state() {
    StaticJsonDocument<200> doc;
    doc["type"] = "move";
    doc["game"] = "pong";

    if (is_host_) {
        doc["bx"] = (int)(ball_x_ * 10);
        doc["by"] = (int)(ball_y_ * 10);
        doc["bdx"] = (int)(ball_dx_ * 100);
        doc["bdy"] = (int)(ball_dy_ * 100);
        doc["pl"] = (int)paddle_l_y_;
        doc["sl"] = score_l_;
        doc["sr"] = score_r_;
    } else {
        doc["pr"] = (int)paddle_r_y_;
    }

    char buf[200];
    serializeJson(doc, buf, sizeof(buf));
    discovery_send_game_data(peer_ip_, buf);
}

void Pong::onNetworkData(const char* json) {
    StaticJsonDocument<200> doc;
    if (deserializeJson(doc, json)) return;

    const char* game = doc["game"];
    if (!game || strcmp(game, "pong") != 0) return;
    if (doc["abandon"] | false) {
        show_winner(true);  // treat as win for remaining player
        return;
    }

    if (!is_host_) {
        // Guest receives ball state + host paddle + scores from host
        if (doc.containsKey("bx")) {
            ball_x_ = doc["bx"].as<int>() / 10.0f;
            ball_y_ = doc["by"].as<int>() / 10.0f;
            ball_dx_ = doc["bdx"].as<int>() / 100.0f;
            ball_dy_ = doc["bdy"].as<int>() / 100.0f;
            paddle_l_y_ = doc["pl"].as<int>();
            int sl = doc["sl"] | score_l_;
            int sr = doc["sr"] | score_r_;
            if (sl != score_l_ || sr != score_r_) {
                score_l_ = sl;
                score_r_ = sr;
                update_score_label();
                if (score_l_ >= WIN_SCORE) { show_winner(true); return; }
                if (score_r_ >= WIN_SCORE) { show_winner(false); return; }
            }
        }
    } else {
        // Host receives guest's right paddle position
        if (doc.containsKey("pr")) {
            paddle_r_y_ = doc["pr"].as<int>();
        }
    }
}

// ── Touch ──

void Pong::touch_cb(lv_event_t* e) {
    if (!s_self || !s_self->playing_) return;
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    float y = p.y - s_self->COURT_Y - s_self->PADDLE_H / 2;
    if (y < 0) y = 0;
    if (y > s_self->COURT_H - s_self->PADDLE_H) y = s_self->COURT_H - s_self->PADDLE_H;

    if (s_self->is_local_ || s_self->is_host_) {
        s_self->paddle_l_y_ = y;
    } else {
        s_self->paddle_r_y_ = y;
    }
}

// ── Screen creation ──

lv_obj_t* Pong::create_game_screen() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lbl_score_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_score_, LV_ALIGN_TOP_MID, 0, 2);

    court_ = lv_obj_create(scr);
    lv_obj_remove_style_all(court_);
    lv_obj_set_size(court_, COURT_W, COURT_H);
    lv_obj_align(court_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(court_, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(court_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(court_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(court_, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < 17; i++) {
        lv_obj_t* dash = lv_obj_create(court_);
        lv_obj_remove_style_all(dash);
        lv_obj_set_size(dash, 2, 6);
        lv_obj_set_pos(dash, COURT_W / 2 - 1, i * 12);
        lv_obj_set_style_bg_color(dash, lv_color_hex(0x333333), 0);
        lv_obj_set_style_bg_opa(dash, LV_OPA_COVER, 0);
        lv_obj_clear_flag(dash, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        center_line_[i] = dash;
    }

    // Left paddle (green)
    paddle_l_ = lv_obj_create(court_);
    lv_obj_remove_style_all(paddle_l_);
    lv_obj_set_size(paddle_l_, PADDLE_W, PADDLE_H);
    lv_obj_set_pos(paddle_l_, 4, COURT_H / 2 - PADDLE_H / 2);
    lv_obj_set_style_bg_color(paddle_l_, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_bg_opa(paddle_l_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(paddle_l_, 3, 0);
    lv_obj_clear_flag(paddle_l_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Right paddle (red)
    paddle_r_ = lv_obj_create(court_);
    lv_obj_remove_style_all(paddle_r_);
    lv_obj_set_size(paddle_r_, PADDLE_W, PADDLE_H);
    lv_obj_set_pos(paddle_r_, COURT_W - PADDLE_W - 4, COURT_H / 2 - PADDLE_H / 2);
    lv_obj_set_style_bg_color(paddle_r_, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(paddle_r_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(paddle_r_, 3, 0);
    lv_obj_clear_flag(paddle_r_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Ball
    ball_ = lv_obj_create(court_);
    lv_obj_remove_style_all(ball_);
    lv_obj_set_size(ball_, BALL_SIZE, BALL_SIZE);
    lv_obj_set_style_bg_color(ball_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_bg_opa(ball_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ball_, BALL_SIZE / 2, 0);
    lv_obj_clear_flag(ball_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(court_, touch_cb, LV_EVENT_PRESSING, NULL);

    update_score_label();
    return scr;
}

// ── Mode selection ──

void Pong::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->is_local_ = true;
    s_self->is_host_ = true;
    s_self->lobby_list_ = nullptr;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);

    lv_obj_t* scr = s_self->create_game_screen();
    s_self->screen_ = scr;
    s_self->reset_game();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
}

void Pong::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    discovery_set_game("pong", "waiting");
    discovery_on_invite(pong_on_invite);
    discovery_on_accept(pong_on_accept);
    discovery_on_game_data(pong_on_game_data);

    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

// ── Lifecycle ──

lv_obj_t* Pong::createScreen() {
    s_self = this;
    pong_invite_msgbox = nullptr;
    screen_ = ui_create_screen();
    ui_create_back_btn(screen_);

    lv_obj_t* title = ui_create_title(screen_, "Pong");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* btn_local = ui_create_btn(screen_, "vs Computer", 140, 50);
    lv_obj_align(btn_local, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn_local, mode_local_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_online = ui_create_btn(screen_, "vs Network", 140, 50);
    lv_obj_align(btn_online, LV_ALIGN_CENTER, 0, 35);
    lv_obj_add_event_cb(btn_online, mode_online_cb, LV_EVENT_CLICKED, NULL);

    court_ = nullptr;
    paddle_l_ = nullptr;
    paddle_r_ = nullptr;
    ball_ = nullptr;
    lbl_score_ = nullptr;
    lobby_list_ = nullptr;
    playing_ = false;
    return screen_;
}

void Pong::update() {
    // Lobby refresh
    if (lobby_list_ && !playing_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);

            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            int shown = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(peers[i].game, "pong") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)",
                             peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, pong_lobby_peer_cb, LV_EVENT_CLICKED,
                                        (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) {
                lv_list_add_text(lobby_list_, "Searching...");
            }
        }
    }

    // Game loop
    if (!playing_) return;

    uint32_t now = millis();
    if (now - last_frame_ >= 33) {
        last_frame_ = now;
        step();
        draw();
    }

    // Network state sync (~20fps)
    if (!is_local_ && now - last_net_send_ >= 50) {
        last_net_send_ = now;
        send_state();
    }
}

void Pong::destroy() {
    if (pong_invite_msgbox) {
        lv_msgbox_close(pong_invite_msgbox);
        pong_invite_msgbox = nullptr;
    }
    if (!is_local_) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"pong\",\"abandon\":true}");
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    court_ = nullptr;
    paddle_l_ = nullptr;
    paddle_r_ = nullptr;
    ball_ = nullptr;
    lbl_score_ = nullptr;
    lobby_list_ = nullptr;
    playing_ = false;
}

void Pong::onPeerJoined(const char* ip_str) {
    peer_ip_.fromString(ip_str);
    is_local_ = false;
    is_host_ = false;
}
