#include "pong.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../net/mp_shell.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>
#include <math.h>

static Pong* s_self = nullptr;
static const MpShellConfig kCfg = { "pong", "Pong", /*show_cpu_button=*/false, /*show_idle_peers=*/false };

void Pong::on_host_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->peer_ip_ = peer.ip;
    s_self->is_local_ = false;
    s_self->is_host_ = true;
    lv_obj_t* scr = s_self->create_game_screen();
    s_self->screen_ = scr;
    s_self->reset_game();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
}

void Pong::on_guest_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->peer_ip_ = peer.ip;
    s_self->is_local_ = false;
    s_self->is_host_ = false;
    lv_obj_t* scr = s_self->create_game_screen();
    s_self->screen_ = scr;
    s_self->reset_game();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
}

void Pong::on_game_data(const char* json) {
    if (!s_self || !s_self->playing_) return;
    s_self->onNetworkData(json);
}

// Fired if the peer declines, or doesn't respond within the retry window.
// Without this, the lobby just silently resumes showing the peer list on
// its next 2s refresh with no explanation of what happened.
void Pong::on_invite_failed(const Peer&) {
    mp_shell_set_status("No response - try again");
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

void Pong::mode_local_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->is_local_ = true;
    s_self->is_host_ = true;
    mp_shell_end(s_self->peer_ip_, false);

    lv_obj_t* scr = s_self->create_game_screen();
    s_self->screen_ = scr;
    s_self->reset_game();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
}

void Pong::mode_online_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->is_local_ = false;
    lv_obj_t* scr = mp_shell_host_lobby(kCfg, on_host_ready, on_guest_ready, on_game_data, on_invite_failed);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

// ── Lifecycle ──

lv_obj_t* Pong::createScreen() {
    s_self = this;
    screen_ = mp_create_mode_select(kCfg, nullptr, mode_local_cb, mode_online_cb);

    court_ = nullptr;
    paddle_l_ = nullptr;
    paddle_r_ = nullptr;
    ball_ = nullptr;
    lbl_score_ = nullptr;
    playing_ = false;
    return screen_;
}

void Pong::update() {
    if (!playing_) mp_shell_lobby_tick();

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
    mp_shell_end(peer_ip_, !is_local_);
    s_self = nullptr;
    screen_ = nullptr;
    court_ = nullptr;
    paddle_l_ = nullptr;
    paddle_r_ = nullptr;
    ball_ = nullptr;
    lbl_score_ = nullptr;
    playing_ = false;
}

void Pong::onPeerJoined(const char* ip_str) {
    peer_ip_.fromString(ip_str);
    is_local_ = false;
    is_host_ = false;
}
