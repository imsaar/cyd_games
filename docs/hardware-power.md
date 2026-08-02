# CYD Hardware: Power & Connections

This doc covers powering the ESP32-2432S028 ("CYD") via the rear **S1 / S3** solder pads as an alternative to the USB port — useful for a permanent install, an internal LiPo, or wiring into a project enclosure.

> **Verify before wiring.** Pad labels and polarity vary across CYD revisions. Probe `S1` against a known `GND` with a multimeter (continuity to the USB shield) before connecting a power source. Reverse polarity will destroy the board.

## Board orientation (rear view)

The diagram below is the **back** of the CYD — the side with the ESP32 module, the USB port at the bottom, and the three JST connectors. The display itself is on the opposite side.

```
       ┌──────────────────────────────────────────────────┐
       │                                                  │
       │       ┌────────────────────────────────┐         │
       │       │                                │         │
       │       │           ESP32-WROOM          │         │
       │       │            (shield)            │         │
       │       │                                │         │
       │       └────────────────────────────────┘         │
       │                                                  │
       │   ┌────┐                                ┌────┐   │
       │   │ P1 │   ┌───┐ ┌───┐ ┌───┐            │ P3 │   │
       │   │JST │   │S1 │ │S2 │ │S3 │            │JST │   │
       │   │    │   │ + │ │ • │ │ − │            │    │   │
       │   └────┘   └─┬─┘ └───┘ └─┬─┘            └────┘   │
       │              │           │                       │
       │              │           │                       │
       │           ┌──┴───────────┴──┐                    │
       │           │     CN1 JST     │                    │
       │           └─────────────────┘                    │
       │                                                  │
       │              ┌─────────────┐                     │
       │              │   USB-C /   │                     │
       │              │   micro-B   │                     │
       │              └─────────────┘                     │
       └──────────────────────────────────────────────────┘
                              ▲
                              │  (cable exits here)
```

## Power wiring (S1 / S3)

```
   external power               CYD rear pads
   ┌──────────────┐
   │              │
   │   V+   ●─────┼────────────►  S1   (V+ rail)
   │              │
   │   GND  ●─────┼────────────►  S3   (GND)
   │              │
   └──────────────┘

   S2 is the middle pad (often unused or routed
   elsewhere depending on revision — leave open).
```

**Notes**

- `S1` ties into the V+ rail shared with the USB port. A regulated **5 V** input (USB charger, buck converter, boost-equipped charging module) is the safe default and gives the on-board regulator full headroom for 3.3 V.
- Do **not** feed more than **5 V** into `S1` — the on-board regulator is sized for the USB rail.
- Powering via `S1`/`S3` and the USB port simultaneously is generally fine (USB has back-feed protection on most CYD revs), but unplug the bench supply before connecting USB to a PC just to be safe.

## LiPo battery via a charging module

Typical portable build: a single-cell LiPo (3.7 V nominal, 4.2 V fully charged, ~3.0 V cutoff) feeds a TP4056-style charger/protection module, whose `OUT+` / `OUT-` (or `B+` / `B-` on simpler modules) wire to the CYD's `S1` / `S3` pads.

```
   ┌──────────┐       ┌───────────────────────┐         CYD rear
   │          │  B+   │   TP4056 charger /    │ OUT+
   │  LiPo    ●───────┤   protection module   ├─────────►  S1
   │  3.7 V   │       │                       │
   │  cell    ●───────┤   (USB-C in for       ├─────────►  S3
   │          │  B−   │    charging)          │ OUT−
   └──────────┘       └───────────┬───────────┘
                                  │
                          USB-C (charging only)
                          ▼
                       wall adapter
```

**What to know with raw LiPo (no boost converter)**

- Output to `S1` swings between **~4.2 V (full)** and **~3.0 V (cutoff)** — below the 5 V the USB port would supply. This works on many CYD revisions because the on-board LDO has low enough dropout to keep 3.3 V stable above ~3.6–3.7 V input, but **expect the display to dim, the WiFi radio to misbehave, or the device to reset as the cell drops past ~3.5 V**. Recharge before then.
- If you want full runtime down to the LiPo cutoff (~3.0 V), add a small **5 V boost converter** (MT3608 etc.) between the charging module's `OUT` and the CYD's `S1`. That gives the on-board regulator constant 5 V regardless of cell state.
- Always use a **protected** LiPo or a charging module with built-in over-discharge protection — bare cells run below 3.0 V degrade fast.
- Don't connect the LiPo's USB-C-to-charge port and the CYD's USB port at the same time unless your charging module explicitly supports load-sharing; otherwise you can back-feed 5 V through the module into the cell.

## Other connectors (reference)

| Connector | Common pinout (verify per board) | Typical use |
|-----------|----------------------------------|-------------|
| P1 (JST 1.25mm 4-pin) | GND, GPIO22, GPIO27, 3V3 | I²C / extension |
| P3 (JST 1.25mm 4-pin) | GND, GPIO35, GPIO22, 3V3 | Sensor input |
| CN1 (JST 1.25mm 4-pin) | GND, TX, RX, 3V3 | UART / programming |
| SPK (JST 1.25mm 2-pin) | Speaker +/− | Amp-driven speaker output |
| USB | 5 V in, USB serial via CH340 | Flashing + power |

For the GPIO pins this firmware actually uses (display, touch, RGB LED, speaker, LDR), see [`include/config.h`](../include/config.h).

## Speaker (GPIO26, SPK JST connector)

The 2.8" CYD has an onboard class-D amp (SC8002B/PAM8002A-class chip) wired to the 2-pin JST "SPK" connector, driven directly by a single GPIO — no I2S wiring needed. This firmware drives it the same way it drives the GPIO22 piezo buzzer: `ledcWriteTone` square-wave PWM (`src/hal/audio.cpp`), just on a separate LEDC channel/pin. Select it in Settings → "Audio Out" (default is the buzzer).

**If the speaker stays silent after selecting it in Settings:** check `http://<device-ip>/debug` first — it now includes a live log of `[audio]`/`[sound]` lines (`src/utils/debug_log.cpp`) showing the channel, pin, and frequency each time a tone plays, with no USB connection needed (useful once the board is in an enclosure). If the log shows `ledcWriteTone_returned` matching the requested frequency, the firmware side is confirmed working and the fault is downstream — check the JST connector is fully seated, confirm the amp chip is actually populated near the connector (some cheap board batches leave the footprint empty), or try swapping the speaker itself. That last one has been the actual root cause here at least once: a dead/faulty speaker unit produced this exact symptom with a firmware-side log that looked completely correct.
