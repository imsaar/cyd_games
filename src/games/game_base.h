#pragma once
#include <lvgl.h>
#include <Arduino.h>

class GameBase {
public:
    virtual ~GameBase() = default;

    // Lifecycle
    virtual lv_obj_t* createScreen() = 0;
    virtual void update() = 0;
    virtual void destroy() = 0;

    // Multiplayer hooks (override in 2-player games)
    virtual void onPeerJoined(const char* peer_ip) {}
    virtual void onPeerLeft() {}
    virtual void onNetworkData(const char* json) {}

    // Metadata
    virtual const char* name() const = 0;
    virtual uint8_t maxPlayers() const = 0;
};
