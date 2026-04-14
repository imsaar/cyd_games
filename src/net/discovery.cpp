#include "discovery.h"
#include "wifi_manager.h"
#include "espnow_transport.h"
#include "../hal/prefs.h"
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

enum TransportMode { TRANSPORT_UDP, TRANSPORT_ESPNOW };

static WiFiUDP udp;
static TransportMode transport = TRANSPORT_UDP;
static Peer peers[MAX_PEERS];
static int  peer_count = 0;
static char my_name[12] = "";
static char my_game[16] = "";
static char my_state[12] = "menu";

static InviteCallback   invite_cb   = nullptr;
static InviteCallback   accept_cb   = nullptr;
static GameDataCallback gamedata_cb = nullptr;

static uint32_t last_announce = 0;

static void build_name() {
    // Use saved name if set, otherwise generate from MAC
    prefs_get_name(my_name, sizeof(my_name));
    if (my_name[0] == '\0') {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(my_name, sizeof(my_name), "CYD-%02X%02X", mac[4], mac[5]);
    }
}

static void send_announce() {
    if (strlen(my_game) == 0) return;  // Only announce when in a game lobby

    StaticJsonDocument<200> doc;
    doc["type"]  = "announce";
    doc["name"]  = my_name;
    doc["game"]  = my_game;
    doc["fw"]    = FW_VERSION;
    doc["state"] = my_state;

    if (transport == TRANSPORT_UDP) {
        doc["ip"] = WiFi.localIP().toString();
        char buf[200];
        size_t len = serializeJson(doc, buf, sizeof(buf));
        udp.beginPacket(IPAddress(255, 255, 255, 255), DISCOVERY_PORT);
        udp.write((uint8_t*)buf, len);
        udp.endPacket();
    } else {
        char buf[200];
        size_t len = serializeJson(doc, buf, sizeof(buf));
        espnow_send_broadcast((uint8_t*)buf, len);
    }
}

static void add_or_update_peer(const char* name, IPAddress ip, const char* game, const char* state) {
    // Don't add ourselves
    if (transport == TRANSPORT_UDP) {
        if (ip == WiFi.localIP()) return;
    } else {
        if (ip == espnow_my_ip()) return;
    }

    // Update existing
    for (int i = 0; i < peer_count; i++) {
        if (peers[i].ip == ip) {
            strncpy(peers[i].game, game, sizeof(peers[i].game) - 1);
            strncpy(peers[i].state, state, sizeof(peers[i].state) - 1);
            peers[i].last_seen = millis();
            return;
        }
    }

    // Add new
    if (peer_count < MAX_PEERS) {
        Peer& p = peers[peer_count++];
        strncpy(p.name, name, sizeof(p.name) - 1);
        p.ip = ip;
        strncpy(p.game, game, sizeof(p.game) - 1);
        strncpy(p.state, state, sizeof(p.state) - 1);
        p.last_seen = millis();
    }
}

static void expire_peers() {
    uint32_t now = millis();
    for (int i = 0; i < peer_count; ) {
        if (now - peers[i].last_seen > 6000) {
            peers[i] = peers[--peer_count];
        } else {
            i++;
        }
    }
}

static void handle_packet(char* buf, int len, IPAddress remote) {
    buf[len] = '\0';

    // Save a copy before parsing — ArduinoJson 6 zero-copy mode
    // modifies buf in-place, corrupting it for later re-parsing.
    char raw[256];
    memcpy(raw, buf, len + 1);

    StaticJsonDocument<256> doc;
    if (deserializeJson(doc, buf)) return;

    const char* type = doc["type"];
    if (!type) return;

    if (strcmp(type, "announce") == 0) {
        add_or_update_peer(
            doc["name"] | "?",
            remote,
            doc["game"] | "",
            doc["state"] | ""
        );
    } else if (strcmp(type, "invite") == 0) {
        if (invite_cb) {
            Peer from;
            strncpy(from.name, doc["name"] | "?", sizeof(from.name) - 1);
            from.ip = remote;
            strncpy(from.game, doc["game"] | "", sizeof(from.game) - 1);
            invite_cb(from);
        }
    } else if (strcmp(type, "accept") == 0) {
        if (accept_cb) {
            Peer from;
            strncpy(from.name, doc["name"] | "?", sizeof(from.name) - 1);
            from.ip = remote;
            strncpy(from.game, doc["game"] | "", sizeof(from.game) - 1);
            accept_cb(from);
        }
    } else if (strcmp(type, "move") == 0) {
        Serial.printf("[Discovery] Move from %s: %s\n",
                      remote.toString().c_str(), raw);
        if (gamedata_cb) {
            gamedata_cb(raw);  // Pass uncorrupted copy
        } else {
            Serial.println("[Discovery] No gamedata_cb registered!");
        }
    }
}

void discovery_init() {
    build_name();

    if (wifi_connected()) {
        transport = TRANSPORT_UDP;
        udp.begin(DISCOVERY_PORT);
        Serial.printf("[Discovery] UDP mode, %s on port %d\n", my_name, DISCOVERY_PORT);
    } else {
        transport = TRANSPORT_ESPNOW;
        espnow_init(ESPNOW_CHANNEL);
        Serial.printf("[Discovery] ESP-NOW mode, %s\n", my_name);
    }
}

void discovery_deinit() {
    if (transport == TRANSPORT_UDP) {
        udp.stop();
    } else {
        espnow_deinit();
    }
}

void discovery_reinit() {
    discovery_deinit();
    peer_count = 0;
    last_announce = 0;
    discovery_init();
}

void discovery_loop() {
    // Send announcement every 2 seconds
    if (millis() - last_announce > 2000) {
        last_announce = millis();
        send_announce();
        expire_peers();
    }

    if (transport == TRANSPORT_UDP) {
        // Receive UDP packets
        int size = udp.parsePacket();
        while (size > 0) {
            char buf[256];
            int len = udp.read(buf, sizeof(buf) - 1);
            if (len > 0) {
                handle_packet(buf, len, udp.remoteIP());
            }
            size = udp.parsePacket();
        }
    } else {
        // Drain ESP-NOW receive queue
        uint8_t mac[6];
        uint8_t buf[256];
        size_t len;
        while (espnow_receive(mac, buf, sizeof(buf) - 1, &len)) {
            IPAddress remote = espnow_mac_to_ip(mac);
            handle_packet((char*)buf, (int)len, remote);
        }
    }
}

void discovery_set_game(const char* game, const char* state) {
    strncpy(my_game, game, sizeof(my_game) - 1);
    strncpy(my_state, state, sizeof(my_state) - 1);
}

void discovery_clear_game() {
    my_game[0] = '\0';
    strncpy(my_state, "menu", sizeof(my_state) - 1);
}

static void send_json_to(IPAddress ip, StaticJsonDocument<200>& doc) {
    char buf[200];
    size_t len = serializeJson(doc, buf, sizeof(buf));

    if (transport == TRANSPORT_UDP) {
        udp.beginPacket(ip, DISCOVERY_PORT);
        udp.write((uint8_t*)buf, len);
        udp.endPacket();
    } else {
        uint8_t mac[6];
        if (espnow_ip_to_mac(ip, mac)) {
            espnow_send_unicast(mac, (uint8_t*)buf, len);
        } else {
            Serial.printf("[Discovery] No MAC for IP %s, broadcasting\n", ip.toString().c_str());
            espnow_send_broadcast((uint8_t*)buf, len);
        }
    }
}

void discovery_send_invite(IPAddress peer_ip) {
    StaticJsonDocument<200> doc;
    doc["type"] = "invite";
    doc["name"] = my_name;
    doc["game"] = my_game;
    if (transport == TRANSPORT_UDP) {
        doc["ip"] = WiFi.localIP().toString();
    }
    send_json_to(peer_ip, doc);
}

void discovery_send_accept(IPAddress peer_ip) {
    StaticJsonDocument<200> doc;
    doc["type"] = "accept";
    doc["name"] = my_name;
    doc["game"] = my_game;
    if (transport == TRANSPORT_UDP) {
        doc["ip"] = WiFi.localIP().toString();
    }
    send_json_to(peer_ip, doc);
}

void discovery_send_game_data(IPAddress peer_ip, const char* json) {
    Serial.printf("[Discovery] Sending move to %s: %s\n",
                  peer_ip.toString().c_str(), json);

    if (transport == TRANSPORT_UDP) {
        udp.beginPacket(peer_ip, DISCOVERY_PORT);
        udp.write((uint8_t*)json, strlen(json));
        udp.endPacket();
    } else {
        uint8_t mac[6];
        size_t len = strlen(json);
        if (len > 250) {
            Serial.println("[Discovery] ESP-NOW payload too large!");
            return;
        }
        if (espnow_ip_to_mac(peer_ip, mac)) {
            espnow_send_unicast(mac, (uint8_t*)json, len);
        } else {
            Serial.println("[Discovery] No MAC for peer, cannot send game data");
        }
    }
}

void discovery_on_invite(InviteCallback cb) { invite_cb = cb; }
void discovery_on_accept(InviteCallback cb) { accept_cb = cb; }
void discovery_on_game_data(GameDataCallback cb) { gamedata_cb = cb; }

int discovery_peer_count() { return peer_count; }
const Peer* discovery_get_peers() { return peers; }

bool discovery_is_espnow() { return transport == TRANSPORT_ESPNOW; }

void discovery_set_name(const char* name) {
    strncpy(my_name, name, sizeof(my_name) - 1);
    my_name[sizeof(my_name) - 1] = '\0';
    prefs_set_name(name);
}

const char* discovery_get_name() {
    return my_name;
}
