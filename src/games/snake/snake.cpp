#include "snake.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "config.h"
#include <Arduino.h>

void Snake::reset() {
    snake_len_ = 3;
    snake_[0] = {8, 5};
    snake_[1] = {7, 5};
    snake_[2] = {6, 5};
    dir_ = RIGHT;
    next_dir_ = RIGHT;
    score_ = 0;
    game_over_ = false;
    step_interval_ = 200;
    last_step_ = millis();
    spawn_food();
}

void Snake::spawn_food() {
    bool valid;
    do {
        food_.x = random(0, GRID_W);
        food_.y = random(0, GRID_H);
        valid = true;
        for (int i = 0; i < snake_len_; i++) {
            if (snake_[i].x == food_.x && snake_[i].y == food_.y) {
                valid = false;
                break;
            }
        }
    } while (!valid);
}

void Snake::step() {
    if (game_over_) return;

    dir_ = next_dir_;

    Pos head = snake_[0];
    switch (dir_) {
        case UP:    head.y--; break;
        case DOWN:  head.y++; break;
        case LEFT:  head.x--; break;
        case RIGHT: head.x++; break;
    }

    if (head.x < 0 || head.x >= GRID_W || head.y < 0 || head.y >= GRID_H) {
        game_over_ = true;
        show_game_over();
        return;
    }

    for (int i = 0; i < snake_len_; i++) {
        if (snake_[i].x == head.x && snake_[i].y == head.y) {
            game_over_ = true;
            show_game_over();
            return;
        }
    }

    bool ate = (head.x == food_.x && head.y == food_.y);

    if (!ate) {
        for (int i = snake_len_ - 1; i > 0; i--) {
            snake_[i] = snake_[i - 1];
        }
    } else {
        for (int i = snake_len_; i > 0; i--) {
            snake_[i] = snake_[i - 1];
        }
        snake_len_++;
        score_ += 10;
        if (step_interval_ > 80) step_interval_ -= 5;
        spawn_food();
    }
    snake_[0] = head;
}

void Snake::draw() {
    if (!game_area_) return;

    // Remove old body objects
    for (int i = 0; i < body_obj_count_; i++) {
        if (body_objs_[i]) lv_obj_del(body_objs_[i]);
    }
    body_obj_count_ = 0;

    // Draw snake body
    for (int i = 0; i < snake_len_; i++) {
        lv_obj_t* seg = lv_obj_create(game_area_);
        lv_obj_remove_style_all(seg);
        lv_obj_set_size(seg, TILE - 2, TILE - 2);
        lv_obj_set_pos(seg, snake_[i].x * TILE + 1, snake_[i].y * TILE + 1);
        lv_obj_set_style_bg_color(seg,
            (i == 0) ? UI_COLOR_SUCCESS : lv_color_hex(0x3d9970), 0);
        lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(seg, (i == 0) ? 4 : 2, 0);
        lv_obj_clear_flag(seg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        body_objs_[body_obj_count_++] = seg;
    }

    // Update food position
    if (food_obj_) {
        lv_obj_set_pos(food_obj_, food_.x * TILE + 1, food_.y * TILE + 1);
    }

    // Update score
    if (lbl_score_) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Score: %d", score_);
        lv_label_set_text(lbl_score_, buf);
    }
}

void Snake::show_game_over() {
    if (!screen_) return;

    overlay_ = lv_obj_create(screen_);
    lv_obj_set_size(overlay_, 200, 120);
    lv_obj_center(overlay_);
    lv_obj_set_style_bg_color(overlay_, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(overlay_, LV_OPA_90, 0);
    lv_obj_set_style_radius(overlay_, 12, 0);
    lv_obj_set_style_border_color(overlay_, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_border_width(overlay_, 2, 0);
    lv_obj_set_style_pad_all(overlay_, 0, 0);

    char buf[48];
    snprintf(buf, sizeof(buf), "Game Over!\nScore: %d", score_);
    lv_obj_t* lbl = lv_label_create(overlay_);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 15);

    lv_obj_t* btn = ui_create_btn(overlay_, "Play Again", 120, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -15);
    lv_obj_add_event_cb(btn, restart_cb, LV_EVENT_CLICKED, this);
}

// Tap-based direction control:
// Tap left half of game area = turn left (relative to screen)
// Tap right half = turn right
// Tap top half = turn up
// Tap bottom half = turn down
// Uses the dominant axis (whichever distance from center is greater)
void Snake::touch_cb(lv_event_t* e) {
    Snake* self = (Snake*)lv_event_get_user_data(e);
    if (self->game_over_) return;

    lv_indev_t* indev = lv_indev_get_act();
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    // Get tap position relative to game area center
    int center_x = SCREEN_WIDTH / 2;
    int center_y = TOP_BAR + (GRID_H * TILE) / 2;
    int dx = p.x - center_x;
    int dy = p.y - center_y;

    // Use dominant axis to determine direction
    if (abs(dx) > abs(dy)) {
        // Horizontal tap
        if (dx > 0 && self->dir_ != LEFT)  self->next_dir_ = RIGHT;
        if (dx < 0 && self->dir_ != RIGHT) self->next_dir_ = LEFT;
    } else {
        // Vertical tap
        if (dy > 0 && self->dir_ != UP)   self->next_dir_ = DOWN;
        if (dy < 0 && self->dir_ != DOWN) self->next_dir_ = UP;
    }
}

void Snake::restart_cb(lv_event_t* e) {
    Snake* self = (Snake*)lv_event_get_user_data(e);
    if (self->overlay_) {
        lv_obj_del(self->overlay_);
        self->overlay_ = nullptr;
    }
    self->reset();
    self->draw();
}

lv_obj_t* Snake::createScreen() {
    screen_ = ui_create_screen();

    // Top bar: back button + score
    ui_create_back_btn(screen_);

    lbl_score_ = lv_label_create(screen_);
    lv_label_set_text(lbl_score_, "Score: 0");
    lv_obj_set_style_text_color(lbl_score_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_score_, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_score_, LV_ALIGN_TOP_RIGHT, -10, 10);

    // Game area below the top bar
    game_area_ = lv_obj_create(screen_);
    lv_obj_remove_style_all(game_area_);
    lv_obj_set_size(game_area_, GRID_W * TILE, GRID_H * TILE);
    lv_obj_set_pos(game_area_, (SCREEN_WIDTH - GRID_W * TILE) / 2, TOP_BAR);
    lv_obj_set_style_bg_color(game_area_, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(game_area_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(game_area_, LV_OBJ_FLAG_SCROLLABLE);

    // Food object
    food_obj_ = lv_obj_create(game_area_);
    lv_obj_remove_style_all(food_obj_);
    lv_obj_set_size(food_obj_, TILE - 2, TILE - 2);
    lv_obj_set_style_bg_color(food_obj_, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(food_obj_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(food_obj_, 4, 0);
    lv_obj_clear_flag(food_obj_, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Tap anywhere on screen to change direction
    lv_obj_add_event_cb(screen_, touch_cb, LV_EVENT_CLICKED, this);

    reset();
    draw();
    return screen_;
}

void Snake::update() {
    if (game_over_) return;

    uint32_t now = millis();
    if (now - last_step_ >= step_interval_) {
        last_step_ = now;
        step();
        draw();
    }
}

void Snake::destroy() {
    body_obj_count_ = 0;
    screen_ = nullptr;
    game_area_ = nullptr;
    lbl_score_ = nullptr;
    overlay_ = nullptr;
    food_obj_ = nullptr;
}
