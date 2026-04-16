#pragma once

// ── Display (ILI9341 on HSPI) ──
#define TFT_BL_PIN      21
#define SCREEN_WIDTH    320
#define SCREEN_HEIGHT   240

// ── XPT2046 Touch (separate SPI from display) ──
#define XPT2046_CS    33
#define XPT2046_CLK   25
#define XPT2046_MOSI  32
#define XPT2046_MISO  39
#define XPT2046_IRQ   36

// ── Touch calibration (landscape rotation 1) ──
#define TOUCH_CAL_X_MIN  200
#define TOUCH_CAL_X_MAX  3700
#define TOUCH_CAL_Y_MIN  240
#define TOUCH_CAL_Y_MAX  3800

// ── RGB LED (active LOW) ──
#define LED_R_PIN        4
#define LED_G_PIN       16
#define LED_B_PIN       17

// ── Audio ──
#define SPEAKER_PIN     22

// ── Light sensor ──
#define LDR_PIN         34

// ── Buttons ──
#define BOOT_BTN_PIN     0

// ── Networking ──
#define DISCOVERY_PORT  4328
#define OTA_PORT          80
#define MAX_PEERS          8
#define ESPNOW_CHANNEL    1

// ── LVGL buffers ──
#define LVGL_BUF_LINES   10
