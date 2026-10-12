## What

<!-- One or two sentences. What does this change do, for a user or for the other board? -->

## Why

<!-- The reasoning. A diff shows what changed; this is the part that is otherwise lost. -->

## How this was tested

CI covers the build and the host unit tests. It cannot cover hardware, so this section is the one that
actually matters. Delete the lines that do not apply rather than leaving them ambiguous.

- [ ] `idf.py build` passes locally for the affected app(s)
- [ ] Host unit tests pass (if `components/` changed)
- [ ] **Flashed to real hardware** — which board, and what did you observe?
- [ ] **Not tested on hardware** — say so explicitly here, and why that is acceptable
- [ ] Bench test matrix run (if the relay, schedule, or protocol changed) — list which rows
- [ ] Soak test run (if the relay, schedule, or protocol changed)

Notes:

<!-- e.g. "Flashed apps/feeder. Observed: relay silent through 20 resets, drop-out on power loss
     verified, 3 g portion weighed 3.1 g. Bench rows 1-5 and 9-11 pass." -->

## Checklist

- [ ] Single concern — this PR does one thing
- [ ] Commit messages follow the Conventional Commits convention in
      [docs/04-repo-and-git-workflow.md](../docs/04-repo-and-git-workflow.md)
- [ ] `docs/` updated if this changed anything they describe — **especially pin numbers and versions**
- [ ] Protocol version bumped if the wire format changed, with both apps updated together
- [ ] No secrets, tokens, WiFi credentials, or `.bin` files added
- [ ] Design tokens changed in Figma, `design/tokens.md`, and `ui_theme.h` together (if UI)

## Screenshot

<!-- For any UI change: the device screenshot next to the Figma frame, side by side.
     Changes that look right on the device alone are often visibly wrong in a comparison. -->

## Related

<!-- Issues, and any decision this changes in docs/10-decision-log.md -->
