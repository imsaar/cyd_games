#include "espnow_transport.h"
#include "config.h"
#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

// ── Receive queue (single-producer from callback, single-consumer from loop) ──

struct RxPacket {
    uint8_t mac[6];
    uint8_t data[250];
    size_t  len;
};

static const int RX_QUEUE_SIZE = 8;
static RxPacket rx_queue[RX_QUEUE_SIZE];
static volatile int rx_head = 0;  // written by callback
static volatile int rx_tail = 0;  // read by main loop

static bool active_ = false;

// ── MAC <-> IP mapping table ──

struct MacEntry {
    uint8_t   mac[6];
    IPAddress ip;
    bool      used;
};

static MacEntry mac_table[MAX_PEERS];
static uint8_t my_mac[6];

// Build synthetic IP from MAC: 10.mac[3].mac[4].mac[5]
static IPAddress mac_to_synthetic_ip(const uint8_t* mac) {
    return IPAddress(10, mac[3], mac[4], mac[5]);
}

IPAddress espnow_mac_to_ip(const uint8_t* mac) {
    // Check if we already have this MAC
    for (int i = 0; i < MAX_PEERS; i++) {
        if (mac_table[i].used && memcmp(mac_table[i].mac, mac, 6) == 0) {
            return mac_table[i].ip;
        }
    }
    // Add new entry
    IPAddress ip = mac_to_synthetic_ip(mac);
    for (int i = 0; i < MAX_PEERS; i++) {
        if (!mac_table[i].used) {
            memcpy(mac_table[i].mac, mac, 6);
            mac_table[i].ip = ip;
            mac_table[i].used = true;
            return ip;
        }
    }
    // Table full — overwrite oldest (index 0)
    memcpy(mac_table[0].mac, mac, 6);
    mac_table[0].ip = ip;
    return mac_table[0].ip;
}

bool espnow_ip_to_mac(IPAddress ip, uint8_t* mac_out) {
    for (int i = 0; i < MAX_PEERS; i++) {
        if (mac_table[i].used && mac_table[i].ip == ip) {
            memcpy(mac_out, mac_table[i].mac, 6);
            return true;
        }
    }
    return false;
}

IPAddress espnow_my_ip() {
    return mac_to_synthetic_ip(my_mac);
}

// ── ESP-NOW callbacks ──

static void on_recv(const uint8_t* mac, const uint8_t* data, int len) {
    int next = (rx_head + 1) % RX_QUEUE_SIZE;
    if (next == rx_tail) return;  // Queue full, drop packet

    RxPacket& pkt = rx_queue[rx_head];
    memcpy(pkt.mac, mac, 6);
    size_t copy_len = (len > 250) ? 250 : len;
    memcpy(pkt.data, data, copy_len);
    pkt.len = copy_len;
    rx_head = next;
}

static void on_send(const uint8_t* mac, esp_now_send_status_t status) {
    // Could track delivery status, but not critical for this use case
}

// ── Public API ──

void espnow_init(uint8_t channel) {
    if (active_) return;

    // Ensure STA mode is active
    if (WiFi.getMode() == WIFI_OFF) {
        WiFi.mode(WIFI_STA);
    }

    // Set fixed channel when not connected to an AP
    if (WiFi.status() != WL_CONNECTED) {
        esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    }

    WiFi.macAddress(my_mac);

    if (esp_now_init() != ESP_OK) {
        Serial.println("[ESP-NOW] Init failed!");
        return;
    }

    esp_now_register_recv_cb(on_recv);
    esp_now_register_send_cb(on_send);

    // Add broadcast peer
    esp_now_peer_info_t peer = {};
    memset(peer.peer_addr, 0xFF, 6);
    peer.channel = 0;  // Use current channel
    peer.encrypt = false;
    esp_now_add_peer(&peer);

    // Clear state
    rx_head = 0;
    rx_tail = 0;
    for (int i = 0; i < MAX_PEERS; i++) mac_table[i].used = false;

    active_ = true;
    Serial.printf("[ESP-NOW] Initialized on channel %d, MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
        channel, my_mac[0], my_mac[1], my_mac[2], my_mac[3], my_mac[4], my_mac[5]);
}

void espnow_deinit() {
    if (!active_) return;
    esp_now_deinit();
    active_ = false;
    Serial.println("[ESP-NOW] Deinitialized");
}

bool espnow_active() {
    return active_;
}

void espnow_send_broadcast(const uint8_t* data, size_t len) {
    if (!active_ || len > 250) return;
    uint8_t broadcast[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    esp_now_send(broadcast, data, len);
}

void espnow_send_unicast(const uint8_t* mac, const uint8_t* data, size_t len) {
    if (!active_ || len > 250) return;

    // Ensure peer is registered
    if (!esp_now_is_peer_exist(mac)) {
        esp_now_peer_info_t peer = {};
        memcpy(peer.peer_addr, mac, 6);
        peer.channel = 0;
        peer.encrypt = false;
        esp_now_add_peer(&peer);
    }

    esp_now_send(mac, data, len);
}

bool espnow_receive(uint8_t* mac_out, uint8_t* buf, size_t buf_size, size_t* len_out) {
    if (rx_tail == rx_head) return false;  // Empty

    RxPacket& pkt = rx_queue[rx_tail];
    memcpy(mac_out, pkt.mac, 6);
    size_t copy_len = (pkt.len > buf_size) ? buf_size : pkt.len;
    memcpy(buf, pkt.data, copy_len);
    *len_out = copy_len;
    rx_tail = (rx_tail + 1) % RX_QUEUE_SIZE;
    return true;
}
