# SmartAqua — ESP32 Fish Feeder

An automatic fish feeder: a wall-mounted touchscreen controller and a remote feeder node that
drives an auger motor through a relay.

* **HMI** — Waveshare ESP32-S3-Touch-LCD-4.3B. A 4.3" capacitive touch panel running LVGL 8.4 on
  ESP-IDF 5.2. Owns the schedule editor, manual feed, history, and settings.
* **Feeder node** — a classic ESP32 devkit next to the tank. Owns the relay, the motor, the clock,
  and the schedule. Feeds on its own even if the network is gone.
* **Link** — ESP-NOW over 2.4 GHz. No router, no cloud, no subscription.

## Why two boards

The S3 board's GPIO budget is fully spent on its own display and peripherals:

| Consumer | Pins |
| --- | --- |
| RGB LCD (16-bit parallel) | 20 |
| SD card (SPI) | 3 |
| I2C bus (touch, RTC, expander) | 2 |
| RS485 | 2 |
| CAN | 2 |
| RTC alarm interrupt | 1 |
| Onboard button | 1 |

Everything else hangs off a CH422G I2C expander whose spare pins are not brought out to a documented
header. The second ESP32 has free GPIO to spare, sits next to the motor where the wiring is short,
and keeps the high-current switching away from the display board. See
[docs/01-architecture.md](docs/01-architecture.md).

```
             ESP-NOW (2.4 GHz)
  ┌──────────────────────┐  ─────────────────────►  ┌────────────────────────┐
  │  HMI  (ESP32-S3)     │   FEED / STOP / CFG      │  Feeder node (ESP32)   │
  │  LVGL 4.3" touch     │  ◄─────────────────────  │  relay + auger motor   │
  │  editor, history     │   STATUS / ACK / event   │  owns schedule + clock │
  └──────────────────────┘                          └────────────────────────┘
                                                              │
                                                       5 V relay → motor
```

## Hardware

| Qty | Part |
| --- | --- |
| 1 | Waveshare ESP32-S3-Touch-LCD-4.3B |
| 1 | ESP32 devkit (ESP32-WROOM-32 / DevKitC class) |
| 1 | 5 V 1-channel relay module |
| 1 | Auger / screw feeder driven by a DC motor |

An optional DS3231 RTC module on the feeder node is recommended — the ESP32's internal clock does not
survive a power cut, and losing the clock silently loses the schedule.

## Quickstart

Requires ESP-IDF **5.2.x** — see [docs/03-toolchain-setup.md](docs/03-toolchain-setup.md).

```bash
cd apps/hmi && idf.py set-target esp32s3 && idf.py -p COM<n> build flash monitor
```

```bash
cd apps/feeder && idf.py set-target esp32 && idf.py -p COM<n> build flash monitor
```

`Ctrl-]` exits the monitor.

Do not wire a motor to anything until you have worked through
[docs/08-testing-and-bringup.md](docs/08-testing-and-bringup.md). Step 0 flashes the vendor's
prebuilt test firmware and proves the board before you write a line of code.

## Documentation

| Doc | What it covers |
| --- | --- |
| [docs/01-architecture.md](docs/01-architecture.md) | System design, the five non-negotiable design rules, data flow |
| [docs/02-hardware-and-wiring.md](docs/02-hardware-and-wiring.md) | Verified pin maps, relay and motor wiring, power, safety |
| [docs/03-toolchain-setup.md](docs/03-toolchain-setup.md) | Windows setup: ESP-IDF, VS Code, git, `gh`, drivers, Figma |
| [docs/04-repo-and-git-workflow.md](docs/04-repo-and-git-workflow.md) | Layout, branches, commits, PRs, releases, asset policy |
| [docs/05-firmware-workflow.md](docs/05-firmware-workflow.md) | Build/flash/monitor, config policy, NVS, logging, OTA |
| [docs/06-ui-design-workflow.md](docs/06-ui-design-workflow.md) | Figma → LVGL hand-off, tokens, screens, fonts, icons |
| [docs/07-wireless-protocol.md](docs/07-wireless-protocol.md) | ESP-NOW pairing, framing, packets, loss-of-link behaviour |
| [docs/08-testing-and-bringup.md](docs/08-testing-and-bringup.md) | Bring-up order, test matrix, soak test, release checklist |
| [docs/09-roadmap-and-milestones.md](docs/09-roadmap-and-milestones.md) | M0–M5 with acceptance criteria |
| [docs/10-decision-log.md](docs/10-decision-log.md) | ADRs: why these choices, and what would change them |
| [design/](design/README.md) | Figma source of truth, design tokens, screen specs |

## Repository layout

```
apps/hmi/            ESP32-S3 firmware (LVGL UI)
apps/feeder/         ESP32 firmware (relay, schedule, ESP-NOW)
components/          Shared code with no IDF dependency (protocol, schedule logic)
design/              Figma links, design tokens, per-screen specs
docs/                This documentation
```

## Status

Scaffolding and documentation only. No application firmware yet —
see [docs/09-roadmap-and-milestones.md](docs/09-roadmap-and-milestones.md).
