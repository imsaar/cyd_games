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

protected:
    // Heartbeat-based resync for turn-based network games.
    // Games embed "mc" (move counter) in every outgoing move message, cache
    // the last sent payload in net_last_move_, and send a small heartbeat
    // every NET_HB_INTERVAL_MS. On receiving a peer heartbeat with
    // peer.mc < local net_mc_, the game resends net_last_move_.
    // On receiving a move, games skip it when incoming.mc <= net_mc_.
    static constexpr uint32_t NET_HB_INTERVAL_MS = 2000;
    uint32_t net_mc_ = 0;
    uint32_t net_last_hb_ms_ = 0;
    char     net_last_move_[200] = {};

    // For request/response games (Battleship fire/result). Attacker sets
    // net_pending_mc_ when firing and doesn't advance net_mc_ until the
    // result arrives. Heartbeat tick retries the cached fire as long as
    // pending > 0. Defender handles duplicate-fire by resending last result.
    uint32_t net_pending_mc_ = 0;

    void net_reset_sync() {
        net_mc_ = 0;
        net_last_hb_ms_ = 0;
        net_last_move_[0] = 0;
        net_pending_mc_ = 0;
    }
};
