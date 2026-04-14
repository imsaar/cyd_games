#include "cup_pong.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <Arduino.h>

static CupPong* s_self = nullptr;

// ── Cup layout: 4-3-2-1 triangle ──

void CupPong::setup_cups() {
    // Triangle of cups centered horizontally, near top of screen
    // Row 0 (back):  4 cups    y ~= 38
    // Row 1:         3 cups    y ~= 72
    // Row 2:         2 cups    y ~= 106
    // Row 3 (front): 1 cup     y ~= 140
    static const int rows[] = {4, 3, 2, 1};
    static const int row_y[] = {38, 72, 106, 140};
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

    // Create cup LVGL objects
    for (int i = 0; i < MAX_CUPS; i++) {
        lv_obj_t* obj = lv_obj_create(screen_);
        lv_obj_remove_style_all(obj);
        lv_obj_set_size(obj, CUP_W, CUP_H);
        lv_obj_set_pos(obj, cups_[i].cx - CUP_W / 2, cups_[i].cy - CUP_H / 2);
        lv_obj_set_style_bg_color(obj, UI_COLOR_ACCENT, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(obj, 4, 0);
        lv_obj_set_style_border_color(obj, lv_color_hex(0xff6680), 0);
        lv_obj_set_style_border_width(obj, 1, 0);
        lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        cups_[i].obj = obj;

        // Cup rim highlight at top
        lv_obj_t* rim = lv_obj_create(obj);
        lv_obj_remove_style_all(rim);
        lv_obj_set_size(rim, CUP_W, 4);
        lv_obj_set_pos(rim, 0, 0);
        lv_obj_set_style_bg_color(rim, lv_color_hex(0xff8899), 0);
        lv_obj_set_style_bg_opa(rim, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(rim, 2, 0);
        lv_obj_clear_flag(rim, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    }
}

// ── Touch handling ──

void CupPong::screen_touch_cb(lv_event_t* e) {
    if (!s_self || s_self->state_ != AIM) return;

    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    if (code == LV_EVENT_PRESSED) {
        s_self->aiming_ = true;
        s_self->touch_start_x_ = p.x;
        s_self->touch_start_y_ = p.y;
        s_self->aim_x_ = p.x;
    } else if (code == LV_EVENT_PRESSING) {
        if (s_self->aiming_) {
            s_self->aim_x_ = p.x;
            s_self->draw_aim();
        }
    } else if (code == LV_EVENT_RELEASED) {
        if (s_self->aiming_) {
            s_self->aiming_ = false;
            s_self->throw_ball();
        }
    }
}

// ── Ball throwing ──

void CupPong::throw_ball() {
    if (state_ != AIM || balls_left_ <= 0) return;

    state_ = FLYING;
    balls_left_--;

    // Ball starts at bottom center
    ball_x_ = 160;
    ball_y_ = 220;

    // Velocity toward aim point — the ball arcs upward
    float dx = aim_x_ - ball_x_;
    ball_vx_ = dx * 0.04f;
    ball_vy_ = -3.5f;   // upward velocity

    // Hide aim line
    if (aim_line_) lv_obj_add_flag(aim_line_, LV_OBJ_FLAG_HIDDEN);

    // Show ball
    if (ball_obj_) {
        lv_obj_clear_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);
    }

    draw_ball();

    if (lbl_balls_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Balls: %d", balls_left_);
        lv_label_set_text(lbl_balls_, buf);
    }
}

void CupPong::update_ball() {
    // Simple physics: gravity pulls ball down
    ball_vy_ += 0.06f;  // gravity
    ball_x_ += ball_vx_;
    ball_y_ += ball_vy_;

    draw_ball();
}

void CupPong::check_hit() {
    // Check collision with each alive cup
    for (int i = 0; i < MAX_CUPS; i++) {
        if (!cups_[i].alive) continue;

        float dx = ball_x_ - cups_[i].cx;
        float dy = ball_y_ - cups_[i].cy;

        // Ball must be within cup bounds and moving downward (falling into cup)
        if (dx > -(CUP_W / 2 + 2) && dx < (CUP_W / 2 + 2) &&
            dy > -(CUP_H / 2) && dy < (CUP_H / 2) &&
            ball_vy_ > 0) {

            // Hit! Remove cup
            cups_[i].alive = false;
            cups_alive_--;
            score_ += 10;

            // Animate cup removal
            lv_obj_set_style_bg_opa(cups_[i].obj, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(cups_[i].obj, 0, 0);

            // Enter sinking state briefly
            state_ = SINKING;
            state_timer_ = millis();

            // Flash ball green
            if (ball_obj_) {
                lv_obj_set_style_bg_color(ball_obj_, UI_COLOR_SUCCESS, 0);
            }

            if (lbl_status_) {
                char buf[24];
                snprintf(buf, sizeof(buf), "Score: %d", score_);
                lv_label_set_text(lbl_status_, buf);
            }
            return;
        }
    }

    // Ball went off screen
    if (ball_y_ > 245 || ball_x_ < -10 || ball_x_ > 330 || ball_y_ < -10) {
        state_ = SINKING;
        state_timer_ = millis();
    }
}

void CupPong::next_throw() {
    // Hide ball
    if (ball_obj_) {
        lv_obj_add_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_bg_color(ball_obj_, lv_color_hex(0xffffff), 0);
    }

    // Check win/lose
    if (cups_alive_ <= 0) {
        show_result();
        return;
    }
    if (balls_left_ <= 0) {
        show_result();
        return;
    }

    // Ready for next throw
    state_ = AIM;
    aim_x_ = 160;
    draw_aim();
    if (aim_line_) lv_obj_clear_flag(aim_line_, LV_OBJ_FLAG_HIDDEN);
}

void CupPong::draw_ball() {
    if (!ball_obj_) return;
    lv_obj_set_pos(ball_obj_, (int)ball_x_ - ball_r_, (int)ball_y_ - ball_r_);
}

void CupPong::draw_aim() {
    if (!aim_line_) return;
    // Aim line from ball start to aim direction
    static lv_point_t pts[2];
    pts[0] = {160, 220};
    pts[1] = {(lv_coord_t)aim_x_, 160};
    lv_line_set_points(aim_line_, pts, 2);
}

void CupPong::show_result() {
    state_ = RESULT;

    if (ball_obj_) lv_obj_add_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);
    if (aim_line_) lv_obj_add_flag(aim_line_, LV_OBJ_FLAG_HIDDEN);

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

    // Status label (score)
    lbl_status_ = lv_label_create(screen_);
    lv_label_set_text(lbl_status_, "Score: 0");
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_MID, 20, 8);

    // Balls remaining
    lbl_balls_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(lbl_balls_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl_balls_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_balls_, LV_ALIGN_TOP_RIGHT, -5, 8);

    // Table surface (green felt area behind cups)
    lv_obj_t* table = lv_obj_create(screen_);
    lv_obj_remove_style_all(table);
    lv_obj_set_size(table, 280, 135);
    lv_obj_set_pos(table, 20, 20);
    lv_obj_set_style_bg_color(table, lv_color_hex(0x1a3322), 0);
    lv_obj_set_style_bg_opa(table, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(table, 8, 0);
    lv_obj_clear_flag(table, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Setup cups
    setup_cups();

    // Ball object (initially hidden)
    ball_obj_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(ball_obj_);
    lv_obj_set_size(ball_obj_, ball_r_ * 2, ball_r_ * 2);
    lv_obj_set_style_bg_color(ball_obj_, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_bg_opa(ball_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ball_obj_, ball_r_, 0);
    lv_obj_clear_flag(ball_obj_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ball_obj_, LV_OBJ_FLAG_HIDDEN);

    // Aim line
    aim_line_ = lv_line_create(screen_);
    lv_obj_set_style_line_color(aim_line_, lv_color_hex(0x888888), 0);
    lv_obj_set_style_line_width(aim_line_, 2, 0);
    lv_obj_set_style_line_dash_gap(aim_line_, 4, 0);
    lv_obj_set_style_line_dash_width(aim_line_, 4, 0);

    // Throw zone label at bottom
    lv_obj_t* hint = lv_label_create(screen_);
    lv_label_set_text(hint, "Touch & drag to aim, release to throw");
    lv_obj_set_style_text_color(hint, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -3);

    // Register touch events on screen
    lv_obj_add_event_cb(screen_, screen_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(screen_, screen_touch_cb, LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(screen_, screen_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_flag(screen_, LV_OBJ_FLAG_CLICKABLE);

    // Init game state
    state_ = AIM;
    score_ = 0;
    balls_left_ = MAX_BALLS;
    aiming_ = false;
    aim_x_ = 160;

    char buf[16];
    snprintf(buf, sizeof(buf), "Balls: %d", balls_left_);
    lv_label_set_text(lbl_balls_, buf);

    draw_aim();

    return screen_;
}

void CupPong::update() {
    if (state_ == FLYING) {
        update_ball();
        check_hit();
    } else if (state_ == SINKING) {
        // Brief pause after hit/miss, then next throw
        if (millis() - state_timer_ > 400) {
            next_throw();
        }
    }
}

void CupPong::destroy() {
    s_self = nullptr;
    screen_ = nullptr;
    ball_obj_ = nullptr;
    aim_line_ = nullptr;
    lbl_status_ = nullptr;
    lbl_balls_ = nullptr;
    for (int i = 0; i < MAX_CUPS; i++) {
        cups_[i].obj = nullptr;
        cups_[i].lbl = nullptr;
    }
}
