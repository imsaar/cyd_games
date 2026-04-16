# CYD Arcade

Multi-game touchscreen arcade machine for the ESP32-2432S028 (Cheap Yellow Display).

Built with LVGL 8, TFT_eSPI, PlatformIO, and ElegantOTA.

## Hardware

- **Board:** ESP32-2432S028 (CYD) — ESP32-WROOM-32, 4MB flash
- **Display:** 2.8" ILI9341 TFT, 320x240, resistive touchscreen (XPT2046)
- **Extras:** RGB LED, passive piezo buzzer (GPIO 22), light sensor, SD card slot

## Games

| Game | Players | Network | Description |
|------|---------|---------|-------------|
| Battleship | 1-2P | Yes | Place ships on 8x8 grid, fire to sink opponent's fleet, vs CPU/local/network |
| Tic-Tac-Toe | 2P | Yes | Classic 3x3 grid |
| Pong | 1-2P | Yes | Touch paddle, vs CPU or network, first to 10 |
| Connect 4 | 1-2P | Yes | vs CPU, local, or network, 4-direction win check |
| Memory Match | 1-2P | Yes | Card matching with 6 pairs, solo/local/network |
| Checkers | 1-2P | Yes | vs CPU, full rules with kings and forced jumps |
| Chess | 1-2P | Yes | vs CPU, Unicode piece symbols, full rules |
| Anagram | 1P | - | Unscramble words, 20s timer, 10 rounds, 80+ words |
| Dots & Boxes | 2P | Yes | Claim boxes by completing lines |
| Whack-a-Mole | 1P | - | Whack brown moles, spare baby faces, POW effects, 30s |
| Cup Pong | 1P | - | Bounce ball off table into 10 red cups, 10 throws |
| Sudoku | 1P | - | 9x9 puzzle, number pad, check cell correctness, new game |
| Pictionary | 2P | Yes | Draw & guess, 6 colors, 30s timer, local or network with live drawing sync |

## Features

- **LVGL UI** — Dark-themed interface with animated screen transitions
- **OTA Updates** — Custom web UI at `http://<IP>/update` showing device info, firmware version, and partition status
- **Dual OTA Partitions** — app0/app1 alternating, with automatic rollback protection
- **Network Multiplayer** — Works over WiFi (UDP) or ESP-NOW (no WiFi needed), invite/accept lobby system
- **ESP-NOW** — Peer-to-peer multiplayer without WiFi infrastructure, automatic fallback when WiFi is unavailable
- **NTP Clock** — Current date/time (Pacific) displayed on the main menu when WiFi is connected
- **Sound Effects** — Piezo buzzer feedback for moves, opponent moves, wins, losses, and startup
- **Persistent Settings** — Brightness, display inversion, and device name saved to NVS across power cycles
- **Settings Screen** — Device name editor, brightness slider, display inversion toggle, WiFi on/off switch, IP, MAC, RSSI, firmware version, partition, heap, uptime, OTA URL

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

If WiFi is unavailable or disabled in Settings, multiplayer automatically uses ESP-NOW instead.

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
│   ├── hal/                # Display, backlight, LED, audio, sound effects, preferences
│   ├── net/                # WiFi, OTA, UDP/ESP-NOW discovery
│   ├── ui/                 # Screen manager, menu, settings, shared styles
│   └── games/              # Battleship, Tic-Tac-Toe, Memory, Pong, Connect 4,
│                           # Checkers, Chess, Anagram, Dots & Boxes,
│                           # Whack-a-Mole, Cup Pong, Sudoku, Pictionary
```

## Multiplayer

Two-player games support two transport modes:

- **WiFi (UDP)** — Broadcast discovery on the local network (port 4328)
- **ESP-NOW** — Direct peer-to-peer, no WiFi network needed, ~200m range

The transport is selected automatically at boot based on WiFi connectivity, or manually via the WiFi toggle in Settings.

### How to play

1. Both devices select "Network" from the game menu
2. Peers appear in the lobby list within seconds
3. Tap a peer to send an invite
4. Other device sees an Accept/Decline popup
5. Game starts once invite is accepted

Each game syncs state independently — turn-based games send moves, Pong syncs ball/paddle positions at 20fps, Memory Match syncs the card layout and flips.

For technical details, see the design documents:
- **[ESP-NOW Transport Design](docs/esp-now-transport.md)** — Transport layer, discovery protocol, peer management, packet formats
- **[Game Network Sync Design](docs/game-network-sync.md)** — Per-game sync patterns, JSON message formats, reliability strategies

### Device name

Set a custom device name in Settings (persisted across reboots). This name appears in lobby lists and invite dialogs instead of the default MAC-based `CYD-XXYY`.
