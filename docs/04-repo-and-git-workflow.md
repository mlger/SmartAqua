# 04 — Repository and git workflow

## Layout

```
SmartAqua/
├─ apps/
│  ├─ hmi/                  ESP32-S3 firmware: LVGL 8.4 UI, target esp32s3
│  └─ feeder/               ESP32 firmware: relay, schedule, ESP-NOW, target esp32
├─ components/              Shared logic with NO IDF dependency — host-testable
│  └─ smartaqua_protocol/   Wire format + schedule/portion logic
├─ design/                  Figma source of truth, tokens, screen specs
├─ docs/                    This documentation
├─ firmware/                Released .bin artifacts, per tag (gitignored contents)
└─ .github/                 CI and templates
```

Two apps, not one, because they are two firmwares for two different chips that ship independently. A
single `main/` with `#if CONFIG_IDF_TARGET_*` switches looks tidy and ages badly: you cannot build or
flash one without configuring the other, and CI has to build both every time either changes.

`components/` is shared by both apps and — this is the important constraint — contains **no ESP-IDF
headers**. That is what lets the schedule maths and the protocol codec be unit-tested on the host in
CI with no board attached. If you find yourself wanting `esp_log.h` inside `components/`, that logic
belongs in the app instead, or it needs a thin injected interface.

## Branching

Trunk-based with short-lived branches. `main` is always buildable and always green.

| Prefix | Use | Example |
| --- | --- | --- |
| `feat/` | New capability | `feat/feeder-relay-driver` |
| `fix/` | Bug fix | `fix/protocol-dedupe-window` |
| `docs/` | Documentation only | `docs/hardware-wiring-table` |
| `chore/` | Tooling, CI, deps | `chore/bump-lvgl-pin` |
| `hw/` | Hardware-adjacent work | `hw/bringup-lcd-confirm-panel` |

Rules:

* Branch from `main`, merge back to `main`, delete after merge.
* Keep a branch under about a week. Long-lived branches rot against the vendor component pins, and
  rebasing becomes archaeology.
* Rebase on `main` rather than merging `main` into your branch. Linear history makes `git bisect`
  actually usable — which matters a lot when a hardware bug appears at an arbitrary commit.

## Commit messages

[Conventional Commits](https://www.conventionalcommits.org/), with the **app as the scope**. This
matters because `feat(hmi):` and `feat(feeder):` feed different release tags later.

```
<type>(<scope>): <subject>

<body — why, not what>

<footer — refs, BREAKING CHANGE>
```

| Type | Meaning |
| --- | --- |
| `feat` | New behaviour for the user or the other board |
| `fix` | Bug fix |
| `docs` | Documentation only |
| `refactor` | No behaviour change |
| `test` | Tests only |
| `chore` | Build, CI, dependencies |

Scopes: `hmi`, `feeder`, `protocol`, `hw`, `docs`, `ci`, `deps`.

Subject in the imperative, no trailing full stop, under ~72 characters. The body explains **why** —
a diff already shows what changed, and the reasoning is the part that is lost otherwise.

Examples:

```
feat(feeder): clamp feed duration to a hard maximum

An overfeed kills a tank faster than a missed feed, so the actuation
layer now refuses any duration above MAX_FEED_MS regardless of what the
command layer asks for. Part of design rule 2.
```

```
fix(protocol): drop duplicate FEED packets in a dedupe window

Retries after a lost ACK were feeding twice. The feeder now remembers the
last 8 sequence numbers and ACKs a repeat without actuating.
```

```
docs(hw): cite source file per pin in the HMI pin map

Pin numbers were unverified claims. Each row now names the vendor file it
came from so a future reader can re-check against a board revision.
```

Mark a protocol change that both apps must adopt together:

```
feat(protocol)!: add protocol version field to every packet

BREAKING CHANGE: both HMI and feeder must be reflashed together.
```

## Pull requests

Small, single-concern, and reviewed even when you are the only developer — the reviewer is future you
reading the diff in six months.

The template (`.github/pull_request_template.md`) asks for **what**, **why**, and **how you tested
it**. That third one is the one that matters here, because CI cannot test hardware. "Builds green" is
not a test result for a change to relay timing. Say what you actually did: what you flashed, what you
observed, what you measured. If you did not touch hardware, say that explicitly — an honest "not
tested on hardware" is far more useful than silence.

Merge with **squash**. One PR becomes one commit on `main` with a conventional message, which keeps
history readable and keeps `git bisect` saner.

Branch protection on `main`: require the CI check to pass, require a linear history, and require
branches to be up to date before merging. Do not allow direct pushes to `main`, including your own —
the habit is what keeps `main` trustworthy.

## Releases and versioning

The two firmwares version independently, so tags carry a prefix:

```
hmi/v0.1.0
hmi/v0.1.1
feeder/v0.1.0
```

SemVer, where the version means what a user would expect: **patch** for a fix that changes no
behaviour, **minor** for a new capability, **major** for anything that breaks compatibility with the
other board — which in practice means a protocol change.

A release is a tag plus a GitHub Release whose notes list, explicitly:

1. Which commit/version the other board must be running (protocol compatibility).
2. Any `sdkconfig.defaults` change requiring a full erase, not just a flash.
3. The hardware verification actually performed, per
   [08-testing-and-bringup.md](08-testing-and-bringup.md#release-checklist).

Build artifacts (`build/*.bin`) are **not** committed. Attach them to the GitHub Release instead —
that keeps the repo small and gives each binary a permanent URL. The `firmware/` directory exists for
local convenience and its contents are gitignored.

## What does not go in git

| Thing | Why | Where instead |
| --- | --- | --- |
| `build/`, `managed_components/`, `sdkconfig` | Generated; large; machine-specific | Regenerated by `idf.py` |
| The ~93 MB vendor demo zip and its extracted tree | Size | Referenced in [02](02-hardware-and-wiring.md); re-downloadable |
| Firmware `.bin` files | Size; artifacts are not source | GitHub Releases |
| Figma exports (PNG/SVG) | Regenerated from Figma, which is the source of truth | `design/exports/`, gitignored |
| Tokens, WiFi credentials, API keys | Obvious and permanent | Never in the repo; see below |

`dependencies.lock` **is** committed. It pins the resolved managed components so a build six months
from now resolves the same versions. If you change a component pin in `idf_component.yml`, commit the
updated lock in the same PR.

## Formatting and linting

`.clang-format` (Espressif-flavoured LLVM: 4 spaces, 120 columns) is enforced by CI. Format before you
commit, or the check fails the PR:

```bash
clang-format -i $(git ls-files '*.c' '*.h' '*.cpp' '*.hpp')
```

`clang-format` does not ship with ESP-IDF — install it separately (`winget install LLVM.LLVM` on
Windows) so your local run matches what CI does. The `.editorconfig` handles indentation for editors
that have no `clang-format` integration.

Do not reformat files unrelated to your change in the same PR. A formatting-only diff hides the real
change and makes review worthless. If a file needs a bulk reformat, do it in its own commit and say so.

## Secrets

Nothing secret belongs in this repository, and the git history is forever.

* WiFi credentials and the ESP-NOW peer MAC go in **NVS at runtime**, set through a provisioning path,
  not compiled in.
* If a value must be build-time configurable, use `idf.py menuconfig` and keep the generated
  `sdkconfig` local — it is gitignored. Never commit a populated `sdkconfig.defaults` containing a
  real password.
* If you ever paste a token into a chat, an issue, or a commit, **rotate it**. Do not delete the
  message and hope. Assume it is compromised the moment it is written down somewhere you do not
  control.

## Everyday commands

```bash
git switch main && git pull --ff-only
```

```bash
git switch -c feat/feeder-schedule-store
```

```bash
git add apps/feeder docs && git commit
```

```bash
git push -u origin feat/feeder-schedule-store
```

```bash
gh pr create --fill
```

After the PR merges:

```bash
git switch main && git pull --ff-only && git branch -d feat/feeder-schedule-store
```

Next: [05-firmware-workflow.md](05-firmware-workflow.md).
