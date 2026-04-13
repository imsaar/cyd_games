#include "memory_match.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <Arduino.h>

// Static self pointer for callbacks (only one MemoryMatch instance exists)
static MemoryMatch* s_self = nullptr;

const char* const MemoryMatch::symbols[6] = {
    LV_SYMBOL_HOME, LV_SYMBOL_BELL, LV_SYMBOL_EYE_OPEN,
    LV_SYMBOL_AUDIO, LV_SYMBOL_GPS, LV_SYMBOL_CHARGE
};

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
    if (!s_self) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);

    if (s_self->checking_) return;
    if (idx < 0 || idx >= NUM_CARDS) return;
    if (s_self->matched_[idx]) return;
    if (s_self->revealed_[idx]) return;

    s_self->reveal(idx);

    if (s_self->first_pick_ == -1) {
        s_self->first_pick_ = idx;
    } else {
        s_self->second_pick_ = idx;
        s_self->moves_++;
        s_self->checking_ = true;
        s_self->check_time_ = millis();

        char buf[16];
        snprintf(buf, sizeof(buf), "Moves: %d", s_self->moves_);
        lv_label_set_text(s_self->lbl_moves_, buf);
    }
}

void MemoryMatch::check_match() {
    if (values_[first_pick_] == values_[second_pick_]) {
        matched_[first_pick_] = true;
        matched_[second_pick_] = true;
        pairs_found_++;

        lv_obj_set_style_bg_color(cards_[first_pick_], lv_color_hex(0x1a4d2e), 0);
        lv_obj_set_style_bg_color(cards_[second_pick_], lv_color_hex(0x1a4d2e), 0);

        char buf[16];
        snprintf(buf, sizeof(buf), "Pairs: %d/6", pairs_found_);
        lv_label_set_text(lbl_pairs_, buf);

        if (pairs_found_ == 6) {
            show_win();
        }
    } else {
        hide(first_pick_);
        hide(second_pick_);
    }

    first_pick_ = -1;
    second_pick_ = -1;
    checking_ = false;
}

void MemoryMatch::show_win() {
    lv_obj_t* overlay = lv_obj_create(screen_);
    lv_obj_set_size(overlay, 200, 100);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
    lv_obj_set_style_radius(overlay, 12, 0);
    lv_obj_set_style_border_color(overlay, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_border_width(overlay, 2, 0);
    lv_obj_set_style_pad_all(overlay, 0, 0);

    char buf[48];
    snprintf(buf, sizeof(buf), "You Win!\n%d moves", moves_);
    lv_obj_t* lbl = lv_label_create(overlay);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
}

lv_obj_t* MemoryMatch::createScreen() {
    s_self = this;
    screen_ = ui_create_screen();
    ui_create_back_btn(screen_);

    lbl_moves_ = lv_label_create(screen_);
    lv_label_set_text(lbl_moves_, "Moves: 0");
    lv_obj_set_style_text_color(lbl_moves_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_moves_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_moves_, LV_ALIGN_TOP_MID, -30, 10);

    lbl_pairs_ = lv_label_create(screen_);
    lv_label_set_text(lbl_pairs_, "Pairs: 0/6");
    lv_obj_set_style_text_color(lbl_pairs_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_pairs_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_pairs_, LV_ALIGN_TOP_RIGHT, -10, 10);

    shuffle();

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

        lv_obj_t* btn = lv_btn_create(screen_);
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

    return screen_;
}

void MemoryMatch::update() {
    if (checking_ && millis() - check_time_ > 800) {
        check_match();
    }
}

void MemoryMatch::destroy() {
    s_self = nullptr;
    screen_ = nullptr;
    lbl_moves_ = nullptr;
    lbl_pairs_ = nullptr;
    for (int i = 0; i < NUM_CARDS; i++) {
        cards_[i] = nullptr;
        card_labels_[i] = nullptr;
    }
}
