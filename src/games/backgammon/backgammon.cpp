#include "backgammon.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../net/mp_shell.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>

static Backgammon* s_self = nullptr;
static const MpShellConfig kCfg = { "backgammon", "Backgammon", /*show_cpu_button=*/true, /*show_idle_peers=*/false };

// ── Board geometry (screen-fixed, matches other games' convention) ──
static const int BOARD_X = 4;
static const int BOARD_Y = 4;
static const int POINT_W = 18;
static const int BAR_W = 16;
static const int HALF_W = 6 * POINT_W;          // 108
static const int TRI_H = 84;
static const int MID_GAP = 28;
static const int BOARD_TOTAL_W = HALF_W * 2 + BAR_W;   // 232
static const int BOARD_TOTAL_H = TRI_H * 2 + MID_GAP;  // 196
static const int CHIP_D = 14;

// Maps an absolute screen tap to a board slot: 0-23 = point index,
// -1 = bar column, -3 = no hit.
static int screen_to_slot(int x, int y) {
    int rx = x - BOARD_X;
    int ry = y - BOARD_Y;
    if (rx < 0 || rx >= BOARD_TOTAL_W || ry < 0 || ry >= BOARD_TOTAL_H) return -3;
    bool bar_zone = (rx >= HALF_W && rx < HALF_W + BAR_W);

    if (ry < TRI_H) {
        if (bar_zone) return -1;
        int i = (rx < HALF_W) ? rx / POINT_W : (rx - BAR_W) / POINT_W;
        if (i < 0 || i > 11) return -3;
        return 12 + i;
    } else if (ry >= TRI_H + MID_GAP) {
        if (bar_zone) return -1;
        int i = (rx < HALF_W) ? rx / POINT_W : (rx - BAR_W) / POINT_W;
        if (i < 0 || i > 11) return -3;
        return 11 - i;
    }
    return bar_zone ? -1 : -3;
}

// ── Mode selection ──

void Backgammon::mode_cpu_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU;
    s_self->my_color_ = WHITE;
    s_self->cpu_pending_ = false;
    mp_shell_end(s_self->peer_ip_, false);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Backgammon::mode_local_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->cpu_pending_ = false;
    mp_shell_end(s_self->peer_ip_, false);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Backgammon::mode_online_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    lv_obj_t* scr = mp_shell_host_lobby(kCfg, on_host_ready, on_guest_ready, on_game_data, nullptr);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Backgammon::on_host_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->mode_ = MODE_NETWORK;
    s_self->my_color_ = WHITE;
    s_self->my_turn_ = true;
    s_self->peer_ip_ = peer.ip;
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Backgammon::on_guest_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->mode_ = MODE_NETWORK;
    s_self->my_color_ = BLACK;
    s_self->my_turn_ = false;
    s_self->peer_ip_ = peer.ip;
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Backgammon::on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != Backgammon::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

// ── Board rules ──

void Backgammon::reset_board() {
    for (int i = 0; i < 24; i++) points_[i] = 0;
    points_[23] = 2; points_[12] = 5; points_[7] = 3; points_[5] = 5;      // White
    points_[0] = -2; points_[11] = -5; points_[16] = -3; points_[18] = -5; // Black
    bar_white_ = bar_black_ = 0;
    off_white_ = off_black_ = 0;
    current_ = WHITE;
    rolled_ = false;
    dice_count_ = 0;
    for (int i = 0; i < 4; i++) dice_used_[i] = false;
    game_done_ = false;
    selected_from_ = -2;
    for (int i = 0; i < 24; i++) legal_dest_[i] = false;
    can_bear_off_ = false;
    net_reset_sync();
}

int Backgammon::checker_count_at(Player p, int idx) const {
    int v = points_[idx];
    if (p == WHITE) return v > 0 ? v : 0;
    return v < 0 ? -v : 0;
}

int Backgammon::opp_count_at(Player p, int idx) const {
    return checker_count_at(p == WHITE ? BLACK : WHITE, idx);
}

bool Backgammon::all_in_home(Player p) const {
    if (bar_count(p) > 0) return false;
    for (int idx = 0; idx < 24; idx++) {
        if (dist_of(p, idx) > 6 && checker_count_at(p, idx) > 0) return false;
    }
    return true;
}

bool Backgammon::has_checker_farther(Player p, int from_dist) const {
    for (int idx = 0; idx < 24; idx++) {
        int d = dist_of(p, idx);
        if (d >= 1 && d <= 6 && d > from_dist && checker_count_at(p, idx) > 0) return true;
    }
    return false;
}

bool Backgammon::compute_dest(Player p, int from, int die, int& to_idx, bool& is_bear_off) const {
    int from_dist = (from == -1) ? 25 : dist_of(p, from);
    int to_dist = from_dist - die;

    if (to_dist == 0) {
        if (!all_in_home(p)) return false;
        is_bear_off = true; to_idx = -1; return true;
    }
    if (to_dist < 0) {
        if (from == -1) return false; // can't overshoot entering from the bar
        if (!all_in_home(p)) return false;
        if (has_checker_farther(p, from_dist)) return false;
        is_bear_off = true; to_idx = -1; return true;
    }
    int idx = idx_of_dist(p, to_dist);
    if (opp_count_at(p, idx) >= 2) return false;
    is_bear_off = false; to_idx = idx; return true;
}

bool Backgammon::any_legal_move(Player p) const {
    bool must_bar = bar_count(p) > 0;
    for (int di = 0; di < dice_count_; di++) {
        if (dice_used_[di]) continue;
        int die = dice_[di];
        if (must_bar) {
            int to_idx; bool bo;
            if (compute_dest(p, -1, die, to_idx, bo)) return true;
            continue;
        }
        for (int from = 0; from < 24; from++) {
            if (checker_count_at(p, from) == 0) continue;
            int to_idx; bool bo;
            if (compute_dest(p, from, die, to_idx, bo)) return true;
        }
    }
    return false;
}

bool Backgammon::try_apply_move(int from, int die, bool send) {
    int to_idx; bool bear_off;
    Player p = current_;
    if (!compute_dest(p, from, die, to_idx, bear_off)) return false;

    int die_idx = -1;
    for (int i = 0; i < dice_count_; i++) {
        if (!dice_used_[i] && dice_[i] == die) { die_idx = i; break; }
    }
    if (die_idx < 0) return false;

    if (from == -1) { if (p == WHITE) bar_white_--; else bar_black_--; }
    else points_[from] -= (p == WHITE ? 1 : -1);

    if (bear_off) {
        if (p == WHITE) off_white_++; else off_black_++;
    } else {
        if (opp_count_at(p, to_idx) == 1) {
            if (p == WHITE) bar_black_++; else bar_white_++;
            points_[to_idx] = 0;
        }
        points_[to_idx] += (p == WHITE ? 1 : -1);
    }
    dice_used_[die_idx] = true;
    clear_selection();
    sound_move();

    if (send && mode_ == MODE_NETWORK) send_move(from, die);

    if ((p == WHITE && off_white_ == 15) || (p == BLACK && off_black_ == 15)) {
        game_done_ = true;
        static char result_buf[32];
        static bool result_is_win;
        if (mode_ == MODE_NETWORK) {
            result_is_win = (p == my_color_);
            snprintf(result_buf, sizeof(result_buf), "%s", result_is_win ? "You Win!" : "You Lose!");
        } else if (mode_ == MODE_CPU) {
            result_is_win = (p == WHITE);
            snprintf(result_buf, sizeof(result_buf), "%s", p == WHITE ? "You Win!" : "CPU Wins!");
        } else {
            result_is_win = true;
            snprintf(result_buf, sizeof(result_buf), "%s Wins!", p == WHITE ? "White" : "Black");
        }
        redraw_board();
        lv_timer_create([](lv_timer_t* t) {
            lv_timer_del(t);
            if (s_self) s_self->show_result(result_buf, result_is_win);
        }, 1200, NULL);
        return true;
    }

    end_turn_if_no_moves();
    redraw_board();
    update_status();
    return true;
}

// ── Turn flow ──

void Backgammon::apply_dice(int d1, int d2) {
    if (d1 == d2) {
        dice_count_ = 4;
        for (int i = 0; i < 4; i++) dice_[i] = d1;
    } else {
        dice_count_ = 2;
        dice_[0] = d1; dice_[1] = d2;
    }
    for (int i = 0; i < 4; i++) dice_used_[i] = (i >= dice_count_);
    rolled_ = true;
}

void Backgammon::roll_dice() {
    int d1 = random(1, 7);
    int d2 = random(1, 7);
    apply_dice(d1, d2);
    sound_move();
}

void Backgammon::send_roll() {
    net_mc_++;
    StaticJsonDocument<160> doc;
    doc["type"] = "move";
    doc["game"] = "backgammon";
    doc["a"] = "roll";
    doc["d1"] = dice_[0];
    doc["d2"] = (dice_count_ == 4) ? dice_[0] : dice_[1];
    doc["mc"] = net_mc_;
    serializeJson(doc, net_last_move_, sizeof(net_last_move_));
    discovery_send_game_data(peer_ip_, net_last_move_);
}

void Backgammon::send_move(int from, int die) {
    net_mc_++;
    StaticJsonDocument<160> doc;
    doc["type"] = "move";
    doc["game"] = "backgammon";
    doc["a"] = "mv";
    doc["from"] = from;
    doc["die"] = die;
    doc["mc"] = net_mc_;
    serializeJson(doc, net_last_move_, sizeof(net_last_move_));
    discovery_send_game_data(peer_ip_, net_last_move_);
}

void Backgammon::end_turn_if_no_moves() {
    bool dice_left = false;
    for (int i = 0; i < dice_count_; i++) if (!dice_used_[i]) dice_left = true;
    if (!dice_left || !any_legal_move(current_)) {
        uint32_t delay = dice_left ? 900 : 500;
        lv_timer_create([](lv_timer_t* t) {
            lv_timer_del(t);
            if (s_self && !s_self->game_done_) s_self->switch_turn();
        }, delay, NULL);
    }
}

void Backgammon::switch_turn() {
    current_ = (current_ == WHITE) ? BLACK : WHITE;
    rolled_ = false;
    dice_count_ = 0;
    for (int i = 0; i < 4; i++) dice_used_[i] = false;
    clear_selection();
    if (mode_ == MODE_NETWORK) my_turn_ = (current_ == my_color_);
    redraw_board();
    update_status();
}

void Backgammon::select_origin(int from) {
    selected_from_ = from;
    for (int i = 0; i < 24; i++) legal_dest_[i] = false;
    can_bear_off_ = false;
    for (int di = 0; di < dice_count_; di++) {
        if (dice_used_[di]) continue;
        int to_idx; bool bo;
        if (compute_dest(current_, from, dice_[di], to_idx, bo)) {
            if (bo) can_bear_off_ = true; else legal_dest_[to_idx] = true;
        }
    }
    if (btn_bear_off_) {
        if (can_bear_off_) lv_obj_clear_state(btn_bear_off_, LV_STATE_DISABLED);
        else lv_obj_add_state(btn_bear_off_, LV_STATE_DISABLED);
    }
    redraw_board();
    update_status();
}

void Backgammon::clear_selection() {
    selected_from_ = -2;
    for (int i = 0; i < 24; i++) legal_dest_[i] = false;
    can_bear_off_ = false;
    if (btn_bear_off_) lv_obj_add_state(btn_bear_off_, LV_STATE_DISABLED);
    redraw_board();
    update_status();
}

void Backgammon::redraw_board() {
    if (board_area_) lv_obj_invalidate(board_area_);
}

void Backgammon::update_status() {
    if (!lbl_status_) return;
    const char* txt;
    if (mode_ == MODE_CPU) {
        txt = (current_ == WHITE) ? (rolled_ ? "Your turn" : "Your turn - Roll") : "CPU thinking...";
    } else if (mode_ == MODE_LOCAL) {
        txt = (current_ == WHITE) ? "White's turn" : "Black's turn";
    } else {
        txt = my_turn_ ? (rolled_ ? "Your turn" : "Your turn - Roll") : "Waiting...";
    }
    bool interactive = (mode_ == MODE_LOCAL) ||
                        (mode_ == MODE_CPU && current_ == WHITE) ||
                        (mode_ == MODE_NETWORK && my_turn_);
    if (interactive && selected_from_ != -2) {
        if (can_bear_off_) {
            txt = "Tap piece again to bear off";
        } else if (selected_from_ >= 0 && dist_of(current_, selected_from_) <= 6 && !all_in_home(current_)) {
            // Looks bear-off-eligible (piece is in the home range) but isn't,
            // because not every checker is home yet — real backgammon rule,
            // surfaced explicitly so it doesn't read as an unresponsive tap.
            txt = "Bring all checkers home first";
        }
    }
    lv_label_set_text(lbl_status_, txt);

    if (lbl_dice_) {
        char buf[24] = {0};
        int p = 0;
        if (rolled_) {
            for (int i = 0; i < dice_count_; i++) {
                if (!dice_used_[i]) p += snprintf(buf + p, sizeof(buf) - p, "%d ", dice_[i]);
            }
        }
        lv_label_set_text(lbl_dice_, buf);
    }
    if (lbl_off_) {
        char buf[24];
        snprintf(buf, sizeof(buf), "Off W:%d B:%d", off_white_, off_black_);
        lv_label_set_text(lbl_off_, buf);
    }
    if (btn_roll_) {
        bool can_roll = !rolled_ && !game_done_ &&
            (mode_ == MODE_LOCAL ||
             (mode_ == MODE_CPU && current_ == WHITE) ||
             (mode_ == MODE_NETWORK && my_turn_));
        if (can_roll) lv_obj_clear_flag(btn_roll_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(btn_roll_, LV_OBJ_FLAG_HIDDEN);
    }
}

void Backgammon::show_result(const char* text, bool is_win) {
    if (!screen_) return;
    if (is_win) sound_win(); else sound_lose();
    lv_color_t color = is_win ? UI_COLOR_SUCCESS : UI_COLOR_ACCENT;
    lv_obj_t* overlay = lv_obj_create(screen_);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_size(overlay, 280, 140);
    lv_obj_center(overlay);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(0x0e0e1a), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(overlay, 16, 0);
    lv_obj_set_style_border_color(overlay, color, 0);
    lv_obj_set_style_border_width(overlay, 3, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* lbl = lv_label_create(overlay);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -15);

    lv_obj_t* again_btn = ui_create_btn(overlay, "Play Again", 120, 36);
    lv_obj_align(again_btn, LV_ALIGN_BOTTOM_MID, -65, -15);
    lv_obj_add_event_cb(again_btn, [](lv_event_t*) {
        screen_manager_switch(screen_manager_current());
    }, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn = ui_create_btn(overlay, "Menu", 90, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 70, -15);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        screen_manager_back_to_menu();
    }, LV_EVENT_CLICKED, NULL);
}

// ── CPU AI (greedy heuristic: bear off > hit > make a point > avoid blots) ──

int Backgammon::score_move(int to_idx, bool bear_off) const {
    if (bear_off) return 1000;
    int score = 0;
    if (opp_count_at(current_, to_idx) == 1) score += 500;
    int mine_after = checker_count_at(current_, to_idx) + 1;
    if (mine_after >= 2) score += 50;
    else score -= 20;
    return score;
}

bool Backgammon::cpu_pick_move(int& from_out, int& die_out) const {
    bool must_bar = bar_count(current_) > 0;
    int best_score = -1000000, best_from = -2, best_die = -1;

    for (int di = 0; di < dice_count_; di++) {
        if (dice_used_[di]) continue;
        int die = dice_[di];
        if (must_bar) {
            int to_idx; bool bo;
            if (compute_dest(current_, -1, die, to_idx, bo)) {
                int sc = score_move(bo ? -1 : to_idx, bo) + 200;
                if (sc > best_score) { best_score = sc; best_from = -1; best_die = die; }
            }
            continue;
        }
        for (int from = 0; from < 24; from++) {
            if (checker_count_at(current_, from) == 0) continue;
            int to_idx; bool bo;
            if (!compute_dest(current_, from, die, to_idx, bo)) continue;
            int sc = score_move(bo ? -1 : to_idx, bo);
            if (sc > best_score) { best_score = sc; best_from = from; best_die = die; }
        }
    }
    if (best_from == -2) return false;
    from_out = best_from; die_out = best_die;
    return true;
}

// ── Board UI ──

lv_obj_t* Backgammon::create_board() {
    lv_obj_t* scr = ui_create_screen();
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    screen_ = scr;

    board_area_ = lv_obj_create(scr);
    lv_obj_remove_style_all(board_area_);
    lv_obj_set_size(board_area_, BOARD_TOTAL_W, BOARD_TOTAL_H);
    lv_obj_set_pos(board_area_, BOARD_X, BOARD_Y);
    lv_obj_clear_flag(board_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(board_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board_area_, board_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(board_area_, board_draw_cb, LV_EVENT_DRAW_POST, NULL);

    int tb_x = BOARD_X + BOARD_TOTAL_W + 4;
    int y = 4;

    // Exit button — bottom-right corner of the screen, out of the way of
    // the board and the dice/roll/bear-off controls.
    {
        lv_obj_t* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 64, 26);
        lv_obj_align(btn, LV_ALIGN_BOTTOM_RIGHT, -6, -6);
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_CLOSE " Exit");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, [](lv_event_t*) { screen_manager_back_to_menu(); }, LV_EVENT_CLICKED, NULL);
    }

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_status_, tb_x, y);
    y += 20;

    lbl_dice_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_dice_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl_dice_, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(lbl_dice_, tb_x, y);
    y += 24;

    btn_roll_ = ui_create_btn(scr, "Roll", 72, 28);
    lv_obj_set_pos(btn_roll_, tb_x, y);
    lv_obj_add_event_cb(btn_roll_, roll_cb, LV_EVENT_CLICKED, NULL);
    y += 32;

    // Always visible (not hidden-until-legal) so the control itself is
    // discoverable — just disabled/dimmed until the selected piece can
    // actually bear off.
    btn_bear_off_ = ui_create_btn(scr, "Bear Off", 72, 28);
    lv_obj_set_pos(btn_bear_off_, tb_x, y);
    lv_obj_add_event_cb(btn_bear_off_, bear_off_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_set_style_bg_opa(btn_bear_off_, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_add_state(btn_bear_off_, LV_STATE_DISABLED);
    lv_obj_set_style_text_font(lv_obj_get_child(btn_bear_off_, 0), &lv_font_montserrat_12, 0);
    y += 32;

    lbl_off_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_off_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_off_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_off_, tb_x, y);

    reset_board();
    update_status();
    redraw_board();
    return scr;
}

void Backgammon::roll_cb(lv_event_t*) {
    if (!s_self || s_self->rolled_ || s_self->game_done_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && s_self->current_ != WHITE) return;
    s_self->roll_dice();
    if (s_self->mode_ == MODE_NETWORK) s_self->send_roll();
    s_self->end_turn_if_no_moves();
    s_self->redraw_board();
    s_self->update_status();
}

void Backgammon::bear_off_cb(lv_event_t*) {
    if (!s_self || s_self->selected_from_ == -2 || !s_self->can_bear_off_) return;
    if (s_self->game_done_ || !s_self->rolled_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && s_self->current_ != WHITE) return;
    Player p = s_self->current_;
    for (int di = 0; di < s_self->dice_count_; di++) {
        if (s_self->dice_used_[di]) continue;
        int to_idx; bool bo;
        if (s_self->compute_dest(p, s_self->selected_from_, s_self->dice_[di], to_idx, bo) && bo) {
            s_self->try_apply_move(s_self->selected_from_, s_self->dice_[di], true);
            return;
        }
    }
}

void Backgammon::board_click_cb(lv_event_t*) {
    if (!s_self || s_self->game_done_ || !s_self->rolled_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && s_self->current_ != WHITE) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    int slot = screen_to_slot(p.x, p.y);
    if (slot == -3) return;

    Backgammon* self = s_self;
    Player pl = self->current_;

    if (slot == -1) {
        if (self->bar_count(pl) == 0) return;
        if (self->selected_from_ == -1) { self->clear_selection(); return; }
        self->select_origin(-1);
        return;
    }

    if (self->selected_from_ != -2) {
        if (slot == self->selected_from_) {
            // Tapping the already-selected checker again bears it off if that's
            // its only legal move (there's no separate on-board square to tap
            // for "off the board", so this doubles as the confirm gesture).
            if (self->can_bear_off_) {
                for (int di = 0; di < self->dice_count_; di++) {
                    if (self->dice_used_[di]) continue;
                    int to_idx; bool bo;
                    if (self->compute_dest(pl, self->selected_from_, self->dice_[di], to_idx, bo) && bo) {
                        self->try_apply_move(self->selected_from_, self->dice_[di], true);
                        return;
                    }
                }
            }
            self->clear_selection();
            return;
        }
        if (self->legal_dest_[slot]) {
            for (int di = 0; di < self->dice_count_; di++) {
                if (self->dice_used_[di]) continue;
                int to_idx; bool bo;
                if (self->compute_dest(pl, self->selected_from_, self->dice_[di], to_idx, bo) &&
                    !bo && to_idx == slot) {
                    self->try_apply_move(self->selected_from_, self->dice_[di], true);
                    return;
                }
            }
            return;
        }
    }

    if (self->bar_count(pl) == 0 && self->checker_count_at(pl, slot) > 0) {
        self->select_origin(slot);
    } else {
        self->clear_selection();
    }
}

void Backgammon::board_draw_cb(lv_event_t* e) {
    if (!s_self) return;
    Backgammon* self = s_self;
    lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
    lv_obj_t* obj = lv_event_get_target(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int ox = area.x1, oy = area.y1;
    int by = oy + TRI_H + MID_GAP; // top of bottom row

    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = UI_COLOR_CARD;
    bg.bg_opa = LV_OPA_COVER;
    bg.radius = 4;
    lv_draw_rect(ctx, &bg, &area);

    lv_draw_rect_dsc_t tri;
    lv_draw_rect_dsc_init(&tri);
    tri.bg_opa = LV_OPA_COVER;

    for (int i = 0; i < 12; i++) {
        int idx = 12 + i;
        int x0 = ox + i * POINT_W + (i >= 6 ? BAR_W : 0);
        lv_point_t pts[3] = {
            { (lv_coord_t)x0, (lv_coord_t)oy },
            { (lv_coord_t)(x0 + POINT_W), (lv_coord_t)oy },
            { (lv_coord_t)(x0 + POINT_W / 2), (lv_coord_t)(oy + TRI_H) }
        };
        tri.bg_color = (self->selected_from_ == idx) ? UI_COLOR_WARNING :
                        ((i % 2 == 0) ? lv_color_hex(0x6b4a2e) : lv_color_hex(0xa9825a));
        lv_draw_polygon(ctx, &tri, pts, 3);
    }
    for (int i = 0; i < 12; i++) {
        int idx = 11 - i;
        int x0 = ox + i * POINT_W + (i >= 6 ? BAR_W : 0);
        lv_point_t pts[3] = {
            { (lv_coord_t)x0, (lv_coord_t)(by + TRI_H) },
            { (lv_coord_t)(x0 + POINT_W), (lv_coord_t)(by + TRI_H) },
            { (lv_coord_t)(x0 + POINT_W / 2), (lv_coord_t)by }
        };
        tri.bg_color = (self->selected_from_ == idx) ? UI_COLOR_WARNING :
                        ((i % 2 == 0) ? lv_color_hex(0x6b4a2e) : lv_color_hex(0xa9825a));
        lv_draw_polygon(ctx, &tri, pts, 3);
    }

    lv_draw_rect_dsc_t barbg;
    lv_draw_rect_dsc_init(&barbg);
    barbg.bg_color = lv_color_hex(0x1a1a2e);
    barbg.bg_opa = LV_OPA_COVER;
    lv_area_t bar_area = { (lv_coord_t)(ox + HALF_W), (lv_coord_t)oy,
                            (lv_coord_t)(ox + HALF_W + BAR_W), (lv_coord_t)(by + TRI_H) };
    lv_draw_rect(ctx, &barbg, &bar_area);

    lv_draw_rect_dsc_t chk;
    lv_draw_rect_dsc_init(&chk);
    chk.radius = LV_RADIUS_CIRCLE;
    chk.bg_opa = LV_OPA_COVER;
    chk.border_width = 1;
    chk.border_opa = LV_OPA_COVER;

    lv_draw_label_dsc_t ldsc;
    lv_draw_label_dsc_init(&ldsc);
    ldsc.font = &lv_font_montserrat_12;
    ldsc.align = LV_TEXT_ALIGN_CENTER;

    for (int idx = 0; idx < 24; idx++) {
        int v = self->points_[idx];
        int cnt = v > 0 ? v : -v;
        if (cnt == 0) continue;
        bool white = v > 0;
        int px, py, dir;
        if (idx >= 12) {
            int i = idx - 12;
            px = ox + i * POINT_W + (i >= 6 ? BAR_W : 0) + POINT_W / 2;
            py = oy + 8; dir = 1;
        } else {
            int i = 11 - idx;
            px = ox + i * POINT_W + (i >= 6 ? BAR_W : 0) + POINT_W / 2;
            py = by + TRI_H - 8; dir = -1;
        }
        int show = cnt > 5 ? 5 : cnt;
        for (int k = 0; k < show; k++) {
            int cy = py + dir * k * 14;
            lv_area_t ca = { (lv_coord_t)(px - CHIP_D / 2), (lv_coord_t)(cy - CHIP_D / 2),
                              (lv_coord_t)(px + CHIP_D / 2), (lv_coord_t)(cy + CHIP_D / 2) };
            chk.bg_color = white ? ui_absolute_color_hex(0xF5F5F5) : ui_absolute_color_hex(0x202020);
            chk.border_color = white ? ui_absolute_color_hex(0x000000) : ui_absolute_color_hex(0xFFFFFF);
            lv_draw_rect(ctx, &chk, &ca);
            if (k == show - 1 && cnt > 5) {
                char buf[4];
                snprintf(buf, sizeof(buf), "%d", cnt);
                ldsc.color = white ? ui_absolute_color_hex(0x000000) : ui_absolute_color_hex(0xFFFFFF);
                lv_area_t ta = { (lv_coord_t)(px - 8), (lv_coord_t)(cy - 7),
                                  (lv_coord_t)(px + 8), (lv_coord_t)(cy + 7) };
                lv_draw_label(ctx, &ldsc, &ta, buf, NULL);
            }
        }
    }

    // Bar checkers
    if (self->bar_white_ > 0) {
        int px = ox + HALF_W + BAR_W / 2, py = oy + TRI_H / 2 - 6;
        lv_area_t ca = { (lv_coord_t)(px - CHIP_D / 2), (lv_coord_t)(py - CHIP_D / 2),
                          (lv_coord_t)(px + CHIP_D / 2), (lv_coord_t)(py + CHIP_D / 2) };
        chk.bg_color = ui_absolute_color_hex(0xF5F5F5);
        chk.border_color = ui_absolute_color_hex(0x000000);
        lv_draw_rect(ctx, &chk, &ca);
        if (self->bar_white_ > 1) {
            char buf[4]; snprintf(buf, sizeof(buf), "%d", self->bar_white_);
            ldsc.color = ui_absolute_color_hex(0x000000);
            lv_area_t ta = { (lv_coord_t)(px - 8), (lv_coord_t)(py - 7), (lv_coord_t)(px + 8), (lv_coord_t)(py + 7) };
            lv_draw_label(ctx, &ldsc, &ta, buf, NULL);
        }
    }
    if (self->bar_black_ > 0) {
        int px = ox + HALF_W + BAR_W / 2, py = by + TRI_H / 2 + 6;
        lv_area_t ca = { (lv_coord_t)(px - CHIP_D / 2), (lv_coord_t)(py - CHIP_D / 2),
                          (lv_coord_t)(px + CHIP_D / 2), (lv_coord_t)(py + CHIP_D / 2) };
        chk.bg_color = ui_absolute_color_hex(0x202020);
        chk.border_color = ui_absolute_color_hex(0xFFFFFF);
        lv_draw_rect(ctx, &chk, &ca);
        if (self->bar_black_ > 1) {
            char buf[4]; snprintf(buf, sizeof(buf), "%d", self->bar_black_);
            ldsc.color = ui_absolute_color_hex(0xFFFFFF);
            lv_area_t ta = { (lv_coord_t)(px - 8), (lv_coord_t)(py - 7), (lv_coord_t)(px + 8), (lv_coord_t)(py + 7) };
            lv_draw_label(ctx, &ldsc, &ta, buf, NULL);
        }
    }

    // Legal-destination markers
    lv_draw_rect_dsc_t mk;
    lv_draw_rect_dsc_init(&mk);
    mk.radius = LV_RADIUS_CIRCLE;
    mk.bg_opa = LV_OPA_TRANSP;
    mk.border_width = 2;
    mk.border_color = UI_COLOR_SUCCESS;
    mk.border_opa = LV_OPA_COVER;
    for (int idx = 0; idx < 24; idx++) {
        if (!self->legal_dest_[idx]) continue;
        int px, py;
        if (idx >= 12) {
            int i = idx - 12;
            px = ox + i * POINT_W + (i >= 6 ? BAR_W : 0) + POINT_W / 2;
            py = oy + TRI_H - 10;
        } else {
            int i = 11 - idx;
            px = ox + i * POINT_W + (i >= 6 ? BAR_W : 0) + POINT_W / 2;
            py = by + 10;
        }
        lv_area_t ma = { (lv_coord_t)(px - 6), (lv_coord_t)(py - 6), (lv_coord_t)(px + 6), (lv_coord_t)(py + 6) };
        lv_draw_rect(ctx, &mk, &ma);
    }
}

// ── Lifecycle ──

lv_obj_t* Backgammon::createScreen() {
    s_self = this;
    mode_ = MODE_SELECT;
    screen_ = mp_create_mode_select(kCfg, mode_cpu_cb, mode_local_cb, mode_online_cb);
    return screen_;
}

void Backgammon::update() {
    if (mode_ == MODE_NETWORK && !game_done_) {
        if (millis() - net_last_hb_ms_ > NET_HB_INTERVAL_MS) {
            net_last_hb_ms_ = millis();
            char hb[80];
            snprintf(hb, sizeof(hb),
                "{\"type\":\"move\",\"game\":\"backgammon\",\"a\":\"hb\",\"mc\":%u}",
                (unsigned)net_mc_);
            discovery_send_game_data(peer_ip_, hb);
        }
    }

    if (mode_ == MODE_CPU && current_ != WHITE && !game_done_) {
        if (!rolled_) {
            if (!cpu_pending_) { cpu_pending_ = true; cpu_think_time_ = millis(); }
            else if (millis() - cpu_think_time_ > 600) {
                cpu_pending_ = false;
                roll_dice();
                end_turn_if_no_moves();
                redraw_board();
                update_status();
            }
        } else {
            bool dice_left = false;
            for (int i = 0; i < dice_count_; i++) if (!dice_used_[i]) dice_left = true;
            if (dice_left) {
                if (!cpu_pending_) { cpu_pending_ = true; cpu_think_time_ = millis(); }
                else if (millis() - cpu_think_time_ > 600) {
                    cpu_pending_ = false;
                    int from, die;
                    if (cpu_pick_move(from, die)) try_apply_move(from, die, false);
                    else end_turn_if_no_moves();
                }
            }
        }
    }

    if (mode_ == MODE_LOBBY) mp_shell_lobby_tick();
}

void Backgammon::destroy() {
    mp_shell_end(peer_ip_, mode_ == MODE_NETWORK && !game_done_);
    s_self = nullptr;
    screen_ = nullptr;
    board_area_ = nullptr;
    lbl_status_ = nullptr;
    lbl_dice_ = nullptr;
    lbl_off_ = nullptr;
    btn_roll_ = nullptr;
    btn_bear_off_ = nullptr;
}

void Backgammon::onNetworkData(const char* json) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "backgammon") != 0) return;
    if (doc["abandon"] | false) {
        show_result("Opponent left", false);
        return;
    }

    const char* action = doc["a"] | "";
    uint32_t peer_mc = doc["mc"] | 0;

    if (strcmp(action, "hb") == 0) {
        if (peer_mc < net_mc_ && net_last_move_[0]) {
            discovery_send_game_data(peer_ip_, net_last_move_);
        }
        return;
    }

    if (peer_mc != net_mc_ + 1) return;

    if (strcmp(action, "roll") == 0) {
        int d1 = doc["d1"] | 1;
        int d2 = doc["d2"] | 1;
        apply_dice(d1, d2);
        net_mc_ = peer_mc;
        sound_opponent_move();
        end_turn_if_no_moves();
        redraw_board();
        update_status();
    } else if (strcmp(action, "mv") == 0) {
        int from = doc["from"] | -2;
        int die = doc["die"] | 0;
        if (from < -1 || from > 23 || die < 1 || die > 6) return;
        net_mc_ = peer_mc;
        sound_opponent_move();
        try_apply_move(from, die, false);
    }
}
