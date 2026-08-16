#include "ludo.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>

static Ludo* s_self = nullptr;
static lv_obj_t* ludo_invite_msgbox = nullptr;
static IPAddress ludo_pending_ip;

// ── Board geometry ──
static const int BOARD_X = 4;
static const int BOARD_Y = 4;
static const int CELL = 13;
static const int BOARD_PX = 15 * CELL; // 195
static const int CHIP_D = 10;

// Standard 52-cell Ludo ring, laid out on a 15x15 grid (row, col).
static const int8_t TRACK_RC[52][2] = {
    {6,1},{6,2},{6,3},{6,4},{6,5},
    {5,6},{4,6},{3,6},{2,6},{1,6},{0,6},
    {0,7},
    {0,8},{1,8},{2,8},{3,8},{4,8},{5,8},
    {6,9},{6,10},{6,11},{6,12},{6,13},{6,14},
    {7,14},
    {8,14},{8,13},{8,12},{8,11},{8,10},{8,9},
    {9,8},{10,8},{11,8},{12,8},{13,8},{14,8},
    {14,7},
    {14,6},{13,6},{12,6},{11,6},{10,6},{9,6},
    {8,5},{8,4},{8,3},{8,2},{8,1},{8,0},
    {7,0},
    {6,0}
};

static const int8_t START_OFFSET[2] = { 0, 26 };            // Red, Yellow
static const int8_t SAFE_SQUARES[8] = { 0, 8, 13, 21, 26, 34, 39, 47 };

static const int8_t RED_HOME[6][2] = { {7,1},{7,2},{7,3},{7,4},{7,5},{7,6} };
static const int8_t YELLOW_HOME[6][2] = { {7,13},{7,12},{7,11},{7,10},{7,9},{7,8} };

static const int8_t YARD_RC[2][4][2] = {
    { {1,1},{1,4},{4,1},{4,4} },
    { {10,10},{10,13},{13,10},{13,13} }
};

// ── Discovery callbacks ──

void ludo_on_invite(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    if (ludo_invite_msgbox) return;

    ludo_pending_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    ludo_invite_msgbox = lv_msgbox_create(NULL, "Ludo Invite", from.name, btns, false);
    lv_obj_set_size(ludo_invite_msgbox, 240, 140);
    lv_obj_center(ludo_invite_msgbox);
    lv_obj_set_style_bg_color(ludo_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(ludo_invite_msgbox, UI_COLOR_TEXT, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(ludo_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(ludo_invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(ludo_pending_ip);
            s_self->peer_ip_ = ludo_pending_ip;
            s_self->mode_ = Ludo::MODE_NETWORK;
            s_self->my_color_ = Ludo::PC_YELLOW;
            s_self->my_turn_ = false;
            discovery_set_game("ludo", "playing");
            lv_msgbox_close(ludo_invite_msgbox);
            ludo_invite_msgbox = nullptr;
            lv_obj_t* scr = s_self->create_board();
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            discovery_send_decline(ludo_pending_ip);
            lv_msgbox_close(ludo_invite_msgbox);
            ludo_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void ludo_on_accept(const Peer& from) {
    if (!s_self || !s_self->lobby_list_) return;
    s_self->peer_ip_ = from.ip;
    s_self->mode_ = Ludo::MODE_NETWORK;
    s_self->my_color_ = Ludo::PC_RED;
    s_self->my_turn_ = true;
    discovery_set_game("ludo", "playing");
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void ludo_on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != Ludo::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

void ludo_lobby_peer_cb(lv_event_t* e) {
    if (!s_self) return;
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const Peer* peers = discovery_get_peers();
    int count = discovery_peer_count();
    if (idx < 0 || idx >= count) return;
    discovery_send_invite(peers[idx].ip);
    if (s_self->lobby_list_) {
        lv_obj_clean(s_self->lobby_list_);
        lv_list_add_text(s_self->lobby_list_, "Invite sent, waiting...");
    }
}

// ── Mode selection ──

void Ludo::mode_cpu_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU;
    s_self->my_color_ = PC_RED;
    s_self->cpu_pending_ = false;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Ludo::mode_local_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->cpu_pending_ = false;
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Ludo::mode_online_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    discovery_set_game("ludo", "waiting");
    discovery_on_invite(ludo_on_invite);
    discovery_on_accept(ludo_on_accept);
    discovery_on_game_data(ludo_on_game_data);
    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* Ludo::create_mode_select() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Ludo");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    lv_obj_t* b1 = ui_create_btn(scr, "vs CPU", 140, 42);
    lv_obj_align(b1, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(b1, mode_cpu_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* b2 = ui_create_btn(scr, "Local (2P)", 140, 42);
    lv_obj_align(b2, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(b2, mode_local_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* b3 = ui_create_btn(scr, "Network (2P)", 140, 42);
    lv_obj_align(b3, LV_ALIGN_CENTER, 0, 50);
    lv_obj_add_event_cb(b3, mode_online_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

lv_obj_t* Ludo::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Ludo - Find Opponent");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);
    lobby_list_ = lv_list_create(scr);
    lv_obj_set_size(lobby_list_, 280, 160);
    lv_obj_align(lobby_list_, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(lobby_list_, UI_COLOR_CARD, 0);
    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap a peer to invite");
    lv_obj_set_style_text_color(hint, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);
    return scr;
}

// ── Board rules ──

void Ludo::reset_board() {
    for (int p = 0; p < 2; p++)
        for (int i = 0; i < 4; i++)
            tokens_[p][i].local_pos = -1;
    current_ = PC_RED;
    rolled_ = false;
    die_ = 1;
    for (int i = 0; i < 4; i++) legal_token_[i] = false;
    game_done_ = false;
    net_reset_sync();
}

bool Ludo::token_grid_pos(Player p, int local, int slot, int& row, int& col) const {
    if (local == -1) {
        row = YARD_RC[p][slot][0];
        col = YARD_RC[p][slot][1];
        return true;
    }
    if (local >= 0 && local <= 50) {
        int shared = (START_OFFSET[p] + local) % 52;
        row = TRACK_RC[shared][0];
        col = TRACK_RC[shared][1];
        return true;
    }
    if (local >= 51 && local <= 56) {
        int hi = local - 51;
        const int8_t (*home)[2] = (p == PC_RED) ? RED_HOME : YELLOW_HOME;
        row = home[hi][0];
        col = home[hi][1];
        return true;
    }
    return false; // finished
}

bool Ludo::is_safe_square(int shared) const {
    for (int i = 0; i < 8; i++) if (SAFE_SQUARES[i] == shared) return true;
    return false;
}

int Ludo::opp_count_at_shared(Player p, int shared) const {
    Player opp = (p == PC_RED) ? PC_YELLOW : PC_RED;
    int cnt = 0;
    for (int i = 0; i < 4; i++) {
        int lp = tokens_[opp][i].local_pos;
        if (lp >= 0 && lp <= 50 && (START_OFFSET[opp] + lp) % 52 == shared) cnt++;
    }
    return cnt;
}

bool Ludo::compute_move(Player p, int token_idx, int die, int& new_local) const {
    int local = tokens_[p][token_idx].local_pos;
    if (local == 57) return false;
    if (local == -1) {
        if (die != 6) return false;
        new_local = 0;
        return true;
    }
    int nl = local + die;
    if (nl > 57) return false;
    new_local = nl;
    return true;
}

bool Ludo::any_legal_move(Player p, int die) const {
    for (int i = 0; i < 4; i++) {
        int nl;
        if (compute_move(p, i, die, nl)) return true;
    }
    return false;
}

bool Ludo::try_apply_move(Player p, int token_idx, int die, bool send) {
    int new_local;
    if (!compute_move(p, token_idx, die, new_local)) return false;

    tokens_[p][token_idx].local_pos = new_local;
    bool captured = false;
    if (new_local >= 0 && new_local <= 50) {
        int shared = (START_OFFSET[p] + new_local) % 52;
        if (!is_safe_square(shared) && opp_count_at_shared(p, shared) == 1) {
            Player opp = (p == PC_RED) ? PC_YELLOW : PC_RED;
            for (int i = 0; i < 4; i++) {
                int lp = tokens_[opp][i].local_pos;
                if (lp >= 0 && lp <= 50 && (START_OFFSET[opp] + lp) % 52 == shared) {
                    tokens_[opp][i].local_pos = -1;
                    captured = true;
                    break;
                }
            }
        }
    }
    sound_move();

    if (send && mode_ == MODE_NETWORK) send_move(token_idx, die);

    bool all_home = true;
    for (int i = 0; i < 4; i++) if (tokens_[p][i].local_pos != 57) all_home = false;
    if (all_home) {
        game_done_ = true;
        static char result_buf[32];
        static bool result_is_win;
        if (mode_ == MODE_NETWORK) {
            result_is_win = (p == my_color_);
            snprintf(result_buf, sizeof(result_buf), "%s", result_is_win ? "You Win!" : "You Lose!");
        } else if (mode_ == MODE_CPU) {
            result_is_win = (p == PC_RED);
            snprintf(result_buf, sizeof(result_buf), "%s", p == PC_RED ? "You Win!" : "CPU Wins!");
        } else {
            result_is_win = true;
            snprintf(result_buf, sizeof(result_buf), "%s Wins!", p == PC_RED ? "Red" : "Yellow");
        }
        redraw_board();
        lv_timer_create([](lv_timer_t* t) {
            lv_timer_del(t);
            if (s_self) s_self->show_result(result_buf, result_is_win);
        }, 1200, NULL);
        return true;
    }

    bool extra_turn = (die == 6) || captured;
    rolled_ = false;
    for (int i = 0; i < 4; i++) legal_token_[i] = false;
    if (!extra_turn) switch_turn();
    else update_status();
    redraw_board();
    return true;
}

// ── Turn flow ──

void Ludo::apply_roll(int die) {
    rolled_ = true;
    die_ = die;
    sound_move();
    bool any = false;
    for (int i = 0; i < 4; i++) {
        int nl;
        legal_token_[i] = compute_move(current_, i, die, nl);
        if (legal_token_[i]) any = true;
    }
    if (!any) {
        if (mode_ == MODE_NETWORK) send_pass(die);
        lv_timer_create([](lv_timer_t* t) {
            lv_timer_del(t);
            if (s_self && !s_self->game_done_) s_self->switch_turn();
        }, 700, NULL);
    }
    redraw_board();
    update_status();
}

void Ludo::send_move(int token_idx, int die) {
    net_mc_++;
    StaticJsonDocument<160> doc;
    doc["type"] = "move";
    doc["game"] = "ludo";
    doc["a"] = "mv";
    doc["p"] = token_idx;
    doc["d"] = die;
    doc["mc"] = net_mc_;
    serializeJson(doc, net_last_move_, sizeof(net_last_move_));
    discovery_send_game_data(peer_ip_, net_last_move_);
}

void Ludo::send_pass(int die) {
    net_mc_++;
    StaticJsonDocument<160> doc;
    doc["type"] = "move";
    doc["game"] = "ludo";
    doc["a"] = "pass";
    doc["d"] = die;
    doc["mc"] = net_mc_;
    serializeJson(doc, net_last_move_, sizeof(net_last_move_));
    discovery_send_game_data(peer_ip_, net_last_move_);
}

void Ludo::switch_turn() {
    current_ = (current_ == PC_RED) ? PC_YELLOW : PC_RED;
    rolled_ = false;
    for (int i = 0; i < 4; i++) legal_token_[i] = false;
    if (mode_ == MODE_NETWORK) my_turn_ = (current_ == my_color_);
    redraw_board();
    update_status();
}

void Ludo::redraw_board() {
    if (board_area_) lv_obj_invalidate(board_area_);
}

void Ludo::update_status() {
    if (!lbl_status_) return;
    const char* txt;
    if (mode_ == MODE_CPU) {
        txt = (current_ == PC_RED) ? (rolled_ ? "Your turn" : "Your turn - Roll") : "CPU thinking...";
    } else if (mode_ == MODE_LOCAL) {
        txt = (current_ == PC_RED) ? "Red's turn" : "Yellow's turn";
    } else {
        txt = my_turn_ ? (rolled_ ? "Your turn" : "Your turn - Roll") : "Waiting...";
    }
    lv_label_set_text(lbl_status_, txt);

    if (lbl_dice_) {
        char buf[8];
        if (rolled_) snprintf(buf, sizeof(buf), "%d", die_);
        else buf[0] = 0;
        lv_label_set_text(lbl_dice_, buf);
    }
    if (lbl_home_) {
        int rh = 0, yh = 0;
        for (int i = 0; i < 4; i++) {
            if (tokens_[PC_RED][i].local_pos == 57) rh++;
            if (tokens_[PC_YELLOW][i].local_pos == 57) yh++;
        }
        char buf[24];
        snprintf(buf, sizeof(buf), "Home R:%d Y:%d", rh, yh);
        lv_label_set_text(lbl_home_, buf);
    }
    if (btn_roll_) {
        bool can_roll = !rolled_ && !game_done_ &&
            (mode_ == MODE_LOCAL ||
             (mode_ == MODE_CPU && current_ == PC_RED) ||
             (mode_ == MODE_NETWORK && my_turn_));
        if (can_roll) lv_obj_clear_flag(btn_roll_, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(btn_roll_, LV_OBJ_FLAG_HIDDEN);
    }
}

void Ludo::show_result(const char* text, bool is_win) {
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

// ── CPU AI (greedy heuristic: finish > hit > leave yard > advance) ──

int Ludo::score_ludo_move(int token_idx, int new_local) const {
    int score = 0;
    if (new_local == 57) score += 1000;
    if (tokens_[current_][token_idx].local_pos == -1) score += 300;
    if (new_local >= 0 && new_local <= 50) {
        int shared = (START_OFFSET[current_] + new_local) % 52;
        if (!is_safe_square(shared) && opp_count_at_shared(current_, shared) == 1) score += 500;
    }
    score += new_local; // mild preference for advancing farther
    return score;
}

int Ludo::cpu_pick_token() const {
    int best = -1, best_score = -1000000;
    for (int i = 0; i < 4; i++) {
        if (!legal_token_[i]) continue;
        int nl;
        compute_move(current_, i, die_, nl);
        int sc = score_ludo_move(i, nl);
        if (sc > best_score) { best_score = sc; best = i; }
    }
    return best;
}

// ── Board UI ──

lv_obj_t* Ludo::create_board() {
    lv_obj_t* scr = ui_create_screen();
    lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    screen_ = scr;

    board_area_ = lv_obj_create(scr);
    lv_obj_remove_style_all(board_area_);
    lv_obj_set_size(board_area_, BOARD_PX, BOARD_PX);
    lv_obj_set_pos(board_area_, BOARD_X, BOARD_Y);
    lv_obj_clear_flag(board_area_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(board_area_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(board_area_, board_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(board_area_, board_draw_cb, LV_EVENT_DRAW_POST, NULL);

    int tb_x = BOARD_X + BOARD_PX + 6;
    int y = 4;

    {
        lv_obj_t* btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 70, 26);
        lv_obj_set_pos(btn, tb_x, y);
        lv_obj_set_style_bg_color(btn, UI_COLOR_PRIMARY, 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_t* lbl = lv_label_create(btn);
        lv_label_set_text(lbl, LV_SYMBOL_LEFT " Back");
        lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, [](lv_event_t*) { screen_manager_back_to_menu(); }, LV_EVENT_CLICKED, NULL);
    }
    y += 32;

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(lbl_status_, tb_x, y);
    y += 22;

    lbl_dice_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_dice_, UI_COLOR_WARNING, 0);
    lv_obj_set_style_text_font(lbl_dice_, &lv_font_montserrat_28, 0);
    lv_obj_set_pos(lbl_dice_, tb_x, y);
    y += 44;

    btn_roll_ = ui_create_btn(scr, "Roll", 84, 30);
    lv_obj_set_pos(btn_roll_, tb_x, y);
    lv_obj_add_event_cb(btn_roll_, roll_cb, LV_EVENT_CLICKED, NULL);
    y += 36;

    lbl_home_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_home_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_home_, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(lbl_home_, tb_x, y);

    lobby_list_ = nullptr;
    reset_board();
    update_status();
    redraw_board();
    return scr;
}

void Ludo::roll_cb(lv_event_t*) {
    if (!s_self || s_self->rolled_ || s_self->game_done_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && s_self->current_ != PC_RED) return;
    int die = random(1, 7);
    s_self->apply_roll(die);
}

void Ludo::board_click_cb(lv_event_t*) {
    if (!s_self || s_self->game_done_ || !s_self->rolled_) return;
    if (s_self->mode_ == MODE_NETWORK && !s_self->my_turn_) return;
    if (s_self->mode_ == MODE_CPU && s_self->current_ != PC_RED) return;

    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);

    Ludo* self = s_self;
    int best = -1, best_d2 = 999999;
    for (int i = 0; i < 4; i++) {
        if (!self->legal_token_[i]) continue;
        int row, col;
        if (!self->token_grid_pos(self->current_, self->tokens_[self->current_][i].local_pos, i, row, col)) continue;
        int px = BOARD_X + col * CELL + CELL / 2;
        int py = BOARD_Y + row * CELL + CELL / 2;
        int dx = p.x - px, dy = p.y - py;
        int d2 = dx * dx + dy * dy;
        if (d2 < best_d2 && d2 <= 15 * 15) { best_d2 = d2; best = i; }
    }
    if (best >= 0) self->try_apply_move(self->current_, best, self->die_, true);
}

void Ludo::board_draw_cb(lv_event_t* e) {
    if (!s_self) return;
    Ludo* self = s_self;
    lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
    lv_obj_t* obj = lv_event_get_target(e);
    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    int ox = area.x1, oy = area.y1;

    auto cell_rect = [&](int r0, int c0, int rn, int cn, lv_color_t color) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_color = color;
        d.bg_opa = LV_OPA_COVER;
        lv_area_t a = { (lv_coord_t)(ox + c0 * CELL), (lv_coord_t)(oy + r0 * CELL),
                         (lv_coord_t)(ox + (c0 + cn) * CELL), (lv_coord_t)(oy + (r0 + rn) * CELL) };
        lv_draw_rect(ctx, &d, &a);
    };

    lv_draw_rect_dsc_t bg;
    lv_draw_rect_dsc_init(&bg);
    bg.bg_color = UI_COLOR_CARD;
    bg.bg_opa = LV_OPA_COVER;
    bg.radius = 4;
    lv_draw_rect(ctx, &bg, &area);

    cell_rect(6, 0, 3, 15, lv_color_hex(0x3a3a52));   // horizontal arm
    cell_rect(0, 6, 15, 3, lv_color_hex(0x3a3a52));   // vertical arm
    cell_rect(0, 0, 6, 6, lv_color_hex(0x5a2a2a));    // Red yard
    cell_rect(9, 9, 6, 6, lv_color_hex(0x5a5228));    // Yellow yard
    cell_rect(6, 6, 3, 3, lv_color_hex(0x44445a));    // center
    cell_rect(7, 1, 1, 6, lv_color_hex(0x8a3a3a));    // Red home column
    cell_rect(7, 8, 1, 6, lv_color_hex(0x8a7a2a));    // Yellow home column

    lv_draw_rect_dsc_t safe;
    lv_draw_rect_dsc_init(&safe);
    safe.radius = LV_RADIUS_CIRCLE;
    safe.bg_color = UI_COLOR_DIM;
    safe.bg_opa = LV_OPA_COVER;
    for (int i = 0; i < 8; i++) {
        int r = TRACK_RC[SAFE_SQUARES[i]][0], c = TRACK_RC[SAFE_SQUARES[i]][1];
        int px = ox + c * CELL + CELL / 2, py = oy + r * CELL + CELL / 2;
        lv_area_t a = { (lv_coord_t)(px - 2), (lv_coord_t)(py - 2), (lv_coord_t)(px + 2), (lv_coord_t)(py + 2) };
        lv_draw_rect(ctx, &safe, &a);
    }

    // Tokens
    lv_draw_rect_dsc_t chk;
    lv_draw_rect_dsc_init(&chk);
    chk.radius = LV_RADIUS_CIRCLE;
    chk.bg_opa = LV_OPA_COVER;
    chk.border_width = 1;
    chk.border_color = ui_absolute_color_hex(0x000000);
    chk.border_opa = LV_OPA_COVER;

    lv_draw_rect_dsc_t ring;
    lv_draw_rect_dsc_init(&ring);
    ring.radius = LV_RADIUS_CIRCLE;
    ring.bg_opa = LV_OPA_TRANSP;
    ring.border_width = 2;
    ring.border_color = UI_COLOR_SUCCESS;
    ring.border_opa = LV_OPA_COVER;

    int drawn_row[8], drawn_col[8], drawn_n = 0;
    for (int p = 0; p < 2; p++) {
        for (int i = 0; i < 4; i++) {
            int local = self->tokens_[p][i].local_pos;
            int row, col;
            if (!self->token_grid_pos((Player)p, local, i, row, col)) continue; // finished

            int offset = 0;
            for (int k = 0; k < drawn_n; k++) if (drawn_row[k] == row && drawn_col[k] == col) offset++;
            drawn_row[drawn_n] = row; drawn_col[drawn_n] = col; drawn_n++;

            int px = ox + col * CELL + CELL / 2 + (offset % 2) * 4;
            int py = oy + row * CELL + CELL / 2 + (offset / 2) * 4;

            chk.bg_color = (p == PC_RED) ? ui_absolute_color_hex(0xCC3333) : ui_absolute_color_hex(0xDDCC33);
            lv_area_t ca = { (lv_coord_t)(px - CHIP_D / 2), (lv_coord_t)(py - CHIP_D / 2),
                              (lv_coord_t)(px + CHIP_D / 2), (lv_coord_t)(py + CHIP_D / 2) };
            lv_draw_rect(ctx, &chk, &ca);

            if (p == self->current_ && self->legal_token_[i]) {
                lv_area_t ra = { (lv_coord_t)(px - CHIP_D / 2 - 2), (lv_coord_t)(py - CHIP_D / 2 - 2),
                                  (lv_coord_t)(px + CHIP_D / 2 + 2), (lv_coord_t)(py + CHIP_D / 2 + 2) };
                lv_draw_rect(ctx, &ring, &ra);
            }
        }
    }
}

// ── Lifecycle ──

lv_obj_t* Ludo::createScreen() {
    s_self = this;
    ludo_invite_msgbox = nullptr;
    mode_ = MODE_SELECT;
    screen_ = create_mode_select();
    return screen_;
}

void Ludo::update() {
    if (mode_ == MODE_NETWORK && !game_done_) {
        if (millis() - net_last_hb_ms_ > NET_HB_INTERVAL_MS) {
            net_last_hb_ms_ = millis();
            char hb[80];
            snprintf(hb, sizeof(hb),
                "{\"type\":\"move\",\"game\":\"ludo\",\"a\":\"hb\",\"mc\":%u}",
                (unsigned)net_mc_);
            discovery_send_game_data(peer_ip_, hb);
        }
    }

    if (mode_ == MODE_CPU && current_ != PC_RED && !game_done_) {
        if (!rolled_) {
            if (!cpu_pending_) { cpu_pending_ = true; cpu_think_time_ = millis(); }
            else if (millis() - cpu_think_time_ > 500) {
                cpu_pending_ = false;
                apply_roll(random(1, 7));
            }
        } else {
            if (!cpu_pending_) { cpu_pending_ = true; cpu_think_time_ = millis(); }
            else if (millis() - cpu_think_time_ > 500) {
                cpu_pending_ = false;
                int idx = cpu_pick_token();
                if (idx >= 0) try_apply_move(current_, idx, die_, false);
            }
        }
    }

    if (mode_ == MODE_LOBBY && lobby_list_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);
            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            int shown = 0;
            for (int i = 0; i < count; i++) {
                if (strcmp(peers[i].game, "ludo") == 0) {
                    char label[32];
                    snprintf(label, sizeof(label), "%s (%s)", peers[i].name, peers[i].state);
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, label);
                    lv_obj_add_event_cb(btn, ludo_lobby_peer_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
                    shown++;
                }
            }
            if (shown == 0) lv_list_add_text(lobby_list_, "Searching...");
        }
    }
}

void Ludo::destroy() {
    if (ludo_invite_msgbox) {
        lv_msgbox_close(ludo_invite_msgbox);
        ludo_invite_msgbox = nullptr;
    }
    if (mode_ == MODE_NETWORK) {
        discovery_send_game_data(peer_ip_,
            "{\"type\":\"move\",\"game\":\"ludo\",\"abandon\":true}");
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    board_area_ = nullptr;
    lbl_status_ = nullptr;
    lbl_dice_ = nullptr;
    lbl_home_ = nullptr;
    btn_roll_ = nullptr;
    lobby_list_ = nullptr;
}

void Ludo::onNetworkData(const char* json) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "ludo") != 0) return;
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
    net_mc_ = peer_mc;
    sound_opponent_move();

    if (strcmp(action, "mv") == 0) {
        int token_idx = doc["p"] | -1;
        int die = doc["d"] | 0;
        if (token_idx < 0 || token_idx > 3 || die < 1 || die > 6) return;
        die_ = die;
        try_apply_move(current_, token_idx, die, false);
    } else if (strcmp(action, "pass") == 0) {
        int die = doc["d"] | 0;
        die_ = die;
        if (!game_done_) switch_turn();
    }
}
