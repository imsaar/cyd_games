#include "cup_pong.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <Arduino.h>

static CupPong* s_self = nullptr;

// ── Cup layout: 4-3-2-1 triangle ──

void CupPong::setup_cups() {
    // Triangle near top of screen
    // Row 0 (back):  4 cups    y = 35
    // Row 1:         3 cups    y = 67
    // Row 2:         2 cups    y = 99
    // Row 3 (front): 1 cup     y = 131
    static const int rows[] = {4, 3, 2, 1};
    static const int row_y[] = {35, 67, 99, 131};
    int gap = CUP_W + 6;
    int idx = 0;

    for (int r = 0; r < 4; r++) {
        int n = rows[r];
        int total_w = n * gap - 6;
        int ox = (320 - total_w) / 2 + CUP_W / 2;
        for (int c = 0; c < n; c++) {
            cups_[idx].cx = ox + c * gap;
            cups_[idx].cy = row_y[r];
            cups_[idx].alive = true;
            idx++;
        }
    }
    cups_alive_ = MAX_CUPS;

    // Create cup LVGL objects — red solo cups
    for (int i = 0; i < MAX_CUPS; i++) {
        lv_obj_t* obj = lv_obj_create(screen_);
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, CUP_W, CUP_H);
        lv_obj_set_pos(obj, cups_[i].cx - CUP_W / 2, cups_[i].cy - CUP_H / 2);
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xcc2222), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(obj, 3, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0xee4444), 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        cups_[i].obj = obj;

        // White rim at top of cup
        lv_obj_t* rim = lv_obj_create(obj);
        lv_obj_remove_style_all(rim);
        lv_obj_set_size(rim, CUP_W, 4);
        lv_obj_set_pos(rim, 0, 0);
        lv_obj_set_style_bg_color(rim, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_bg_opa(rim, LV_OPA_80, 0);
        lv_obj_set_style_radius(rim, 2, 0);
        lv_obj_clear_flag(rim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

// ── Touch handling — tap to set aim point ──

void CupPong::aim_touch_cb(lv_event_t* e) {
    if (!s_self || s_self->state_ != AIM) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    // Only aim in the lower area (below table)
    if (p.y < TABLE_Y + 10) return;

    s_self->aim_x_ = p.x;
    s_self->aim_y_ = p.y;
    s_self->update_aim_visual();
}

void CupPong::throw_cb(lv_event_t* e) {
    if (!s_self || s_self->state_ != AIM) return;
    s_self->throw_ball();
}

void CupPong::update_aim_visual() {
    // Move crosshair marker
    if (aim_marker_) {
        lv_obj_set_pos(aim_marker_, aim_x_ - 8, aim_y_ - 8);
    }
    // Update aim line from ball start to aim point
    if (aim_line_) {
        static lv_point_t pts[2];
        pts[0] = {160, 228};
        pts[1] = {(lv_coord_t)aim_x_, (lv_coord_t)aim_y_};
        lv_line_set_points(aim_line_, pts, 2);
    }
}

// ── Ball throwing & physics ──

void CupPong::throw_ball() {
    if (state_ != AIM || balls_left_ <= 0) return;

    state_ = FLYING;
    balls_left_--;
    has_bounced_ = false;

    // Ball starts at bottom center
    ball_x_ = 160;
    ball_y_ = 228;

    // Aim toward the aim marker — ball arcs to that point
    float dx = aim_x_ - ball_x_;
    float dy = aim_y_ - ball_y_;
    float dist = sqrtf(dx * dx + dy * dy);
    if (dist < 1) dist = 1;

    // Normalize and scale velocity
    float speed = 3.0f;
    ball_vx_ = (dx / dist) * speed;
    ball_vy_ = (dy / dist) * speed;

    // Hide aim visuals
    if (aim_line_) lv_obj_add_flag(aim_line_, LV_OBJ_FLAG_HIDDEN);
    if (aim_marker_) lv_obj_add_flag(aim_marker_, LV_OBJ_FLAG_HIDDEN);
    if (throw_btn_) lv_obj_add_flag(throw_btn_, LV_OBJ_FLAG_HIDDEN);

    // Show ball
    if (ball_obj_) lv_obj_clear_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);
    draw_ball();

    if (lbl_balls_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Balls: %d", balls_left_);
        lv_label_set_text(lbl_balls_, buf);
    }
}

void CupPong::update_ball() {
    // Gravity
    ball_vy_ += 0.05f;

    ball_x_ += ball_vx_;
    ball_y_ += ball_vy_;

    // Bounce off table surface
    if (!has_bounced_ && ball_y_ >= TABLE_Y && ball_vy_ > 0) {
        has_bounced_ = true;
        ball_vy_ = -3.2f;  // bounce upward

        // Add some randomness to the bounce
        ball_vx_ += (random(-10, 11)) * 0.05f;

        // Flash table
        if (table_obj_) {
            lv_obj_set_style_bg_color(table_obj_, lv_color_hex(0xffee55), 0);
        }
        state_ = BOUNCED;
    }

    // Reset table color after bounce
    if (state_ == BOUNCED && ball_y_ < TABLE_Y - 10) {
        if (table_obj_) {
            lv_obj_set_style_bg_color(table_obj_, lv_color_hex(0xc8a832), 0);
        }
        state_ = FLYING;
    }

    draw_ball();
}

void CupPong::check_hit() {
    if (state_ != FLYING && state_ != BOUNCED) return;

    // Only check cup hits after bounce (ball coming down from above)
    if (has_bounced_) {
        for (int i = 0; i < MAX_CUPS; i++) {
            if (!cups_[i].alive) continue;

            float dx = ball_x_ - cups_[i].cx;
            float dy = ball_y_ - cups_[i].cy;

            // Check if ball is inside cup area
            if (dx > -(CUP_W / 2 + 3) && dx < (CUP_W / 2 + 3) &&
                dy > -(CUP_H / 2 + 3) && dy < (CUP_H / 2 + 3)) {

                // Hit!
                cups_[i].alive = false;
                cups_alive_--;
                score_ += 10;

                // Hide cup
                lv_obj_add_flag(cups_[i].obj, LV_OBJ_FLAG_HIDDEN);

                // Flash ball green
                if (ball_obj_)
                    lv_obj_set_style_bg_color(ball_obj_, UI_COLOR_SUCCESS, 0);

                state_ = SINKING;
                state_timer_ = millis();

                if (lbl_status_) {
                    char buf[24];
                    snprintf(buf, sizeof(buf), "Score: %d", score_);
                    lv_label_set_text(lbl_status_, buf);
                }
                return;
            }
        }
    }

    // Ball went off screen
    if (ball_y_ > 245 || ball_y_ < -20 || ball_x_ < -20 || ball_x_ > 340) {
        state_ = SINKING;
        state_timer_ = millis();
    }
}

void CupPong::next_throw() {
    // Hide ball, reset color
    if (ball_obj_) {
        lv_obj_add_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ball_obj_, lv_color_hex(0xffffff), 0);
    }
    // Reset table color
    if (table_obj_) {
        lv_obj_set_style_bg_color(table_obj_, lv_color_hex(0xc8a832), 0);
    }

    // Check win/lose
    if (cups_alive_ <= 0 || balls_left_ <= 0) {
        show_result();
        return;
    }

    // Ready for next throw
    state_ = AIM;
    aim_x_ = 160;
    aim_y_ = 190;
    update_aim_visual();
    if (aim_line_) lv_obj_clear_flag(aim_line_, LV_OBJ_FLAG_HIDDEN);
    if (aim_marker_) lv_obj_clear_flag(aim_marker_, LV_OBJ_FLAG_HIDDEN);
    if (throw_btn_) lv_obj_clear_flag(throw_btn_, LV_OBJ_FLAG_HIDDEN);
}

void CupPong::draw_ball() {
    if (!ball_obj_) return;
    lv_obj_set_pos(ball_obj_, (int)ball_x_ - ball_r_, (int)ball_y_ - ball_r_);
}

void CupPong::show_result() {
    state_ = RESULT;

    if (ball_obj_) lv_obj_add_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);
    if (aim_line_) lv_obj_add_flag(aim_line_, LV_OBJ_FLAG_HIDDEN);
    if (aim_marker_) lv_obj_add_flag(aim_marker_, LV_OBJ_FLAG_HIDDEN);
    if (throw_btn_) lv_obj_add_flag(throw_btn_, LV_OBJ_FLAG_HIDDEN);

    bool won = (cups_alive_ <= 0);
    lv_color_t color = won ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT;

    lv_obj_t* ov = lv_obj_create(screen_);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, 260, 140);
    lv_obj_center(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ov, 16, 0);
    lv_obj_set_style_border_color(ov, color, 0);
    lv_obj_set_style_border_width(ov, 3, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    char buf[48];
    if (won) {
        snprintf(buf, sizeof(buf), "Cleared!\nScore: %d", score_);
    } else {
        snprintf(buf, sizeof(buf), "Game Over\n%d/%d cups left", cups_alive_, MAX_CUPS);
    }

    lv_obj_t* lbl = lv_label_create(ov);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t* btn = ui_create_btn(ov, "Menu", 100, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_add_event_cb(btn, [](lv_event_t* e) {
        screen_manager_back_to_menu();
    }, LV_EVENT_CLICKED, NULL);
}

// ── Lifecycle ──

lv_obj_t* CupPong::createScreen() {
    s_self = this;
    screen_ = ui_create_screen();
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_back_btn(screen_);

    // Score label
    lbl_status_ = lv_label_create(screen_);
    lv_label_set_text(lbl_status_, "Score: 0");
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_MID, 20, 10);

    // Balls remaining
    lbl_balls_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(lbl_balls_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl_balls_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_balls_, LV_ALIGN_TOP_RIGHT, -5, 10);

    // Yellow table surface — the bounce target
    table_obj_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(table_obj_);
    lv_obj_set_size(table_obj_, 260, 8);
    lv_obj_set_pos(table_obj_, 30, TABLE_Y - 4);
    lv_obj_set_style_bg_color(table_obj_, lv_color_hex(0xc8a832), 0);
    lv_obj_set_style_bg_opa(table_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(table_obj_, 3, 0);
    lv_obj_set_style_border_color(table_obj_, lv_color_hex(0xe0c040), 0);
    lv_obj_set_style_border_width(table_obj_, 1, 0);
    lv_obj_clear_flag(table_obj_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Setup cups (above table)
    setup_cups();

    // Ball object (hidden until thrown)
    ball_obj_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(ball_obj_);
    lv_obj_set_size(ball_obj_, ball_r_ * 2, ball_r_ * 2);
    lv_obj_set_style_bg_color(ball_obj_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ball_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ball_obj_, ball_r_, 0);
    lv_obj_clear_flag(ball_obj_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);

    // Aim line (dashed)
    aim_line_ = lv_line_create(screen_);
    lv_obj_set_style_line_color(aim_line_, lv_color_hex(0x888888), 0);
    lv_obj_set_style_line_width(aim_line_, 2, 0);
    lv_obj_set_style_line_dash_gap(aim_line_, 4, 0);
    lv_obj_set_style_line_dash_width(aim_line_, 4, 0);

    // Aim crosshair marker
    aim_marker_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(aim_marker_);
    lv_obj_set_size(aim_marker_, 16, 16);
    lv_obj_set_style_bg_opa(aim_marker_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(aim_marker_, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_border_width(aim_marker_, 2, 0);
    lv_obj_set_style_radius(aim_marker_, 8, 0);
    lv_obj_clear_flag(aim_marker_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Aim zone — clickable area below table for setting aim
    lv_obj_t* aim_zone = lv_obj_create(screen_);
    lv_obj_remove_style_all(aim_zone);
    lv_obj_set_size(aim_zone, 240, 80);
    lv_obj_set_pos(aim_zone, 0, TABLE_Y + 8);
    lv_obj_set_style_bg_opa(aim_zone, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(aim_zone, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(aim_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(aim_zone, aim_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(aim_zone, aim_touch_cb, LV_EVENT_PRESSING, NULL);

    // Throw button — bottom right
    throw_btn_ = ui_create_btn(screen_, LV_SYMBOL_PLAY " Go", 70, 36);
    lv_obj_set_pos(throw_btn_, 245, 198);
    lv_obj_add_event_cb(throw_btn_, throw_cb, LV_EVENT_CLICKED, NULL);

    // Init game state
    state_ = AIM;
    score_ = 0;
    balls_left_ = MAX_BALLS;
    aim_x_ = 160;
    aim_y_ = 190;

    char buf[16];
    snprintf(buf, sizeof(buf), "Balls: %d", balls_left_);
    lv_label_set_text(lbl_balls_, buf);

    update_aim_visual();

    return screen_;
}

void CupPong::update() {
    if (state_ == FLYING || state_ == BOUNCED) {
        update_ball();
        check_hit();
    } else if (state_ == SINKING) {
        if (millis() - state_timer_ > 500) {
            next_throw();
        }
    }
}

void CupPong::destroy() {
    s_self = nullptr;
    screen_ = nullptr;
    ball_obj_ = nullptr;
    aim_line_ = nullptr;
    aim_marker_ = nullptr;
    throw_btn_ = nullptr;
    lbl_status_ = nullptr;
    lbl_balls_ = nullptr;
    table_obj_ = nullptr;
    for (int i = 0; i < MAX_CUPS; i++) {
        cups_[i].obj = nullptr;
    }
}
