# ESP-NOW Transport Design

## Overview

CYD Arcade supports two network transports for multiplayer: **WiFi UDP** and **ESP-NOW**. The transport is selected automatically at boot based on WiFi connectivity, or manually via the WiFi toggle in Settings. ESP-NOW enables peer-to-peer multiplayer without any WiFi infrastructure.

## Architecture

```
┌─────────────┐     ┌─────────────────┐     ┌──────────────────┐
│  Game Logic  │────>│  Discovery Layer │────>│  Transport Layer  │
│  (per game)  │<────│  (discovery.cpp) │<────│  UDP or ESP-NOW   │
└─────────────┘     └─────────────────┘     └──────────────────┘
```

The discovery layer abstracts the transport. Games call `discovery_send_game_data()` without knowing whether UDP or ESP-NOW is active underneath.

## Transport Selection

```cpp
void discovery_init() {
    if (wifi_connected()) {
        transport = TRANSPORT_UDP;   // Listen on DISCOVERY_PORT (4328)
    } else {
        transport = TRANSPORT_ESPNOW; // Init ESP-NOW on fixed channel
    }
}
```

Switching transports (e.g., toggling WiFi in Settings) calls `discovery_reinit()` which tears down the current transport and re-initializes.

## ESP-NOW Transport Layer

**Files:** `src/net/espnow_transport.h`, `src/net/espnow_transport.cpp`

### Initialization

`espnow_init(channel)` performs:
1. Set WiFi to STA mode
2. Set the WiFi channel (if not connected to an AP)
3. Read own MAC address
4. Call `esp_now_init()`
5. Register send and receive callbacks
6. Add broadcast peer (`FF:FF:FF:FF:FF:FF`)
7. Clear the receive queue and MAC lookup table

### Synthetic IP Addressing

ESP-NOW operates on MAC addresses, but the discovery layer uses IP addresses for peer identification. A synthetic IP scheme bridges this:

```
MAC aa:bb:cc:dd:ee:ff  →  IP 10.dd.ee.ff
```

This mapping is deterministic — the same MAC always produces the same IP. A lookup table (`mac_table[]`) caches MAC-to-IP and IP-to-MAC reverse lookups. New entries are added when broadcasts are received from unknown peers.

### Sending Data

| Function | Target | Use Case |
|----------|--------|----------|
| `espnow_send_broadcast(data, len)` | `FF:FF:FF:FF:FF:FF` | Peer announcements |
| `espnow_send_unicast(mac, data, len)` | Specific MAC | Game moves, invites |

Both enforce a **250-byte maximum payload** (ESP-NOW hardware limit). Unicast auto-registers the target as an ESP-NOW peer if not already registered.

If the discovery layer can't resolve an IP to a MAC (peer not yet in the table), it falls back to broadcast.

### Receive Queue

A lock-free ring buffer handles the ISR-to-main-loop handoff:

```
ISR callback (on_recv)          Main loop (espnow_receive)
        │                               │
        ▼                               ▼
   rx_queue[head] ──────────────> rx_queue[tail]
   (write, advance head)         (read, advance tail)
```

- **Queue size:** 8 slots
- **Packet size:** Up to 250 bytes each
- **Overflow:** Packets dropped silently if queue is full
- **Thread safety:** Single-producer (ISR) / single-consumer (main loop) design

Each received packet stores the sender's MAC address, which is used to populate the MAC lookup table.

## UDP Transport

When WiFi is connected, standard UDP is used:

- **Port:** `DISCOVERY_PORT` (4328)
- **Announcements:** Broadcast to `255.255.255.255:4328`
- **Game data:** Unicast to peer's real IP address
- **No payload limit** (practical limit ~1400 bytes per UDP datagram)

## Discovery Protocol

**File:** `src/net/discovery.h`, `src/net/discovery.cpp`

### Message Types

All messages are JSON. The `"type"` field determines routing:

#### 1. Announce (broadcast, every 2 seconds)
```json
{
  "type": "announce",
  "name": "CYD-A1B2",
  "game": "tictactoe",
  "fw": "2.2.3",
  "state": "waiting",
  "ip": "192.168.1.100"     // UDP only
}
```

Announces are only sent when a game lobby is active (`discovery_set_game()` has been called). The `"ip"` field is included only in UDP mode; ESP-NOW peers derive IPs from MAC addresses.

#### 2. Invite (unicast)
```json
{
  "type": "invite",
  "name": "CYD-A1B2",
  "game": "tictactoe",
  "ip": "192.168.1.100"     // UDP only
}
```

#### 3. Accept (unicast)
```json
{
  "type": "accept",
  "name": "CYD-C3D4",
  "game": "tictactoe",
  "ip": "192.168.1.101"     // UDP only
}
```

#### 4. Game Data (unicast)
```json
{
  "type": "move",
  ...game-specific fields...
}
```

The `"type": "move"` field is **required** — the discovery layer only routes packets with this type to the game data callback. Game-specific fields vary per game (see [Game Network Sync](game-network-sync.md)).

### Peer Management

| Parameter | Value |
|-----------|-------|
| Max peers | `MAX_PEERS` (from config.h) |
| Announce interval | 2 seconds |
| Peer expiry | 6 seconds without announce |

Peers are stored in a flat array. On each announce, existing entries are updated (by IP match) or new entries are added. Expired peers are removed every 2 seconds.

### Connection Flow

```
Device A (Host)                    Device B (Guest)
     │                                   │
     │  Both enter game lobby            │
     │  discovery_set_game("game",       │
     │                     "waiting")    │
     │                                   │
     │◄──── announce ────────────────────│
     │────── announce ──────────────────►│
     │                                   │
     │  A taps B's name in lobby list    │
     │────── invite ────────────────────►│
     │                                   │
     │                    B sees popup,  │
     │                    taps Accept    │
     │◄──── accept ─────────────────────│
     │                                   │
     │  Both set state="playing"         │
     │  Game begins                      │
     │                                   │
     │◄────── move ─────────────────────►│
     │  (game data flows bidirectionally)│
```

### Callback Registration

Games register three callbacks when entering the lobby:

```cpp
discovery_on_invite(my_invite_handler);   // Incoming invite
discovery_on_accept(my_accept_handler);   // Invite accepted
discovery_on_game_data(my_data_handler);  // Game moves
```

All three must be cleared (`nullptr`) in the game's `destroy()` method.

## Limitations

- **No delivery guarantee**: Both UDP and ESP-NOW are unreliable. Packets can be lost or arrive out of order.
- **No encryption**: ESP-NOW peer info has `encrypt = false`.
- **250-byte packet limit**: ESP-NOW hardware constraint. Games must keep JSON compact or chunk large payloads.
- **Fixed channel**: All ESP-NOW peers must use the same WiFi channel.
- **No NAT traversal**: UDP multiplayer only works on the same LAN.
- **Single game at a time**: Only one set of discovery callbacks can be active.
- **~200m range**: ESP-NOW typical outdoor range; less indoors.
