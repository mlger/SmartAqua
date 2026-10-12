# 05 — Firmware workflow

## Per-app commands

The HMI targets `esp32s3`; the feeder node targets `esp32`. Always work from inside the app directory.

```bash
cd apps/hmi && idf.py set-target esp32s3 && idf.py -p COM<n> build flash monitor
```

```bash
cd apps/feeder && idf.py set-target esp32 && idf.py -p COM<n> build flash monitor
```

`Ctrl-]` exits the monitor. If you have both boards plugged in, **always pass `-p` explicitly** —
autodetection will pick the wrong one and you will spend ten minutes wondering why your change had no
effect on the board you were looking at.

Useful variants:

```bash
idf.py build
```

```bash
idf.py -p COM<n> flash monitor
```

```bash
idf.py size-components
```

```bash
idf.py fullclean
```

`set-target` wipes the existing `sdkconfig` — it is a build-configuration reset, not a small edit.
Only run it when you mean it.

## Build configuration policy

| File | Committed? | Purpose |
| --- | --- | --- |
| `sdkconfig.defaults` | **Yes** | The intended configuration, reviewed like code |
| `sdkconfig` | **No** (gitignored) | Generated, machine-specific, may contain local values |
| `sdkconfig.old` | **No** | Backup from the last `menuconfig` run |
| `dependencies.lock` | **Yes** | Pins resolved managed-component versions |

Every change to `sdkconfig.defaults` must state *why* in the commit body. A silent config change is one
of the hardest bugs to find later, because the code looks correct and only this file differs.

Regenerate the defaults from a working config rather than editing by hand:

```bash
idf.py save-defconfig
```

### `apps/hmi/sdkconfig.defaults`

Derived from the vendor demo (`ESP-IDF/08_lvgl_Porting/sdkconfig.defaults`). These values are
**required** for this board — the panel will not produce a stable image without them.

```ini
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_FETCH_INSTRUCTIONS=y
CONFIG_SPIRAM_RODATA=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE=64
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y

# LVGL: task on core 1, 2 ms tick, tearing avoidance
CONFIG_LV_COLOR_SCREEN_TRANSP=y
CONFIG_LV_MEM_CUSTOM=y
CONFIG_LV_MEMCPY_MEMSET_STD=y
CONFIG_LV_USE_LOG=y
CONFIG_LV_LOG_PRINTF=y
CONFIG_LV_USE_PERF_MONITOR=y
CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=y
CONFIG_LV_FONT_MONTSERRAT_16=y
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_24=y
```

Points worth understanding rather than copying:

* **`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`** — logs go out the native USB port. This is also why the
  HMI needs no UART driver.
* **`CONFIG_SPIRAM_FETCH_INSTRUCTIONS` / `RODATA`** — the RGB framebuffers live in PSRAM. Do not
  disable these to save memory; the display will not work.
* **`CONFIG_LV_MEM_CUSTOM=y`** — LVGL allocates from the IDF heap, so it can use PSRAM.
* **Tearing avoidance** is selected in `menuconfig` → *Example Configuration → Display*, not in
  `sdkconfig.defaults`, because the vendor exposes it through `Kconfig.projbuild`. Pick **Mode 3**
  (LCD double-buffer + LVGL direct mode) which the vendor defaults to. Modes 1 and 2 consume more
  PSRAM.
* **Bounce buffer height 10** is the vendor default and the value that stopped screen drift on this
  panel. Leave it alone unless you are chasing a specific artefact.

### `apps/feeder/sdkconfig.defaults`

The feeder node has different constraints: no PSRAM, no display, and it must fit OTA partitions in
4 MB of flash.

```ini
CONFIG_IDF_TARGET="esp32"
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_CONSOLE_UART_DEFAULT=y
```

Adjust flash size to your actual module (4 MB is typical for WROOM-32; some are 8 MB). Getting this
wrong produces a boot loop that looks like a firmware bug.

## Partition tables

The HMI gets the IDF default (16 MB, no OTA) to start. When OTA lands, use a custom table with two app
slots.

The feeder node needs a **custom `partitions.csv`** immediately, because OTA is a near-term requirement
(rule 1 says the node must survive being unattended — you cannot climb into a tank cabinet every time
you fix a bug):

```
# Name,      Type, SubType, Offset,   Size,     Flags
nvs,         data, nvs,     0x9000,   0x6000,
otadata,     data, ota,     0xf000,   0x2000,
phy_init,    data, phy,     0x11000,  0x1000,
ota_0,       app,  ota_0,   0x20000,  0x180000,
ota_1,       app,  ota_1,   0x1A0000, 0x180000,
nvs_keys,    data, nvs_keys,0x320000, 0x1000,   encrypted
```

Two 1.5 MB app slots plus NVS fits comfortably in 4 MB. Check your actual binary size with
`idf.py size` before committing — if the app grows past the slot, the build fails at image creation
with a clear error, but only if you have the custom table in place. With the default single-slot table
you would discover the problem later, at OTA time.

## NVS layout

NVS is the feeder node's database. Nail the keys down early; renaming one later is a migration.

### `apps/feeder` — namespace `saq_cfg`

| Key | Type | Meaning |
| --- | --- | --- |
| `wifi_ssid` | str | Provisioned network |
| `wifi_pass` | str | Provisioned password |
| `peer_mac` | blob(6) | HMI's MAC address for ESP-NOW |
| `portion_g` | u16 | Default portion in grams |
| `calib_mspg` | u32 | Calibration: milliseconds of run per gram |
| `tz_offset` | i32 | Timezone offset in seconds |
| `clock_conf` | u8 | Whether the clock has synced since boot (rule 3) |
| `feed_in_progress` | u8 | Set before actuating, cleared after — see *Crash safety* |

### `apps/feeder` — namespace `saq_sched`

| Key | Type | Meaning |
| --- | --- | --- |
| `count` | u8 | Number of active schedule entries |
| `e0` … `eN` | blob | One entry per slot, fixed-size struct |

A fixed-size struct copies straight in and out with no parsing. Keep the struct versioned — put a
`version` byte first, and refuse to load an entry whose version you do not understand rather than
misinterpreting its bytes.

### `apps/feeder` — namespace `saq_log`

| Key | Type | Meaning |
| --- | --- | --- |
| `head` / `count` | u16 | Ring buffer position |
| `l0` … `l31` | blob | Bounded recent feed events |

This is deliberately **bounded**, and the HMI is where the full history lives
([01-architecture.md](01-architecture.md)). Flash has finite erase cycles; a feeder logging to NVS on
every feed for years is a slow-motion wear-out. 32 entries is enough to reconcile after a long outage.

## Logging

Use `ESP_LOGx` with a per-file tag, and set the level from `menuconfig` so production builds are
quiet.

```c
static const char *TAG = "feeder/relay";

ESP_LOGI(TAG, "energise %" PRIu32 " ms (portion %u g)", duration_ms, portion_g);
ESP_LOGW(TAG, "feed %" PRIu32 " ms exceeded clamp, truncated to %d", requested, MAX_FEED_MS);
ESP_LOGE(TAG, "relay drive failed: %s", esp_err_to_name(err));
```

Conventions:

* Tag is `app/subject` — `feeder/relay`, `hmi/schedule`, `protocol/codec`. Filtering the monitor by tag
  is how you find anything in a busy log.
* Never log WiFi passwords or the full NVS blob.
* Log the **decision**, not just the outcome. `"energise 4200 ms"` tells you the clamp was not applied;
  `"feed done"` tells you nothing.
* Use `ESP_DRAM_LOGx` only in code that can run with the cache disabled — which, in this project, is
  nothing. Plain `ESP_LOGx` is correct everywhere here.

The relay actuation path deserves a log line on **both** edges. A log that says "energise" with no
matching "de-energise" is the clearest signal that a board reset mid-feed, and that is exactly the
scenario rule 2 exists for.

## Error handling

* Initialisation failure → `ESP_ERROR_CHECK`. If the relay GPIO, NVS, or ESP-NOW cannot initialise,
  the node is useless and should say so loudly at boot.
* Runtime paths → return `esp_err_t`, log at the call site, and recover. Never `abort()` in a path
  that can reach the relay.
* **Fail toward off.** If anything about a feed is uncertain — a bad duration, an unexpected state, a
  failed NVS write — do not feed.
* Do not use `assert()` in the actuation path. An assert reboots the chip; that happens to de-energise
  the relay, but a reboot loop that keeps re-firing a partially-completed feed is worse than the bug
  you were asserting on.

### Crash safety

The failure to design against is: the node resets *after* energising the relay but *before* recording
that the feed happened. On reboot it must not feed again.

The `feed_in_progress` flag in NVS handles this. Write it **before** energising, clear it **after**
de-energising, and at boot, if it is set, log an interrupted feed and do not re-run it. Losing one
feed to an interruption is acceptable; a reboot loop that re-feeds on every boot is what kills the
tank.

## OTA

Planned, not yet implemented. The design:

| App | Approach |
| --- | --- |
| HMI | `esp_https_ota` on demand from the Settings screen, or from USB. Large flash, easy. |
| Feeder node | Push the image from the HMI over ESP-NOW in chunks (the 250-byte payload ceiling means a real chunking protocol), or pull over WiFi when provisioned. |

Requirements either way:

* Partition table with two app slots (`ota_0`, `ota_1`) — see above. This cannot be retrofitted without
  a serial reflash, because the partition table itself changes.
* `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`, and the app must call
  `esp_ota_mark_app_valid_cancel_rollback()` only after it has proven it can reach the network **and
  actuate the relay**. An image that boots but cannot feed should roll back automatically.
* Never OTA the feeder node while a feed is in progress. Check the state first.
* A power loss mid-OTA must leave the old image bootable — that is what two slots are for, and it is
  the reason the partition table has to be right before the first OTA, not after.

## Flashing gotchas

* **`set-target` resets `sdkconfig`.** Re-run `menuconfig` afterwards if you had local changes.
* **Flash size mismatch** → boot loop. Set `CONFIG_ESPTOOLPY_FLASHSIZE_*` to your actual module.
* **Erase NVS when the layout changes:** `idf.py -p COM<n> erase-flash`. A stale NVS with an old struct
  layout is a common source of "it boots into a weird state" after a schema change.
* **HMI appears dead** → the `USB_SEL` expander pin. See
  [03-toolchain-setup.md](03-toolchain-setup.md#5-board-ports-and-the-two-port-gotcha).
* **Monitor shows nothing on the HMI** → it logs over USB-Serial-JTAG, not UART0. Make sure you are on
  the right port.

Next: [06-ui-design-workflow.md](06-ui-design-workflow.md).
