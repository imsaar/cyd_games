#include "screen_manager.h"
#include "screen_menu.h"
#include "screen_settings.h"
#include "../games/battleship/battleship.h"
#include "../apps/clock_app/clock_app.h"
#include "../games/memory_match/memory_match.h"
#include "../games/pong/pong.h"
#include "../games/connect4/connect4.h"
#include "../games/dots_boxes/dots_boxes.h"
#include "../games/checkers/checkers.h"
#include "../games/chess/chess.h"
#include "../games/anagram/anagram.h"
#include "../games/whack_mole/whack_mole.h"
#include "../games/cup_pong/cup_pong.h"
#include "../games/sudoku/sudoku.h"
#include "../games/pictionary/pictionary.h"

static ScreenDef screens[SCREEN_COUNT];
static ScreenID  current_screen = SCREEN_MENU;

// Static game instances (avoid heap fragmentation)
static Battleship  battleship_game;
// Clock app uses free functions, not a class instance
static MemoryMatch memory_game;
static Pong        pong_game;
static Connect4    connect4_game;
static DotsBoxes   dots_boxes_game;
static Checkers    checkers_game;
static Chess       chess_game;
static Anagram     anagram_game;
static WhackMole   whack_mole_game;
static CupPong     cup_pong_game;
static Sudoku      sudoku_game;
static Pictionary  pictionary_game;

void screen_manager_init() {
    screens[SCREEN_MENU] = {
        "Menu", screen_menu_create, nullptr, nullptr, false, 0
    };
    screens[SCREEN_SETTINGS] = {
        "Settings", screen_settings_create, screen_settings_update, nullptr, false, 0
    };
    screens[SCREEN_BATTLESHIP] = {
        battleship_game.name(),
        []() -> lv_obj_t* { return battleship_game.createScreen(); },
        []() { battleship_game.update(); },
        []() { battleship_game.destroy(); },
        true, battleship_game.maxPlayers()
    };
    screens[SCREEN_CLOCK] = {
        "Clock", clock_app_create, clock_app_update, clock_app_destroy, false, 0
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
    screens[SCREEN_CONNECT4] = {
        connect4_game.name(),
        []() -> lv_obj_t* { return connect4_game.createScreen(); },
        []() { connect4_game.update(); },
        []() { connect4_game.destroy(); },
        true, connect4_game.maxPlayers()
    };
    screens[SCREEN_CHECKERS] = {
        checkers_game.name(),
        []() -> lv_obj_t* { return checkers_game.createScreen(); },
        []() { checkers_game.update(); },
        []() { checkers_game.destroy(); },
        true, checkers_game.maxPlayers()
    };
    screens[SCREEN_CHESS] = {
        chess_game.name(),
        []() -> lv_obj_t* { return chess_game.createScreen(); },
        []() { chess_game.update(); },
        []() { chess_game.destroy(); },
        true, chess_game.maxPlayers()
    };
    screens[SCREEN_ANAGRAM] = {
        anagram_game.name(),
        []() -> lv_obj_t* { return anagram_game.createScreen(); },
        []() { anagram_game.update(); },
        []() { anagram_game.destroy(); },
        true, anagram_game.maxPlayers()
    };
    screens[SCREEN_DOTS_BOXES] = {
        dots_boxes_game.name(),
        []() -> lv_obj_t* { return dots_boxes_game.createScreen(); },
        []() { dots_boxes_game.update(); },
        []() { dots_boxes_game.destroy(); },
        true, dots_boxes_game.maxPlayers()
    };
    screens[SCREEN_WHACK_MOLE] = {
        whack_mole_game.name(),
        []() -> lv_obj_t* { return whack_mole_game.createScreen(); },
        []() { whack_mole_game.update(); },
        []() { whack_mole_game.destroy(); },
        true, whack_mole_game.maxPlayers()
    };
    screens[SCREEN_CUP_PONG] = {
        cup_pong_game.name(),
        []() -> lv_obj_t* { return cup_pong_game.createScreen(); },
        []() { cup_pong_game.update(); },
        []() { cup_pong_game.destroy(); },
        true, cup_pong_game.maxPlayers()
    };
    screens[SCREEN_SUDOKU] = {
        sudoku_game.name(),
        []() -> lv_obj_t* { return sudoku_game.createScreen(); },
        []() { sudoku_game.update(); },
        []() { sudoku_game.destroy(); },
        true, sudoku_game.maxPlayers()
    };
    screens[SCREEN_PICTIONARY] = {
        pictionary_game.name(),
        []() -> lv_obj_t* { return pictionary_game.createScreen(); },
        []() { pictionary_game.update(); },
        []() { pictionary_game.destroy(); },
        true, pictionary_game.maxPlayers()
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
