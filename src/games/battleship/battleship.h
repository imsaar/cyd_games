#pragma once
#include "../game_base.h"
#include "../../net/discovery.h"

class Battleship : public GameBase {
    friend void bs_on_invite(const Peer& from);
    friend void bs_on_accept(const Peer& from);
    friend void bs_on_game_data(const char* json);
    friend void bs_lobby_peer_cb(lv_event_t* e);

public:
    lv_obj_t* createScreen() override;
    void update() override;
    void destroy() override;
    void onNetworkData(const char* json) override;

    const char* name() const override { return "Battleship"; }
    uint8_t maxPlayers() const override { return 2; }

    static const int GRID = 8;
    static const int NUM_SHIPS = 5;

    enum Cell : int8_t {
        EMPTY = 0, SHIP = 1, HIT = 2, MISS = 3, SUNK = 4
    };

private:
    enum Mode { MODE_SELECT, MODE_CPU, MODE_LOCAL, MODE_LOBBY, MODE_NETWORK };
    enum Phase {
        PHASE_MODE_SELECT, PHASE_LOBBY,
        PHASE_PLACE,       // placing ships
        PHASE_HANDOFF,     // local: "pass to Player X" screen
        PHASE_BATTLE,      // firing at opponent
        PHASE_WAIT,        // network: waiting for opponent's shot
        PHASE_GAMEOVER
    };

    struct Ship { int size; int row; int col; bool horiz; int hits; };

    // Boards: [0] = player 1 / self, [1] = player 2 / opponent
    Cell board_[2][GRID * GRID] = {};
    Ship ships_[2][NUM_SHIPS] = {};
    int  ships_placed_[2] = {};
    int  ships_alive_[2] = {};
    static const int ship_sizes_[NUM_SHIPS];

    // Current placement state
    int place_ship_idx_ = 0;
    bool place_horiz_ = true;
    int selected_ship_ = -1;  // index of selected placed ship (-1 = none)

    void remove_ship(int player, int ship_idx);
    int  find_ship_at(int player, int row, int col);
    void highlight_ship(int player, int ship_idx);
    void refresh_placement_grid(int player);
    bool validate_placement(int player);
    void rebuild_board_from_ships(int player);
    void set_ship_pos(int player, int ship_idx, int row, int col, bool horiz);

    // Game state
    Mode mode_ = MODE_SELECT;
    Phase phase_ = PHASE_MODE_SELECT;
    int current_player_ = 0;  // 0 or 1 (for local pass-and-play)
    bool my_turn_ = true;
    bool game_done_ = false;
    bool is_host_ = false;
    IPAddress peer_ip_;

    // CPU AI
    bool cpu_pending_ = false;
    uint32_t cpu_think_time_ = 0;
    int cpu_hunt_stack_[GRID * GRID];
    int cpu_hunt_top_ = 0;
    int cpu_pick_target();
    void cpu_place_ships();

    // UI
    lv_obj_t* screen_ = nullptr;
    lv_obj_t* lbl_status_ = nullptr;
    lv_obj_t* lobby_list_ = nullptr;
    lv_obj_t* grid_objs_[2][GRID * GRID] = {};
    lv_obj_t* grid_panels_[2] = {};

    // Screen builders
    lv_obj_t* create_mode_select();
    lv_obj_t* create_lobby();
    lv_obj_t* create_placement(int player);
    lv_obj_t* create_handoff(int next_player);
    lv_obj_t* create_battle(int attacker);
    void show_gameover(int winner);
    void delayed_gameover(int winner, int hit_row, int hit_col, int grid_side);

    // Helpers
    void reset_game();
    bool can_place(int player, int ship_idx, int row, int col, bool horiz);
    void do_place(int player, int ship_idx, int row, int col, bool horiz);
    Cell fire(int defender, int row, int col, int* sunk_ship);
    bool all_sunk(int player);
    void draw_cell(lv_obj_t* obj, Cell c, bool show_ships);
    void refresh_grids(int attacker);
    void send_json(const char* buf);

    static void place_grid_cb(lv_event_t* e);
    static void battle_grid_cb(lv_event_t* e);
    static void rotate_cb(lv_event_t* e);
    static void done_place_cb(lv_event_t* e);
    static void mode_cpu_cb(lv_event_t* e);
    static void mode_local_cb(lv_event_t* e);
    static void mode_online_cb(lv_event_t* e);
};
