# LVGL 8 + ESP32 Lessons Learned

Hard-won knowledge from building 13 games and a clock/weather app on the ESP32-2432S028 (CYD) with LVGL 8, 320x240 TFT, and resistive touchscreen.

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

## ArduinoJson Buffer Sizes

`StaticJsonDocument<N>` pool: each object member and array element uses ~16 bytes on ESP32 (32-bit). **Fields added last are silently dropped** when the pool is full.

```cpp
// BAD: if doc overflows, s0/s1 are silently missing
score_[0] = doc["s0"] | 0;  // defaults to 0, zeroing the score!

// GOOD: preserve current value if field is missing
score_[0] = doc["s0"] | score_[0];
```

The serialized JSON output buffer (`char buf[N]`) is separate from the document pool — size them independently.
