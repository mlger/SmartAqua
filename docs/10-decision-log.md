# 10 — Decision log

Architecture decisions, the reasoning behind them, and what would make us revisit. Written as
[ADRs](https://adr.github.io/) so that a future reader can tell a considered choice from an accident.

**A decision recorded here is not permanent.** It is the best answer at the time, with the evidence
available. If you reverse one, do not edit the entry — add a new one that supersedes it and say what
changed.

---

## ADR-001 — The relay is driven by a second ESP32, not the S3 board

**Status:** Accepted
**Date:** 2026-10-12

**Context.** The ESP32-S3-Touch-LCD-4.3B board has no free GPIO. Verified from the vendor package: 20
pins for the 16-bit RGB LCD, 3 for SD, 2 for I2C, 2 for RS485, 2 for CAN, GPIO 6 for the RTC alarm
interrupt, GPIO 4 for the GT911 touch IRQ (corrected 2026-10-12 — this line previously said "an
onboard button"; the wiki pinout and schematic wire GPIO 4 to the touch controller's `TP_IRQ` line,
which strengthens, not weakens, the conclusion), GPIO 19/20 for USB. GPIO 26–32 are internal
flash, 33–37 are consumed by octal PSRAM, and 22–25 do not exist on the ESP32-S3. The only spare
user I/O is behind a CH422G I2C expander, whose unused pins (EXIO6, EXIO7, OC2, OC3) are not
documented as reaching any header.

**Decision.** Drive the relay from a second, classic ESP32 devkit placed next to the motor. The S3
board is the HMI only.

**Consequences.**

* The HMI has no high-current switching near its sensitive RGB display timing.
* A second firmware, a second CI target, and a wireless link must be built and maintained.
* The relay's off-path does not traverse I2C. An I2C hang cannot leave the relay stuck on — which
  matters because a stuck-on auger is the worst failure in this system.
* A second board is required, and the two must be paired.

**Revisit if.** A board revision exposes free GPIO on a header, *and* you are willing to put motor
current next to the display. Both conditions are needed, and the second is unlikely to become true.

---

## ADR-002 — ESP-NOW rather than RS485

**Status:** Accepted
**Date:** 2026-10-12

**Context.** The S3 board has an onboard RS485 transceiver (GPIO 43/44), which would need only a
twisted pair between the two boards. RS485 is wired, deterministic, and needs no WiFi. The user chose
wireless.

**Decision.** Use ESP-NOW on 2.4 GHz, peer to peer, with no router required in the recommended mode.

**Consequences.**

* No cable to run to the tank, and no conduit, gland, or cable in a wet environment.
* ESP-NOW gives a link-layer ack but **no** application-level ordering, delivery, or deduplication. A
  real framing protocol with sequence numbers, retries, and a dedupe window is required — it is not an
  ESP-NOW feature. See [07-wireless-protocol.md](07-wireless-protocol.md).
* Range is affected by the enclosure, the water, and the motor's switching noise. Range must be measured
  in situ, not on a bench.
* Either no-router mode (fixed channel, time from the HMI's RTC) or AP mode (channel follows the AP,
  silent failures if the nodes disagree).
* Losing the link must not lose a feed. This is what forced ADR-003.

**Revisit if.** The measured range in the real installation proves marginal, or the link proves
unreliable near the running motor. RS485 is a strictly more robust physical layer and the transceiver
is already on the board; switching would mean abandoning ADR-003's wireless reconciliation, though not
ADR-003's autonomy.

---

## ADR-003 — The feeder node owns the schedule and works offline

**Status:** Accepted
**Date:** 2026-10-12

**Context.** The obvious design has the HMI hold the schedule and command feeds, making the feeder node
a thin actuator. It is simpler: one source of truth, no reconciliation, no clock on the feeder node.

**Decision.** The feeder node stores the schedule in NVS and feeds on its own clock. The HMI is for
configuration and override only. When the two disagree about history, the feeder node wins.

**Consequences.**

* The feeder keeps working through a router failure, a WiFi change, or an HMI that is unplugged or
  powered off. With the thin-actuator design, any of those means fish go unfed.
* The feeder node needs its own reliable clock, which pulls in SNTP, RTC holdover, and a
  clock-confidence signal. That requirement is design rule 3 and the `clock_conf` field.
* Two copies of the schedule must be reconciled, which is real work.
* Every command needs to be safe to re-deliver, because the HMI retries.
* This decision is what makes the whole system depend on getting time right, which is why rule 3 and
  the `clock_conf` field exist.

**Revisit if.** Never, for a feeder. Autonomy is the point. If a future version adds a second node
type that is mains-powered and network-attached, this still applies to it.

---

## ADR-004 — A hard, non-configurable maximum feed duration

**Status:** Accepted
**Date:** 2026-10-12

**Context.** The most damaging failure in an aquarium feeder is not a missed feed — it is an auger that
runs until the hopper is empty. Causes include a malformed command, a hung task, a welded relay
contact, and a reboot loop re-firing a partially-completed feed.

**Decision.** A `MAX_FEED_MS` constant, enforced in the actuation layer, below the command layer, and
not exposed in the UI or over the protocol.

**Consequences.**

* No command, bug, or corrupted packet can run the motor indefinitely.
* The clamp is deliberately not user-configurable. This is the one place where a limit is hard-coded
  on purpose, and it should be justified in review if anyone proposes making it settable.
* `feed_in_progress` in NVS makes a mid-feed reboot not re-feed.
* Firmware still cannot detect a **welded relay contact**. That is why the mechanical backstop
  (shear pin, slip clutch, or physical stop) is a hard requirement in
  [02-hardware-and-wiring.md](02-hardware-and-wiring.md#safety) — it is the only protection for the one
  failure software cannot see.

**Revisit if.** Never remove the clamp. The value may be tuned per mechanism; the existence of a hard
ceiling may not.

---

## ADR-005 — Figma is a specification; LVGL is hand-written

**Status:** Accepted
**Date:** 2026-10-12

**Context.** Tools claiming Figma-to-LVGL generation exist. LVGL's own tooling (LVGL Editor, SquareLine
Studio) does not import Figma files. Community plugins emit generated C that is awkward to maintain and
fights dynamic behaviour.

**Decision.** Figma holds the visual specification: exact panel-size frames, design tokens as Figma
variables, one frame per screen. LVGL C is hand-written against a checked-in token table, and the
hand-off is verified by screenshot comparison.

**Consequences.**

* No tool lock-in, and no generated code nobody can read or review in a PR.
* More typing, and the possibility of drift between the design and the implementation — which is why the
  side-by-side screenshot review is a required step, not a suggestion.
* Tokens must be transcribed by hand into `ui_theme.h`. A mechanical Figma variable → C name mapping
  keeps that reliable, and a grep for stray hex literals outside that header catches the rest.
* Screens are ordinary, reviewable code that behaves like the rest of the firmware.

**Revisit if.** A generator appears that emits idiomatic LVGL 8 code *and* handles state and events, not
just static layout. Evaluate it on a throwaway screen before adopting.

---

## ADR-006 — LVGL pinned to 8.x, not 9

**Status:** Accepted
**Date:** 2026-10-12

**Context.** LVGL 9 exists and changes the API substantially (widget names, styles, colour handling,
the driver interface). The vendor demo targets LVGL 8: the vendored component is 8.4.0, and the
manifest constrains `lvgl/lvgl: ">8.3.9,<9"`.

**Decision.** Use LVGL 8.4.x, pinned in `idf_component.yml`, with the resolved version in
`dependencies.lock`.

**Consequences.**

* The vendor examples, their `lv_conf` settings, and their RGB porting code work as-is. The
  panel configuration on this board is fiddly, and starting from verified-working config is worth a
  great deal.
* LVGL 8 is a known, stable API with abundant examples.
* `lv_btn`, `lv_img`, and the `lv_style_t` model are LVGL v8 idioms; v9 tutorials will not apply
  directly.
* Upgrading later is real work — the porting layer, the styles, and every screen.

**Revisit if.** Staying on 8 blocks a feature you need, or a vendor BSP for this board moves to 9. Do
not upgrade casually: the panel bring-up is the risky part of this project and 8.x is proven on this
hardware.

---

## ADR-007 — Two apps in one repo, with a shared dependency-free component

**Status:** Accepted
**Date:** 2026-10-12

**Context.** One app with `#if CONFIG_IDF_TARGET_*` switches, two apps in one repo, or two repos.

**Decision.** Two apps in one repo (`apps/hmi`, `apps/feeder`), plus `components/` shared code that
**must not include ESP-IDF headers**.

**Consequences.**

* Each app builds, flashes, and versions independently. CI builds both in a matrix.
* Changes that must land together — a protocol change — are one PR, which is the property that matters
  most here. Two repos would make a protocol change a coordination problem across repositories, and the
  two halves could merge at different times.
* The no-IDF-header rule is what makes the protocol codec and schedule logic unit-testable on the host
  in CI with no board attached. It is a real constraint, enforced in review: needing `esp_log.h` inside
  `components/` means the logic belongs in an app instead.
* `dependencies.lock` is committed so both apps resolve identical component versions.

**Revisit if.** The apps' dependencies diverge so far that a shared build config becomes a nuisance, or
one app needs a different IDF version. Neither is currently true, and a protocol change staying atomic
is worth more than either.

---

## Superseded

None yet. When you reverse a decision, add a new entry above and link it here rather than editing
history.
