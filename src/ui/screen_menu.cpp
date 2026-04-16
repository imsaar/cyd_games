#include "screen_menu.h"
#include "screen_manager.h"
#include "ui_common.h"
#include "../net/ntp_time.h"

struct MenuItem {
    const char* label;
    ScreenID    screen;
};

static const MenuItem items[] = {
    { LV_SYMBOL_PLAY " Battleship",     SCREEN_BATTLESHIP },
    { LV_SYMBOL_SHUFFLE " Tic-Tac-Toe", SCREEN_TICTACTOE },
    { LV_SYMBOL_REFRESH " Pong",        SCREEN_PONG },
    { LV_SYMBOL_CHARGE " Connect 4",    SCREEN_CONNECT4 },
    { LV_SYMBOL_IMAGE " Memory",        SCREEN_MEMORY },
    { LV_SYMBOL_EYE_OPEN " Checkers",   SCREEN_CHECKERS },
    { LV_SYMBOL_EDIT " Chess",          SCREEN_CHESS },
    { LV_SYMBOL_LOOP " Anagram",        SCREEN_ANAGRAM },
    { LV_SYMBOL_LIST " Dots&Boxes",     SCREEN_DOTS_BOXES },
    { LV_SYMBOL_BELL " Whack-a-Mole",   SCREEN_WHACK_MOLE },
    { LV_SYMBOL_DOWNLOAD " Cup Pong",   SCREEN_CUP_PONG },
    { LV_SYMBOL_NEW_LINE " Sudoku",    SCREEN_SUDOKU },
    { LV_SYMBOL_AUDIO " Pictionary", SCREEN_PICTIONARY },
    { LV_SYMBOL_SETTINGS " Settings",   SCREEN_SETTINGS },
};
static const int NUM_ITEMS = sizeof(items) / sizeof(items[0]);

static lv_obj_t* lbl_time = nullptr;
static lv_timer_t* time_timer = nullptr;

static void update_time_label(lv_timer_t*) {
    if (lbl_time && ntp_valid()) {
        lv_label_set_text(lbl_time, ntp_get_display_str());
    }
}

static void menu_btn_cb(lv_event_t* e) {
    ScreenID id = (ScreenID)(intptr_t)lv_event_get_user_data(e);
    screen_manager_switch(id);
}

lv_obj_t* screen_menu_create() {
    lv_obj_t* scr = ui_create_screen();

    lv_obj_t* title = ui_create_title(scr, "CYD Arcade");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 6, 2);

    // Date/time label (top-right, Pacific timezone)
    lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_time, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_RIGHT, -6, 6);
    if (ntp_valid()) {
        lv_label_set_text(lbl_time, ntp_get_display_str());
    } else {
        lv_label_set_text(lbl_time, "");
    }
    // Update every 30 seconds; delete timer when screen is destroyed
    time_timer = lv_timer_create(update_time_label, 30000, NULL);
    lv_obj_add_event_cb(scr, [](lv_event_t*) {
        if (time_timer) { lv_timer_del(time_timer); time_timer = nullptr; }
        lbl_time = nullptr;
    }, LV_EVENT_DELETE, NULL);

    // 2 columns x 7 rows
    static const lv_coord_t col_w = 140;
    static const lv_coord_t row_h = 28;
    static const lv_coord_t gap_x = 8;
    static const lv_coord_t gap_y = 2;
    lv_coord_t start_x = (320 - 2 * col_w - gap_x) / 2;
    lv_coord_t start_y = 22;

    for (int i = 0; i < NUM_ITEMS; i++) {
        int col = i % 2;
        int row = i / 2;
        lv_coord_t x = start_x + col * (col_w + gap_x);
        lv_coord_t y = start_y + row * (row_h + gap_y);

        lv_obj_t* btn = ui_create_btn(scr, items[i].label, col_w, row_h);
        lv_obj_set_pos(btn, x, y);
        lv_obj_add_event_cb(btn, menu_btn_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)items[i].screen);
    }

    return scr;
}
