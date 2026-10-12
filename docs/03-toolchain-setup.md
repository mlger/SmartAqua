# 03 — Toolchain setup (Windows)

Target: a working `idf.py build` for both apps, plus git and Figma. Everything here is for a Windows
11 host — which is what this project is developed on.

## 1. ESP-IDF 5.2.x

The two apps require different IDF feature sets, so pin one version and use it for both:
**ESP-IDF v5.2.x**. The vendor demo's own `sdkconfig.defaults` is generated from 5.2.0, and the LVGL
example's manifest requires `idf: ">=5.1.0"`.

### Option A — official installer (recommended)

1. Download the ESP-IDF Windows Installer (v5.2.x) from
   `https://dl.espressif.com/dl/esp-idf/`.
2. Run it, choose the **ESP-IDF v5.2.x** release, and let it install IDF plus the toolchain
   (xtensa-esp-elf, CMake, Ninja, Python, esptool).
3. Do **not** install into a path with spaces or non-ASCII characters. `C:\Espressif` is the default
   and is a good choice.

### Option B — manual clone

```bash
git clone -b v5.2.0 --recursive https://github.com/espressif/esp-idf.git C:/Espressif/esp-idf
cd C:/Espressif/esp-idf && ./install.bat esp32,esp32s3
```

Note the target list: **both** `esp32` and `esp32s3`, because this repo builds for two chips.

### Using it

The installer adds Start Menu shortcuts. Launch **"ESP-IDF 5.2 PowerShell"** for all `idf.py`
commands — it sets up `IDF_PATH`, `PATH`, and the Python environment. Running `idf.py` from a plain
PowerShell window will fail with `idf.py not recognized`, which is a setup problem, not a code
problem.

Verify:

```bash
idf.py --version
```

Expect `ESP-IDF v5.2.x`. If you see a different major version, both apps may fail to build against
the vendor-derived component pins.

## 2. Git and GitHub CLI

```bash
winget install --id Git.Git -e
winget install --id GitHub.cli -e
```

Git for Windows brings **Git Credential Manager**, which is what you should use to authenticate
pushes. It stores credentials in the Windows Credential Manager and prompts in a browser on first
use.

```bash
gh auth login
```

Choose GitHub.com → HTTPS → authenticate in browser. After this, `git push` to your own repo works
without embedding a token anywhere.

> **Never put a token in the remote URL.** `git remote set-url origin https://<token>@github.com/...`
> writes the secret into `.git/config` in plaintext, where it survives in backups and is one
> `git add -f` away from being published. If you have done this, clear it with
> `git remote set-url origin https://github.com/<owner>/<repo>.git` and rotate the token.

Verify:

```bash
git --version
```

```bash
gh auth status
```

## 3. VS Code

```bash
winget install --id Microsoft.VisualStudioCode -e
```

Then install the **Espressif IDF** extension (publisher `espressif`). Configure it once: command
palette → `ESP-IDF: Configure ESP-IDF Extension` → **Use existing setup** → point it at your IDF path
(`C:\Espressif\esp-idf`) and the tools directory.

The extension gives you a build/flash/monitor toolbar, `idf.py size` reporting, and per-target
configuration. It is a convenience over the CLI, not a replacement — the CLI is what CI runs, so make
sure the CLI works before relying on the GUI.

Useful companion extensions: `ms-vscode.cpptools`, `EditorConfig.EditorConfig`,
`xaver.clang-format`.

## 4. USB drivers

| Board | Driver needed |
| --- | --- |
| ESP32-S3-Touch-LCD-4.3B (HMI) | Depends on which side of the board's USB switch the port is routed to: the ESP32-S3's native USB-Serial-JTAG needs **no driver** on Windows 11, but the onboard **CH343** USB-UART bridge needs WCH's driver (CH343SER). Windows often fetches it via Windows Update — if a COM port shows a warning triangle in Device Manager, install it from WCH's site. |
| Classic ESP32 devkit (feeder) | **Yes**, whichever UART bridge it carries — CP2102 (Silicon Labs) or CH340/CH9102 (WCH). Install the vendor driver. |

If no port appears, or you see an unknown device, it is almost always a missing UART driver or a
charge-only USB cable. Charge-only cables are a genuinely common trap — they look identical.

## 5. The single USB-C port and the USB_SEL switch

The S3 board has **one USB-C port**, and what sits behind it is switchable: an **FSUSB42** analog
switch routes the port's data lines either to the ESP32-S3's native USB (GPIO 19/20,
USB-Serial-JTAG) or to the onboard **CH343** USB-UART bridge, which lands on UART0 (GPIO 43/44 —
the same UART the RS485 transceiver uses). The switch is steered by CH422G expander pin 5, which
the vendor's Arduino demos call `USB_SEL` and drive **low** for normal native-USB operation
(`03_SD_Test.ino:23-24`; high, per the vendor's comment, disables the USB function).

The failure this produces is subtle: if the port is routed to the CH343 when you expect
USB-Serial-JTAG (or the other way round), you get a *different COM port than expected* — or none at
all, if the needed driver is missing — and the board looks dead even though it is fine. Before you
conclude the board is broken:

1. Check Device Manager: which COM port appeared, and under what device? USB-Serial-JTAG shows up as
   a USB Serial Device; the CH343 shows as a WCH USB-SERIAL device (driver permitting).
2. If nothing appears, hold **BOOT**, tap **RST**, release BOOT to force download mode — the ROM
   bootloader answers on UART0, which the CH343 can always reach. (This is also the wiki's own
   advice for an unrecognised port.)
3. Only then start debugging the firmware.

This is the single most common 20-minute time sink on this board. It is documented here so it does not
cost you that twice. See [02-hardware-and-wiring.md](02-hardware-and-wiring.md#hmi-board--ch422g-expander)
for the expander pin involved and the *Verify first* item for what to bench-check.

Find the port after plugging in:

```bash
mode
```

Or check `idf.py -p COMx monitor` for each candidate. The feeder node and the HMI will not be the same
port, so once you have identified both, write them down — or use the `idf.py -p` flag each time rather
than relying on autodetection, which picks the wrong board when both are attached.

## 6. Figma

1. Install the **Figma desktop app** (better than the browser for Dev Mode, and Dev Mode is how you
   read exact dimensions and export tokens).
2. Confirm you have access to the project's Figma file — the link lives in
   [../design/README.md](../design/README.md).
3. Turn on **Dev Mode** (the toggle in the top-right) so you can inspect spacing, colour, and
   typography numerically rather than by eye.

The design workflow itself is in [06-ui-design-workflow.md](06-ui-design-workflow.md).

## 7. Python tooling for the host tests

ESP-IDF ships its own Python, so you usually do not need a separate install. For the host-side unit
tests described in [08-testing-and-bringup.md](08-testing-and-bringup.md) and for
`pytest-embedded`, install inside the IDF environment:

```bash
python -m pip install pytest pytest-embedded pytest-embedded-serial-esp
```

## 8. Optional: esptool standalone

Needed only for flashing the vendor's prebuilt `.bin` files in bring-up step 0, where you are not
building anything yet. It comes with IDF, but a standalone install is handy:

```bash
python -m pip install esptool
```

Verify:

```bash
esptool.py version
```

## Setup verification checklist

- [ ] `idf.py --version` reports v5.2.x, from the ESP-IDF PowerShell shortcut
- [ ] `idf.py --list-targets` includes both `esp32` and `esp32s3`
- [ ] `git --version` works
- [ ] `gh auth status` shows you logged in
- [ ] The HMI board enumerates a COM port on its USB-C port — note in Device Manager whether it is
      USB-Serial-JTAG or the CH343, and that the CH343 driver is installed
- [ ] The feeder node enumerates a COM port (driver installed)
- [ ] `apps/hmi` builds: `cd apps/hmi && idf.py set-target esp32s3 && idf.py build`
- [ ] `apps/feeder` builds: `cd apps/feeder && idf.py set-target esp32 && idf.py build`
- [ ] Figma desktop opens the project file in Dev Mode

Next: [04-repo-and-git-workflow.md](04-repo-and-git-workflow.md).
