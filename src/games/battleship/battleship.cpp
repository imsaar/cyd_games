#include "battleship.h"
#include "../../ui/ui_common.h"
#include "../../ui/screen_manager.h"
#include "../../hal/sound.h"
#include <ArduinoJson.h>

static Battleship* s_self = nullptr;
static lv_obj_t* bs_invite_msgbox = nullptr;
static IPAddress bs_pending_ip;

const int Battleship::ship_sizes_[NUM_SHIPS] = {5, 4, 3, 3, 2};

// ── Grid drawing constants ──
// Two 8x8 grids side by side on 320x240 screen
// Left grid = "my board", Right grid = "enemy board"
static const int CELL_SZ = 18;  // 18*8=144 px per grid
static const int GRID_W = CELL_SZ * Battleship::GRID;
static const int GRID_GAP = 6;
static const int GRID_Y = 50;
static const int GRID_L_X = (320 - GRID_W * 2 - GRID_GAP) / 2;
static const int GRID_R_X = GRID_L_X + GRID_W + GRID_GAP;

// ── Discovery callbacks ──

void bs_on_invite(const Peer& from) {
    if (!s_self || s_self->phase_ != Battleship::PHASE_LOBBY) return;
    if (bs_invite_msgbox) return;
    bs_pending_ip = from.ip;
    static const char* btns[] = {"Accept", "Decline", ""};
    bs_invite_msgbox = lv_msgbox_create(NULL, "Battleship Invite",
        from.name, btns, false);
    lv_obj_set_size(bs_invite_msgbox, 240, 140);
    lv_obj_center(bs_invite_msgbox);
    lv_obj_set_style_bg_color(bs_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(bs_invite_msgbox, UI_COLOR_TEXT, 0);
    lv_obj_t* btnm = lv_msgbox_get_btns(bs_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t*) {
        uint16_t btn_id = lv_msgbox_get_active_btn(bs_invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(bs_pending_ip);
            s_self->peer_ip_ = bs_pending_ip;
            s_self->is_host_ = false;
            s_self->mode_ = Battleship::MODE_NETWORK;
            discovery_set_game("battleship", "playing");
            lv_msgbox_close(bs_invite_msgbox);
            bs_invite_msgbox = nullptr;
            s_self->reset_game();
            lv_obj_t* scr = s_self->create_placement(0);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            lv_msgbox_close(bs_invite_msgbox);
            bs_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

void bs_on_accept(const Peer& from) {
    if (!s_self || s_self->phase_ != Battleship::PHASE_LOBBY) return;
    s_self->peer_ip_ = from.ip;
    s_self->is_host_ = true;
    s_self->mode_ = Battleship::MODE_NETWORK;
    discovery_set_game("battleship", "playing");
    s_self->reset_game();
    lv_obj_t* scr = s_self->create_placement(0);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void bs_on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != Battleship::MODE_NETWORK) return;
    s_self->onNetworkData(json);
}

void bs_lobby_peer_cb(lv_event_t* e) {
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

// ── Lifecycle ──

lv_obj_t* Battleship::createScreen() {
    s_self = this;
    bs_invite_msgbox = nullptr;
    screen_ = create_mode_select();
    return screen_;
}

void Battleship::update() {
    // Heartbeat + pending-fire retry for network resync.
    if (mode_ == MODE_NETWORK && !game_done_ &&
        (phase_ == PHASE_BATTLE || phase_ == PHASE_WAIT)) {
        if (millis() - net_last_hb_ms_ > NET_HB_INTERVAL_MS) {
            net_last_hb_ms_ = millis();
            char hb[80];
            snprintf(hb, sizeof(hb),
                "{\"type\":\"move\",\"game\":\"battleship\",\"a\":\"hb\",\"mc\":%u}",
                (unsigned)net_mc_);
            send_json(hb);
            // Attacker-side retry: if we have a fire that hasn't been
            // acked (result still missing), resend the cached fire.
            if (net_pending_mc_ > 0 && net_last_move_[0]) {
                send_json(net_last_move_);
            }
        }
    }

    // Lobby peer refresh
    if (phase_ == PHASE_LOBBY && lobby_list_) {
        static uint32_t last_refresh = 0;
        if (millis() - last_refresh > 2000) {
            last_refresh = millis();
            lv_obj_clean(lobby_list_);
            const Peer* peers = discovery_get_peers();
            int count = discovery_peer_count();
            bool found = false;
            for (int i = 0; i < count; i++) {
                if (peers[i].game[0] == '\0' ||
                    (strcmp(peers[i].game, "battleship") == 0 &&
                     strcmp(peers[i].state, "waiting") == 0)) {
                    lv_obj_t* btn = lv_list_add_btn(lobby_list_, LV_SYMBOL_WIFI, peers[i].name);
                    lv_obj_add_event_cb(btn, bs_lobby_peer_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
                    found = true;
                }
            }
            if (!found) {
                lv_list_add_text(lobby_list_, "Searching for peers...");
            }
        }
    }

    // CPU turn
    if (mode_ == MODE_CPU && phase_ == PHASE_BATTLE && cpu_pending_) {
        if (millis() - cpu_think_time_ > 500) {
            cpu_pending_ = false;
            int target = cpu_pick_target();
            int row = target / GRID, col = target % GRID;
            int sunk = -1;
            Cell result = fire(0, row, col, &sunk);
            if (result == HIT || result == SUNK) {
                sound_opponent_move();
                // Add adjacent cells to hunt stack
                int adj[] = {target - GRID, target + GRID, target - 1, target + 1};
                for (int a : adj) {
                    if (a < 0 || a >= GRID * GRID) continue;
                    // Don't wrap rows
                    if ((a % GRID == 0 && col == GRID - 1) ||
                        (a % GRID == GRID - 1 && col == 0)) continue;
                    if (board_[0][a] == EMPTY || board_[0][a] == SHIP) {
                        // Push if not already targeted
                        bool dup = false;
                        for (int s = 0; s < cpu_hunt_top_; s++)
                            if (cpu_hunt_stack_[s] == a) { dup = true; break; }
                        if (!dup) cpu_hunt_stack_[cpu_hunt_top_++] = a;
                    }
                }
            } else {
                sound_opponent_move();
            }
            refresh_grids(0);  // Show CPU's shot on player's board
            if (all_sunk(0)) {
                game_done_ = true;
                delayed_gameover(1, row, col, 0);  // CPU wins, highlight on left grid
            } else {
                my_turn_ = true;
                if (lbl_status_) lv_label_set_text(lbl_status_, "Your turn - fire!");
            }
        }
    }
}

void Battleship::destroy() {
    if (bs_invite_msgbox) {
        lv_msgbox_close(bs_invite_msgbox);
        bs_invite_msgbox = nullptr;
    }
    if (mode_ == MODE_NETWORK) {
        StaticJsonDocument<96> doc;
        doc["type"] = "move"; doc["game"] = "battleship";
        doc["abandon"] = true;
        char buf[96];
        serializeJson(doc, buf, sizeof(buf));
        send_json(buf);
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
    screen_ = nullptr;
    lbl_status_ = nullptr;
    lobby_list_ = nullptr;
    memset(grid_objs_, 0, sizeof(grid_objs_));
    memset(grid_panels_, 0, sizeof(grid_panels_));
}

// ── Game logic ──

void Battleship::reset_game() {
    memset(board_, 0, sizeof(board_));
    memset(ships_, 0, sizeof(ships_));
    ships_placed_[0] = ships_placed_[1] = 0;
    ships_alive_[0] = ships_alive_[1] = NUM_SHIPS;
    place_ship_idx_ = 0;
    place_horiz_ = true;
    selected_ship_ = -1;
    current_player_ = 0;
    my_turn_ = true;
    game_done_ = false;
    cpu_pending_ = false;
    cpu_hunt_top_ = 0;
    net_reset_sync();
}

bool Battleship::can_place(int player, int ship_idx, int row, int col, bool horiz) {
    int sz = ship_sizes_[ship_idx];
    for (int i = 0; i < sz; i++) {
        int r = row + (horiz ? 0 : i);
        int c = col + (horiz ? i : 0);
        if (r < 0 || r >= GRID || c < 0 || c >= GRID) return false;
        if (board_[player][r * GRID + c] != EMPTY) return false;
    }
    return true;
}

void Battleship::do_place(int player, int ship_idx, int row, int col, bool horiz) {
    Ship& s = ships_[player][ship_idx];
    s.size = ship_sizes_[ship_idx];
    s.row = row; s.col = col; s.horiz = horiz; s.hits = 0;
    for (int i = 0; i < s.size; i++) {
        int r = row + (horiz ? 0 : i);
        int c = col + (horiz ? i : 0);
        board_[player][r * GRID + c] = SHIP;
    }
    ships_placed_[player]++;
}

Battleship::Cell Battleship::fire(int defender, int row, int col, int* sunk_ship) {
    *sunk_ship = -1;
    int idx = row * GRID + col;
    if (board_[defender][idx] == SHIP) {
        board_[defender][idx] = HIT;
        // Check which ship was hit and if it's sunk
        for (int s = 0; s < NUM_SHIPS; s++) {
            Ship& sh = ships_[defender][s];
            for (int i = 0; i < sh.size; i++) {
                int r = sh.row + (sh.horiz ? 0 : i);
                int c = sh.col + (sh.horiz ? i : 0);
                if (r == row && c == col) {
                    sh.hits++;
                    if (sh.hits >= sh.size) {
                        // Mark all cells as SUNK
                        for (int j = 0; j < sh.size; j++) {
                            int sr = sh.row + (sh.horiz ? 0 : j);
                            int sc = sh.col + (sh.horiz ? j : 0);
                            board_[defender][sr * GRID + sc] = SUNK;
                        }
                        *sunk_ship = s;
                        ships_alive_[defender]--;
                    }
                    return (*sunk_ship >= 0) ? SUNK : HIT;
                }
            }
        }
        return HIT;
    } else if (board_[defender][idx] == EMPTY) {
        board_[defender][idx] = MISS;
        return MISS;
    }
    return board_[defender][idx];  // already hit/miss/sunk
}

bool Battleship::all_sunk(int player) {
    return ships_alive_[player] <= 0;
}

void Battleship::remove_ship(int player, int ship_idx) {
    ships_[player][ship_idx].size = 0;
    ships_placed_[player]--;
}

void Battleship::set_ship_pos(int player, int ship_idx, int row, int col, bool horiz) {
    Ship& s = ships_[player][ship_idx];
    s.size = ship_sizes_[ship_idx];
    s.row = row; s.col = col; s.horiz = horiz; s.hits = 0;
    if (ships_placed_[player] < NUM_SHIPS) ships_placed_[player] = 0;
    // Recount placed ships
    ships_placed_[player] = 0;
    for (int i = 0; i < NUM_SHIPS; i++)
        if (ships_[player][i].size > 0) ships_placed_[player]++;
}

int Battleship::find_ship_at(int player, int row, int col) {
    for (int s = 0; s < NUM_SHIPS; s++) {
        Ship& sh = ships_[player][s];
        if (sh.size == 0) continue;
        for (int i = 0; i < sh.size; i++) {
            int r = sh.row + (sh.horiz ? 0 : i);
            int c = sh.col + (sh.horiz ? i : 0);
            if (r == row && c == col) return s;
        }
    }
    return -1;
}

void Battleship::rebuild_board_from_ships(int player) {
    memset(board_[player], 0, sizeof(board_[player]));
    for (int s = 0; s < NUM_SHIPS; s++) {
        Ship& sh = ships_[player][s];
        if (sh.size == 0) continue;
        for (int i = 0; i < sh.size; i++) {
            int r = sh.row + (sh.horiz ? 0 : i);
            int c = sh.col + (sh.horiz ? i : 0);
            if (r >= 0 && r < GRID && c >= 0 && c < GRID)
                board_[player][r * GRID + c] = SHIP;
        }
    }
}

bool Battleship::validate_placement(int player) {
    if (ships_placed_[player] < NUM_SHIPS) return false;
    int8_t count[GRID * GRID] = {};
    for (int s = 0; s < NUM_SHIPS; s++) {
        Ship& sh = ships_[player][s];
        if (sh.size == 0) return false;
        for (int i = 0; i < sh.size; i++) {
            int r = sh.row + (sh.horiz ? 0 : i);
            int c = sh.col + (sh.horiz ? i : 0);
            if (r < 0 || r >= GRID || c < 0 || c >= GRID) return false;
            count[r * GRID + c]++;
            if (count[r * GRID + c] > 1) return false;
        }
    }
    return true;
}

void Battleship::refresh_placement_grid(int player) {
    // Build a count grid to detect overlaps
    int8_t count[GRID * GRID] = {};
    for (int s = 0; s < NUM_SHIPS; s++) {
        Ship& sh = ships_[player][s];
        if (sh.size == 0) continue;
        for (int i = 0; i < sh.size; i++) {
            int r = sh.row + (sh.horiz ? 0 : i);
            int c = sh.col + (sh.horiz ? i : 0);
            if (r >= 0 && r < GRID && c >= 0 && c < GRID)
                count[r * GRID + c]++;
        }
    }
    // Render cells
    for (int i = 0; i < GRID * GRID; i++) {
        if (!grid_objs_[0][i]) continue;
        if (count[i] > 1)
            lv_obj_set_style_bg_color(grid_objs_[0][i], lv_color_hex(0xff6600), 0);  // orange = overlap
        else if (count[i] == 1)
            lv_obj_set_style_bg_color(grid_objs_[0][i], lv_color_hex(0x666666), 0);  // gray = ship
        else
            lv_obj_set_style_bg_color(grid_objs_[0][i], lv_color_hex(0x1a2a4a), 0);  // water
    }
    // Highlight selected ship in blue
    if (selected_ship_ >= 0) {
        Ship& s = ships_[player][selected_ship_];
        for (int i = 0; i < s.size; i++) {
            int r = s.row + (s.horiz ? 0 : i);
            int c = s.col + (s.horiz ? i : 0);
            if (r >= 0 && r < GRID && c >= 0 && c < GRID) {
                int idx = r * GRID + c;
                if (grid_objs_[0][idx])
                    lv_obj_set_style_bg_color(grid_objs_[0][idx], lv_color_hex(0x44aaff), 0);
            }
        }
    }
}

// ── Cell drawing ──

void Battleship::draw_cell(lv_obj_t* obj, Cell c, bool show_ships) {
    switch (c) {
        case EMPTY:
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x1a2a4a), 0);  // dark blue water
            break;
        case SHIP:
            if (show_ships)
                lv_obj_set_style_bg_color(obj, lv_color_hex(0x666666), 0);  // gray ship
            else
                lv_obj_set_style_bg_color(obj, lv_color_hex(0x1a2a4a), 0);  // hidden
            break;
        case MISS:
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x334466), 0);  // lighter blue
            break;
        case HIT:
            lv_obj_set_style_bg_color(obj, lv_color_hex(0xe94560), 0);  // red
            break;
        case SUNK:
            lv_obj_set_style_bg_color(obj, lv_color_hex(0x8b0000), 0);  // dark red
            break;
    }
}

void Battleship::refresh_grids(int attacker) {
    int defender = attacker ^ 1;
    // Left grid = attacker's own board (show ships)
    for (int i = 0; i < GRID * GRID; i++) {
        if (grid_objs_[0][i])
            draw_cell(grid_objs_[0][i], board_[attacker][i], true);
    }
    // Right grid = defender's board (hide ships)
    for (int i = 0; i < GRID * GRID; i++) {
        if (grid_objs_[1][i])
            draw_cell(grid_objs_[1][i], board_[defender][i], false);
    }
}

void Battleship::send_json(const char* buf) {
    discovery_send_game_data(peer_ip_, buf);
}

// ── CPU AI ──

void Battleship::cpu_place_ships() {
    for (int s = 0; s < NUM_SHIPS; s++) {
        int attempts = 0;
        while (attempts < 200) {
            int row = esp_random() % GRID;
            int col = esp_random() % GRID;
            bool horiz = (esp_random() % 2) == 0;
            if (can_place(1, s, row, col, horiz)) {
                do_place(1, s, row, col, horiz);
                break;
            }
            attempts++;
        }
    }
}

int Battleship::cpu_pick_target() {
    // Target mode: try cells from hunt stack
    while (cpu_hunt_top_ > 0) {
        int target = cpu_hunt_stack_[--cpu_hunt_top_];
        Cell c = board_[0][target];
        if (c == EMPTY || c == SHIP) return target;
    }
    // Hunt mode: random untargeted cell
    int tries = 0;
    while (tries < 200) {
        int t = esp_random() % (GRID * GRID);
        Cell c = board_[0][t];
        if (c == EMPTY || c == SHIP) return t;
        tries++;
    }
    // Fallback: first available
    for (int i = 0; i < GRID * GRID; i++) {
        Cell c = board_[0][i];
        if (c == EMPTY || c == SHIP) return i;
    }
    return 0;
}

// ── Mode selection ──

void Battleship::mode_cpu_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_CPU;
    s_self->reset_game();
    s_self->cpu_place_ships();
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_placement(0);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Battleship::mode_local_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOCAL;
    s_self->reset_game();
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    lv_obj_t* scr = s_self->create_placement(0);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void Battleship::mode_online_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->phase_ = PHASE_LOBBY;
    discovery_set_game("battleship", "waiting");
    discovery_on_invite(bs_on_invite);
    discovery_on_accept(bs_on_accept);
    discovery_on_game_data(bs_on_game_data);
    lv_obj_t* scr = s_self->create_lobby();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

lv_obj_t* Battleship::create_mode_select() {
    phase_ = PHASE_MODE_SELECT;
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Battleship");
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

// ── Lobby ──

lv_obj_t* Battleship::create_lobby() {
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);
    lv_obj_t* title = ui_create_title(scr, "Battleship - Find Opponent");
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

// ── Ship placement ──

// Store absolute grid positions for reliable touch detection
static int grid_abs_x_[2] = {};
static int grid_abs_y_[2] = {};

static lv_obj_t* build_grid(lv_obj_t* parent, int x, int y, int grid_idx,
                             lv_obj_t* objs[Battleship::GRID * Battleship::GRID],
                             lv_event_cb_t cb, void* user_data) {
    grid_abs_x_[grid_idx] = x;
    grid_abs_y_[grid_idx] = y;
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, GRID_W, GRID_W);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x0a1a3a), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 4, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    if (cb) {
        lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(panel, cb, LV_EVENT_CLICKED, user_data);
    }

    for (int r = 0; r < Battleship::GRID; r++) {
        for (int c = 0; c < Battleship::GRID; c++) {
            int idx = r * Battleship::GRID + c;
            lv_obj_t* cell = lv_obj_create(panel);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, CELL_SZ - 2, CELL_SZ - 2);
            lv_obj_set_pos(cell, c * CELL_SZ + 1, r * CELL_SZ + 1);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x1a2a4a), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_radius(cell, 1, 0);
            lv_obj_clear_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
            if (objs) objs[idx] = cell;
        }
    }
    return panel;
}

void Battleship::place_grid_cb(lv_event_t* e) {
    if (!s_self || s_self->phase_ != PHASE_PLACE) return;
    lv_point_t pt;
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_indev_get_point(indev, &pt);

    int col = (pt.x - grid_abs_x_[0]) / CELL_SZ;
    int row = (pt.y - grid_abs_y_[0]) / CELL_SZ;
    if (row < 0 || row >= GRID || col < 0 || col >= GRID) return;

    int player = (s_self->mode_ == MODE_LOCAL) ? s_self->current_player_ : 0;

    // If no ship selected, check if tapping an existing ship to pick it up
    if (s_self->selected_ship_ < 0) {
        int tapped = s_self->find_ship_at(player, row, col);
        if (tapped >= 0) {
            s_self->selected_ship_ = tapped;
            s_self->place_horiz_ = s_self->ships_[player][tapped].horiz;
            sound_move();
            s_self->refresh_placement_grid(player);
            if (s_self->lbl_status_) {
                char buf[40];
                snprintf(buf, sizeof(buf), "Ship (%d) selected",
                         s_self->ships_[player][tapped].size);
                lv_label_set_text(s_self->lbl_status_, buf);
            }
            return;
        }
        // Place the next unplaced ship
        int si = s_self->place_ship_idx_;
        if (si >= NUM_SHIPS) return;
        // Clamp so ship stays in bounds
        int sz = ship_sizes_[si];
        if (s_self->place_horiz_ && col + sz > GRID) col = GRID - sz;
        if (!s_self->place_horiz_ && row + sz > GRID) row = GRID - sz;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        s_self->set_ship_pos(player, si, row, col, s_self->place_horiz_);
        sound_move();
        s_self->place_ship_idx_++;
    } else {
        // Move selected ship to tapped location (allow overlap)
        int si = s_self->selected_ship_;
        int sz = ship_sizes_[si];
        // Clamp to grid bounds
        if (s_self->place_horiz_ && col + sz > GRID) col = GRID - sz;
        if (!s_self->place_horiz_ && row + sz > GRID) row = GRID - sz;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        s_self->set_ship_pos(player, si, row, col, s_self->place_horiz_);
        sound_move();
        s_self->selected_ship_ = -1;  // deselect after move
    }

    s_self->refresh_placement_grid(player);

    if (s_self->place_ship_idx_ >= NUM_SHIPS && s_self->ships_placed_[player] >= NUM_SHIPS) {
        if (s_self->lbl_status_)
            lv_label_set_text(s_self->lbl_status_, "Tap Done or tap ships to edit");
    } else if (s_self->place_ship_idx_ < NUM_SHIPS) {
        char buf[40];
        snprintf(buf, sizeof(buf), "Place ship (%d)",
                 ship_sizes_[s_self->place_ship_idx_]);
        if (s_self->lbl_status_)
            lv_label_set_text(s_self->lbl_status_, buf);
    }
}

void Battleship::rotate_cb(lv_event_t*) {
    if (!s_self) return;
    int player = (s_self->mode_ == MODE_LOCAL) ? s_self->current_player_ : 0;

    if (s_self->selected_ship_ >= 0) {
        // Rotate selected ship clockwise, clamp to stay in bounds
        int si = s_self->selected_ship_;
        Ship& sh = s_self->ships_[player][si];
        bool new_horiz = !sh.horiz;
        int row = sh.row, col = sh.col;
        int sz = sh.size;
        // Clamp anchor so ship stays in grid
        if (new_horiz && col + sz > GRID) col = GRID - sz;
        if (!new_horiz && row + sz > GRID) row = GRID - sz;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        s_self->set_ship_pos(player, si, row, col, new_horiz);
        s_self->place_horiz_ = new_horiz;
        sound_move();
        s_self->refresh_placement_grid(player);
    } else {
        s_self->place_horiz_ = !s_self->place_horiz_;
    }
}

void Battleship::done_place_cb(lv_event_t*) {
    if (!s_self) return;
    int player = (s_self->mode_ == MODE_LOCAL) ? s_self->current_player_ : 0;
    if (s_self->ships_placed_[player] < NUM_SHIPS) {
        if (s_self->lbl_status_)
            lv_label_set_text(s_self->lbl_status_, "Place all ships first!");
        return;
    }
    if (!s_self->validate_placement(player)) {
        if (s_self->lbl_status_)
            lv_label_set_text(s_self->lbl_status_, "Fix overlaps first!");
        return;
    }
    s_self->selected_ship_ = -1;
    s_self->rebuild_board_from_ships(player);

    if (s_self->mode_ == MODE_LOCAL) {
        if (s_self->current_player_ == 0) {
            // P1 done, hand off to P2
            s_self->current_player_ = 1;
            s_self->place_ship_idx_ = 0;
            s_self->place_horiz_ = true;
            lv_obj_t* scr = s_self->create_handoff(1);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            // Both placed, start battle — P1 goes first
            s_self->current_player_ = 0;
            lv_obj_t* scr = s_self->create_handoff(0);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        }
    } else if (s_self->mode_ == MODE_CPU) {
        // Start battle vs CPU
        s_self->my_turn_ = true;
        lv_obj_t* scr = s_self->create_battle(0);
        lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
        s_self->screen_ = scr;
    } else if (s_self->mode_ == MODE_NETWORK) {
        // Send board to peer, wait for them or start battle
        StaticJsonDocument<128> doc;
        doc["type"] = "move"; doc["game"] = "battleship";
        doc["a"] = "ready";
        char buf[128];
        serializeJson(doc, buf, sizeof(buf));
        s_self->send_json(buf);

        if (s_self->ships_placed_[1] > 0) {
            // Opponent already ready
            s_self->my_turn_ = s_self->is_host_;
            s_self->phase_ = PHASE_BATTLE;
            lv_obj_t* scr = s_self->create_battle(0);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            // Wait for opponent
            s_self->phase_ = PHASE_WAIT;
            lv_obj_t* scr = ui_create_screen();
            lv_obj_t* lbl = lv_label_create(scr);
            lv_label_set_text(lbl, "Waiting for opponent\nto place ships...");
            lv_obj_set_style_text_color(lbl, UI_COLOR_DIM, 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_center(lbl);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        }
    }
}

lv_obj_t* Battleship::create_placement(int player) {
    phase_ = PHASE_PLACE;
    place_ship_idx_ = 0;
    place_horiz_ = true;

    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    char tbuf[30];
    if (mode_ == MODE_LOCAL)
        snprintf(tbuf, sizeof(tbuf), "P%d - Place Ships", player + 1);
    else
        snprintf(tbuf, sizeof(tbuf), "Place Your Ships");
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, tbuf);
    lv_obj_set_style_text_color(title, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 6);

    lbl_status_ = lv_label_create(scr);
    char sbuf[30];
    snprintf(sbuf, sizeof(sbuf), "Place ship (%d)", ship_sizes_[0]);
    lv_label_set_text(lbl_status_, sbuf);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_TOP_MID, 0, 24);

    // Single grid centered for placement
    int cx = (320 - GRID_W) / 2;
    memset(grid_objs_, 0, sizeof(grid_objs_));
    grid_panels_[0] = build_grid(scr, cx, GRID_Y, 0, grid_objs_[0],
                                  place_grid_cb, this);

    // Show already-placed ships
    for (int i = 0; i < GRID * GRID; i++) {
        draw_cell(grid_objs_[0][i], board_[player][i], true);
    }

    // Rotate button
    lv_obj_t* rot = ui_create_btn(scr, LV_SYMBOL_LOOP " Rotate", 90, 30);
    lv_obj_align(rot, LV_ALIGN_BOTTOM_LEFT, 20, -8);
    lv_obj_add_event_cb(rot, rotate_cb, LV_EVENT_CLICKED, NULL);

    // Done button
    lv_obj_t* done = ui_create_btn(scr, "Done", 70, 30);
    lv_obj_align(done, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
    lv_obj_add_event_cb(done, done_place_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

// ── Handoff (local pass-and-play) ──

lv_obj_t* Battleship::create_handoff(int next_player) {
    phase_ = PHASE_HANDOFF;
    lv_obj_t* scr = ui_create_screen();

    char buf[40];
    bool placing = ships_placed_[next_player] < NUM_SHIPS;
    if (placing)
        snprintf(buf, sizeof(buf), "Pass to Player %d\nto place ships", next_player + 1);
    else
        snprintf(buf, sizeof(buf), "Pass to Player %d\nto fire!", next_player + 1);

    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -20);

    lv_obj_t* btn = ui_create_btn(scr, "Ready", 120, 40);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 40);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        if (!s_self) return;
        int np = s_self->current_player_;
        if (s_self->ships_placed_[np] < NUM_SHIPS) {
            lv_obj_t* scr = s_self->create_placement(np);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        } else {
            lv_obj_t* scr = s_self->create_battle(np);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        }
    }, LV_EVENT_CLICKED, NULL);

    return scr;
}

// ── Battle ──

void Battleship::battle_grid_cb(lv_event_t* e) {
    if (!s_self || s_self->game_done_) return;
    if (s_self->phase_ != PHASE_BATTLE) return;

    // Only the right grid (enemy board, grid index 1) is clickable
    lv_point_t pt;
    lv_indev_t* indev = lv_indev_get_act();
    if (!indev) return;
    lv_indev_get_point(indev, &pt);

    int col = (pt.x - grid_abs_x_[1]) / CELL_SZ;
    int row = (pt.y - grid_abs_y_[1]) / CELL_SZ;
    if (row < 0 || row >= GRID || col < 0 || col >= GRID) return;

    int attacker, defender;
    if (s_self->mode_ == MODE_LOCAL) {
        attacker = s_self->current_player_;
        defender = attacker ^ 1;
    } else {
        attacker = 0;
        defender = 1;
        if (!s_self->my_turn_) return;
    }

    int idx = row * GRID + col;
    Cell c = s_self->board_[defender][idx];
    if (c == HIT || c == MISS || c == SUNK) return;  // already targeted

    int sunk = -1;
    Cell result = s_self->fire(defender, row, col, &sunk);
    sound_move();
    s_self->refresh_grids(attacker);

    if (s_self->all_sunk(defender)) {
        s_self->game_done_ = true;
        if (s_self->mode_ == MODE_NETWORK) {
            // Send final shot to peer (game-ending — no need for ack/retry,
            // but we tag it with mc for consistency).
            s_self->net_pending_mc_ = s_self->net_mc_ + 1;
            StaticJsonDocument<160> doc;
            doc["type"] = "move"; doc["game"] = "battleship";
            doc["a"] = "fire";
            doc["r"] = row; doc["c"] = col;
            doc["mc"] = s_self->net_pending_mc_;
            serializeJson(doc, s_self->net_last_move_, sizeof(s_self->net_last_move_));
            s_self->send_json(s_self->net_last_move_);
        }
        int winner = (s_self->mode_ == MODE_LOCAL) ? attacker : 0;
        s_self->delayed_gameover(winner, row, col, 1);  // highlight on right grid (enemy)
        return;
    }

    if (s_self->mode_ == MODE_CPU) {
        if (result == HIT || result == SUNK) {
            // Player gets another turn on hit
            if (s_self->lbl_status_)
                lv_label_set_text(s_self->lbl_status_, sunk >= 0 ? "Sunk! Fire again!" : "Hit! Fire again!");
        } else {
            // CPU's turn
            s_self->my_turn_ = false;
            s_self->cpu_pending_ = true;
            s_self->cpu_think_time_ = millis();
            if (s_self->lbl_status_)
                lv_label_set_text(s_self->lbl_status_, "CPU thinking...");
        }
    } else if (s_self->mode_ == MODE_LOCAL) {
        if (result == HIT || result == SUNK) {
            if (s_self->lbl_status_)
                lv_label_set_text(s_self->lbl_status_, sunk >= 0 ? "Sunk! Fire again!" : "Hit! Fire again!");
        } else {
            // Switch players
            s_self->current_player_ ^= 1;
            lv_obj_t* scr = s_self->create_handoff(s_self->current_player_);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            s_self->screen_ = scr;
        }
    } else if (s_self->mode_ == MODE_NETWORK) {
        // Send shot to opponent. We don't advance net_mc_ yet — it only
        // advances when the result comes back. net_pending_mc_ tracks the
        // in-flight fire so the heartbeat tick can retry it on loss.
        s_self->net_pending_mc_ = s_self->net_mc_ + 1;
        StaticJsonDocument<160> doc;
        doc["type"] = "move"; doc["game"] = "battleship";
        doc["a"] = "fire";
        doc["r"] = row; doc["c"] = col;
        doc["mc"] = s_self->net_pending_mc_;
        serializeJson(doc, s_self->net_last_move_, sizeof(s_self->net_last_move_));
        s_self->send_json(s_self->net_last_move_);

        if (result == HIT || result == SUNK) {
            if (s_self->lbl_status_)
                lv_label_set_text(s_self->lbl_status_, sunk >= 0 ? "Sunk! Fire again!" : "Hit! Fire again!");
        } else {
            s_self->my_turn_ = false;
            s_self->phase_ = PHASE_WAIT;
            if (s_self->lbl_status_)
                lv_label_set_text(s_self->lbl_status_, "Opponent's turn...");
        }
    }
}

lv_obj_t* Battleship::create_battle(int attacker) {
    phase_ = PHASE_BATTLE;
    lv_obj_t* scr = ui_create_screen();
    ui_create_back_btn(scr);

    // Labels above grids (clear of back button)
    lv_obj_t* my_lbl = lv_label_create(scr);
    lv_label_set_text(my_lbl, "Yours");
    lv_obj_set_style_text_color(my_lbl, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(my_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(my_lbl, GRID_L_X + GRID_W / 2 - 16, GRID_Y - 16);

    lv_obj_t* en_lbl = lv_label_create(scr);
    lv_label_set_text(en_lbl, "Enemy");
    lv_obj_set_style_text_color(en_lbl, UI_COLOR_ACCENT, 0);
    lv_obj_set_style_text_font(en_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(en_lbl, GRID_R_X + GRID_W / 2 - 18, GRID_Y - 16);

    lbl_status_ = lv_label_create(scr);
    lv_obj_set_style_text_color(lbl_status_, UI_COLOR_TEXT, 0);
    lv_obj_set_style_text_font(lbl_status_, &lv_font_montserrat_12, 0);
    lv_obj_align(lbl_status_, LV_ALIGN_BOTTOM_MID, 0, -6);

    if (mode_ == MODE_LOCAL) {
        char sbuf[20];
        snprintf(sbuf, sizeof(sbuf), "P%d - Fire!", attacker + 1);
        lv_label_set_text(lbl_status_, sbuf);
    } else if (my_turn_) {
        lv_label_set_text(lbl_status_, "Your turn - fire!");
    } else {
        lv_label_set_text(lbl_status_, "Opponent's turn...");
    }

    // Left grid: own board (no click)
    memset(grid_objs_, 0, sizeof(grid_objs_));
    grid_panels_[0] = build_grid(scr, GRID_L_X, GRID_Y, 0, grid_objs_[0],
                                  nullptr, nullptr);
    // Right grid: enemy board (clickable)
    grid_panels_[1] = build_grid(scr, GRID_R_X, GRID_Y, 1, grid_objs_[1],
                                  battle_grid_cb, this);

    refresh_grids(attacker);

    // Ships remaining counter
    int defender = attacker ^ 1;
    lv_obj_t* ships_lbl = lv_label_create(scr);
    char rbuf[30];
    snprintf(rbuf, sizeof(rbuf), "Enemy ships: %d/%d", ships_alive_[defender], NUM_SHIPS);
    lv_label_set_text(ships_lbl, rbuf);
    lv_obj_set_style_text_color(ships_lbl, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(ships_lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_pos(ships_lbl, GRID_R_X, GRID_Y + GRID_W + 4);

    return scr;
}

// ── Game Over ──

static int pending_winner_ = 0;

void Battleship::delayed_gameover(int winner, int hit_row, int hit_col, int grid_side) {
    // Highlight the winning cell with a flashing bright color
    int idx = hit_row * GRID + hit_col;
    if (grid_objs_[grid_side][idx]) {
        lv_obj_set_style_bg_color(grid_objs_[grid_side][idx], lv_color_hex(0xffff00), 0);  // yellow flash
    }
    if (lbl_status_) {
        lv_label_set_text(lbl_status_, winner == 0 ? "Final blow!" : "You've been sunk!");
    }

    // Play the sound immediately
    if (winner == 0) sound_win(); else sound_lose();

    // Show gameover overlay after 2 seconds
    pending_winner_ = winner;
    lv_timer_create([](lv_timer_t* t) {
        lv_timer_del(t);
        if (s_self) s_self->show_gameover(pending_winner_);
    }, 2000, NULL);
}

void Battleship::show_gameover(int winner) {
    phase_ = PHASE_GAMEOVER;

    const char* text;
    bool is_win;
    if (mode_ == MODE_LOCAL) {
        static char local_buf[20];
        snprintf(local_buf, sizeof(local_buf), "Player %d Wins!", winner + 1);
        text = local_buf;
        is_win = true;
    } else {
        is_win = (winner == 0);
        text = is_win ? "You Win!" : "You Lose!";
    }

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

    lv_obj_t* btn = ui_create_btn(overlay, "Menu", 100, 36);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 30);
    lv_obj_add_event_cb(btn, [](lv_event_t*) {
        screen_manager_back_to_menu();
    }, LV_EVENT_CLICKED, NULL);
}

// ── Network ──

void Battleship::onNetworkData(const char* json) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, json)) return;
    const char* game = doc["game"];
    if (!game || strcmp(game, "battleship") != 0) return;

    if (doc["abandon"] | false) {
        game_done_ = true;
        show_gameover(0);  // opponent left, we win
        return;
    }

    const char* action = doc["a"];
    if (!action) return;

    // Heartbeat: if peer is behind our state, resend our last outgoing.
    if (strcmp(action, "hb") == 0) {
        uint32_t peer_mc = doc["mc"] | 0;
        if (peer_mc < net_mc_ && net_last_move_[0]) {
            send_json(net_last_move_);
        }
        return;
    }

    if (strcmp(action, "ready") == 0) {
        // Opponent has placed ships — mark as placed.
        ships_placed_[1] = NUM_SHIPS;
        ships_alive_[1] = NUM_SHIPS;

        if (phase_ == PHASE_WAIT) {
            // We were waiting for opponent — start battle
            my_turn_ = is_host_;
            phase_ = PHASE_BATTLE;
            lv_obj_t* scr = create_battle(0);
            lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
            screen_ = scr;
        }
    }
    else if (strcmp(action, "fire") == 0) {
        uint32_t peer_mc = doc["mc"] | 0;

        // Duplicate fire (attacker retried) — resend our cached result.
        if (peer_mc != 0 && peer_mc == net_mc_ && net_last_move_[0]) {
            send_json(net_last_move_);
            return;
        }
        // Strict +1: reject gaps and out-of-order.
        if (peer_mc != net_mc_ + 1) return;

        int row = doc["r"] | -1;
        int col = doc["c"] | -1;
        if (row < 0 || col < 0 || row >= GRID || col >= GRID) return;

        sound_opponent_move();
        int sunk = -1;
        Cell result = fire(0, row, col, &sunk);
        net_mc_ = peer_mc;

        // Send (and cache) result back with matching mc.
        StaticJsonDocument<160> rdoc;
        rdoc["type"] = "move"; rdoc["game"] = "battleship";
        rdoc["a"] = "result";
        rdoc["r"] = row; rdoc["c"] = col;
        rdoc["hit"] = (result == HIT || result == SUNK);
        rdoc["sunk"] = (result == SUNK);
        rdoc["mc"] = net_mc_;
        serializeJson(rdoc, net_last_move_, sizeof(net_last_move_));
        send_json(net_last_move_);

        if (all_sunk(0)) {
            game_done_ = true;
            if (grid_objs_[0][0]) refresh_grids(0);
            delayed_gameover(1, row, col, 0);
            return;
        }

        if (result == HIT || result == SUNK) {
            phase_ = PHASE_WAIT;
        } else {
            my_turn_ = true;
            phase_ = PHASE_BATTLE;
        }

        if (grid_objs_[0][0]) refresh_grids(0);
        if (lbl_status_) {
            lv_label_set_text(lbl_status_, my_turn_ ? "Your turn - fire!" : "Opponent's turn...");
        }
    }
    else if (strcmp(action, "result") == 0) {
        uint32_t peer_mc = doc["mc"] | 0;
        // Result must match our in-flight fire (net_pending_mc_).
        if (peer_mc == 0 || peer_mc != net_pending_mc_) return;

        int row = doc["r"] | -1;
        int col = doc["c"] | -1;
        bool hit = doc["hit"] | false;
        bool sunk = doc["sunk"] | false;
        if (row < 0 || col < 0) return;

        int idx = row * GRID + col;
        if (sunk) {
            board_[1][idx] = SUNK;
            ships_alive_[1]--;
        } else if (hit) {
            board_[1][idx] = HIT;
        } else {
            board_[1][idx] = MISS;
        }

        // Fire is now confirmed — advance mc, clear pending.
        net_mc_ = peer_mc;
        net_pending_mc_ = 0;

        if (grid_objs_[1][0]) refresh_grids(0);

        if (all_sunk(1)) {
            game_done_ = true;
            delayed_gameover(0, row, col, 1);
            return;
        }

        if (hit || sunk) {
            my_turn_ = true;
            phase_ = PHASE_BATTLE;
            if (lbl_status_)
                lv_label_set_text(lbl_status_, sunk ? "Sunk! Fire again!" : "Hit! Fire again!");
        } else {
            my_turn_ = false;
            phase_ = PHASE_WAIT;
            if (lbl_status_)
                lv_label_set_text(lbl_status_, "Opponent's turn...");
        }
    }
}
