# LVGL 8 + ESP32 Lessons Learned

Hard-won knowledge from building 15 games and a clock/weather app and a paint app on the ESP32-2432S028 (CYD) with LVGL 8, 320x240 TFT, and resistive touchscreen.

## Custom Fonts (lv_font_conv)

**Critical: use `--no-compress --no-prefilter`** when generating fonts for ESP32.

Compressed bitmap fonts compile fine but silently fail to render on hardware (invisible text, no error, no crash). The ESP32's flash access pattern doesn't work well with lv_font_conv's default compression.

```bash
# Working command for ESP32:
lv_font_conv --bpp 4 --size 72 --font Montserrat-Medium.ttf \
  --symbols "0123456789:. " \
  --format lvgl -o font_digit_72.c \
  --lv-include lvgl.h --lv-font-name font_digit_72 \
  --no-kerning --no-compress --no-prefilter \
  --lv-fallback lv_font_montserrat_48
```

Key flags:
- `--no-compress --no-prefilter` — uncompressed bitmaps, guaranteed to work on ESP32
- `--lv-fallback` — bakes fallback font into the struct so missing glyphs render instead of vanishing
- `--symbols` — more reliable than `--range` for specifying exact glyphs
- `--no-kerning` — reduces size, not needed for digit-only fonts

Other font gotchas:
- Don't use characters not in the font (e.g. `--:--` when `-` isn't included)
- Line height for digit-only fonts is smaller than point size (96pt → 69px) — no ascenders/descenders
- Digit-only fonts are compact: 72pt ≈ 65KB, 96pt ≈ 110KB uncompressed

## Text Rendering & Glyph Positioning

**Check actual glyph advance widths** from the generated font .c file. The `adv_w` field is in 1/16ths of a pixel.

```
// From font_digit_72.c:
{.adv_w = 768, ...}  // "0" = 768/16 = 48px wide
{.adv_w = 262, ...}  // ":" = 262/16 = 16px wide
// So "00" = 96px, NOT ~56px as you might guess
```

When composing digit displays with separate labels:
- **Calculate positions from actual advance widths**, not estimates
- **Use smaller font for colons/separators** (e.g. montserrat_28 between 72pt digits) — a 72pt colon is disproportionate
- **Offset smaller colons vertically** to center against taller digits (e.g. `y + 12`)
- **Use `LV_LABEL_LONG_CLIP`** to prevent text wrapping when large fonts might exceed label width

Proportional fonts (Montserrat) make `printf` padding (`%-5s`) useless for column alignment. Use separate labels at fixed pixel x positions instead.

## Touch / Click Handling

**Use stored absolute coordinates**, not `lv_obj_get_x/y()` at click time.

`lv_obj_get_x/y` returns position relative to parent's content area (affected by padding). `lv_indev_get_point` returns absolute screen coordinates. The mismatch causes offset errors.

```cpp
// Store at build time:
static int grid_abs_x = x_position;
static int grid_abs_y = y_position;

// Use in click handler:
lv_indev_get_point(indev, &pt);
int col = (pt.x - grid_abs_x) / CELL_SIZE;
int row = (pt.y - grid_abs_y) / CELL_SIZE;
```

Make click targets at least 18px for resistive touchscreens. 14px is too small for reliable tapping.

## Scrolling

Always disable scrolling on game/app screens:

```cpp
lv_obj_set_scrollbar_mode(screen_, LV_SCROLLBAR_MODE_OFF);
lv_obj_clear_flag(screen_, LV_OBJ_FLAG_SCROLLABLE);
```

LVGL enables scrolling by default on `lv_obj_create` containers. Also disable on child panels.

## Layout for 320x240 Landscape

- Design for **landscape** (320 wide, 240 tall). Don't stack vertically leaving the right half empty.
- 48pt is the largest built-in Montserrat. For bigger, generate custom fonts.
- A 96pt "12:30" is ~250px wide — fits 320px but not inside a 150px circle.
- Put the primary element (time) large/centered, secondary info in corners.
- Full-width nav bars should sum button widths to 320px — no dead gaps.

## Tabview

- `LV_USE_TABVIEW` must be set to `1` in `lv_conf.h` (default is 0).
- Tabview doesn't support multi-row tab buttons. For 5+ tabs on 320px, build a custom button bar with `LV_OBJ_FLAG_HIDDEN` panel switching.

## Custom Drawing (Icons)

Built-in LVGL 8 symbols have NO weather icons, emoji, or pictographs. `LV_SYMBOL_EYE_CLOSE` is a crossed-out eye, not a cloud.

For domain-specific icons, draw them programmatically using `LV_EVENT_DRAW_POST` callbacks:

```cpp
static void icon_draw_cb(lv_event_t* e) {
    lv_draw_ctx_t* ctx = lv_event_get_draw_ctx(e);
    // Use lv_draw_rect(), lv_draw_line(), lv_draw_arc()
    // Same pattern as Sudoku grid lines
}
```

Works well for weather icons (sun rays, cloud circles, rain lines, lightning bolts) at 18-36px sizes.

## LVGL Memory Pool (`LV_MEM_SIZE`) Exhaustion

`LV_MEM_SIZE` in `lv_conf.h` is a **fixed-size static pool** (40KB in this project) that all LVGL objects/styles are allocated from at runtime. It is completely separate from the ESP32's general heap — `ESP.getFreeHeap()` looking healthy tells you nothing about how full this pool is.

Symptom: a screen that builds many widgets crashes (PANIC reset) with a blank flash and a bounce back to the previous screen, even though general heap is fine. This happens because `lv_*_create()` returns `NULL` when the pool is full, and unchecked code then dereferences it.

Root cause we hit: the Clock app's screen builds all 6 tabs eagerly (so switching tabs is instant), so every tab's objects coexist in the pool simultaneously. Adding a 7x6 day-grid calendar tab with **one `lv_obj_t` label per cell** (42 objects, each with several `lv_obj_set_style_*` calls) pushed cumulative usage over the 40KB ceiling — confirmed by checkpointing `lv_mem_monitor()` right before the tab was built, which showed only ~4.5KB free.

Fix: **use a single custom-draw object per grid/repeating-widget area instead of one object per cell**, same technique as the [weather icons](#custom-drawing-icons) above — draw all cells' text/backgrounds in one `LV_EVENT_DRAW_POST` callback using `lv_draw_label()` / `lv_draw_rect()`. This took the calendar tab from ~55 objects down to ~11, using a small fraction of the pool.

Things that *don't* fix this:
- **Bumping `LV_MEM_SIZE`** — on this board the static DRAM segment is already nearly maxed out; even a 4KB increase overflowed the link budget (`region 'dram0_0_seg' overflowed`). Check with a real build, don't assume there's headroom.
- **Shared `lv_style_t` objects** — cuts per-object *style* overhead (useful, and worth doing regardless — see below), but doesn't help if the object count itself is the problem.

Still worth doing when you do need N similarly-styled objects: set common properties once via a shared `lv_style_t` + `lv_obj_add_style()`, instead of calling `lv_obj_set_style_*()` individually on each object. Each individual local-style call grows a per-object property list out of the pool; N objects × M properties adds up fast.

```cpp
static lv_style_t cell_style;
lv_style_init(&cell_style);
lv_style_set_text_font(&cell_style, &lv_font_montserrat_12);
// ... set shared props once ...
for (int i = 0; i < N; i++) {
    lv_obj_t* cell = lv_label_create(parent);
    lv_obj_add_style(cell, &cell_style, 0);  // no per-object property storage
}
```

### Diagnosing without a USB connection

A panic reboots the board before you can read `Serial` output unless you already had a monitor attached. To debug remotely:

- `esp_reset_reason()` (from `esp_system.h`) tells you if the last boot was caused by `ESP_RST_PANIC` / `ESP_RST_TASK_WDT` / `ESP_RST_BROWNOUT` vs. a normal reset — expose it over the existing OTA web server (`GET /debug` in this project) so `curl http://<ip>/debug` works after a crash.
- `RTC_NOINIT_ATTR` globals survive panic/watchdog resets (cleared only on power-on), so you can drop lightweight "checkpoint" breadcrumbs (name + `lv_mem_monitor()` stats) at each major build step and read the last one back after reboot — see `src/utils/crash_trace.h`. This pinpointed the exact tab-build call that was running when the pool ran out, without ever plugging in a cable.

## ArduinoJson Buffer Sizes

`StaticJsonDocument<N>` pool: each object member and array element uses ~16 bytes on ESP32 (32-bit). **Fields added last are silently dropped** when the pool is full.

```cpp
// BAD: if doc overflows, s0/s1 are silently missing
score_[0] = doc["s0"] | 0;  // defaults to 0, zeroing the score!

// GOOD: preserve current value if field is missing
score_[0] = doc["s0"] | score_[0];
```

The serialized JSON output buffer (`char buf[N]`) is separate from the document pool — size them independently.

**Parsing from `const char*` costs more than the wire size suggests.** `deserializeJson(doc, buf)` on a *mutable* `char*` can zero-copy — string values just point back into `buf`. `deserializeJson(doc, json)` on a `const char*` (the signature every game's `onNetworkData(const char* json)` uses) cannot: every key and string value found during parsing gets **duplicated** into the pool on top of the per-slot cost. A 4-key object wrapping a 12-element array is `(4+12)*16 = 256` bytes in slots alone, with zero room left for that duplication — `StaticJsonDocument<256>` parsing it from a `const char*` fails `NoMemory` on every single attempt, not intermittently. This isn't a network reliability problem and retry logic won't fix it. See [Game Network Sync → StaticJsonDocument Sizing](game-network-sync.md#staticjsondocument-sizing-parsing-workspace--wire-size) for the full writeup (this bit Memory Match's board-sync message for a while). Always check the `DeserializationError` return value and log it rather than assuming a silent parse failure is a dropped packet.

## "Dark Mode" Is a Hardware Panel Inversion, Not a Palette Swap

This project's Dark/Light Mode toggle doesn't swap between two color palettes — it calls `tft.invertDisplay(bool)`, a hardware feature that complements every RGB channel of every pixel the panel outputs. All the app's `UI_COLOR_*` constants are one fixed dark-theme palette; Light Mode is achieved for free by inverting the whole picture, which happens to work for ordinary UI chrome since inverting a dark-bg/light-text pair still gives light-bg/dark-text — direction preserved, contrast preserved, zero extra code.

It breaks down for any color meant to carry an **absolute, mode-independent** meaning rather than a relative contrast pairing — e.g. a chess/checkers piece belonging to "the white player" must always render white, on both Dark and Light Mode, and on both devices in a network game regardless of which color each side is. A literal `lv_color_hex(0xffffff)` looks white in Dark Mode but gets hardware-inverted to look black in Light Mode, silently swapping which player's pieces look like which color.

Fix: pre-complement such colors so the hardware inversion cancels back out to the intended color.

```cpp
// src/ui/ui_common.cpp
lv_color_t ui_absolute_color_hex(uint32_t hex) {
    if (prefs_get_inverted()) return lv_color_hex(hex);       // Dark Mode: as-authored
    return lv_color_hex(0xFFFFFF - (hex & 0xFFFFFF));          // Light Mode: pre-invert
}
```

Board/background decoration colors that don't carry this kind of absolute meaning (e.g. chess's cream/brown squares) don't need this — they're fine relying on the same automatic contrast-preservation as the rest of the UI.

## Per-Player Board Orientation (Network 2P)

For a network 2-player board game, each device should show its own pieces starting closest to the bottom, mirrored for the other player — but move logic, win-checking, and the wire protocol need a single, consistent coordinate space shared by both sides. Solving this by physically storing the board pre-flipped per device would require translating every move index sent over the network; instead, keep the board array in **one canonical orientation** always (fixed rows for each side, e.g. White always at logical rows 6-7) and apply the flip **only at the render/input boundary**:

```cpp
// Chess: a runtime index transform, applied wherever a logical square
// touches the screen — drawing, highlighting, and touch input.
int vidx(int idx) const { return flipped_ ? 63 - idx : idx; }
// draw_piece(idx) writes into piece_labels_[vidx(idx)]
// cell_cb touch handler: if (flipped_) idx = 63 - idx;  (before using idx)
```

```cpp
// Checkers: the same idea applied at cell-creation time instead — bake
// the flip into each cell's screen position once, keyed by the same
// canonical idx used everywhere else (cell_objs_[idx], board_[idx]).
int dr = my_color_red_ ? r : 7 - r;
int dc = my_color_red_ ? c : 7 - c;
lv_obj_set_pos(cell, dc * CELL, dr * CELL);   // cell_objs_[idx] unchanged
// touch handler inverts the same way: if (!my_color_red_) { row=7-row; col=7-col; }
```

Either approach works — pick whichever fits how the game already indexes its screen objects. The key invariant: `send_move()`/`onNetworkData()` and all win/legality logic only ever see the canonical (unflipped) index; the transform exists solely between that index and screen pixels, applied identically (and inversely) in both directions on both devices.

## Canvas & Custom Board Drawing

`LV_USE_CANVAS` is `0` by default in this project's `lv_conf.h`. It has to be flipped to `1` for anything that needs a real per-pixel buffer — a freehand paint app with flood fill (Color Fusion), as opposed to a board that's just redrawn from fixed shapes each time. The canvas buffer itself should be heap-allocated (`new lv_color_t[w*h]`) in the screen's `createScreen()`/`_create()` and `delete[]`d on destroy, not a permanent static array: a 240x200 RGB565 buffer is ~94KB, too much to reserve for the app's entire lifetime on a no-PSRAM ESP32 when it's only needed while that one screen is open.

Board games with more cells/pieces than is comfortable as individual `lv_obj`s (Backgammon's 24 points + bar, Ludo's 15x15 grid with up to 8 tokens) don't need the canvas widget at all — `lv_draw_polygon(draw_ctx, &rect_dsc, points, point_cnt)` and `lv_draw_triangle(...)` (declared in `lv_draw_triangle.h`) are generic primitives wired unconditionally into the software renderer. They work in a plain `lv_obj`'s `LV_EVENT_DRAW_POST` callback with no `LV_USE_CANVAS` dependency, using the same `lv_draw_rect_dsc_t` (bg_color/bg_opa) as `lv_draw_rect` for fill styling. Draw the whole board in **one** callback on a single `lv_obj`, reading state straight off `self` (static member function + file-scope `s_self`, same pattern as `Connect4::col_cb`); do hit-testing separately in a `LV_EVENT_CLICKED` callback that maps the tap point to board coordinates via fixed pixel constants — there's nothing to hit-test against per-cell since no per-cell objects exist.

A player-owned piece/token color (backgammon checkers, ludo tokens) needs the same `ui_absolute_color_hex()` treatment as chess/checkers pieces above, even though red/yellow aren't literally black/white — the color still carries "this team" meaning that must survive the Light Mode panel inversion.

**Why:** Building Color Fusion (paint app) and Backgammon/Ludo (custom board games) all needed rendering that plain `lv_obj` widgets don't scale to, and the canvas/polygon APIs required a config flip and weren't otherwise used anywhere in this codebase.

**A border or label drawn on top of an absolute-colored piece needs the same treatment as the fill.** `ui_absolute_color_hex()` on a checker's *fill* doesn't help if its border or a text label stamped on top still uses a plain `lv_color_black()`/`lv_color_white()` — those get hardware-inverted independently of the fill, and can silently flip "black text on a white chip" into "white text on a white chip" (invisible) once Light Mode inverts the panel. This bit Backgammon's stacked-checker count label and its chip border:

```cpp
// BAD: border/label color doesn't track the fill's absolute-ness
chk.bg_color = white ? ui_absolute_color_hex(0xF5F5F5) : ui_absolute_color_hex(0x202020);
chk.border_color = lv_color_black();               // inverts to white in Light Mode
ldsc.color = white ? lv_color_black() : lv_color_white();  // same bug

// GOOD: border/label follow the same absolute logic, inverted from the fill
chk.bg_color = white ? ui_absolute_color_hex(0xF5F5F5) : ui_absolute_color_hex(0x202020);
chk.border_color = white ? ui_absolute_color_hex(0x000000) : ui_absolute_color_hex(0xFFFFFF);
ldsc.color = white ? ui_absolute_color_hex(0x000000) : ui_absolute_color_hex(0xFFFFFF);
```

Plain decorative background colors (board triangles, yard quadrants) don't need this — contrast between two *non-absolute* colors is exactly preserved under global inversion (inverting both sides of a color pair leaves their RGB distance unchanged). The bug only appears where an absolute-colored piece's outline/label was left non-absolute, and it's invisible in Dark Mode testing since the plain colors happen to be correct there by coincidence — only surfaces when someone actually checks Light Mode.

## Static Buffers Have a Much Smaller Ceiling Than "Free Heap"

A `static` array sized for something non-trivial (a few thousand elements, tens of KB) can blow the link — not fail at runtime, fail to *link* — even when the overall "RAM used" percentage PlatformIO reports looks comfortable:

```
region `dram0_0_seg' overflowed by 16064 bytes
```

This happened adding Color Fusion's undo/redo history as `static HistPoint hist_points_[4000]` (16,000 bytes) plus a small `static HistAction actions_[30]` array — together barely 17KB, yet it overflowed the ESP32's fixed DRAM/BSS segment, a much harder and smaller ceiling than the general free-heap number suggests (that segment also holds every other static/global across the whole firmware — LVGL's object pool, both TFT_eSPI draw buffers, WiFi/BT internals, every game's static instance in `screen_manager.cpp`, etc. — so an innocuous-looking "37.8% RAM used" can still leave `dram0_0_seg` itself with very little slack).

Fix: allocate it on the heap instead, exactly like the canvas-buffer pattern above (`new (std::nothrow) T[n]` in create, `delete[]` in destroy, null-guarded everywhere it's read). Heap allocations only fail if there's truly no room *at that moment*; a static array fails unconditionally at link time regardless of runtime heap headroom. Any buffer sized beyond a few hundred bytes for a single screen/app (as opposed to a shared, always-resident structure like the per-game static instances in `screen_manager.cpp`) should default to heap, not `static`.

**Why:** `pio run` catches this at link time with a clear error, but only once it happens — worth defaulting to heap allocation up front for any sizeable buffer rather than discovering the DRAM ceiling by hitting it.

## Shrink Canvas Memory with `lv_img_set_zoom`, Not a Smaller Widget

`lv_canvas`'s underlying class is `lv_img`, so it inherits `lv_img_set_zoom()`/`lv_img_set_pivot()` — a canvas can render a *smaller* buffer scaled back up to its intended on-screen footprint, for a `SCALE²` memory cut with no change to how much screen space it occupies:

```cpp
// Buffer at 1/SCALE resolution — SCALE=2 here is a 4x memory cut
// (240x240 RGB565 = ~112.5KB -> 120x120 = ~28.8KB).
buf_ = new (std::nothrow) lv_color_t[BUF_W * BUF_H];
lv_canvas_set_buffer(canvas_, buf_, BUF_W, BUF_H, LV_IMG_CF_TRUE_COLOR);
lv_img_set_pivot(canvas_, 0, 0);       // scale from the top-left corner,
lv_img_set_zoom(canvas_, 256 * SCALE); // not LVGL's default center pivot
```

All drawing/fill/hit-testing math then happens in the small buffer's coordinate space; only the touch-input mapping (divide the raw touch point by `SCALE`) and the widget's on-screen position/size need to know about the full, zoomed footprint. `lv_img_set_zoom()` calls `lv_obj_refresh_ext_draw_size()` internally, so `lv_obj_invalidate()` correctly redraws the *full* zoomed area, not just the small nominal buffer size — no extra invalidation bookkeeping needed. The visible cost is chunkier, blockier pixels (each buffer pixel becomes a `SCALE x SCALE` screen block), which reads as "coarse" or "pixel art" rather than a defect — a reasonable trade for a freehand paint app that needed its full on-screen footprint back after `new (std::nothrow)` alone wasn't enough headroom.

**Why:** Color Fusion's canvas was enlarged to fill the screen (~112.5KB buffer), which reliably produced "Out of memory" on this no-PSRAM device even with the nothrow fix already in place — the fix wasn't shrinking the *visible* canvas back down, it was shrinking the *buffer* while keeping the on-screen size.
