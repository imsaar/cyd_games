#include "screen_solo_menu.h"
#include "screen_manager.h"
#include "ui_common.h"

struct SoloMenuItem {
    const char* label;
    ScreenID    screen;
};

static const SoloMenuItem items[] = {
    { LV_SYMBOL_LOOP " Anagram",      SCREEN_ANAGRAM },
    { LV_SYMBOL_BELL " Whack-a-Mole", SCREEN_WHACK_MOLE },
    { LV_SYMBOL_NEW_LINE " Sudoku",   SCREEN_SUDOKU },
};
static const int NUM_ITEMS = sizeof(items) / sizeof(items[0]);

static void solo_menu_btn_cb(lv_event_t* e) {
    ScreenID id = (ScreenID)(intptr_t)lv_event_get_user_data(e);
    screen_manager_switch(id);
}

lv_obj_t* screen_solo_menu_create() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    lv_obj_t* title = ui_create_title(scr, "Solo Games");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    static const lv_coord_t btn_w = 180;
    static const lv_coord_t btn_h = 36;
    static const lv_coord_t gap_y = 14;
    lv_coord_t start_y = 60;

    for (int i = 0; i < NUM_ITEMS; i++) {
        lv_obj_t* btn = ui_create_btn(scr, items[i].label, btn_w, btn_h);
        lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, start_y + i * (btn_h + gap_y));
        lv_obj_add_event_cb(btn, solo_menu_btn_cb, LV_EVENT_CLICKED,
                            (void*)(intptr_t)items[i].screen);
    }

    return scr;
}
