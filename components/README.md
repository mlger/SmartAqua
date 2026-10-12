# Shared components

Code shared between `apps/hmi` and `apps/feeder`.

## The one rule

**Nothing in this directory may include an ESP-IDF header.**

No `esp_log.h`, no `esp_err_t`, no `freertos/*`, no `nvs.h`. Standard C and C++ only.

This is not tidiness. It is what makes `components/` testable on the host, in CI, with no board
attached — which is the only way the protocol codec, the schedule resolution, and the portion
calibration get real automated tests. A test that needs an ESP32 to run does not run on every commit,
so it does not run.

If you find yourself wanting `esp_log.h` in here, one of two things is true:

1. The logic is really app-specific → it belongs in `apps/hmi` or `apps/feeder`.
2. It genuinely belongs here and needs to report problems → take a callback or return an error code,
   and let the app decide how to log it.

`esp_err_t` is a mild temptation because it is convenient. Do not. Define a local enum; the app can map
it to `esp_err_t` at the boundary.

## What goes here

| Component | Contents |
| --- | --- |
| `smartaqua_protocol` | Frame encode/decode, packet types, the dedupe window, test vectors |

Planned, when it exists:

| Component | Contents |
| --- | --- |
| `smartaqua_schedule` | Schedule entry struct, "is this entry due at time T", midnight and day-of-week handling |
| `smartaqua_portion` | grams ↔ milliseconds, the hard clamp |

Keep the split by concern. A single `common` component becomes a junk drawer within a month.

## Testing

Host tests live in `tests/host/` and are run by CI once that directory has a `CMakeLists.txt`. They are
plain CMake plus Unity — no ESP-IDF, so they run in seconds.

The protocol test vectors in
[../docs/07-wireless-protocol.md](../docs/07-wireless-protocol.md#test-vectors) are the contract. If a
change breaks one, either the change is wrong or the protocol version needs bumping. Do not update a
vector to match new output without deciding which of those it is.
