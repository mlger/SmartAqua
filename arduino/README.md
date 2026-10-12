# SmartAqua firmware — Arduino IDE

The complete SmartAqua system as two Arduino sketches: a touchscreen HMI and an
autonomous feeder node, talking ESP-NOW. This is the supported build path —
no ESP-IDF install required. (The ESP-IDF scaffolding under `apps/` is kept
for reference; the architecture and protocol docs apply to both.)

```
arduino/
├── SmartAquaHMI/        HMI firmware  — ESP32-S3-Touch-LCD-4.3B (LVGL 8.4 UI)
├── SmartAquaFeeder/     Feeder firmware — classic ESP32 devkit (relay, schedule)
└── libraries/
    └── SmartAquaCore/   Shared library: wire protocol, schedule logic, clamp
```

## Bill of materials

| Qty | Part | Role |
| --- | --- | --- |
| 1 | Waveshare ESP32-S3-Touch-LCD-4.3B | HMI: display, touch, PCF85063A RTC |
| 1 | ESP32 devkit (ESP32-WROOM-32 class) | Feeder node: relay, schedule, clock |
| 1 | 5 V 1-channel relay module (3.3 V-triggerable, active-high) | Switches the auger motor |
| 1 | DC auger/screw feeder motor + supply | The thing that feeds |
| 1 | Flyback diode (1N4007/1N5819) across the motor | Kills the inductive kick |
| 1 | Momentary button to GND | Feeder: manual feed / pairing (GPIO 27) |
| 1 | DS3231 RTC module *(recommended, optional)* | Battery-backed clock holdover on the feeder |

Wiring, power tree, and the safety notes that outrank everything else:
[../docs/02-hardware-and-wiring.md](../docs/02-hardware-and-wiring.md). Pin summary:
relay IN → **GPIO 26**, button → **GPIO 27** (internal pull-up), LED → **GPIO 2**,
optional DS3231 → I2C **21/22**.

## One-time setup

### 1. Arduino IDE + core

Install the Arduino IDE 2.x, then in **Boards Manager** install
**esp32 by Espressif Systems** (tested with **3.1.1**; any 3.1.x should work —
the 2.x cores are NOT supported by ESP32_Display_Panel 1.0.0).

### 2. Libraries (exact versions matter)

In **Library Manager**, install:

| Library | Version | Why |
| --- | --- | --- |
| `lvgl` | **8.4.0** | UI framework — the panel library's port targets 8.x, and this repo's UI code is written against the v8 API |
| `ESP32_Display_Panel` | **1.0.0** | Board/LCD/touch abstraction (as vendored by Waveshare for this board) |
| `ESP32_IO_Expander` | **1.0.1** | CH422G expander driver |
| `esp-lib-utils` | **0.1.2** | Dependency of the above |

These are the exact versions bundled in Waveshare's
`ESP32-S3-Touch-LCD-4.3B-BOX-Demo` package. Newer major versions change APIs
(especially LVGL 9) — do not "upgrade" casually.

### 3. Install the shared library and the LVGL config

Two copies, both mechanical:

1. **SmartAquaCore** — copy `arduino/libraries/SmartAquaCore/` into your
   sketchbook's `libraries/` folder (`~/Arduino/libraries/` on Linux,
   `Documents/Arduino/libraries/` on Windows).
2. **`lv_conf.h`** — copy `arduino/SmartAquaHMI/lv_conf.h` into the lvgl
   library folder itself, next to its `src/` folder
   (`~/Arduino/libraries/lvgl/lv_conf.h`). This enables the fonts and buffer
   sizes the UI expects. (This is the standard LVGL 8 Arduino arrangement;
   the vendored `lvgl_v8_port` finds it there.)

### 4. Board settings

Open `arduino/SmartAquaHMI/SmartAquaHMI.ino`, select:

| Setting | HMI (ESP32-S3-Touch-LCD-4.3B) | Feeder (ESP32 devkit) |
| --- | --- | --- |
| Board | ESP32S3 Dev Module | ESP32 Dev Module |
| USB CDC On Boot | Enabled | (any) |
| USB Mode | Hardware CDC and JTAG | — |
| Flash Size | 16MB | 4MB (default) |
| PSRAM | OPI PSRAM | — |
| Flash Mode / Freq | QIO 80MHz | defaults |
| Partition Scheme | Huge APP (3MB) | Default |

Plug the HMI board by its USB-C port and upload. If the port does not appear,
hold **BOOT** while clicking reset, and try again — the board's single USB-C
routes through an FSUSB42 switch between native USB-Serial-JTAG and the
onboard CH343 bridge (see docs/03 § USB), so enumeration can land on either.

### 5. First run

1. **Flash the feeder first.** On boot it drives the relay OFF as its first
   action (that is deliberate — check it with a meter before wiring the motor,
   per docs/08). The serial console (115200) prints the protocol self-test and
   the clamp test.
2. **Flash the HMI.** You get the Home screen with "Not paired" and "Time?".
3. **Set the time** on the HMI: Settings → TIME → *Set time* (this writes the
   PCF85063A; fit a CR927 backup cell or the time is lost on power cuts).
4. **Pair:** Settings → LINK → *Pair*, then **hold the feeder's button for
   3 s** (its LED starts fast-blinking for 60 s). Pairing completes on its own;
   the HMI pushes time, feeding config, and its schedule, and pulls history.
5. **Calibrate** before trusting portions: Settings → FEEDING → *Calibrate*,
   catch what the 20 s test feed dispenses, weigh it, enter the grams.
6. **Schedule** a feed on the Schedule tab, and watch the serial consoles on
   both boards while it fires.

## How the two boards talk

Everything is documented in [../docs/07-wireless-protocol.md](../docs/07-wireless-protocol.md)
and implemented once, in `SmartAquaCore`. The short version:

* **Mode A** — fixed channel 1, no router, no WiFi credentials anywhere.
  Both nodes are peers on ESP-NOW with a compiled-in device-family PMK/LMK
  (`SmartAquaCore/src/saq_keys.h` — change both keys before deploying).
* **Pairing** needs a human at the tank: the feeder beacons only while its
  button is held, the HMI accepts beacons only while the pair flow is armed.
  The PAIR_REQ/ACK handshake happens unencrypted, then both sides upgrade the
  peer to the LMK — the first STATUS may be lost in that handover and simply
  arrives on the next 10 s interval.
* **The feeder is autonomous.** Schedule and calibration live in its NVS. The
  HMI is a view plus a keyboard; unplug it and the fish still get fed.
* **The relay is fail-safe by construction**: de-energised is off, and a hard
  `SAQ_MAX_FEED_MS` (30 s) clamp sits below the command layer, not in it.

## Host tests

The shared library is deliberately platform-free, so its logic is testable on
any machine with a C compiler:

```bash
scripts/run_host_tests.sh     # protocol vectors + schedule + clamp, ~1 s
```

This runs the docs/07 test vectors byte-for-byte. Both firmwares also run the
same self-test at boot and print the result on the serial console.

## Deviations from the design docs (honest list)

* **FeedNow is the Feed tab** rather than a tab-bar-less push screen
  (design/screens.md). During `Running` the tab bar locks, which preserves the
  "cannot wander off mid-task" guarantee. One implementation instead of two.
* **Schedule deletion** is a Delete button in the editor with a confirm
  (instead of swipe-left-to-reveal); the wire encoding compacts the list —
  the last slot moves into the deleted slot and `count` shrinks.
* **Swipe/scroll niceties** and the Figma pixel-match pass are future work;
  the screen set (Home/FeedNow/Schedule/Editor/History/Settings/Diagnostics)
  and all their states exist.
* **HMI history** is log-driven: live FEED_DONE updates the feed screen, and
  entries persist via LOG_ENTRY reconciliation (STATUS → auto STATUS_REQ →
  replay). An offline stretch reconciles exactly as docs/01 describes.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| HMI: white screen / no console | CDCOnBoot or USBMode wrong for the port you are on; try the other (FSUSB42 switch — see docs/03) |
| HMI: image shifted or split | panel revision — confirm 800×480 with the vendor colour-bar test (docs/08 step 0) |
| Pairing finds nothing | feeder not in its 60 s window (hold button 3 s), or channel/keys differ between the two sketches |
| Paired, but nothing delivers | the LMK/PMK pair differs between the firmwares — encrypted frames vanish silently (docs/07 § Encryption) |
| Relay clicks at boot | wrong pin (you want GPIO 26), or an active-low module — see the avoid-table in docs/02 |
| Feeds fire at odd times | clock unsynced — the LED triple-blinks, Diagnostics shows "UNSYNCED"; set time from the HMI |
| Build fails in lvgl headers | `lv_conf.h` not copied into the lvgl library folder, or an LVGL 9 got installed — pin 8.4.0 |
