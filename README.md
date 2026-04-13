# CYD Arcade

Multi-game touchscreen arcade machine for the ESP32-2432S028 (Cheap Yellow Display).

Built with LVGL 8, TFT_eSPI, PlatformIO, and ElegantOTA.

## Hardware

- **Board:** ESP32-2432S028 (CYD) — ESP32-WROOM-32, 4MB flash
- **Display:** 2.8" ILI9341 TFT, 320x240, resistive touchscreen (XPT2046)
- **Extras:** RGB LED, speaker (GPIO 26), light sensor, SD card slot

## Games

| Game | Players | Online | Description |
|------|---------|--------|-------------|
| Snake | 1P | - | D-pad controlled, progressive speed |
| Tic-Tac-Toe | 2P | Yes | Classic 3x3 grid |
| Pong | 1-2P | Yes | Touch paddle, vs CPU or online |
| Connect 4 | 2P | Yes | Drop discs, first to 4 wins |
| Memory Match | 1-2P | Yes | Card matching with 6 pairs |
| Checkers | 2P | Yes | Full rules with kings and forced jumps |
| Dots & Boxes | 2P | Yes | Claim boxes by completing lines |

## Features

- **LVGL UI** — Dark-themed interface with animated screen transitions
- **OTA Updates** — Custom web UI at `http://<IP>/update` showing device info, firmware version, and partition status
- **Dual OTA Partitions** — app0/app1 alternating, with automatic rollback protection
- **Network Multiplayer** — UDP broadcast peer discovery on port 4328, invite/accept lobby system
- **Settings Screen** — Shows IP, MAC, RSSI, firmware version, partition, heap, uptime, and OTA URL

## Build & Flash

### PlatformIO (recommended)

```bash
# Build
pio run

# Flash via USB
pio run -t upload

# Serial monitor
pio device monitor
```

### esptool (alternative)

```bash
esptool --chip esp32 --port /dev/cu.usbserial-110 write_flash \
  0x1000  .pio/build/esp32-2432S028/bootloader.bin \
  0x8000  .pio/build/esp32-2432S028/partitions.bin \
  0xe000  /Users/arizvi/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin \
  0x10000 .pio/build/esp32-2432S028/firmware.bin
```

### OTA (over-the-air)

```bash
# CLI upload
./ota-upload.sh <DEVICE_IP>

# Or open in browser
http://<DEVICE_IP>/update
```

## WiFi Setup

Create `include/secrets.h` (gitignored):

```cpp
#pragma once
#define WIFI_SSID     "YourSSID"
#define WIFI_PASSWORD "YourPassword"
```

## Project Structure

```
├── platformio.ini          # Build config, TFT_eSPI pins, library deps
├── partitions.csv          # Dual OTA partition layout
├── lv_conf.h               # LVGL configuration
├── ota-upload.sh           # CLI OTA upload script
├── include/
│   ├── config.h            # Pin definitions and constants
│   └── secrets.h           # WiFi credentials (gitignored)
├── src/
│   ├── main.cpp            # Setup/loop orchestration
│   ├── hal/                # Display, backlight, LED, audio drivers
│   ├── net/                # WiFi, OTA, UDP discovery
│   ├── ui/                 # Screen manager, menu, settings, shared styles
│   └── games/              # Snake, Tic-Tac-Toe, Memory, Pong, Connect 4,
│                           # Checkers, Dots & Boxes
```

## Multiplayer

Two-player games use UDP broadcast discovery on the local network:

1. Both devices select "Online" from the game menu
2. Peers appear in the lobby list within seconds
3. Tap a peer to send an invite
4. Other device sees an Accept/Decline popup
5. Game starts once invite is accepted

Each game syncs state independently — Tic-Tac-Toe and Connect 4 send moves, Pong syncs ball/paddle positions at 20fps, Memory Match syncs the card layout and flips.
