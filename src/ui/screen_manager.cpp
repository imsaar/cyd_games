#include "screen_manager.h"
#include "screen_menu.h"
#include "screen_settings.h"
#include "../games/snake/snake.h"
#include "../games/tictactoe/tictactoe.h"
#include "../games/memory_match/memory_match.h"
#include "../games/pong/pong.h"

static ScreenDef screens[SCREEN_COUNT];
static ScreenID  current_screen = SCREEN_MENU;

// Static game instances (avoid heap fragmentation)
static Snake       snake_game;
static TicTacToe   ttt_game;
static MemoryMatch memory_game;
static Pong        pong_game;

void screen_manager_init() {
    screens[SCREEN_MENU] = {
        "Menu", screen_menu_create, nullptr, nullptr, false, 0
    };
    screens[SCREEN_SETTINGS] = {
        "Settings", screen_settings_create, screen_settings_update, nullptr, false, 0
    };
    screens[SCREEN_SNAKE] = {
        snake_game.name(),
        []() -> lv_obj_t* { return snake_game.createScreen(); },
        []() { snake_game.update(); },
        []() { snake_game.destroy(); },
        true, snake_game.maxPlayers()
    };
    screens[SCREEN_TICTACTOE] = {
        ttt_game.name(),
        []() -> lv_obj_t* { return ttt_game.createScreen(); },
        []() { ttt_game.update(); },
        []() { ttt_game.destroy(); },
        true, ttt_game.maxPlayers()
    };
    screens[SCREEN_MEMORY] = {
        memory_game.name(),
        []() -> lv_obj_t* { return memory_game.createScreen(); },
        []() { memory_game.update(); },
        []() { memory_game.destroy(); },
        true, memory_game.maxPlayers()
    };
    screens[SCREEN_PONG] = {
        pong_game.name(),
        []() -> lv_obj_t* { return pong_game.createScreen(); },
        []() { pong_game.update(); },
        []() { pong_game.destroy(); },
        true, pong_game.maxPlayers()
    };
}

void screen_manager_switch(ScreenID id) {
    if (screens[current_screen].destroy) {
        screens[current_screen].destroy();
    }

    lv_obj_t* scr = screens[id].create();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, true);
    current_screen = id;
}

void screen_manager_update() {
    if (screens[current_screen].update) {
        screens[current_screen].update();
    }
}

ScreenID screen_manager_current() {
    return current_screen;
}

void screen_manager_back_to_menu() {
    screen_manager_switch(SCREEN_MENU);
}
