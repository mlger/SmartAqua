# 02 — Hardware and wiring

Everything on this page is either **verified** (traced to a file in the vendor package, cited inline)
or explicitly marked **UNVERIFIED** (plausible but not checked against a physical board). Do not
remove an UNVERIFIED marker without measuring.

Vendor reference, referred to below as *the package*:

```
ESP32-S3-Touch-LCD-4.3B-BOX-Demo/          (extracted from ESP32-S3-Touch-LCD-4.3B-BOX-Demo.zip)
```

The download lives on the [Waveshare wiki](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-4.3B).
The wiki's own *Flash Firmware* section refers to `ESP32-S3-Touch-LCD-4.3B-Demo\Firmware\bin` — same
package, inconsistent wiki naming. The zip file's name is the one above.

It is gitignored — 93 MB — but the facts below are quoted from it so future readers do not need it on
disk. If you re-download it, the paths in the *Source* column will resolve. Facts marked **wiki** were
cross-checked against the wiki page itself (spec tables, pinouts, and the schematic PDF it links).

## Bill of materials

| Qty | Part | Role |
| --- | --- | --- |
| 1 | Waveshare ESP32-S3-Touch-LCD-4.3B | HMI: display, touch, RTC |
| 1 | ESP32 devkit, ESP32-WROOM-32 / DevKitC class | Feeder node: relay, schedule, clock |
| 1 | 5 V 1-channel relay module | Switches the auger motor |
| 1 | DC auger / screw feeder motor | Moves the food |
| 1 | 5 V supply for the relay coil | Isolated from the ESP32's 5 V pin |
| 1 | Motor supply, voltage per motor | Sized for inrush, not running current |
| 1 | Flyback diode (DC motor) or RC snubber (AC) | Kills the inductive kick |
| 1 | DS3231 RTC module **(recommended)** | Battery-backed holdover — design rule 3 |
| 1 | Inline fuse on the motor circuit | See *Mechanical backstop* below |

## HMI board — verified peripheral map

| Function | Detail | Source |
| --- | --- | --- |
| Target | `esp32s3`, 240 MHz | `ESP-IDF/08_lvgl_Porting/sdkconfig.defaults` |
| Flash | 16 MB, QIO, 80 MHz | same |
| PSRAM | Octal, 80 MHz, fetch-instructions + rodata | same |
| Console | USB-Serial-JTAG over the native USB (GPIO 19/20) — the vendor's chosen default. If the USB-C port is routed to the CH343 instead (see the USB row above), the console must be UART0 | same, plus `03_SD_Test.ino:23-24` |
| Panel | RGB 16-bit parallel, RGB565, 800 × 480 @ 16 MHz pclk | `Arduino/examples/09_lvgl_Porting/esp_panel_board_custom_conf.h:104`, `ESP-IDF/08_lvgl_Porting/main/waveshare_rgb_lcd_port.h:36`, **wiki spec table** |
| Panel controller | **ST7262** — the wiki's datasheet list, the Arduino demo configs (`EXAMPLE_LCD_NAME ST7262`), and the IO test's README all agree. The `ST7701` in the IDF example's README is stale boilerplate from the upstream Espressif example it derives from. Confirm on your unit with the colour-bar test anyway | `esp_panel_board_custom_conf.h:68`, `Arduino/examples/05_IO_Test/waveshare_io_port.h`, wiki datasheet links |
| Touch | GT911 capacitive, I2C @ 400 kHz, `esp_lcd_touch_gt911` 1.1.1 | `ESP-IDF/08_lvgl_Porting/components/espressif__esp_lcd_touch_gt911/idf_component.yml` |
| I2C bus | SDA 8, SCL 9, 400 kHz, port 0 | `waveshare_rgb_lcd_port.h:17-20` |
| IO expander | CH422G — 8 quasi-bidirectional pins (EXIO0–7) plus 4 output pins (OC0–3). Register-as-address device: it ACKs at its command addresses (0x23, 0x24, 0x26, 0x38 in the vendor driver), **not** one fixed slave address — see the expander section below | `ESP-IDF/05_IO_Test/lib/CH422G/CH422G.h`, `ESP-IDF/08_lvgl_Porting/components` driver source |
| RTC | PCF85063A at I2C address **0x51** | `Arduino/examples/04_RTC_Test/waveshare_pcf85063a.h:33` |
| RTC backup battery | CR927 coin cell in the onboard socket — without it the RTC loses time on power loss | wiki *Onboard Resources* item 20 |
| USB | One USB-C port, routed through an FSUSB42 switch between the ESP32-S3 native USB (GPIO 19/20) and an onboard **CH343** USB-UART bridge on UART0 (GPIO 43/44) — see the toolchain doc's USB section | schematic, wiki component list |
| LVGL | 8.4.0, constraint `>8.3.9,<9` | `ESP-IDF/08_lvgl_Porting/components/lvgl__lvgl/idf_component.yml` |

## HMI board — RGB LCD pin map

All verified from `ESP-IDF/08_lvgl_Porting/main/waveshare_rgb_lcd_port.h:44-63`, confirmed identically
in `Arduino/examples/09_lvgl_Porting/esp_panel_board_custom_conf.h:127-156`.

| Signal | GPIO | Signal | GPIO |
| --- | --- | --- | --- |
| VSYNC | 3 | DATA6 | 0 |
| HSYNC | 46 | DATA7 | 45 |
| DE | 5 | DATA8 | 48 |
| PCLK | 7 | DATA9 | 47 |
| DATA0 | 14 | DATA10 | 21 |
| DATA1 | 38 | DATA11 | 1 |
| DATA2 | 18 | DATA12 | 2 |
| DATA3 | 17 | DATA13 | 42 |
| DATA4 | 10 | DATA14 | 41 |
| DATA5 | 39 | DATA15 | 40 |

LCD reset and backlight are **not** GPIOs — they go through the expander. There is no `DISP` pin
(`-1`). Panel reset is driven active-low via expander pin 3, and the backlight via expander pin 2.

> Note the panel is 16-bit RGB565 but the vendor config runs **DATA_WIDTH 16 with RGB565 pixel
> bits**, not 18-bit. Do not "upgrade" to 18-bit without checking the panel.

## HMI board — other peripherals

| Function | Pins | Source |
| --- | --- | --- |
| I2C | SDA 8, SCL 9 | everywhere in the package |
| SD card, SPI mode | MOSI 11, CLK 12, MISO 13, CS = expander pin 4 | `Arduino/examples/03_SD_Test/waveshare_sd_card.h:20-23` |
| RS485 | RX 43, TX 44 | `Arduino/examples/02_RS485_Test/02_RS485_Test.ino:2-3` |
| CAN / TWAI | TX 15, RX 16 | `Arduino/examples/06_TWAItransmit/waveshare_twai_port.h:10-11` |
| RTC alarm interrupt | GPIO 6 — confirmed in the schematic (`IO6 ↔ RTC_INT`) | `Arduino/examples/04_RTC_Test/waveshare_pcf85063a.h:30`, schematic, **wiki pinout** |
| Touch IRQ | **GPIO 4 — this is the GT911's interrupt line (`TP_IRQ`), not an onboard button.** The wiki pinout and the schematic (`IO4 ↔ CTP_IRQ`) agree; the vendor's IDF demo merely configures it as a plain input and never uses it | **wiki pinout**, schematic, `waveshare_rgb_lcd_port.h:25` |

`IMPORTANT` — the vendor's macro and argument naming around I2C is inconsistent, so do not copy a
name at face value. The vendored `ESP32_IO_Expander` library's constructor is
`CH422G(int scl_io, int sda_io, uint8_t address)` — **scl first**, which is the opposite of most
Arduino libraries — and the demo sketches pass `(EXAMPLE_I2C_SCL_PIN, EXAMPLE_I2C_SDA_PIN, ...)`
while `waveshare_sd_card.h` declares `EXAMPLE_I2C_SDA_PIN 8` and `EXAMPLE_I2C_SCL_PIN 9`. The
physical result is consistent everywhere (SDA 8, SCL 9), but verify against the GPIO number, not the
macro name.

## HMI board — CH422G expander

The CH422G is a **12-I/O** chip: eight quasi-bidirectional pins the vendor calls EXIO0–EXIO7 (driver
pin numbers 0–7) plus four output pins OC0–OC3 (driver pin numbers 8–11). Its I2C interface is
unusual: the chip has **no single slave address** — it uses its function registers *as* addresses
(the vendor's IDF header documents 0x23, 0x24, 0x26 and 0x38; the Espressif driver hardcodes those
and ignores the address you pass it — the `0x20` in `esp_panel_board_custom_conf.h:351` is a
placeholder, never put on the wire). Expect an `i2cdetect` scan to show the expander answering at
several addresses in the 0x23–0x38 range, not one line at 0x20.

The vendor driver calls `enableAllIO_Output()` at init (`esp_panel_board_custom_conf.h:369`), so
every EXIO pin defaults to output. The pin map below is stated by the vendor's own IDF header
(`ESP-IDF/05_IO_Test/lib/CH422G/CH422G.h`) and matches the wiki's pinout tables and the schematic:

| Pin (driver #) | Function | Status |
| --- | --- | --- |
| EXIO0 (0) | Isolated digital input **DI0** | Claimed — not spare |
| EXIO1 (1) | Touch panel reset | Claimed |
| EXIO2 (2) | LCD backlight | Claimed |
| EXIO3 (3) | LCD reset | Claimed |
| EXIO4 (4) | SD card chip select | Claimed |
| EXIO5 (5) | Isolated digital input **DI1** — and, as `USB_SEL`, the line the Arduino demos drive to steer the FSUSB42 USB switch | Claimed under both names — see *Verify first* |
| EXIO6 (6) | — | Unused per the vendor's IDF header (unlabelled there) |
| EXIO7 (7) | — | Unused per the vendor's IDF header |
| OC0 (8) | Isolated digital output **DO0** (open-drain capable) | Claimed |
| OC1 (9) | Isolated digital output **DO1** | Claimed |
| OC2–OC3 (10–11) | — | Unused |

The vendor package itself uses two names for pin 5: the IDF header and the wiki call it DI1 (an
isolated input), while the Arduino SD demo calls the same pin `USB_SEL` and drives it low with a
comment saying high "enables FSUSB42UMX … and don't use USB Function". Both descriptions point at
the same net — it steers the USB switch and is read as the isolated input. Bench-verify which
state your firmware leaves it in before relying on either behaviour.

Even with EXIO6, EXIO7, and OC2–OC3 spare, the expander is the wrong place for the relay
([01-architecture.md](01-architecture.md#design-rules) rule 5): an I2C hang would leave you unable to
de-energise the relay, and the off path must be as short as possible.

## HMI board — complete GPIO budget

Every GPIO on the ESP32-S3, accounted for. This is the table that justifies the two-board design.

| GPIO | Use | GPIO | Use |
| --- | --- | --- | --- |
| 0 | LCD DATA6 | 25 | does not exist on S3 |
| 1 | LCD DATA11 | 26 | internal SPI flash |
| 2 | LCD DATA12 | 27 | internal SPI flash |
| 3 | LCD VSYNC *(also a strapping pin)* | 28 | internal SPI flash |
| 4 | GT911 touch IRQ (`TP_IRQ`) — verified, wiki + schematic | 29 | internal SPI flash |
| 5 | LCD DE | 30 | internal SPI flash |
| 6 | RTC alarm interrupt | 31 | internal SPI flash |
| 7 | LCD PCLK | 32 | internal SPI flash |
| 8 | I2C SDA | 33 | octal PSRAM — reserved |
| 9 | I2C SCL | 34 | octal PSRAM — reserved |
| 10 | LCD DATA4 | 35 | octal PSRAM — reserved |
| 11 | SD MOSI | 36 | octal PSRAM — reserved |
| 12 | SD CLK | 37 | octal PSRAM — reserved |
| 13 | SD MISO | 38 | LCD DATA1 |
| 14 | LCD DATA0 | 39 | LCD DATA5 |
| 15 | CAN TX | 40 | LCD DATA15 |
| 16 | CAN RX | 41 | LCD DATA14 |
| 17 | LCD DATA3 | 42 | LCD DATA13 |
| 18 | LCD DATA2 | 43 | RS485 RX |
| 19 | USB D− | 44 | RS485 TX |
| 20 | USB D+ | 45 | LCD DATA7 *(also a strapping pin)* |
| 21 | LCD DATA10 | 46 | LCD HSYNC *(also a strapping pin)* |
| 22 | does not exist on S3 | 47 | LCD DATA9 |
| 23 | does not exist on S3 | 48 | LCD DATA8 |
| 24 | does not exist on S3 | | |

**Result: zero free GPIO.** The only ways to reach a spare pin are the expander, or giving up
RS485 (43, 44) / CAN (15, 16). None of those is worth it when a second ESP32 costs less than the
cable you would run to the tank.

This conclusion no longer rests on a caveat. GPIO 4 was previously listed as "input in the IDF
demo, possibly unwired" — that question is settled: the wiki pinout and the schematic both wire it
to the GT911's touch-interrupt line (`TP_IRQ`). Every GPIO is consumed, and rule 5's noise argument
stands on its own regardless.

## Feeder node — pin plan

Assumes a classic ESP32 (ESP32-WROOM-32 / DevKitC class). **UNVERIFIED until you confirm the board.**

| Function | GPIO | Rationale |
| --- | --- | --- |
| Relay IN | **26** | Safe output. Not a strapping pin, no boot glitch, no ADC2 conflict. |
| Manual feed button | **27** | Safe output/input, internal pull-up available, away from the relay line |
| Optional motor current sense | **35** | ADC1, so it still works while WiFi is on (ADC2 does not) |
| Optional DS3231 RTC | I2C 21 (SDA) / 22 (SCL) | The classic ESP32's default I2C pins. Note these are *different physical pins* from the S3 board's 8/9 — do not copy an S3 pin number onto this chip. |
| Status LED | 2 or 13 | Optional; GPIO 2 is the onboard LED on most devkits |

### GPIOs to avoid on the classic ESP32

This table exists because a beginner's relay wiring lands on one of these by accident and produces a
bug that looks like a software fault.

| GPIO | Problem |
| --- | --- |
| 6–11 | Connected to the SPI flash on the module. Driving these bricks the board. |
| 34, 35, 36, 37, 38, 39 | **Input only.** No output driver at all — a relay here never switches. |
| 0 | Boot strapping. Also the BOOT button. |
| 2 | Boot strapping; onboard LED on many devkits; must be low or floating at boot. |
| 5 | Boot strapping; emits a PWM burst at boot, so anything on it twitches on reset. |
| 12 | **Dangerous.** Strapping pin selecting flash voltage. Held high at boot, the board may fail to boot or misconfigure its flash supply. This is the one that wastes an afternoon. |
| 15 | Boot strapping; emits boot log / PWM at boot, so a relay here twitches on every reset. |
| 1, 3 | UART0 TX/RX — used for programming and the serial monitor. |
| 19, 20 | USB D−/D+ on boards with native USB. |

Note the pattern: pins that glitch or float **high** during reset are actively harmful for a relay,
because `high` on an active-high relay module means the motor runs for the first milliseconds of every
boot. GPIO 26 is chosen precisely because it stays low and quiet.

### Relay module selection

| Property | What to pick | Why |
| --- | --- | --- |
| Logic level | **3.3 V-compatible trigger** | The ESP32 outputs 3.3 V. Many 5 V modules have an optocoupler and transistor and trigger fine at 3.3 V, but some do not — check the datasheet or measure. |
| Trigger polarity | **Active-high** if you can choose | An active-low module is energised by a floating or glitching pin — the exact reset hazard above. |
| Coil supply | Separate 5 V rail | Rule 4. |
| Protection | Optocoupler isolation preferred | Keeps the coil's noise off the logic ground. |

If you must use a 5 V-logic-only or active-low module: add an NPN transistor (2N2222, BC547) with a
base resistor to drive the IN pin, plus a 10 kΩ pull-down on the transistor base so it cannot float
high during boot. Do not "fix" a level mismatch with a resistor divider on the IN pin — it degrades
the optocoupler drive and causes intermittent relay chatter.

## Wiring

```
   Feeder node (ESP32)                          Relay module                Motor circuit
  ┌───────────────────┐                    ┌────────────────────┐      ┌──────────────────┐
  │                   │                    │                    │      │                  │
  │  GPIO 26 ─────────┼────────────────────┤ IN                 │      │                  │
  │                   │                    │                    │      │                  │
  │  GPIO 27 ──┐      │                    │ VCC ───────────────┼──┐   │                  │
  │            │      │                    │ GND ───────────────┼──┼───┼──┐               │
  │        [button]──GND                   │                    │  │   │  │               │
  │                   │                    │ COM ───────────────┼──┼───┼──┤               │
  │  GND ─────────────┼────────────────────┤                    │  │   │  │  Motor        │
  │                   │                    │ NO ────────────────┼──┼───┼──┤ supply (+)    │
  │  3V3 ──(optional DS3231)               │                    │  │   │  │               │
  │                   │                    └────────────────────┘  │   │  │               │
  └───────────────────┘                         5 V supply        │   │  └──────┬────────┘
        │                                          │              │   │         │
        └────────────── common ground ─────────────┴──────────────┘   │    [flyback]
                                                                     │     diode
                        motor ─────────────────────────────────────┘         │
                                                                            GND
```

Read it in two halves, because they are electrically separate:

1. **Control side.** GPIO 26 → relay IN. The relay's VCC comes from its own 5 V supply. The ESP32
   ground and the 5 V supply ground are tied together — a relay module with no common ground
   reference will not trigger reliably.
2. **Switched side.** The motor supply goes through the relay's **COM / NO** contacts. `NO` (normally
   open) is the correct choice: de-energised means the motor circuit is open. **Never use `NC`** — that
   would mean the motor runs whenever the relay is off, inverting rule 2.

The motor supply never touches the ESP32. Its ground may be common or isolated; if you use a separate
isolated supply, the optocoupler on the relay keeps the two sides apart, which is the cleanest option.

## Power tree

```
   5 V PSU ──────────► Relay coil (VCC)
   │
   └──────────────────► ESP32 devkit 5V pin (or a separate USB supply)

   Motor PSU ────────► Relay COM/NO ──► Motor ──► (flyback diode across motor)
   (voltage per motor,
    current ≥ 2× the motor's
    rated running current)

   ESP32 GND ──────── common ──────── 5 V PSU GND
```

The feeder node's logic supply can be a USB adapter or a 5 V buck from the motor supply — **after** the
motor's inrush has been accounted for. What must not happen is the motor drawing through the same
regulator that feeds the ESP32, which browns out the chip mid-feed.

## Safety

**The board cannot drive a relay coil directly.** An ESP32 GPIO is rated around 20 mA recommended
(40 mA absolute maximum). A 5 V relay coil wants 70–90 mA. Wiring a bare coil to a GPIO will not
work and risks the pin. Every relay module worth using has a built-in driver stage; use one.

**Inductive kickback.** A motor is an inductor. Interrupting its current produces a voltage spike that
will arc across the relay contacts, erode them, and radiate noise into your logic. A DC motor needs a
flyback diode across its terminals (cathode to the positive side — a 1N4007 is adequate for small
motors, a Schottky like 1N5819 is better). An AC motor needs an RC snubber across the contacts.
Skipping this is the usual reason a feeder works on the bench for a week and then starts resetting.

**Inrush.** A DC motor at stall draws many times its rated current, and even at start-up it looks
nearly like a short. Size the motor supply and the relay's contact rating for **inrush**, not the
nameplate running current. A relay rated 10 A resistive may be marginal for a 2 A inductive load.

**Mechanical backstop.** Firmware cannot detect a welded relay contact. An auger with a welded relay
empties the hopper. Fit a mechanical limit: a shear pin, a slip clutch, or a physical stop on the auger.
This is not optional for an unattended feeder — it is the only protection against the failure that
software cannot see.

**Overfeeding kills faster than underfeeding.** Given a choice between a design that might miss a feed
and one that might overfeed, choose the one that misses. That preference is baked into rules 2 and 3.

## Verify first — before you wire anything

Work through this on your physical boards. Items the wiki and schematic now answer are marked
**settled** — the check becomes confirmation rather than investigation.

- [ ] **Panel controller** — **settled as ST7262** by the wiki (datasheet list) and every Arduino demo
      config; the IDF README's ST7701 is stale upstream boilerplate. Still flash `08_DrawColorBar`
      (step 0 of [08-testing-and-bringup.md](08-testing-and-bringup.md)) and confirm your unit draws a
      correct image — that is cheap insurance against a board revision.
- [ ] **Panel resolution** — **settled as 800 × 480** by the wiki's spec table; the demo code ships a
      1024 × 600 configuration only because Waveshare shares one codebase across boards. Confirm with
      the colour-bar test anyway: a wrong resolution gives a shifted or split image rather than an
      obvious failure.
- [ ] **Is there an expansion header at all?** If yes, photograph the silkscreen and record the pinout
      here. The package never documents one.
- [ ] **Expander pin map** — settled against the vendor's IDF header (`05_IO_Test/lib/CH422G/CH422G.h`):
      pin 0 = DI0, pin 5 = DI1/`USB_SEL`, OC0/OC1 = DO0/DO1; EXIO6/7 and OC2/3 are unlabelled. Confirm
      with `05_IO_Test` (it loopbacks DO0→DI0 and DO1→DI1 through external jumper wires and turns the
      screen green on success) and note that pin 0 is an input, not something the test drives.
- [ ] **GPIO 4 (touch IRQ)** — **settled: wired to the GT911's `TP_IRQ`** (wiki pinout, schematic
      `IO4 ↔ CTP_IRQ`). Remaining check is functional: confirm the line toggles when the panel is
      touched, which is how the HMI will eventually get its touch events without polling.
- [ ] **USB_SEL** (expander pin 5) — the board has **one** USB-C port; this line steers the FSUSB42
      switch between the ESP32-S3 native USB (GPIO 19/20) and the onboard CH343 UART bridge. Confirm
      which COM port each state produces in Device Manager, and what the state is at raw power-up before
      any firmware configures the expander — that decides whether a factory-fresh board flashes over
      USB-Serial-JTAG or over the CH343.
- [ ] **Feeder node chip** — confirm it is a classic ESP32 (target `esp32`), not an S3 or C3. The
      pin plan above is for classic ESP32 and does not transfer.
- [ ] **Relay trigger** — measure the IN pin threshold, and confirm active-high or active-low, before
      trusting it. Then confirm the module is silent with IN floating.
- [ ] **Common ground** — meter continuity between the ESP32 ground and the relay supply ground.
