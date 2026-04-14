#pragma once
#include <Arduino.h>

void     espnow_init(uint8_t channel);
void     espnow_deinit();
bool     espnow_active();
void     espnow_send_broadcast(const uint8_t* data, size_t len);
void     espnow_send_unicast(const uint8_t* mac, const uint8_t* data, size_t len);

// Drain one received packet. Returns true if a packet was available.
bool     espnow_receive(uint8_t* mac_out, uint8_t* buf, size_t buf_size, size_t* len_out);

// MAC <-> synthetic IP mapping
IPAddress espnow_mac_to_ip(const uint8_t* mac);
bool      espnow_ip_to_mac(IPAddress ip, uint8_t* mac_out);
IPAddress espnow_my_ip();
