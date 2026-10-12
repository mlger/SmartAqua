# Screen specifications

One section per screen. Each has a matching top-level Figma frame of the same name, and a matching
container in the firmware.

Dimensions assume the 800 × 480 panel. Content area is **800 × 376** (480 minus a 40 px status bar and
a 64 px tab bar). All spacing and colour values refer to [tokens.md](tokens.md).

## Shared chrome

Every screen sits inside the same frame:

```
┌──────────────────────────────────────────────────────────────┐
│  status bar                                              40  │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│  content area                                           376  │
│                                                              │
├──────────────────────────────────────────────────────────────┤
│  tab bar                                                 64  │
└──────────────────────────────────────────────────────────────┘
```

**Status bar** (left to right): screen title, then right-aligned — clock confidence, link state, current
time. Clock confidence and link state are the two indicators that must never be missing; they are how a
user knows whether to trust what they are reading.

| Indicator | States |
| --- | --- |
| Link | Up (`SAQ_C_OK` dot + "Feeder") / Down (`SAQ_C_DANGER` dot + "Offline") / Pairing (`SAQ_C_WARN`) |
| Clock | Synced (muted time) / Unsynced (`SAQ_C_WARN` "Time?" — tappable, opens `Settings`) |

**Tab bar** (4 items): Home, Feed, Schedule, Settings. `History` and `Diagnostics` are reached from
elsewhere rather than given tabs — four is the limit before targets get too small on this panel.
`ScheduleEditor` is a push from `Schedule`, so it has no tab.

---

## Home

**Purpose:** answer "is everything fine?" in under two seconds, from across the room.

**Layout**

```
┌──────────────────────────────────────────────────────────────┐
│  Home                                    ● Feeder   14:32    │
├──────────────────────────────────────────────────────────────┤
│  ┌────────────────────────┐  ┌────────────────────────────┐  │
│  │  NEXT FEED             │  │  HOPPER                    │  │
│  │  18:00                 │  │  ████████░░   80%          │  │
│  │  in 3 h 28 m           │  │                            │  │
│  └────────────────────────┘  └────────────────────────────┘  │
│  ┌────────────────────────┐  ┌────────────────────────────┐  │
│  │  LAST FEED             │  │  [    FEED NOW    ]        │  │
│  │  This morning 08:00    │  │                            │  │
│  │  3 g · scheduled       │  │                            │  │
│  └────────────────────────┘  └────────────────────────────┘  │
├──────────────────────────────────────────────────────────────┤
│   Home        Feed        Schedule        Settings           │
└──────────────────────────────────────────────────────────────┘
```

Two cards per row, each 376 wide with a 16 px gutter, inside a 24 px outer margin. Cards are
`SAQ_C_SURFACE` with a 1 px `SAQ_C_BORDER` and `SAQ_R_MD`.

**States**

| State | What changes |
| --- | --- |
| Normal | As drawn |
| Link lost | Status dot red, "Offline"; the four cards show last-known values **with a stale timestamp** and the whole content area gets a subtle `SAQ_C_WARN` 1 px border |
| Clock unsynced | "Next feed" shows `—` rather than a time, with "Time not set" in `SAQ_C_WARN` |
| Fault | A `SAQ_C_DANGER` banner replaces the top card row: fault text and a "Details" button |
| No schedule | "NEXT FEED" shows "None scheduled" and a "Set a schedule" button |

**Interactions**

* Tap the hopper card → `Diagnostics`.
* Tap the last-feed card → `History`.
* **FEED NOW** → pushes `FeedNow` (never feeds directly from here).
* Tap the clock indicator → `Settings`, time section.

**Never** show a "next feed" time derived from a clock that is not synced. Showing a confident wrong
time is worse than showing nothing — it is the failure rule 3 exists to prevent.

---

## FeedNow

**Purpose:** feed immediately, with a deliberate confirmation.

**Layout**

```
┌──────────────────────────────────────────────────────────────┐
│  ←  Feed Now                                    14:32        │
├──────────────────────────────────────────────────────────────┤
│                                                              │
│              Portion:            3 g                         │
│         ◄──────●──────────────────────────►                  │
│              1 g          5 g          10 g                  │
│                                                              │
│              ┌──────────────────────────┐                    │
│              │       FEED 3 g           │                    │
│              └──────────────────────────┘                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

This screen has **no tab bar** — it is a push with a back arrow, so the user cannot wander off mid-task.

**States**

| State | What changes |
| --- | --- |
| Idle | Slider at the configured default portion |
| Confirming | Modal: "Feed 3 g now?" with Cancel / Feed. Scrim behind. |
| Running | Slider disabled; `lv_spinner` plus "Feeding… 1.2 s" — a real elapsed count, not just a spinner |
| Done | Toast: "Fed 3 g" in `SAQ_C_OK`, auto-dismiss after 3 s, then pop back |
| Failed | Toast in `SAQ_C_DANGER`: "Feeder not responding" — **and no feed is claimed** |
| Clamped | Toast: "Limited to 5 s" in `SAQ_C_WARN` — says what actually happened |
| Offline | The Feed button is disabled with "Feeder offline" beneath it |

**Interactions**

* Slider ranges 1–10 g in 0.5 g steps. The value label follows the drag live.
* **FEED** opens the confirmation modal. There is no single-tap feed anywhere in this UI.
* During `Running`, the screen cannot be dismissed. The user can press Stop, which sends `STOP`.
* On `Done` or `Failed`, pop back to `Home`.

**LVGL notes.** The confirmation lives on `lv_layer_top()` so it is above the tab bar and cannot be
dismissed by tapping through. The spinner and elapsed label update from the same tick that handles the
protocol state — do not spin up a second timer for the label.

The confirmation is not ceremony. An accidental tap that dumps 10 g of food into a small tank is a real
way to kill fish.

---

## Schedule

**Purpose:** see and edit when the tank gets fed.

**Layout**

```
┌──────────────────────────────────────────────────────────────┐
│  Schedule                                       14:32        │
├──────────────────────────────────────────────────────────────┤
│  08:00   Every day              3 g                    ›     │
│  18:00   Mon Wed Fri            3 g                    ›     │
│  ────────────────────────────────────────────────────────    │
│                                                              │
│                                       [  +  Add feed  ]      │
├──────────────────────────────────────────────────────────────┤
│   Home        Feed        Schedule        Settings           │
└──────────────────────────────────────────────────────────────┘
```

Rows are 56 px. Each shows time, day summary, portion, and a chevron. The floating **Add** button sits
bottom-right of the content area, 24 px in from the margins.

**States**

| State | What changes |
| --- | --- |
| Empty | Centred: "No feeds scheduled", body text, and the Add button. **Designed, not improvised.** |
| Populated | As drawn |
| Offline | Rows shown from cache, with a `SAQ_C_WARN` banner: "Offline — showing last known schedule" |
| More than fits | Scrollable. Six rows fit; beyond that, `lv_obj` scroll with a visible scrollbar. |

**Interactions**

* Tap a row → `ScheduleEditor` in edit mode.
* Swipe a row left → reveal Delete. Deleting asks for confirmation, because a swipe is easy to do by
  accident.
* **Add** → `ScheduleEditor` in new mode.

**Never** allow editing a schedule while offline without making it obvious the change cannot be saved.
A better rule: disable editing while offline and say why. A schedule edit that silently fails to reach
the feeder node is worse than a disabled button.

---

## ScheduleEditor

**Purpose:** add or change one scheduled feed.

**Layout**

```
┌──────────────────────────────────────────────────────────────┐
│  ←  Edit feed                                   14:32        │
├──────────────────────────────────────────────────────────────┤
│   Time        [ 08 : 00 ]        ▲ ▼                         │
│                                                              │
│   Days        [S][M][T][W][T][F][S]                          │
│                                                              │
│   Portion     ◄────●──────────►      3 g                     │
│                                                              │
│              [ Cancel ]          [   Save   ]                │
└──────────────────────────────────────────────────────────────┘
```

**States**

| State | What changes |
| --- | --- |
| New | Fields at sensible defaults: next round hour, every day, default portion |
| Edit | Fields from the entry |
| Validation error | Inline message under the offending field in `SAQ_C_DANGER`; Save disabled |
| Saving | Save button shows a spinner and is disabled |
| Save failed | Error banner: "Could not reach feeder. Not saved." with Retry |

**Validation rules**

* At least one day selected — "Pick at least one day".
* Time must be a valid 24 h time.
* Portion 1–10 g.
* **Warn on a duplicate time**: two entries at the same minute will both fire, which is legal but
  usually unintended. Warn, do not block.
* Warn if the portion implies a feed duration above the hard clamp. The clamp will truncate it, and the
  user should know before saving rather than discovering it in a log afterwards.

**Interactions**

* Day toggles are at least 44 × 44. Seven of them across 752 px of content is tight — that is the
  arithmetic, so the toggles are 44 px wide with 48 px of space each. Verify against the panel.
* Save writes to the feeder node and waits for an ACK. Only pop back on a confirmed ACK.

---

## History

**Purpose:** what actually happened, including while the HMI was offline.

**Layout**

```
┌──────────────────────────────────────────────────────────────┐
│  ←  History                                     14:32        │
├──────────────────────────────────────────────────────────────┤
│  Today                                                       │
│    08:00   Fed 3 g          scheduled            ✓           │
│    14:22   Fed 3 g          manual               ✓           │
│  Yesterday                                                   │
│    08:00   Fed 3 g          scheduled            ✓           │
│    18:00   Interrupted      scheduled            !           │
└──────────────────────────────────────────────────────────────┘
```

Grouped by day, newest first. 56 px rows, scrollable.

**States**

| State | What changes |
| --- | --- |
| Populated | As drawn |
| Empty | "Nothing fed yet" |
| Stale | A `SAQ_C_WARN` banner: "Last updated 3 h ago" — because history collected from a node that has
since gone offline is not current |
| Reconciling | A small inline spinner with "Syncing from feeder…" while log entries come back |

**Interactions**

* Scroll only. Rows are not tappable — this is a record, not a control surface.

The `!` marker on an interrupted feed matters: it is how a user learns a feed did not complete, which
is exactly the information a plain "feed log" tends to lose.

---

## Settings

**Purpose:** configuration, pairing, calibration, and the door to diagnostics.

Sections, each a card with a heading:

| Section | Contents |
| --- | --- |
| **Link** | Pairing status, peer MAC (last 4 hex, not the whole thing), signal quality, "Re-pair" button |
| **Time** | Current time, source (RTC / NTP / pushed), timezone offset, "Set time" |
| **Feeding** | Default portion, calibration (`ms/g`), food type, "Calibrate" → guided flow, "Last calibrated" date |
| **Device** | HMI firmware version, feeder firmware version, protocol version, uptime, "Diagnostics" |
| **About** | Project name, links, licences |

**States**

| State | What changes |
| --- | --- |
| Idle | As drawn |
| Pairing | Pairing modal: "Hold the button on the feeder for 3 s", with a progress indicator and Cancel |
| Unpaired | Link section shows "Not paired" and a prominent Pair button |
| Saving | Inline spinner on the changed field only |
| Error | Inline message in `SAQ_C_DANGER` next to the field |

**Interactions**

* Calibration is a **guided flow**, not a raw number field. Someone should not have to know what
"ms per gram" means to calibrate the feeder: the flow should ask them to run a test feed for 20 s,
enter the weight they measured, and compute the value.
* Re-pairing replaces the stored peer MAC. Confirm first.
* Changing timezone pushes `SET_TIME` to the feeder node.

---

## Diagnostics

**Purpose:** the screen you are grateful for when something is subtly wrong.

Reached from `Settings` → Device. Not in the tab bar; it is for debugging, not daily use.

| Field | Why it is here |
| --- | --- |
| Link state and last ACK age | Distinguishes "offline" from "quiet but fine" |
| Uplink / downlink packet counts, retry count, drop count | A rising retry count is the earliest sign of range trouble |
| Feeder uptime | A short uptime means it is resetting — which the log may not show |
| Clock: source, last sync, confidence | The single most useful field on this screen |
| Free heap, free PSRAM, min free heap | Leak detection without a debugger attached |
| HMI uptime | |
| Feeder firmware and protocol version | Firmware mismatch shows up here before it shows up as a bug |
| Last feed: epoch, duration requested vs actual | Where a clamp or a truncation becomes visible |
| Raw last-status payload, hex | When all else fails |

An honest note about scope: this screen is more work than it looks and it will save more time than any
other screen. It is tempting to cut. Do not cut it — cut a tab item instead.

---

## Screen implementation order

1. **Shared chrome** — status bar, tab bar, navigation. Everything else sits inside it, and building it
   once prevents six slightly different status bars.
2. **Home** — the most-read screen, and it exercises the link and clock indicators.
3. **FeedNow** — exercises the confirmation pattern and a real command round-trip.
4. **Schedule** and **ScheduleEditor** — the most complex forms.
5. **Settings** — plumbing, plus the pairing flow.
6. **History** — needs reconciliation to be working first.
7. **Diagnostics** — last, because it is most useful once everything else exists.
