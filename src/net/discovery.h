#pragma once
#include <Arduino.h>
#include <IPAddress.h>
#include "config.h"

struct Peer {
    char      name[12];
    IPAddress ip;
    char      game[16];
    char      state[12];
    uint32_t  last_seen;
};

typedef void (*InviteCallback)(const Peer& from);
typedef void (*GameDataCallback)(const char* json);

void        discovery_init();
void        discovery_loop();
void        discovery_set_game(const char* game, const char* state);
void        discovery_clear_game();
void        discovery_send_invite(IPAddress peer_ip);
void        discovery_send_accept(IPAddress peer_ip);
void        discovery_send_game_data(IPAddress peer_ip, const char* json);
void        discovery_on_invite(InviteCallback cb);
void        discovery_on_accept(InviteCallback cb);
void        discovery_on_game_data(GameDataCallback cb);
int         discovery_peer_count();
const Peer* discovery_get_peers();
void        discovery_deinit();
void        discovery_reinit();  // Tear down and re-init (for transport switch)
bool        discovery_is_espnow();
