#pragma once
#include <lvgl.h>
#include "discovery.h"

// Shared mode-select / lobby / invite-accept "shell" used by every 2-player
// game. Singleton, like discovery.h itself — only one game's mode-select or
// lobby is ever on screen at a time (screen_manager only shows one screen).
// Scope is strictly the pre-game handshake: it does not touch GameBase's
// net_mc_/heartbeat resync machinery or any game's own in-game move-sync
// protocol (those stay exactly as each game already implements them).

struct MpShellConfig {
    const char* game_id;       // discovery_set_game() + Peer.game match + JSON "game" field, e.g. "connect4"
    const char* display_name;  // mode-select title, invite msgbox title, lobby title, e.g. "Connect 4"
    bool show_cpu_button;      // false for games with no CPU AI (Pong, Pictionary)
    bool show_idle_peers;      // true only for Battleship: also show peers not yet announcing ANY game
};

// Fired once the network role is settled; peer.ip is the confirmed remote
// address. The game sets its own peer_ip_/my_turn_/color fields and switches
// to its own board screen — the shell does not auto-switch screens.
typedef void (*MpRoleCb)(const Peer& peer);

// Forwarded verbatim for in-game "move" messages, once past the handshake —
// same payload every game's onNetworkData() already expects.
typedef void (*MpDataCb)(const char* json);

// Builds "vs CPU / Local (2P) / Network (2P)" (2 or 3 buttons depending on
// cfg.show_cpu_button) with a back button + title.
//
// If `into` is null (the common case), creates and returns a new screen via
// ui_create_screen() — the pattern every game except Pictionary uses. If
// `into` is non-null, builds the buttons as children of that object instead
// and returns it unchanged — for Pictionary's single-persistent-screen
// model, which rebuilds its one `screen_` in place via lv_obj_clean()
// rather than switching screens.
lv_obj_t* mp_create_mode_select(const MpShellConfig& cfg,
                                 lv_event_cb_t cpu_cb,
                                 lv_event_cb_t local_cb,
                                 lv_event_cb_t online_cb,
                                 lv_obj_t* into = nullptr);

// Enters the lobby: registers the shell's internal discovery trampolines,
// calls discovery_set_game(cfg.game_id, "waiting"), builds the peer list +
// hint (same `into`-or-new-screen rule as mp_create_mode_select). Copies
// cfg by value internally, so the caller does not need a static config.
//
// on_host_ready fires when OUR invite gets accepted (we go first).
// on_guest_ready fires when WE accept someone else's invite (we go second).
// on_data forwards in-game "move" messages once past the handshake.
// on_invite_failed (nullable) fires on explicit decline or handshake
// timeout — pass nullptr to skip registering it (matches Battleship today).
lv_obj_t* mp_shell_host_lobby(const MpShellConfig& cfg,
                               MpRoleCb on_host_ready,
                               MpRoleCb on_guest_ready,
                               MpDataCb on_data,
                               MpRoleCb on_invite_failed,
                               lv_obj_t* into = nullptr);

// Call once per ~2s tick from update() while the lobby is showing (mirrors
// the per-game `if (mode_==MODE_LOBBY && lobby_list_) {...}` block every
// game used to have). No-op if the lobby isn't currently active.
void mp_shell_lobby_tick();

// Lets a role callback show status text in place of the peer list, for
// games with a wait-for-host-data step before the board is playable
// (Memory Match's board sync, Pictionary's round setup).
void mp_shell_set_status(const char* text);

// Cleanup: closes any open invite popup, sends {"abandon":true} to peer_ip
// if send_abandon is true, then discovery_clear_game() + clears all 4
// discovery callback slots. Call from destroy() and from mode_cpu_cb/
// mode_local_cb (with send_abandon=false) to guarantee a clean slate.
void mp_shell_end(IPAddress peer_ip, bool send_abandon);
