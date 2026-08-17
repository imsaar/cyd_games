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
| **Turn-based** | Send moves, derive state locally | Tic-Tac-Toe, Connect 4, Chess, Checkers, Dots & Boxes, Memory Match, Backgammon, Ludo |
| **Continuous** | Stream state at fixed intervals | Pong, Pictionary |

## Common Patterns: the `mp_shell` Library

The mode-select screen, lobby screen + peer list, invite/accept popup, and
discovery-callback wiring used to be reimplemented per game (~90-140 near-
identical lines each — a file-scope `s_self` pointer, `friend` declarations
for four discovery-callback free functions, a hand-built invite `lv_msgbox`,
a lobby peer-refresh loop in `update()`, and manual `discovery_on_*(nullptr)`
cleanup in `destroy()`). That's now centralized in `src/net/mp_shell.h`/
`.cpp` — every 2-player game plugs into it instead of reimplementing it.
This section describes the current pattern; scope is strictly the pre-game
handshake — `GameBase`'s heartbeat/move-counter resync and each game's own
in-game protocol (documented per-game below) are untouched by this layer.

### Per-Game Config

```cpp
static const MpShellConfig kCfg = {
    "mygame",   // game_id — discovery_set_game() + Peer.game match + JSON "game" field
    "My Game",  // display_name — mode-select title, invite msgbox title, lobby title
    true,       // show_cpu_button — false for games with no CPU AI (Pong, Pictionary)
    false,      // show_idle_peers — true only for Battleship (see below)
};
```

### Mode Select & Lobby

```cpp
lv_obj_t* MyGame::createScreen() {
    s_self = this;
    mode_ = MODE_SELECT;
    screen_ = mp_create_mode_select(kCfg, mode_cpu_cb, mode_local_cb, mode_online_cb);
    return screen_;
}

void MyGame::mode_online_cb(lv_event_t*) {
    if (!s_self) return;
    s_self->mode_ = MODE_LOBBY;
    lv_obj_t* scr = mp_shell_host_lobby(kCfg, on_host_ready, on_guest_ready, on_game_data, nullptr);
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}
```

`mp_create_mode_select()` builds the standard "vs CPU / Local (2P) / Network
(2P)" screen (2 or 3 buttons per `show_cpu_button`). `mp_shell_host_lobby()`
registers the shell's own discovery callbacks, calls
`discovery_set_game(game_id, "waiting")`, and builds the peer list + "Tap a
peer to invite" hint. Both take an optional trailing `lv_obj_t* into` — if
provided, they build into that existing object instead of creating a new
screen (used by Pictionary, whose `screen_` is one persistent object rebuilt
in place via `lv_obj_clean()` rather than swapped via `lv_scr_load_anim()`).

Call `mp_shell_lobby_tick()` once per `update()` tick while the lobby is
showing (mirrors the old per-game peer-refresh loop; no-ops otherwise), and
`mp_shell_set_status(text)` from a role callback to show status text in
place of the peer list (Memory Match's/Pictionary's "Waiting for host to
start..." — see below). **Caution:** don't call `mp_shell_set_status()`
after a `lv_obj_clean()` on the screen the lobby list was built into — that
invalidates the shell's internal list pointer out from under it; build a
plain label directly instead (Pictionary's `on_guest_ready` does this).

### Role Callbacks

```cpp
// Our invite was accepted — we're host, we go first.
void MyGame::on_host_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->mode_ = MODE_NETWORK;
    s_self->peer_ip_ = peer.ip;
    s_self->my_turn_ = true;
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

// We accepted someone's invite — we're guest, we go second.
void MyGame::on_guest_ready(const Peer& peer) {
    if (!s_self) return;
    s_self->mode_ = MODE_NETWORK;
    s_self->peer_ip_ = peer.ip;
    s_self->my_turn_ = false;
    lv_obj_t* scr = s_self->create_board();
    lv_scr_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, true);
    s_self->screen_ = scr;
}

void MyGame::on_game_data(const char* json) {
    if (!s_self || s_self->mode_ != MODE_NETWORK) return;
    s_self->onNetworkData(json);
}
```

The shell shows the Accept/Decline popup itself and calls
`discovery_send_accept()`/`discovery_send_decline()` — a game never
hand-builds an invite `lv_msgbox` anymore. `on_host_ready`/`on_guest_ready`
are free to do more than switch straight to the board: Memory Match's host
builds+shuffles the board and calls its own `send_board_sync()`, while its
guest calls `mp_shell_set_status("Waiting for host to start...")` and
doesn't build a board until the sync arrives; Pictionary's host resets
scores/round and calls `start_round()` (which sends a "setup" message).
Battleship's callbacks go to its ship-placement phase instead of straight
to a playable board.

`show_idle_peers` (Battleship only): the lobby's default peer filter is
"announcing this game, and not already `\"playing\"` with someone else".
Battleship's is more permissive — it also shows peers not yet announcing
*any* game — since a fresh peer might not have picked Network mode yet.

### Cleanup

```cpp
void MyGame::destroy() {
    mp_shell_end(peer_ip_, mode_ == MODE_NETWORK && !game_done_);
    s_self = nullptr;
    screen_ = nullptr;
}
```

`mp_shell_end()` closes any open invite popup, sends `{"abandon":true}` to
`peer_ip` if the second argument is true, then calls `discovery_clear_game()`
and clears all four discovery callback slots. Pass `false` from
`mode_cpu_cb`/`mode_local_cb` too (cheap even when no network session was
active — it guarantees a clean slate before entering CPU/local play).

### Role Assignment

- **Host** = device that sent the invite (receives accept, fires
  `on_host_ready`). Typically plays first / controls game flow.
- **Guest** = device that accepted the invite (fires `on_guest_ready`).

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
- `line`: Line index 0-39 (20 horizontal + 20 vertical, 5x5 dot grid / 4x4 boxes)

**Sync pattern:** Move replication with bonus turns. When a line completes a box, the current player gets another turn. Scoring is computed dynamically from the `boxes_[]` array.

---

### Memory Match

**Roles:** Host = P1 (creates board, first turn), Guest = P2

**Board sync (host to guest, once at game start):**
```json
{"type": "move", "game": "memory", "action": "sync", "v": [0, 1, 2, 3, 4, 5, 2, 1, 3, 4, 0, 5]}
```
- `v`: Array of 12 card values (6 pairs)

**Sync ack (guest to host, once the board is built):**
```json
{"type": "move", "game": "memory", "a": "sync_ack"}
```

**Flip message:**
```json
{"type": "move", "game": "memory", "action": "flip", "idx": 7}
```
- `idx`: Card index 0-11

**Sync pattern:** Host generates the randomized board layout and sends it to the guest before the game starts; the guest doesn't create its board (and stays on the lobby screen showing "Waiting for host to start...") until that message arrives. Because the guest is the only one waiting on a single message rather than a stream, the sync/ack pair gets its own resend path instead of piggybacking on the generic heartbeat: the host keeps resending the layout every heartbeat tick until `sync_ack` arrives, and — to cut worst-case recovery to one guest heartbeat instead of two — a guest heartbeat received before the ack also triggers an immediate resend. During play, each flip sends a message and match/mismatch logic runs identically on both sides.

---

### Backgammon

**Roles:** Host = White (first), Guest = Black

**Roll message (roller → other side, once per turn):**
```json
{"type": "move", "game": "backgammon", "a": "roll", "d1": 4, "d2": 2, "mc": 7}
```
- `d1`, `d2`: the two dice as actually rolled. The receiver re-derives `dice_count_`/doubles handling from these two values with the exact same logic the roller used (`d1==d2` → four moves of that value), so no separate "doubles" flag is needed.

**Move message (one per checker moved):**
```json
{"type": "move", "game": "backgammon", "a": "mv", "from": 23, "die": 4, "mc": 8}
```
- `from`: point index 0-23, or `-1` for a checker entering from the bar
- `die`: which of the rolled dice this move consumes

**Sync pattern:** The roller sends `roll` once, then one `mv` per checker moved (a double can send up to 4). The receiver doesn't roll independently — it applies `d1`/`d2` and each `mv` with the same `compute_dest()`/`try_apply_move()` the roller used, so bearing-off legality, hits, and "no legal move → turn passes" are all re-derived identically on both sides rather than transmitted. Whichever message (`roll` or `mv`) was sent most recently is what the heartbeat resends.

---

### Ludo

**Roles:** Host = Red (first, top-left yard), Guest = Yellow (bottom-right yard, opposite corner)

**Move message:**
```json
{"type": "move", "game": "ludo", "a": "mv", "p": 2, "d": 6, "mc": 5}
```
- `p`: token index 0-3, `d`: the rolled die (1-6)

**Pass message (rolled, but no token can legally move):**
```json
{"type": "move", "game": "ludo", "a": "pass", "d": 3, "mc": 6}
```

**Sync pattern:** Same "receiver re-derives, doesn't get told the result" approach as Backgammon — the mover rolls locally, checks whether any of its 4 tokens has a legal move for that die, and either sends `mv` (receiver applies via the same `compute_move()`/`try_apply_move()`) or `pass` (receiver just calls `switch_turn()`). Captures, safe squares, exact-roll bear-in, and the extra-turn-on-6/capture logic all run identically on both sides from the same inputs, so only the die and the chosen token ever cross the wire.

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
| Backgammon (roll) | ~70 bytes | Yes |
| Backgammon (move) | ~65 bytes | Yes |
| Ludo (move/pass) | ~60 bytes | Yes |
| Pong (host) | ~100 bytes | Yes |
| Pong (guest) | ~50 bytes | Yes |
| Pictionary (setup) | ~130 bytes | Yes |
| Pictionary (stroke chunk) | ~230 bytes | Yes (28 pts) |
| Pictionary (control) | ~70 bytes | Yes |

### StaticJsonDocument Sizing (parsing workspace ≠ wire size)

The 250-byte ESP-NOW limit above is about the *serialized* message on the
wire. `StaticJsonDocument<N>` capacity is a completely separate budget for
the *parsing workspace*, and it's easy to undersize without noticing —
this bit Memory Match's board sync for a while (guest never received it,
every single time, with no error visible anywhere but the serial log).

- Every object key/value pair and every array element costs one
  `VariantSlot` — 16 bytes on this (32-bit) target — regardless of the
  value's actual size. A 4-key object wrapping a 12-element array is
  `(4 + 12) * 16 = 256 bytes` in slots **alone**, before a single byte of
  string data.
- `deserializeJson(doc, buf)` on a **mutable `char*`** can zero-copy —
  strings just point back into `buf`, no extra cost. `deserializeJson(doc,
  json)` on a **`const char*`** cannot; every key and string value found
  during parsing gets duplicated into the document's pool on top of the
  slot cost.
- `discovery.cpp`'s `handle_packet()` gets the mutable buffer (zero-copy,
  cheap); every game's `onNetworkData(const char* json)` gets the `const`
  one (must copy). The same JSON can fit comfortably in one and silently
  fail `NoMemory` in the other.
- Failure is silent unless you check for it: `deserializeJson` returns a
  `DeserializationError` that's truthy on failure, and the common
  `if (deserializeJson(doc, json)) return;` pattern used everywhere here
  just drops the message with zero indication why. If a message type is
  never arriving, log `err.c_str()` before assuming it's a network/packet
  problem — worth checking before adding retry logic, since retrying a
  message that fails to parse for its own size just fails identically
  every time.
- Rule of thumb: size for `JSON_OBJECT_SIZE(keys) + JSON_ARRAY_SIZE(elements)`
  (16 bytes each) plus real headroom for string duplication — Memory
  Match's sync and Pictionary's `onNetworkData` both moved from `256` to
  `384` for this reason. Discovery's own `handle_packet()` parser was
  bumped the same way even though it uses the mutable buffer, since it has
  to parse the whole payload (any game's, including array/string-heavy
  ones) just to read `"type"`.

## Reliability Considerations

Since both UDP and ESP-NOW are unreliable:

1. **Turn-based games** use a **heartbeat + move-counter** pattern to auto-recover from lost moves (see below).
2. **Pong** sends state at 20fps — a lost packet is immediately superseded by the next update 50ms later.
3. **Pictionary** uses periodic full syncs (every 3 seconds) to recover from incremental packet loss.
4. **Peer disconnect detection** still relies on the discovery layer's 6-second announce timeout rather than application-level heartbeats — this is the *fallback* for a silent disappearance, distinct from the explicit `abandon` message below, which is faster and gives a proper "opponent left" screen instead of the game just going quiet.
5. **The `abandon` check must run before any field-dependent early return.** Every game's `destroy()` sends `{"type": "move", "game": "<game>", "abandon": true}` — a minimal envelope with no `"a"`/`"action"` field. Pictionary's `onNetworkData` used to read `doc["a"]` and `return` if absent *before* checking `doc["abandon"]`, so the abandon notice was silently dropped every time and the other side's game just kept running with no indication the opponent had left. Check `abandon` first, unconditionally, like every other game already does.

### Heartbeat & Move-Counter Resync (turn-based games)

All turn-based network games (Connect 4, Chess, Checkers, Dots & Boxes, Memory Match, Battleship, Backgammon, Ludo) share a common resync pattern, implemented via protected fields on `GameBase` (`net_mc_`, `net_last_move_`, `net_last_hb_ms_`, `net_pending_mc_`).

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
