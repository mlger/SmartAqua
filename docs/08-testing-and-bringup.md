# 08 — Testing and bring-up

## The principle

Bring up one subsystem at a time, in order, and prove each before moving on. A fish feeder has a
nasty property: a bug that shows up as "the fish died three days later" is almost impossible to trace
back to its cause. The bring-up order below is arranged so that every step isolates a single variable,
and so that the first thing you test is the thing most likely to be a hardware fault rather than your
code.

Do not skip step 0. It costs ten minutes and eliminates the entire class of "is my board broken or is
my firmware broken" questions before you have any firmware.

## Step 0 — Prove the hardware with vendor firmware

The vendor package ships prebuilt binaries. Flash them; you are not building anything yet.

```bash
esptool.py --chip esp32s3 --port COM<n> --baud 921600 erase_flash
```

```bash
esptool.py --chip esp32s3 --port COM<n> --baud 921600 --flash_mode dio --flash_size 16MB --flash_freq 80m write_flash -z 0x0 09_lvgl_Porting_800x480.bin
```

Binaries live in `ESP32-S3-Touch-LCD-4.3B-BOX-Demo/Firmware/`. **The vendor's bins are full merged
images flashed at offset `0x0`** — that is the Waveshare convention, and it is why you erase first. If
a bin does not boot, try `0x10000` (the app-partition offset for a standard IDF layout) before
concluding anything is broken.

Note the `--flash_mode dio` in that command is the vendor's conservative flashing default; it patches
the merged image's header to DIO, which always boots. Your own builds configure QIO through
`sdkconfig.defaults` — do not "fix" that to match the flashing command, or the other way round.

| File | Proves |
| --- | --- |
| `01_I2C_Test.bin` | The I2C bus and the devices on it |
| `02_RS485_Test.bin` | RS485 transceiver |
| `03_SD_Test.bin` | SD card slot |
| `04_RTC_Test.bin` | PCF85063A RTC |
| `05_IO_Test.bin` | The CH422G expander — drives DO0/DO1, reads them back on DI0/DI1 through **external jumper wires you fit on the terminals**; screen turns green when the loopback reads back correctly |
| `06_TWAItransmit.bin`, `07_TWAIreceive.bin` | CAN, needs two boards |
| `08_DrawColorBar_800x480.bin`, `..._1024x600.bin` | **Which panel resolution your board actually has** — the wiki says this product is 800 × 480, so the 1024 × 600 bin exists only because the demo codebase is shared across Waveshare boards |
| `09_lvgl_Porting_800x480.bin`, `..._1024x600.bin` | Panel **and** GT911 touch together |

### Settle the panel question now

Flash the two `08_DrawColorBar` variants. Exactly one will draw a correct, stable, full-screen colour
bar. The wrong one gives a split or shifted image, or nothing. Expect the 800 × 480 bin to be the
correct one — the wiki's spec table says that is this product's panel — but run both anyway: this
test costs two minutes and settles the fact on *your* unit rather than on a wiki page.

Record the answer in [02-hardware-and-wiring.md](02-hardware-and-wiring.md#verify-first--before-you-wire-anything)
and in `apps/hmi/sdkconfig.defaults`. Everything downstream — the Figma frame size, the LVGL buffers,
the tab-bar height — depends on this number, and guessing it wrong means redoing the UI.

While you are here, also run `05_IO_Test`: it loopbacks the isolated outputs DO0/DO1 to the inputs
DI0/DI1 through jumper wires you fit on the terminals, and turns the screen green when the readback
matches — which proves the CH422G expander and its pin map.

## Step 1 — I2C enumeration

Flash `01_I2C_Test` (it is the `i2c-tools` example — a serial console with `i2cdetect`). At the
console:

```
i2cconfig --port=0 --sda=8 --scl=9 --freq=100000
i2cdetect
```

Expected devices:

| Address | Device |
| --- | --- |
| several of `0x23`, `0x24`, `0x26`, `0x38` | CH422G IO expander — it is a **register-as-address** device with no single slave address, so it answers at its command addresses (0x20 is *not* one of them) |
| `0x51` | PCF85063A RTC |
| `0x5D` or `0x14` | GT911 touch (address depends on its reset sequence — both are valid) |

If you see entries in the 0x23–0x38 range plus `0x51` but nothing for the touch, that is normal **when the touch is not
initialised** — the GT911 only appears after its reset line has been pulsed by the expander. The
`09_lvgl_Porting` firmware is the real touch test.

If `i2cdetect` shows nothing at all, the bus is not working: check that you are calling the I2C init
before the scan, and that SDA/SCL are not swapped. Do not start editing firmware — this is a
configuration or wiring fault.

## Step 2 — Display and touch

Flash `09_lvgl_Porting_<your resolution>.bin`. You should get the vendor's demo UI, and touches should
respond.

Things to specifically look at, because they are the failure modes that will otherwise show up later
as mystery UI bugs:

* **Screen drift or a diagonal tear** — the bounce buffer. The vendor config uses height 10; if you see
  drift, that value is the first thing to check.
* **Touch offset** — where you touch versus where the cursor appears. If it is off by a consistent
  offset, the panel resolution in the config is wrong. If it is mirrored or rotated, the touch
  orientation needs inverting.
* **Dead edges** — the last few millimetres near the bezel. This is why
  [06-ui-design-workflow.md](06-ui-design-workflow.md) asks for a 20 px dead margin.
* **Backlight** — driven by expander pin 2. If the image is correct but dark, the backlight state is
  what to check, not the panel.

## Step 3 — Remaining HMI peripherals

Only the ones you will actually use. SD (`03`) is optional; the RTC (`04`) you will use. Run each, and
note in the verification checklist which you have proven.

For the RTC, confirm both directions: set the time, power-cycle, and check it held. A PCF85063A with a
dead backup battery reads correctly until the power drops, then silently resets — which is exactly the
failure rule 3 exists to catch, and it is much easier to find here than in the field.

## Step 4 — Feeder relay, on the bench

Do this **without the motor connected first**. No exceptions.

### 4a — LED, not motor

Wire the relay's switched contacts to an LED with a resistor, in series with a battery or a bench
supply. Then:

1. Confirm the relay is **silent with the ESP32 held in reset.** If it clicks during reset or boot, you
   are on a strapping/glitch pin — move to GPIO 26 per
   [02-hardware-and-wiring.md](02-hardware-and-wiring.md#feeder-node--pin-plan).
2. Drive the pin high; the relay should click and the LED light.
3. Hold the pin high and interrupt power to the ESP32. The relay **must** drop out. If it stays
   energised, you are wired through `NC` instead of `NO`, or the module is latching. Fix it before
   going further. This is rule 2 being verified physically.
4. Trigger a deliberate crash (`abort()`) while energised. The relay must drop out.

Step 4 is the one people skip and the one that matters most. Test it.

### 4b — Motor, separate supply

Now connect the motor, on its own supply, with:

* A flyback diode across the motor terminals (DC motors).
* The motor supply sized for **inrush**, not running current.
* A common ground between the ESP32 and the relay coil supply, but the motor supply isolated or
  heavily decoupled.

Observe:

* Does the ESP32 reset when the relay switches? If yes, that is inrush browning out the logic supply —
  rule 4 is violated. Fix the power, not the software.
* Does the ESP32 reset when the motor *stops*? That is inductive kickback — the diode is missing or
  backwards.
* Measure the relay's switched current with a clamp meter and compare against the contact rating.

### 4c — Duration clamp

Send a deliberately absurd duration and confirm it is truncated, with a warning in the log. Then
confirm the same thing happens for a command sent over ESP-NOW. Rule 2 has to hold at the boundary
where the untrusted input arrives, not just in the happy path.

### 4d — Calibrate grams to milliseconds

Portion accuracy is what makes this a feeder rather than a switch.

1. Fit the hopper with real food.
2. Run the auger for a known long duration (say 20 s) over a container and weigh the output.
3. `ms_per_gram = 20000 / grams_delivered`.
4. Store it in NVS `saq_cfg.calib_mspg`.
5. Verify: request 3 g, weigh it. Expect within about ±10%. Augers are not precise; ±10% is a realistic
   target and ±5% needs a different mechanism, not better maths.
6. **Re-calibrate whenever the food type or the auger changes.** Store the food type alongside the
   calibration, so a stale value is visible rather than assumed.

## Step 5 — The wireless link

### 5a — Pair and talk

Pair as described in [07-wireless-protocol.md](07-wireless-protocol.md#pairing), then confirm in the
log:

* The feeder node stores the HMI's MAC.
* `STATUS` arrives on the HMI at the expected interval.
* A `FEED` is ACKed and produces exactly one `FEED_DONE`.

### 5b — Range

Walk the HMI away from the feeder node with the real enclosure in place and log where ACKs stop. Then
place the feeder node for real with at least that much margin. **Test inside the actual cabinet or
shed**, with the hopper full and the motor running — the motor's switching noise and the enclosure's
attenuation both hurt, and both are absent from a bench test.

### 5c — Link-loss behaviour

| Test | Expected |
| --- | --- |
| Power off the HMI, wait past one scheduled feed | Feeder feeds on time, logs it (rule 1) |
| Power the HMI back on | It reconciles the missed history from the feeder's log |
| Block the radio (wrap the feeder node in foil) mid-feed | Feed completes; `FEED_DONE` queues |
| Send `FEED` twice with the same `seq` | Feeds **once**, ACKs twice |
| Send `STOP` while running | Relay drops immediately, even mid-feed |
| Power-cycle the feeder node mid-feed | No re-feed on boot; a `feed interrupted` entry in the log |
| Boot the feeder with no clock sync | Status reports `clock_conf = 0`; the HMI shows a warning |

That last one is rule 3's whole point. If the HMI does not visibly warn about an unsynced clock, the
warning does not exist.

## Host-side unit tests

Logic that does not touch hardware is tested on the host, in CI, with no board attached. This is what
keeps the fast feedback loop fast.

What belongs in `components/` and therefore gets tests:

| Module | Tested behaviour |
| --- | --- |
| Protocol codec | Encode and decode every packet type; **the test vectors in [07](07-wireless-protocol.md#test-vectors)** |
| Protocol framing | Reject bad magic, major-version mismatch, unknown type, truncated payload, oversized payload |
| Dedupe window | Same `seq` twice acts once; out-of-order within the window accepted; older than the window dropped; `STOP` never deduped |
| Schedule resolution | Given a time, which entries are due; midnight wrap; day-of-week matching; no double-fire inside the same minute |
| Portion calibration | grams ↔ ms, including the hard clamp |
| Packet struct round-trip | Field-by-field encode/decode survives a struct-layout difference between host and target |

That last row is worth calling out. Encoding with `memcpy` per field, rather than dumping a struct, is
what makes the host test *meaningful* for the target. If you dump structs, the host and the Xtensa core
can disagree on padding and the test passes while the device fails.

Run them with a plain CMake + Unity project in `tests/host/` — no ESP-IDF needed, so CI is fast:

```bash
cmake -S tests/host -B build/host -G Ninja && cmake --build build/host && ctest --test-dir build/host --output-on-failure
```

## On-target integration tests (manual)

`pytest-embedded` can run test cases against a real board over serial. Use it as a **manual lane**, not
in CI — CI has no hardware.

```bash
cd apps/feeder && pytest --embedded-services esp,serial -s tests/integration
```

Worth automating here: boot banner, NVS schema version, relay-off-on-boot, and the clock-confidence
report. Keep the list short; anything requiring a human to watch an LED is a checklist item, not a
test.

## Bench test matrix

Run this before any release that touches the relay, the schedule, or the protocol.

| # | Test | Pass criteria |
| --- | --- | --- |
| 1 | Relay silent during reset/boot | No click, meter confirms off |
| 2 | Relay drops on power loss while energised | Contacts open |
| 3 | Relay drops on firmware crash while energised | Contacts open |
| 4 | Hard duration clamp, locally | Truncated to `MAX_FEED_MS`, warning logged |
| 5 | Hard duration clamp, over the wire | Same |
| 6 | No ESP32 reset when the motor starts | Stable, no brownout log |
| 7 | No ESP32 reset when the motor stops | Stable |
| 8 | Portion accuracy at 3 g | Within ±10% |
| 9 | `FEED` retry with a duplicate `seq` | One feed, two ACKs |
| 10 | `STOP` mid-feed | Immediate drop |
| 11 | Reboot mid-feed | No re-feed; interrupted entry logged |
| 12 | HMI offline across a scheduled feed | Feed still happens |
| 13 | HMI reconnect after outage | History reconciles |
| 14 | Unsynced clock reported to the HMI | Visible warning, not silence |
| 15 | NVS survives 100 power cycles | Schedule and calibration intact |

## Soak test

**24 hours minimum, dry.** Dummy load instead of food, both boards on their real power supplies, in
the real enclosure if you have it. Then a wet run with real food.

What the soak is actually looking for, none of which a bench test finds:

* Slow memory leaks — heap and PSRAM free-memory trend over 24 h.
* Clock drift and whether the schedule still lands on the right minute.
* Brownouts and resets that only happen with thermal cycling.
* Whether a feed ever fires twice across many schedule transitions, especially at midnight.
* Whether NVS wear or a failed write shows up.

Log free heap and PSRAM periodically and check the trend, not just the final value. A slow leak is
invisible in a six-hour test and fatal in a three-week unattended run.

Then leave it running for a week before trusting it with fish.

## Release checklist

CI cannot test hardware, so this is a human step and it is not optional.

- [ ] CI green on `main` for both targets
- [ ] Host unit tests pass, including the protocol test vectors
- [ ] Bench test matrix run on real hardware; results recorded in the release notes
- [ ] Soak test passed if the relay, schedule, or protocol changed
- [ ] Protocol version noted, and whether the other board must be reflashed
- [ ] Any `sdkconfig.defaults` change that requires a full erase is called out
- [ ] Portion calibration re-verified if the food or mechanism changed
- [ ] `git tag hmi/vX.Y.Z` or `feeder/vX.Y.Z` pushed
- [ ] `.bin` attached to the GitHub Release
- [ ] `docs/` updated for anything the release changed

## Symptom → cause

| Symptom | Look here first |
| --- | --- |
| No COM port on the HMI | `USB_SEL` routing (native USB vs CH343), a missing WCH CH343 driver, or download mode — see [03](03-toolchain-setup.md#5-the-single-usb-c-port-and-the-usb_sel-switch) |
| Board appears dead | Is it in download mode? Hold BOOT, tap RST, release BOOT |
| Split or shifted image | Wrong resolution in config; re-run step 0 |
| Image correct, screen dark | Backlight, expander pin 2 |
| Touch works, offset | Touch orientation, then panel resolution |
| Relay clicks on boot | Wrong pin — a strapping or boot-glitch GPIO |
| Motor never turns | Input-only GPIO (34–39); or `NC` instead of `NO` |
| ESP32 resets when the motor starts | Inrush browning out the logic supply (rule 4) |
| ESP32 resets when the motor stops | Missing or reversed flyback diode |
| Random resets, no pattern | Supply noise, or a floating input |
| Feeds twice | Dedupe window — see [07](07-wireless-protocol.md#sequence-numbers-retries-dedupe) |
| Misses a feed | Clock not synced; check `clock_conf` on the HMI |
| Build fails after changing config | `set-target` wiped `sdkconfig`; re-run `menuconfig` |
| Boot loop | Flash size mismatch in `sdkconfig.defaults` |
| ESP-NOW silent, pairing fine | Channel mismatch, or PMK/LMK mismatch (fails silently) |

Next: [09-roadmap-and-milestones.md](09-roadmap-and-milestones.md).
