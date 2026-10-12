# Design

Figma is the visual source of truth for the HMI. This directory holds the parts of the design that
belong in version control — the token values and the written screen specifications — because the
firmware depends on them and they need to be reviewable in a PR.

## Contents

| File | What it is |
| --- | --- |
| [tokens.md](tokens.md) | Colour, spacing, radii, typography, layout. The source of truth for `ui_theme.h`. |
| [screens.md](screens.md) | One specification section per screen: layout, states, interactions, LVGL notes |
| `exports/` | Scratch space for PNG/SVG exports. **Gitignored** — regenerate from Figma instead. |

## The Figma file

> **Add the file URL here once it exists.** Anyone implementing a screen needs it, and a design file
> nobody can find is a design file nobody follows.

```
Figma file:  <paste the URL here>
Dev Mode:    required for reading exact spacing and colour
```

Frames in the file, one per screen, named to match [screens.md](screens.md) and the firmware:

```
Home            FeedNow         Schedule        ScheduleEditor
History         Settings        Diagnostics
```

The panel size for those frames is **800 × 480** — settled by the wiki's spec table for this product.
The colour-bar test in step 0 of
[../docs/08-testing-and-bringup.md](../docs/08-testing-and-bringup.md#step-0--prove-the-hardware-with-vendor-firmware)
remains the final word on your specific unit before anyone draws in the frames. The panel size is a
hardware fact, not a design choice, and a frame drawn at the wrong size causes every screen to be
subtly wrong.

## What lives where, and why

| Thing | Where | Reason |
| --- | --- | --- |
| Token values | `tokens.md`, in git | The firmware depends on them; they need to be reviewable |
| Screen specifications | `screens.md`, in git | Layout intent, states, and interactions are decisions worth recording |
| The visual design itself | Figma | Vector editing, and it is where the designer works |
| Icon source SVGs | Figma, exported into the app when used | Text formats diff usefully; keep them small in number |
| Generated font and image `.c` files | `apps/hmi/main/ui/` | They are firmware source, not design artifacts |
| PNG/SVG exports | **Not in git** | Regenerated on demand; Figma is the source |

The rule underneath: **anything the firmware reads is in git, and anything Figma can regenerate is
not.** Exports in version control go stale silently and then get mistaken for the current design.

## Workflow

The full process is in [../docs/06-ui-design-workflow.md](../docs/06-ui-design-workflow.md). The short
version:

1. Design the screen in Figma at the exact panel size, using only variables from `tokens.md`.
2. Write its specification section in `screens.md` — layout, all states, interactions.
3. Run the [handoff checklist](../docs/06-ui-design-workflow.md#handoff-checklist).
4. Implement in LVGL, static first, then states, then interaction.
5. Screenshot on device and compare **side by side** against the Figma frame.
6. PR with the screenshot in the description.

## Keeping design and firmware in step

Two failure modes, and how each is caught:

**Figma drifts ahead of the firmware.** A designer changes a colour, nobody updates `ui_theme.h`, and
the device keeps rendering the old one. Caught by the rule that `tokens.md` is authoritative for
shipping: if Figma and the Markdown disagree, the Markdown is what the device does. Either update the
Markdown in the same PR or revert the Figma change.

**Figma drifts behind the firmware.** Someone tweaks a value directly in code because it is faster. This
is the more common failure and the more damaging one, because now the design file lies. Caught by
review: a hex literal outside `ui_theme.h`, or a spacing value not on the 4 px scale, is a review
comment. If a value genuinely needs to change, it is a token change and it happens in Figma, in
`tokens.md`, and in the header — together.

## Notes on designing for this specific panel

* **16-bit colour.** Near-identical dark tones collapse into the same RGB565 value and become invisible
  against each other. Test dark-on-dark contrast on the device, not in Figma.
* **No shadows or blurs.** LVGL 8 on this panel cannot render them. Encode depth with 1 px borders and
  the surface tokens instead.
* **Viewing distance is an arm's length,** in a room that may have a tank light glaring off the glass.
  Err on the side of larger type and higher contrast than a phone design would use.
* **Fingers are not cursors.** 44 px is a real minimum here, and the bezel is close to the active area,
  so keep primary actions 20 px in from every edge.
* **Landscape only.** Rotation is fixed at 0°, which removes an entire class of layout bugs. Do not
  design a portrait variant.
