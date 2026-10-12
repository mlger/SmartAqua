# Design tokens

The single source of truth for colour, spacing, and layout in the HMI UI. The firmware reads these as
`apps/hmi/main/ui/ui_theme.h`; Figma reads them as Local Variables. **If this file and Figma disagree,
this file is what ships** — fix Figma.

Every value here is a *proposal* based on the panel and the use case. Replace the palette with your own
if you like, but keep the structure and the naming, and re-check contrast after any change.

## Panel

| Property | Value |
| --- | --- |
| Resolution | **800 × 480** — confirmed by the wiki spec table for this product (the demo code's 1024 × 600 option is shared codebase, not a board variant); still bench-confirm with the colour-bar test per [../docs/08-testing-and-bringup.md](../docs/08-testing-and-bringup.md#settle-the-panel-question-now) |
| Colour depth | 16-bit RGB565 |
| Orientation | Landscape only, rotation 0° |
| Touch | Capacitive, GT911 |

If your physical unit somehow draws correctly with the 1024 × 600 colour bar instead (a board
revision, not a documented variant), the grid below still applies but the margins and tab-bar height
should be revisited — and the Figma frames must be redrawn at the correct size before anyone designs
in them.

## Colour

Dark theme. A tank controller is usually in a room lit by the tank, and a dark UI is easier to read at
night without lighting the whole room.

| Token | Figma variable | Hex | Use |
| --- | --- | --- | --- |
| `SAQ_C_BG` | `colour/bg` | `#0E1621` | App background |
| `SAQ_C_SURFACE` | `colour/surface` | `#1B2735` | Cards, list rows, panels |
| `SAQ_C_SURFACE_RAISED` | `colour/surface-raised` | `#243447` | Modals, popovers |
| `SAQ_C_BORDER` | `colour/border` | `#2E4057` | 1 px separators and outlines |
| `SAQ_C_PRIMARY` | `colour/primary` | `#2E9BD6` | Primary actions, selection |
| `SAQ_C_PRIMARY_PRESSED` | `colour/primary-pressed` | `#1F7BAA` | Pressed state |
| `SAQ_C_ON_PRIMARY` | `colour/on-primary` | `#0E1621` | Text on a primary fill |
| `SAQ_C_TEXT` | `colour/text` | `#F2F6FA` | Body and headings |
| `SAQ_C_TEXT_MUTED` | `colour/text-muted` | `#93A4B8` | Captions, secondary info, disabled |
| `SAQ_C_OK` | `colour/ok` | `#3FB950` | Successful feed, link up |
| `SAQ_C_WARN` | `colour/warn` | `#D29922` | Clock unsynced, stale data |
| `SAQ_C_DANGER` | `colour/danger` | `#F85149` | Faults, destructive actions |
| `SAQ_C_SCRIM` | `colour/scrim` | `#000000` @ 60% | Behind modals |

Notes on specific choices:

* **Text on primary is dark, not white.** `#F2F6FA` on `#2E9BD6` is roughly 3:1 — below the 4.5:1 bar
  for body text. `#0E1621` on `#2E9BD6` is roughly 6.5:1. If you change the primary colour, re-check
  this pair. It is the easiest contrast mistake to make and the hardest to notice on a bright panel.
* **`SAQ_C_WARN` is deliberately not red.** An unsynced clock is a warning, not a fault. Reserving red
  for faults means red still means something.
* **`SAQ_C_BORDER` is a visible 1 px line, not a shadow.** LVGL 8 cannot render the drop shadows this
  UI would otherwise use. Borders separate surfaces instead.

**Verify these numerically** with a contrast checker before implementing. Targets: 4.5:1 for body text,
3:1 for large text (≥ 18 px) and UI component boundaries. The values above are approximations, and
RGB565 quantisation on the real panel shifts them slightly.

Two panel-specific cautions:

* **Adjacent dark surfaces can collapse.** `SAQ_C_BG` and `SAQ_C_SURFACE` are close enough that on a
  16-bit panel they may read as almost the same colour. That is why `SAQ_C_BORDER` exists — always give
  a surface a 1 px border rather than relying on the fill to separate it.
* **Do not put anti-aliased text on a gradient.** Keep text on flat fills.

## Spacing

4 px base scale. **Any value not in this table is a mistake** — off-scale spacing is almost always a
dragging accident in Figma.

| Token | Figma variable | px |
| --- | --- | --- |
| `SAQ_SP_XS` | `space/xs` | 4 |
| `SAQ_SP_SM` | `space/sm` | 8 |
| `SAQ_SP_MD` | `space/md` | 16 |
| `SAQ_SP_LG` | `space/lg` | 24 |
| `SAQ_SP_XL` | `space/xl` | 32 |
| `SAQ_SP_2XL` | `space/2xl` | 48 |

## Radii

| Token | Figma variable | px | Use |
| --- | --- | --- | --- |
| `SAQ_R_SM` | `radius/sm` | 4 | Chips, small inputs |
| `SAQ_R_MD` | `radius/md` | 8 | Buttons, cards, list rows |
| `SAQ_R_PILL` | `radius/pill` | 20 | Toggle, pill buttons |

## Typography

Montserrat, the sizes already enabled in `apps/hmi/sdkconfig.defaults`. Adding a size means generating
a font with the LVGL font converter — see
[../docs/06-ui-design-workflow.md](../docs/06-ui-design-workflow.md#fonts).

| Token | Figma variable | Size | Weight | Use |
| --- | --- | --- | --- | --- |
| `SAQ_FONT_CAPTION` | `type/caption` | 16 | Regular | Captions, secondary values, table cells |
| `SAQ_FONT_BODY` | `type/body` | 20 | Medium | Body text, list rows, button labels |
| `SAQ_FONT_HEADING` | `type/heading` | 24 | SemiBold | Screen titles, primary numeric readouts |

Note the trap: Figma's font style for Montserrat SemiBold is spelled **"Semi Bold"**, two words. The
LVGL converter expects the style name it finds in the font file.

Only three sizes. A fourth is usually a layout that needs more room rather than another font — solve
it with spacing.

## Layout

| Token | Value | Notes |
| --- | --- | --- |
| Screen | 800 × 480 | Landscape, fixed |
| Outer margin | 24 | `SAQ_SP_LG` on all four edges |
| Grid | 12 columns | Gutters `SAQ_SP_MD` (16) |
| Status bar height | 40 | Persistent chrome, top |
| Tab bar height | 64 | Persistent chrome, bottom |
| Content height | 480 − 40 − 64 = **376** | What a screen actually gets |
| Minimum touch target | **44 × 44** | Below this, taps miss. Not negotiable. |
| Edge dead margin | 20 | Keep tappable things away from the bezel |
| Separator | 1 px `SAQ_C_BORDER` | Replaces shadows |

The content height number matters more than it looks. Every screen's layout is designed against 376 px,
not 480, because the chrome is always there. Designing against 480 and discovering the chrome later
means redoing every screen.

## Component sizes

| Element | Height | Notes |
| --- | --- | --- |
| Button, primary | 56 | Comfortably above the touch minimum |
| Button, small | 44 | The floor, not the default |
| List row | 56 | Schedules and history |
| Toggle | 44 | |
| Input field | 56 | |
| Modal | 400 wide max | Centred, `SAQ_C_SURFACE_RAISED`, `SAQ_R_MD` |

## Measurement reference

Useful conversions for checking a Figma frame against the panel:

| Thing | Pixels at 800 × 480 |
| --- | --- |
| Physical panel diagonal | 4.3" (10.9 cm) |
| Approximate pixel density | ~217 ppi |
| 44 px touch target | ≈ 5.1 mm — a small but acceptable fingertip |
| 56 px button | ≈ 6.6 mm — comfortable |
| 20 px edge margin | ≈ 2.3 mm from the bezel |

A 44 px target is genuinely small on a 4.3" panel. Treat 56 px as the working default and 44 px as the
absolute floor for dense lists.

## C mapping

Keep this mapping mechanical, so a reviewer can verify it by eye:

| Figma variable | C macro |
| --- | --- |
| `colour/primary` | `SAQ_C_PRIMARY` |
| `space/md` | `SAQ_SP_MD` |
| `radius/md` | `SAQ_R_MD` |
| `type/body` | `SAQ_FONT_BODY` |

Lowercase-with-hyphens in Figma becomes `SAQ_<GROUP>_<NAME>` in uppercase with underscores.

## Changing a token

1. Change it in Figma **and** here, in the same PR.
2. If it is a colour, re-check contrast against every surface it is used on.
3. If it is a spacing or size value, check every screen that uses it — Figma will not tell you what
   broke, and layouts fail in the least convenient direction.
4. If it is a typography token, regenerate the LVGL font and record the flash cost.
