#include "pong.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <ArduinoJson.h>
#include <math.h>

static Pong* s_self = nullptr;

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
        // Clamp AI paddle
        if (paddle_r_y_ < 0) paddle_r_y_ = 0;
        if (paddle_r_y_ > COURT_H - PADDLE_H) paddle_r_y_ = COURT_H - PADDLE_H;
    }

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
    }

    // Right paddle collision
    if (ball_x_ >= COURT_W - PADDLE_W - 4 - BALL_SIZE && ball_x_ <= COURT_W - 4 - BALL_SIZE &&
        ball_y_ + BALL_SIZE >= paddle_r_y_ && ball_y_ <= paddle_r_y_ + PADDLE_H) {
        ball_dx_ = -fabsf(ball_dx_) * 1.05f;
        float hit = (ball_y_ + BALL_SIZE / 2 - paddle_r_y_ - PADDLE_H / 2) / (PADDLE_H / 2);
        ball_dy_ = hit * 4.0f;
    }

    // Score
    if (ball_x_ < 0) {
        score_r_++;
        update_score_label();
        reset_ball();
    }
    if (ball_x_ > COURT_W) {
        score_l_++;
        update_score_label();
        reset_ball();
    }

    // Clamp ball speed
    if (fabsf(ball_dx_) > 6.0f) ball_dx_ = (ball_dx_ > 0) ? 6.0f : -6.0f;
}

void Pong::draw() {
    if (!court_) return;

    if (paddle_l_) lv_obj_set_y(paddle_l_, (int)paddle_l_y_);
    if (paddle_r_) lv_obj_set_y(paddle_r_, (int)paddle_r_y_);

    if (ball_) {
        lv_obj_set_pos(ball_, (int)ball_x_, (int)ball_y_);
    }
}

void Pong::update_score_label() {
    if (!lbl_score_) return;
    char buf[32];
    snprintf(buf, sizeof(buf), "YOU %d  -  %d CPU", score_l_, score_r_);
    lv_label_set_text(lbl_score_, buf);
}

void Pong::touch_cb(lv_event_t* e) {
    if (!s_self || !s_self->playing_) return;
    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    // p.y is in screen coords; court starts at COURT_Y from top
    float y = p.y - s_self->COURT_Y - s_self->PADDLE_H / 2;
    if (y < 0) y = 0;
    if (y > s_self->COURT_H - s_self->PADDLE_H) y = s_self->COURT_H - s_self->PADDLE_H;

    s_self->paddle_l_y_ = y;
}

lv_obj_t* Pong::create_game_screen() {
    lv_obj_t* scr = ui_create_screen();

    ui_create_back_btn(scr);

    lbl_score_ = lv_label_create(scr);
    lv_label_set_text(lbl_score_, "YOU 0  -  0 CPU");
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_score_, LV_ALIGN_TOP_MID, 0, 2);

    // Court container
    court_ = lv_obj_create(scr);
    lv_obj_remove_style_all(court_);
    lv_obj_set_size(court_, COURT_W, COURT_H);
    lv_obj_align(court_, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(court_, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_bg_opa(court_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(court_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(court_, LV_OBJ_FLAG_CLICKABLE);

    // Dashed center line
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

    // Left paddle (YOU — green)
    paddle_l_ = lv_obj_create(court_);
    lv_obj_remove_style_all(paddle_l_);
    lv_obj_set_size(paddle_l_, PADDLE_W, PADDLE_H);
    lv_obj_set_pos(paddle_l_, 4, COURT_H / 2 - PADDLE_H / 2);
    lv_obj_set_style_bg_color(paddle_l_, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_bg_opa(paddle_l_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(paddle_l_, 3, 0);
    lv_obj_clear_flag(paddle_l_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Right paddle (CPU — red)
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

    // Touch court to control your paddle (LV_EVENT_PRESSING = continuous)
    lv_obj_add_event_cb(court_, touch_cb, LV_EVENT_PRESSING, NULL);

    return scr;
}

void Pong::mode_local_cb(lv_event_t* e) {
    if (!s_self) return;
    s_self->is_local_ = true;
    s_self->is_host_ = true;
    discovery_clear_game();

    lv_obj_t* scr = s_self->create_game_screen();
    s_self->screen_ = scr;
    s_self->reset_game();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
}

void Pong::mode_online_cb(lv_event_t* e) {
    if (!s_self) return;
    // Show "coming soon" message
    lv_obj_t* msgbox = lv_msgbox_create(NULL, "Online Pong",
        "Coming soon!\nUse Tic-Tac-Toe for online play.", NULL, true);
    lv_obj_center(msgbox);
}

lv_obj_t* Pong::createScreen() {
    s_self = this;
    screen_ = ui_create_screen();
    ui_create_back_btn(screen_);

    lv_obj_t* title = ui_create_title(screen_, "Pong");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* btn_local = ui_create_btn(screen_, "vs Computer", 140, 50);
    lv_obj_align(btn_local, LV_ALIGN_CENTER, 0, -30);
    lv_obj_add_event_cb(btn_local, mode_local_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_online = ui_create_btn(screen_, "vs Online", 140, 50);
    lv_obj_align(btn_online, LV_ALIGN_CENTER, 0, 35);
    lv_obj_add_event_cb(btn_online, mode_online_cb, LV_EVENT_CLICKED, NULL);

    court_ = nullptr;
    paddle_l_ = nullptr;
    paddle_r_ = nullptr;
    ball_ = nullptr;
    lbl_score_ = nullptr;
    playing_ = false;
    return screen_;
}

void Pong::update() {
    if (!playing_) return;

    uint32_t now = millis();
    if (now - last_frame_ >= 33) {
        last_frame_ = now;
        step();
        draw();
    }
}

void Pong::destroy() {
    discovery_clear_game();
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

void Pong::onNetworkData(const char* json) {
    StaticJsonDocument<128> doc;
    if (deserializeJson(doc, json)) return;
    if (doc.containsKey("paddle_y")) {
        float y = doc["paddle_y"];
        if (is_host_) paddle_r_y_ = y;
        else paddle_l_y_ = y;
    }
}
