#include "sudoku.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include <stdlib.h>
#include <string.h>

// ── Grid geometry ──
// 320x240 landscape. Grid on left, number pad on right.
static const int CELL   = 24;
static const int GRID_X = 4;
static const int GRID_Y = 4;
static const int GRID_PX = CELL * 9;  // 216

// ── Puzzle generation (no recursion — safe for ESP32 stack) ──

static void swap_rows(uint8_t grid[9][9], int a, int b) {
    uint8_t tmp[9];
    memcpy(tmp, grid[a], 9);
    memcpy(grid[a], grid[b], 9);
    memcpy(grid[b], tmp, 9);
}

static void swap_cols(uint8_t grid[9][9], int a, int b) {
    for (int r = 0; r < 9; r++) {
        uint8_t t = grid[r][a]; grid[r][a] = grid[r][b]; grid[r][b] = t;
    }
}

static void swap_bands(uint8_t grid[9][9], int a, int b) {
    for (int i = 0; i < 3; i++) swap_rows(grid, a * 3 + i, b * 3 + i);
}

static void swap_stacks(uint8_t grid[9][9], int a, int b) {
    for (int i = 0; i < 3; i++) swap_cols(grid, a * 3 + i, b * 3 + i);
}

void Sudoku::generate() {
    static const uint8_t base[9][9] = {
        {5,3,4,6,7,8,9,1,2},{6,7,2,1,9,5,3,4,8},{1,9,8,3,4,2,5,6,7},
        {8,5,9,7,6,1,4,2,3},{4,2,6,8,5,3,7,9,1},{7,1,3,9,2,4,8,5,6},
        {9,6,1,5,3,7,2,8,4},{2,8,7,4,1,9,6,3,5},{3,4,5,2,8,6,1,7,9}
    };
    memcpy(solution_, base, sizeof(solution_));

    for (int band = 0; band < 3; band++) {
        int a = esp_random() % 3, b = esp_random() % 3;
        if (a != b) swap_rows(solution_, band * 3 + a, band * 3 + b);
        a = esp_random() % 3; b = esp_random() % 3;
        if (a != b) swap_rows(solution_, band * 3 + a, band * 3 + b);
    }
    for (int stack = 0; stack < 3; stack++) {
        int a = esp_random() % 3, b = esp_random() % 3;
        if (a != b) swap_cols(solution_, stack * 3 + a, stack * 3 + b);
        a = esp_random() % 3; b = esp_random() % 3;
        if (a != b) swap_cols(solution_, stack * 3 + a, stack * 3 + b);
    }
    int a = esp_random() % 3, b = esp_random() % 3;
    if (a != b) swap_bands(solution_, a, b);
    a = esp_random() % 3; b = esp_random() % 3;
    if (a != b) swap_stacks(solution_, a, b);

    uint8_t perm[10] = {0,1,2,3,4,5,6,7,8,9};
    for (int i = 9; i > 1; i--) {
        int j = 1 + esp_random() % i;
        uint8_t t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            solution_[r][c] = perm[solution_[r][c]];

    memcpy(puzzle_, solution_, sizeof(puzzle_));
    remove_cells(45);

    memcpy(board_, puzzle_, sizeof(board_));
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            given_[r][c] = (puzzle_[r][c] != 0);

    memset(checked_, 0, sizeof(checked_));
    sel_r_ = -1; sel_c_ = -1;
    solved_ = false;
}

void Sudoku::remove_cells(int count) {
    int removed = 0;
    int attempts = 0;
    while (removed < count && attempts < 200) {
        int r = esp_random() % 9, c = esp_random() % 9;
        if (puzzle_[r][c] == 0) { attempts++; continue; }
        puzzle_[r][c] = 0;
        removed++;
        attempts = 0;
    }
}

// ── UI callbacks ──

void Sudoku::grid_cb(lv_event_t* e) {
    Sudoku* self = (Sudoku*)lv_event_get_user_data(e);
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    // Convert screen coords to grid row/col
    int c = (p.x - GRID_X) / CELL;
    int r = (p.y - GRID_Y) / CELL;
    if (r >= 0 && r < 9 && c >= 0 && c < 9) {
        self->select_cell(r, c);
    }
}

// Custom draw event: draw grid lines on the grid_area_ object
void Sudoku::grid_draw_cb(lv_event_t* e) {
    lv_draw_ctx_t* draw_ctx = lv_event_get_draw_ctx(e);
    lv_obj_t* obj = lv_event_get_target(e);
    lv_area_t obj_area;
    lv_obj_get_coords(obj, &obj_area);

    lv_draw_line_dsc_t line_dsc;
    lv_draw_line_dsc_init(&line_dsc);

    for (int i = 0; i <= 9; i++) {
        bool thick = (i % 3 == 0);
        line_dsc.width = thick ? 2 : 1;
        line_dsc.color = thick ? lv_color_hex(0x6688aa) : lv_color_hex(0x334466);

        // Vertical line
        lv_point_t pv[2] = {
            {(lv_coord_t)(obj_area.x1 + i * CELL), obj_area.y1},
            {(lv_coord_t)(obj_area.x1 + i * CELL), (lv_coord_t)(obj_area.y1 + 9 * CELL)}
        };
        lv_draw_line(draw_ctx, &line_dsc, &pv[0], &pv[1]);

        // Horizontal line
        lv_point_t ph[2] = {
            {obj_area.x1, (lv_coord_t)(obj_area.y1 + i * CELL)},
            {(lv_coord_t)(obj_area.x1 + 9 * CELL), (lv_coord_t)(obj_area.y1 + i * CELL)}
        };
        lv_draw_line(draw_ctx, &line_dsc, &ph[0], &ph[1]);
    }
}

// Button matrix map: "1","2","3","\n","4","5","6","\n","7","8","9","\n","Clr"
static const char* numpad_map[] = {
    "1", "2", "3", "\n",
    "4", "5", "6", "\n",
    "7", "8", "9", "\n",
    "Clear", ""
};

void Sudoku::numpad_cb(lv_event_t* e) {
    Sudoku* self = (Sudoku*)lv_event_get_user_data(e);
    lv_obj_t* obj = lv_event_get_target(e);
    uint32_t id = lv_btnmatrix_get_selected_btn(obj);
    if (id == LV_BTNMATRIX_BTN_NONE) return;
    const char* txt = lv_btnmatrix_get_btn_text(obj, id);
    if (!txt) return;
    if (txt[0] == 'C') {
        self->place_number(0);  // Clear
    } else {
        self->place_number(txt[0] - '0');
    }
}

void Sudoku::check_cb(lv_event_t* e) {
    Sudoku* self = (Sudoku*)lv_event_get_user_data(e);
    int r = self->sel_r_, c = self->sel_c_;
    if (r < 0 || c < 0 || self->given_[r][c] || self->solved_) return;
    uint8_t v = self->board_[r][c];
    if (v == 0) return;
    self->checked_[r][c] = (v == self->solution_[r][c]) ? 1 : -1;
    self->draw_board();
}

void Sudoku::new_game_cb(lv_event_t* e) {
    Sudoku* self = (Sudoku*)lv_event_get_user_data(e);
    self->generate();
    if (self->overlay_) { lv_obj_del(self->overlay_); self->overlay_ = nullptr; }
    self->draw_board();
}

// ── Screen creation ──

static void back_cb(lv_event_t*) { screen_manager_back_to_menu(); }

lv_obj_t* Sudoku::createScreen() {
    screen_ = ui_create_screen();

    generate();

    // Single grid background area — handles clicks and custom line drawing
    grid_area_ = lv_obj_create(screen_);
    lv_obj_set_size(grid_area_, GRID_PX, CELL * 9);
    lv_obj_set_pos(grid_area_, GRID_X, GRID_Y);
    lv_obj_set_style_bg_color(grid_area_, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(grid_area_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(grid_area_, 0, 0);
    lv_obj_set_style_border_width(grid_area_, 0, 0);
    lv_obj_set_style_pad_all(grid_area_, 0, 0);
    lv_obj_set_scrollbar_mode(grid_area_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(grid_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(grid_area_, grid_cb, LV_EVENT_CLICKED, this);
    lv_obj_add_event_cb(grid_area_, grid_draw_cb, LV_EVENT_DRAW_POST, this);

    // 81 labels placed directly on grid_area_ (no container per cell)
    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            lv_obj_t* lbl = lv_label_create(grid_area_);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_size(lbl, CELL, CELL);
            lv_obj_set_pos(lbl, c * CELL, r * CELL);
            lv_obj_set_style_pad_top(lbl, 4, 0);
            lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
            cell_lbls_[r][c] = lbl;
        }
    }

    // Right panel: Back button, numpad, check, status, new game
    int pad_x = GRID_X + GRID_PX + 8;
    int pad_w = 96;
    int y = GRID_Y;

    // Back button (consistent with other games)
    {
        lv_obj_t* btn = lv_btn_create(screen_);
        lv_obj_set_size(btn, 60, 30);
        lv_obj_set_pos(btn, pad_x, y);
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    }
    y += 34;

    // Number pad
    numpad_ = lv_btnmatrix_create(screen_);
    lv_btnmatrix_set_map(numpad_, numpad_map);
    lv_obj_set_size(numpad_, pad_w, 130);
    lv_obj_set_pos(numpad_, pad_x, y);
    lv_obj_set_style_bg_color(numpad_, UI_COLOR_BG, 0);
    lv_obj_set_style_border_width(numpad_, 0, 0);
    lv_obj_set_style_pad_all(numpad_, 2, 0);
    lv_obj_set_style_pad_gap(numpad_, 4, 0);
    lv_obj_set_style_bg_color(numpad_, UI_COLOR_PRIMARY, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(numpad_, UI_COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(numpad_, UI_COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(numpad_, &lv_font_montserrat_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(numpad_, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(numpad_, numpad_cb, LV_EVENT_VALUE_CHANGED, this);
    y += 134;

    // Check button
    {
        lv_obj_t* btn = ui_create_btn(screen_, "Check", pad_w, 26);
        lv_obj_set_pos(btn, pad_x, y);
        lv_obj_add_event_cb(btn, check_cb, LV_EVENT_CLICKED, this);
    }
    y += 30;

    // Status label
    lbl_status_ = lv_label_create(screen_);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_status_, pad_x, y);
    lv_label_set_text(lbl_status_, "");

    // New Game button (bottom)
    {
        lv_obj_t* btn = ui_create_btn(screen_, "New", pad_w, 26);
        lv_obj_set_pos(btn, pad_x, 240 - 32);
        lv_obj_add_event_cb(btn, new_game_cb, LV_EVENT_CLICKED, this);
    }

    draw_board();
    return screen_;
}

void Sudoku::update() {}

void Sudoku::destroy() {
    screen_ = nullptr;
    grid_area_ = nullptr;
    overlay_ = nullptr;
    numpad_ = nullptr;
    lbl_status_ = nullptr;
    memset(cell_lbls_, 0, sizeof(cell_lbls_));
}

void Sudoku::draw_board() {
    for (int r = 0; r < 9; r++) {
        for (int c = 0; c < 9; c++) {
            lv_obj_t* lbl = cell_lbls_[r][c];
            uint8_t v = board_[r][c];

            if (v == 0) {
                lv_label_set_text(lbl, "");
            } else {
                char txt[2] = { (char)('0' + v), 0 };
                lv_label_set_text(lbl, txt);
            }

            // Text color: given=white, unchecked player=cyan, checked correct=blue, checked wrong=red
            if (given_[r][c]) {
                lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
            } else if (checked_[r][c] == 1) {
                lv_obj_set_style_text_color(lbl, UI_COLOR_SUCCESS, 0);   // blue/green = correct
            } else if (checked_[r][c] == -1) {
                lv_obj_set_style_text_color(lbl, UI_COLOR_ACCENT, 0);    // red = wrong
            } else {
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x88bbdd), 0);  // neutral cyan
            }

            // Cell background via label bg — selection highlight
            bool sel = (r == sel_r_ && c == sel_c_);
            bool related = sel_r_ >= 0 && (r == sel_r_ || c == sel_c_ ||
                          (r/3 == sel_r_/3 && c/3 == sel_c_/3));
            bool dark_box = ((r/3 + c/3) % 2 == 0);

            if (sel) {
                lv_obj_set_style_bg_color(lbl, lv_color_hex(0x2a4070), 0);
                lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
            } else if (related) {
                lv_obj_set_style_bg_color(lbl, lv_color_hex(0x1a3050), 0);
                lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
            } else if (!dark_box) {
                lv_obj_set_style_bg_color(lbl, lv_color_hex(0x1e2a45), 0);
                lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_bg_opa(lbl, LV_OPA_TRANSP, 0);
            }
        }
    }
}

void Sudoku::select_cell(int r, int c) {
    if (solved_) return;
    sel_r_ = r;
    sel_c_ = c;
    if (given_[r][c]) {
        lv_label_set_text(lbl_status_, "Fixed cell");
    } else {
        lv_label_set_text(lbl_status_, "");
    }
    draw_board();
}

void Sudoku::place_number(int n) {
    if (solved_ || sel_r_ < 0 || sel_c_ < 0) return;
    if (given_[sel_r_][sel_c_]) return;

    board_[sel_r_][sel_c_] = (uint8_t)n;
    checked_[sel_r_][sel_c_] = 0;  // reset check state on new input
    draw_board();

    if (n != 0 && check_solved()) {
        solved_ = true;
        show_win();
    }
}

bool Sudoku::check_solved() {
    for (int r = 0; r < 9; r++)
        for (int c = 0; c < 9; c++)
            if (board_[r][c] != solution_[r][c]) return false;
    return true;
}

void Sudoku::show_win() {
    overlay_ = lv_obj_create(screen_);
    lv_obj_set_size(overlay_, 200, 80);
    lv_obj_center(overlay_);
    lv_obj_set_style_bg_color(overlay_, UI_COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_90, 0);
    lv_obj_set_style_radius(overlay_, 12, 0);
    lv_obj_set_style_border_color(overlay_, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_border_width(overlay_, 2, 0);

    lv_obj_t* lbl = lv_label_create(overlay_);
    lv_label_set_text(lbl, "Puzzle Solved!");
    lv_obj_set_style_text_color(lbl, UI_COLOR_SUCCESS, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_t* btn = ui_create_btn(overlay_, "New Game", 120, 30);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_add_event_cb(btn, new_game_cb, LV_EVENT_CLICKED, this);
}
