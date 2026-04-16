#include "whack_mole.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../hal/sound.h"
#include <Arduino.h>

static WhackMole* s_self = nullptr;

void WhackMole::hole_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= HOLES) return;
    s_self->whack(idx);
}

void WhackMole::whack(int idx) {
    if (!mole_up_[idx]) return;
    if (effect_active_[idx]) return;  // Already showing effect

    if (mole_bomb_[idx]) {
        // Whacked a baby — penalty!
        score_ -= 15;
        if (score_ < 0) score_ = 0;
        show_whack_effect(idx, false);
    } else {
        // Whacked a mole — score based on reaction time
        uint32_t reaction = millis() - mole_time_[idx];
        int pts = 10;
        if (reaction < 300) pts = 25;
        else if (reaction < 600) pts = 15;
        score_ += pts;
        sound_move();
        show_whack_effect(idx, true);
    }

    mole_up_[idx] = false;
    mole_bomb_[idx] = false;
    round_++;
    update_display();
}

void WhackMole::show_whack_effect(int idx, bool good) {
    effect_active_[idx] = true;
    effect_time_[idx] = millis();

    if (good) {
        // Successful whack — green flash with "POW!"
        lv_obj_set_style_bg_color(hole_btns_[idx], UI_COLOR_SUCCESS, 0);
        lv_label_set_text(hole_lbls_[idx], "POW!");
        lv_obj_set_style_text_color(hole_lbls_[idx], lv_color_hex(0xffffff), 0);
    } else {
        // Whacked a baby — red flash with "NO!"
        lv_obj_set_style_bg_color(hole_btns_[idx], UI_COLOR_ACCENT, 0);
        lv_label_set_text(hole_lbls_[idx], "-15");
        lv_obj_set_style_text_color(hole_lbls_[idx], lv_color_hex(0xffffff), 0);
    }
}

void WhackMole::spawn_mole() {
    // Count active moles
    int active = 0;
    for (int i = 0; i < HOLES; i++) {
        if (mole_up_[i] || effect_active_[i]) active++;
    }
    if (active >= 3) return;

    // Pick a random empty hole
    int empty[HOLES];
    int n = 0;
    for (int i = 0; i < HOLES; i++) {
        if (!mole_up_[i] && !effect_active_[i]) empty[n++] = i;
    }
    if (n == 0) return;

    int idx = empty[random(0, n)];
    mole_up_[idx] = true;
    mole_time_[idx] = millis();

    // 20% chance of baby (don't hit) after round 8
    mole_bomb_[idx] = (round_ > 8 && random(0, 5) == 0);

    if (mole_bomb_[idx]) {
        // Baby face — pink background, cute face
        lv_obj_set_style_bg_color(hole_btns_[idx], lv_color_hex(0xffaacc), 0);
        lv_label_set_text(hole_lbls_[idx], "^_^");
        lv_obj_set_style_text_color(hole_lbls_[idx], lv_color_hex(0x662244), 0);
    } else {
        // Mole — brown background, mole face
        lv_obj_set_style_bg_color(hole_btns_[idx], lv_color_hex(0x7a5c2e), 0);
        lv_label_set_text(hole_lbls_[idx], "(o.o)");
        lv_obj_set_style_text_color(hole_lbls_[idx], lv_color_hex(0x1a1000), 0);
    }
}

void WhackMole::hide_mole(int idx) {
    mole_up_[idx] = false;
    mole_bomb_[idx] = false;
    effect_active_[idx] = false;
    lv_label_set_text(hole_lbls_[idx], "");
    lv_obj_set_style_bg_color(hole_btns_[idx], lv_color_hex(0x2a2a3e), 0);
}

void WhackMole::update_display() {
    if (lbl_score_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Score: %d", score_);
        lv_label_set_text(lbl_score_, buf);
    }
    if (lbl_round_) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d/%d", round_, TOTAL_ROUNDS);
        lv_label_set_text(lbl_round_, buf);
    }
}

void WhackMole::show_result() {
    if (!screen_) return;
    game_done_ = true;
    sound_gameover();

    for (int i = 0; i < HOLES; i++) hide_mole(i);

    lv_obj_t* ov = lv_obj_create(screen_);
    lv_obj_remove_style_all(ov);
    lv_obj_set_size(ov, 260, 140);
    lv_obj_center(ov);
    lv_obj_set_style_bg_color(ov, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(ov, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ov, 16, 0);
    lv_obj_set_style_border_color(ov, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_border_width(ov, 3, 0);
    lv_obj_clear_flag(ov, LV_OBJ_FLAG_SCROLLABLE);

    char buf[48];
    snprintf(buf, sizeof(buf), "Game Over!\nScore: %d", score_);
    lv_obj_t* lbl = lv_label_create(ov);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, UI_COLOR_SUCCESS, 0);
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

lv_obj_t* WhackMole::createScreen() {
    s_self = this;
    screen_ = ui_create_screen();
    lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
    ui_create_back_btn(screen_);

    // Score label
    lbl_score_ = lv_label_create(screen_);
    lv_label_set_text(lbl_score_, "Score: 0");
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_score_, LV_ALIGN_TOP_MID, 0, 8);

    // Timer label
    lbl_timer_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(lbl_timer_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl_timer_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_timer_, LV_ALIGN_TOP_RIGHT, -5, 8);

    // Round label
    lbl_round_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(lbl_round_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_round_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_round_, LV_ALIGN_TOP_MID, 70, 10);

    // Create hole grid: 4 columns x 3 rows
    int btn_w = 64, btn_h = 52;
    int gap_x = 8, gap_y = 8;
    int total_w = COLS * btn_w + (COLS - 1) * gap_x;
    int total_h = ROWS * btn_h + (ROWS - 1) * gap_y;
    int ox = (320 - total_w) / 2;
    int oy = 32 + (240 - 32 - total_h) / 2;

    for (int r = 0; r < ROWS; r++) {
        for (int c = 0; c < COLS; c++) {
            int i = r * COLS + c;

            lv_obj_t* btn = lv_btn_create(screen_);
            lv_obj_set_size(btn, btn_w, btn_h);
            lv_obj_set_pos(btn, ox + c * (btn_w + gap_x), oy + r * (btn_h + gap_y));
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x2a2a3e), 0);
            lv_obj_set_style_bg_color(btn, lv_color_hex(0x3a3a5e), LV_STATE_PRESSED);
            lv_obj_set_style_radius(btn, 12, 0);
            lv_obj_set_style_shadow_width(btn, 0, 0);
            lv_obj_set_style_border_color(btn, lv_color_hex(0x444466), 0);
            lv_obj_set_style_border_width(btn, 2, 0);
            lv_obj_add_event_cb(btn, hole_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
            hole_btns_[i] = btn;

            lv_obj_t* lbl = lv_label_create(btn);
            lv_label_set_text(lbl, "");
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_center(lbl);
            hole_lbls_[i] = lbl;
        }
    }

    // Init game state
    score_ = 0;
    round_ = 0;
    misses_ = 0;
    game_done_ = false;
    game_start_ = millis();
    last_spawn_ = millis();
    spawn_interval_ = 800;
    mole_duration_ = 1200;
    for (int i = 0; i < HOLES; i++) {
        mole_up_[i] = false;
        mole_bomb_[i] = false;
        effect_active_[i] = false;
    }

    update_display();
    return screen_;
}

void WhackMole::update() {
    if (game_done_) return;

    uint32_t now = millis();

    // Timer countdown
    int elapsed = (now - game_start_) / 1000;
    int remaining = game_time_ - elapsed;
    if (remaining < 0) remaining = 0;

    if (lbl_timer_) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", remaining);
        lv_label_set_text(lbl_timer_, buf);
        lv_obj_set_style_text_color(lbl_timer_,
            remaining <= 5 ? UI_COLOR_ACCENT : UI_COLOR_WARNING, 0);
    }

    // Time's up
    if (remaining <= 0) {
        show_result();
        return;
    }

    // Clear whack effects after 300ms
    for (int i = 0; i < HOLES; i++) {
        if (effect_active_[i] && (now - effect_time_[i] > 300)) {
            hide_mole(i);
        }
    }

    // Hide expired moles
    for (int i = 0; i < HOLES; i++) {
        if (mole_up_[i] && !effect_active_[i] &&
            (now - mole_time_[i] > (uint32_t)mole_duration_)) {
            if (!mole_bomb_[i]) misses_++;
            hide_mole(i);
        }
    }

    // Spawn new moles
    if (now - last_spawn_ > (uint32_t)spawn_interval_) {
        last_spawn_ = now;
        spawn_mole();

        // Gradually increase difficulty
        if (spawn_interval_ > 400) spawn_interval_ -= 8;
        if (mole_duration_ > 600) mole_duration_ -= 10;
    }
}

void WhackMole::destroy() {
    s_self = nullptr;
    screen_ = nullptr;
    lbl_score_ = nullptr;
    lbl_timer_ = nullptr;
    lbl_round_ = nullptr;
    for (int i = 0; i < HOLES; i++) {
        hole_btns_[i] = nullptr;
        hole_lbls_[i] = nullptr;
    }
}
