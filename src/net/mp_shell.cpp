#include "mp_shell.h"
#include "../ui/ui_common.h"
#include <string.h>
#include <stdio.h>

static MpShellConfig g_cfg = { "", "", false, false };
static lv_obj_t* g_lobby_list = nullptr;
static lv_obj_t* g_invite_msgbox = nullptr;
static IPAddress g_pending_ip;
static Peer g_pending_peer;
static MpRoleCb g_on_host_ready = nullptr;
static MpRoleCb g_on_guest_ready = nullptr;
static MpRoleCb g_on_invite_failed = nullptr;
static MpDataCb g_on_data = nullptr;
static bool g_lobby_active = false;
static uint32_t g_last_lobby_refresh = 0;

// ── Discovery trampolines (the shell's own callbacks; it dispatches to
// whichever game most recently called mp_shell_host_lobby) ──

static void mps_on_invite(const Peer& from) {
    if (!g_lobby_active) return;
    if (g_invite_msgbox) return;

    g_pending_peer = from;
    g_pending_ip = from.ip;

    static char title_buf[40];
    snprintf(title_buf, sizeof(title_buf), "%s Invite", g_cfg.display_name);
    static const char* btns[] = { "Accept", "Decline", "" };
    g_invite_msgbox = lv_msgbox_create(NULL, title_buf, from.name, btns, false);
    lv_obj_set_size(g_invite_msgbox, 240, 140);
    lv_obj_center(g_invite_msgbox);
    lv_obj_set_style_bg_color(g_invite_msgbox, UI_COLOR_CARD, 0);
    lv_obj_set_style_text_color(g_invite_msgbox, UI_COLOR_TEXT, 0);

    lv_obj_t* btnm = lv_msgbox_get_btns(g_invite_msgbox);
    lv_obj_add_event_cb(btnm, [](lv_event_t* e) {
        uint16_t btn_id = lv_msgbox_get_active_btn(g_invite_msgbox);
        if (btn_id == 0) {
            discovery_send_accept(g_pending_ip);
            lv_msgbox_close(g_invite_msgbox);
            g_invite_msgbox = nullptr;
            g_lobby_active = false;
            discovery_set_game(g_cfg.game_id, "playing");
            if (g_on_guest_ready) g_on_guest_ready(g_pending_peer);
        } else {
            discovery_send_decline(g_pending_ip);
            lv_msgbox_close(g_invite_msgbox);
            g_invite_msgbox = nullptr;
        }
    }, LV_EVENT_CLICKED, NULL);
}

static void mps_on_accept(const Peer& from) {
    if (!g_lobby_active) return;
    g_lobby_active = false;
    discovery_set_game(g_cfg.game_id, "playing");
    if (g_on_host_ready) g_on_host_ready(from);
}

static void mps_on_game_data(const char* json) {
    if (g_lobby_active) return; // not past the handshake yet — game board may not exist
    if (g_on_data) g_on_data(json);
}

static void mps_on_invite_failed(const Peer& from) {
    if (g_on_invite_failed) g_on_invite_failed(from);
}

// A peer qualifies for the lobby list if it's announcing this game and
// isn't already mid-match with someone else, or — for Battleship only — if
// it's not announcing any game at all yet.
static bool peer_matches(const Peer& p) {
    if (strcmp(p.game, g_cfg.game_id) == 0 && strcmp(p.state, "playing") != 0) return true;
    if (g_cfg.show_idle_peers && p.game[0] == '\0') return true;
    return false;
}

static void lobby_peer_cb(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const Peer* peers = discovery_get_peers();
    int count = discovery_peer_count();
    if (idx < 0 || idx >= count) return;
    discovery_send_invite(peers[idx].ip);
    if (g_lobby_list) {
        lv_obj_clean(g_lobby_list);
        lv_list_add_text(g_lobby_list, "Invite sent, waiting...");
    }
}

// ── Public API ──

lv_obj_t* mp_create_mode_select(const MpShellConfig& cfg,
                                 lv_event_cb_t cpu_cb,
                                 lv_event_cb_t local_cb,
                                 lv_event_cb_t online_cb,
                                 lv_obj_t* into) {
    lv_obj_t* scr = into;
    if (!scr) {
        scr = ui_create_screen();
        ui_create_back_btn(scr);
    }
    lv_obj_t* title = ui_create_title(scr, cfg.display_name);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    int start_y = cfg.show_cpu_button ? -50 : -25;
    int i = 0;

    if (cfg.show_cpu_button) {
        lv_obj_t* b = ui_create_btn(scr, "vs CPU", 140, 42);
        lv_obj_align(b, LV_ALIGN_CENTER, 0, start_y + i * 50);
        lv_obj_add_event_cb(b, cpu_cb, LV_EVENT_CLICKED, NULL);
        i++;
    }

    lv_obj_t* bl = ui_create_btn(scr, "Local (2P)", 140, 42);
    lv_obj_align(bl, LV_ALIGN_CENTER, 0, start_y + i * 50);
    lv_obj_add_event_cb(bl, local_cb, LV_EVENT_CLICKED, NULL);
    i++;

    lv_obj_t* bn = ui_create_btn(scr, "Network (2P)", 140, 42);
    lv_obj_align(bn, LV_ALIGN_CENTER, 0, start_y + i * 50);
    lv_obj_add_event_cb(bn, online_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

lv_obj_t* mp_shell_host_lobby(const MpShellConfig& cfg,
                               MpRoleCb on_host_ready,
                               MpRoleCb on_guest_ready,
                               MpDataCb on_data,
                               MpRoleCb on_invite_failed,
                               lv_obj_t* into) {
    g_cfg = cfg;
    g_on_host_ready = on_host_ready;
    g_on_guest_ready = on_guest_ready;
    g_on_invite_failed = on_invite_failed;
    g_on_data = on_data;
    g_invite_msgbox = nullptr;
    g_last_lobby_refresh = 0;
    g_lobby_active = true;

    discovery_set_game(cfg.game_id, "waiting");
    discovery_on_invite(mps_on_invite);
    discovery_on_accept(mps_on_accept);
    discovery_on_game_data(mps_on_game_data);
    discovery_on_invite_failed(on_invite_failed ? mps_on_invite_failed : nullptr);

    lv_obj_t* scr = into;
    if (!scr) {
        scr = ui_create_screen();
        ui_create_back_btn(scr);
    }

    static char title_buf[48];
    snprintf(title_buf, sizeof(title_buf), "%s - Find Opponent", cfg.display_name);
    lv_obj_t* title = ui_create_title(scr, title_buf);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

    g_lobby_list = lv_list_create(scr);
    lv_obj_set_size(g_lobby_list, 280, 160);
    lv_obj_align(g_lobby_list, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_bg_color(g_lobby_list, UI_COLOR_CARD, 0);

    lv_obj_t* hint = lv_label_create(scr);
    lv_label_set_text(hint, "Tap a peer to invite");
    lv_obj_set_style_text_color(hint, UI_COLOR_DIM, 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -5);

    return scr;
}

void mp_shell_lobby_tick() {
    if (!g_lobby_active || !g_lobby_list) return;
    if (millis() - g_last_lobby_refresh < 2000) return;
    if (discovery_invite_pending()) return; // don't clobber "Invite sent, waiting..." text
    g_last_lobby_refresh = millis();

    lv_obj_clean(g_lobby_list);
    const Peer* peers = discovery_get_peers();
    int count = discovery_peer_count();
    int shown = 0;
    for (int i = 0; i < count; i++) {
        if (!peer_matches(peers[i])) continue;
        char label[32];
        snprintf(label, sizeof(label), "%s (%s)", peers[i].name, peers[i].state);
        lv_obj_t* btn = lv_list_add_btn(g_lobby_list, LV_SYMBOL_WIFI, label);
        lv_obj_add_event_cb(btn, lobby_peer_cb, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        shown++;
    }
    if (shown == 0) lv_list_add_text(g_lobby_list, "Searching...");
}

void mp_shell_set_status(const char* text) {
    if (!g_lobby_list) return;
    lv_obj_clean(g_lobby_list);
    lv_list_add_text(g_lobby_list, text);
}

void mp_shell_end(IPAddress peer_ip, bool send_abandon) {
    if (g_invite_msgbox) {
        lv_msgbox_close(g_invite_msgbox);
        g_invite_msgbox = nullptr;
    }
    if (send_abandon) {
        char buf[96];
        snprintf(buf, sizeof(buf), "{\"type\":\"move\",\"game\":\"%s\",\"abandon\":true}", g_cfg.game_id);
        discovery_send_game_data(peer_ip, buf);
    }
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_invite_failed(nullptr);
    discovery_on_game_data(nullptr);

    g_lobby_active = false;
    g_lobby_list = nullptr;
    g_on_host_ready = nullptr;
    g_on_guest_ready = nullptr;
    g_on_invite_failed = nullptr;
    g_on_data = nullptr;
}
