#pragma once
#include "../game_base.h"

class Sudoku : public GameBase {
public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;

    const char* name() const override { return "Sudoku"; }
    uint8_t maxPlayers() const override { return 1; }

private:
    static const int SZ = 9;

    lv_obj_t* screen_ = nullptr;
    lv_obj_t* grid_area_ = nullptr;      // single clickable grid background
    lv_obj_t* cell_lbls_[SZ][SZ] = {};   // 81 lightweight labels (no containers)
    lv_obj_t* numpad_ = nullptr;          // single btnmatrix for 1-9 + Clear
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* overlay_ = nullptr;

    uint8_t solution_[SZ][SZ] = {};
    uint8_t puzzle_[SZ][SZ] = {};
    uint8_t board_[SZ][SZ] = {};
    bool given_[SZ][SZ] = {};
    int8_t checked_[SZ][SZ] = {};  // 0=unchecked, 1=correct, -1=wrong

    int sel_r_ = -1;
    int sel_c_ = -1;
    bool solved_ = false;

    void generate();
    void remove_cells(int count);
    void draw_board();
    void select_cell(int r, int c);
    void place_number(int n);
    bool check_solved();
    void show_win();

    static void grid_cb(lv_event_t* e);
    static void numpad_cb(lv_event_t* e);
    static void check_cb(lv_event_t* e);
    static void new_game_cb(lv_event_t* e);
    static void grid_draw_cb(lv_event_t* e);
};
