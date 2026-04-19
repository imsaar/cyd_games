# Game Network Sync Design

## Overview

Each multiplayer game in CYD Arcade syncs state over the network using JSON messages routed through the discovery layer. All messages share a common envelope:

```json
{"type": "move", "game": "<game_name>", ...game-specific fields...}
```

The `"type": "move"` field is mandatory — the discovery layer drops packets without it. The `"game"` field lets the receiver filter messages for the correct game.

Games fall into two sync categories:

| Category | Pattern | Examples |
|----------|---------|----------|
| **Turn-based** | Send moves, derive state locally | Tic-Tac-Toe, Connect 4, Chess, Checkers, Dots & Boxes, Memory Match |
| **Continuous** | Stream state at fixed intervals | Pong, Pictionary |

## Common Patterns

### Static Self Pointer

Every network game uses a file-scope static pointer so global C-style discovery callbacks can access the game instance:

```cpp
static MyGame* s_self = nullptr;

void my_on_game_data(const char* json) {
    if (!s_self || !s_self->network_mode_) return;
    s_self->onNetworkData(json);
}
```

Set in `createScreen()`, cleared in `destroy()`.

### Friend Declarations

Discovery callbacks need access to private members:

```cpp
class MyGame : public GameBase {
    friend void my_on_invite(const Peer& from);
    friend void my_on_accept(const Peer& from);
    friend void my_on_game_data(const char* json);
};
```

### Lobby Registration

When entering network mode, every game registers callbacks and announces:

```cpp
discovery_set_game("mygame", "waiting");
discovery_on_invite(my_on_invite);
discovery_on_accept(my_on_accept);
discovery_on_game_data(my_on_game_data);
```

### Cleanup on Destroy

```cpp
void MyGame::destroy() {
    discovery_clear_game();
    discovery_on_invite(nullptr);
    discovery_on_accept(nullptr);
    discovery_on_game_data(nullptr);
    s_self = nullptr;
}
```

### Role Assignment

- **Host** = device that sent the invite (receives accept). Typically plays first / controls game flow.
- **Guest** = device that accepted the invite.

---

## Turn-Based Games

### Tic-Tac-Toe

**Roles:** Host = X (first), Guest = O

**Move message:**
```json
{"type": "move", "game": "tictactoe", "cell": 4}
```
- `cell`: Board index 0-8

**Sync pattern:** Pure move replication. Each side maintains identical board state. Receiver applies move, checks win/draw, flips turn.

---

### Connect 4

**Roles:** Host = Red (first), Guest = Yellow

**Move message:**
```json
{"type": "move", "game": "connect4", "col": 3}
```
- `col`: Column index 0-6 (disc falls to lowest empty row)

**Sync pattern:** Same as Tic-Tac-Toe — move replication with local state derivation.

---

### Chess

**Roles:** Host = White (first), Guest = Black

**Move message:**
```json
{"type": "move", "game": "chess", "from": 12, "to": 28}
```
- `from`, `to`: Board indices 0-63

**Sync pattern:** Move replication. Receiver applies move, checks checkmate/stalemate locally. Includes all special moves (castling, en passant, promotion) handled by the move application logic.

---

### Checkers

**Roles:** Host = Red (first), Guest = Black

**Move message:**
```json
{"type": "move", "game": "checkers", "from": 5, "to": 14}
```
- `from`, `to`: Board indices 0-63

**Sync pattern:** Move replication with multi-jump support. Each jump in a chain sends a separate message. After a jump, if more jumps are available (`must_jump_` flag), the turn continues — otherwise it switches.

---

### Dots & Boxes

**Roles:** Host = Red/P1 (first), Guest = Blue/P2

**Move message:**
```json
{"type": "move", "game": "dotsboxes", "line": 10}
```
- `line`: Line index 0-23 (12 horizontal + 12 vertical)

**Sync pattern:** Move replication with bonus turns. When a line completes a box, the current player gets another turn. Scoring is computed dynamically from the `boxes_[]` array.

---

### Memory Match

**Roles:** Host = P1 (creates board, first turn), Guest = P2

**Board sync (host to guest, once at game start):**
```json
{"type": "move", "game": "memory", "action": "sync", "v": [0, 1, 2, 3, 4, 5, 2, 1, 3, 4, 0, 5]}
```
- `v`: Array of 12 card values (6 pairs)

**Flip message:**
```json
{"type": "move", "game": "memory", "action": "flip", "idx": 7}
```
- `idx`: Card index 0-11

**Sync pattern:** Host generates the randomized board layout and sends it to the guest before the game starts. During play, each flip sends a message. Match/mismatch logic runs identically on both sides.

---

## Continuous Sync Games

### Pong

**Roles:** Host = Left paddle (authoritative on physics), Guest = Right paddle

**Host sends (every 50ms = 20fps):**
```json
{
  "type": "move", "game": "pong",
  "bx": 1200, "by": 800,
  "bdx": 300, "bdy": -50,
  "pl": 80,
  "sl": 3, "sr": 2
}
```
- `bx`, `by`: Ball position (multiplied by 10 for precision without floats)
- `bdx`, `bdy`: Ball velocity (multiplied by 100)
- `pl`: Left paddle Y position
- `sl`, `sr`: Left/right scores

**Guest sends (every 50ms):**
```json
{"type": "move", "game": "pong", "pr": 100}
```
- `pr`: Right paddle Y position

**Sync pattern:** Asymmetric authority. Host runs all physics (ball movement, collision, scoring) and streams the authoritative state. Guest only controls its own paddle. Both send at 20fps for smooth gameplay.

---

### Pictionary

**Roles:** Host = controls game flow. Drawer/guesser roles alternate each round.

Pictionary has the most complex sync protocol due to the real-time drawing data.

#### Round Setup (drawer to guesser)
```json
{
  "type": "move", "game": "pictionary", "a": "setup",
  "w": 15, "cc": 2,
  "ch": [15, 20, 8, 35],
  "r": 0, "d": 0,
  "s0": 0, "s1": 0
}
```
- `w`: Word index into the word bank
- `cc`: Correct choice index (0-3)
- `ch`: Array of 4 word indices for multiple choice
- `r`: Round number, `d`: Drawer (0=host, 1=guest)
- `s0`, `s1`: Current scores

#### Drawing Data — Hex-Encoded Binary

Points are encoded as raw bytes in hexadecimal to maximize density:

```
Each point = 3 bytes = 6 hex chars:
  byte 0: x coordinate (0-240, or 0xFF for stroke separator)
  byte 1: y coordinate (0-200, or 0xFF for stroke separator)
  byte 2: color index (0-5)

Stroke separator: "FFFF00"
Example: "1E640050C80100A0A003" = 3 points + partial stroke
```

This encoding fits **28 points per 250-byte ESP-NOW packet** (168 hex chars + JSON overhead).

#### Incremental Strokes (every 200ms)
```json
{"type": "move", "game": "pictionary", "a": "i", "h": "1E640050C801..."}
```
- `h`: Hex-encoded new points (appended to receiver's buffer)

#### Full Sync (every 3 seconds + on Done)
```json
{"type": "move", "game": "pictionary", "a": "f", "o": 0, "t": 85, "h": "1E640050C801..."}
```
- `o`: Offset — which point index this chunk starts at
- `t`: Total point count (receiver sets its count to this)
- `h`: Hex-encoded points for this chunk

Full sync sends ALL points in sequential chunks of 28. Each chunk writes to a specific offset, so lost packets only cause small gaps rather than corrupting the entire drawing.

#### Control Messages
```json
{"type": "move", "game": "pictionary", "a": "clr"}          // Clear canvas
{"type": "move", "game": "pictionary", "a": "done"}         // Drawer finished
{"type": "move", "game": "pictionary", "a": "guess", "c": 2, "ok": true, "early": true}
{"type": "move", "game": "pictionary", "a": "next", "r": 1, "d": 1, "s0": 1, "s1": 2}
{"type": "move", "game": "pictionary", "a": "over", "s0": 3, "s1": 2}
```

#### Sync Strategy

```
Time ──────────────────────────────────────────────────►

Drawer:  [incremental 200ms] [incremental] [full 3s] [incremental] ... [full+done]
              │                    │            │          │                 │
Guesser: ....append...........append.......replace....append..........replace+guess
```

- **Incremental (200ms):** Sends only new points since last send. Guesser appends.
- **Full sync (3s):** Resends entire drawing with per-chunk offsets. Guesser overwrites at correct positions. Recovers from any packet loss.
- **On Done:** Final full sync ensures guesser has the complete picture before guessing.

#### Early Guess Bonus

The guesser sees choice buttons immediately while the drawing appears in real-time. Guessing correctly before the drawer hits Done awards **+2 points** instead of +1.

---

## Packet Size Budget

ESP-NOW has a **250-byte hard limit**. Here's how each game fits:

| Game | Typical Message Size | Fits? |
|------|---------------------|-------|
| Tic-Tac-Toe | ~55 bytes | Yes |
| Connect 4 | ~55 bytes | Yes |
| Chess | ~60 bytes | Yes |
| Checkers | ~60 bytes | Yes |
| Dots & Boxes | ~55 bytes | Yes |
| Memory Match (sync) | ~120 bytes | Yes |
| Memory Match (flip) | ~60 bytes | Yes |
| Pong (host) | ~100 bytes | Yes |
| Pong (guest) | ~50 bytes | Yes |
| Pictionary (setup) | ~130 bytes | Yes |
| Pictionary (stroke chunk) | ~230 bytes | Yes (28 pts) |
| Pictionary (control) | ~70 bytes | Yes |

## Reliability Considerations

Since both UDP and ESP-NOW are unreliable:

1. **Turn-based games** use a **heartbeat + move-counter** pattern to auto-recover from lost moves (see below).
2. **Pong** sends state at 20fps — a lost packet is immediately superseded by the next update 50ms later.
3. **Pictionary** uses periodic full syncs (every 3 seconds) to recover from incremental packet loss.
4. **Peer disconnect detection** still relies on the discovery layer's 6-second announce timeout rather than application-level heartbeats.

### Heartbeat & Move-Counter Resync (turn-based games)

All turn-based network games (Connect 4, Chess, Checkers, Dots & Boxes, Memory Match, Battleship) share a common resync pattern, implemented via protected fields on `GameBase` (`net_mc_`, `net_last_move_`, `net_last_hb_ms_`, `net_pending_mc_`).

**Per-move:**
- Every outgoing move carries `"mc": N` — a monotonically increasing move counter.
- `net_mc_` reflects the count of moves **applied** locally. Sender increments on local apply (which happens on their own move). Receivers apply moves with strict `+1` dedupe — `peer.mc == local.mc + 1` or the message is dropped (duplicate or gap).
- The last outgoing move JSON is cached in `net_last_move_` so it can be re-sent on demand.

**Heartbeat tick (every 2000 ms while in the network game):**
```json
{"type": "move", "game": "<game>", "a": "hb", "mc": <local mc>}
```
On receipt:
- `peer.mc < local.mc` → peer is behind; resend our `net_last_move_`.
- `peer.mc >= local.mc` → no action.

This auto-heals the common "both waiting for the other" desync: whichever side is ahead will resend, and the receiver's strict `+1` dedupe will either apply the missing move (gap filled) or drop the duplicate (noop).

**Battleship request/response variant.** Battleship's fire→result pair needs a different semantic because the attacker doesn't apply its own shot until the defender reports the outcome. The attacker sets `net_pending_mc_` when firing and retries the cached fire on every heartbeat tick until the result arrives. The defender:
- Applies a fresh fire (`peer.mc == local.mc + 1`), advances `net_mc_`, and sends back a matching `result`.
- On a duplicate fire (`peer.mc == local.mc`, attacker retried), resends the cached result.

This survives either fire-loss or result-loss. The attacker only advances `net_mc_` when the result arrives, so `net_pending_mc_` stays non-zero until the round completes.

**Known limitation — multi-move chains.** Checkers multi-jumps send each jump as a separate message. If a middle jump in the chain is lost, the strict `+1` dedupe on the receiver side will drop all subsequent jumps, and the heartbeat can only resend the latest cached jump. The chain will not auto-recover; eventual abandon is the fallback.
