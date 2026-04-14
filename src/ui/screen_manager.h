#pragma once
#include <lvgl.h>

enum ScreenID {
    SCREEN_MENU,
    SCREEN_SETTINGS,
    SCREEN_SNAKE,
    SCREEN_TICTACTOE,
    SCREEN_MEMORY,
    SCREEN_PONG,
    SCREEN_CONNECT4,
    SCREEN_DOTS_BOXES,
    SCREEN_CHECKERS,
    SCREEN_CHESS,
    SCREEN_ANAGRAM,
    SCREEN_WHACK_MOLE,
    SCREEN_COUNT
};

struct ScreenDef {
    const char*  name;
    lv_obj_t*  (*create)();
    void       (*update)();
    void       (*destroy)();
    bool         is_game;
    uint8_t      max_players;
};

void     screen_manager_init();
void     screen_manager_switch(ScreenID id);
void     screen_manager_update();
ScreenID screen_manager_current();
void     screen_manager_back_to_menu();
