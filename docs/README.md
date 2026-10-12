# Documentation index

Start with the root [README](../README.md) for what this project is. Come here for how it is built.

## Where do I look for…?

| I want to… | Read |
| --- | --- |
| Understand how the two boards fit together | [01-architecture.md](01-architecture.md) |
| Know which pin does what, or wire the relay | [02-hardware-and-wiring.md](02-hardware-and-wiring.md) |
| Set up my machine from scratch | [03-toolchain-setup.md](03-toolchain-setup.md) |
| Open a PR, name a branch, cut a release | [04-repo-and-git-workflow.md](04-repo-and-git-workflow.md) |
| Build, flash, and monitor firmware | [05-firmware-workflow.md](05-firmware-workflow.md) |
| Turn a Figma screen into LVGL code | [06-ui-design-workflow.md](06-ui-design-workflow.md) |
| Change a message the boards exchange | [07-wireless-protocol.md](07-wireless-protocol.md) |
| Test something, or bring up a bare board | [08-testing-and-bringup.md](08-testing-and-bringup.md) |
| Know what to build next | [09-roadmap-and-milestones.md](09-roadmap-and-milestones.md) |
| Know why a decision was made | [10-decision-log.md](10-decision-log.md) |
| Find a colour, spacing, or screen spec | [../design/README.md](../design/README.md) |

## Conventions used in these docs

**Verified vs. assumed.** Pin numbers and component versions are marked with the file they came
from. Anything that has not been checked against physical hardware is marked **UNVERIFIED** and
listed in a *Verify first* checklist. Do not remove an UNVERIFIED marker by reasoning your way to
confidence — remove it by measuring.

**The five design rules.** Architectural constraints that outrank convenience. They are stated once
in [01-architecture.md](01-architecture.md#design-rules) and referenced by number elsewhere rather
than restated.

**Rules vs. recommendations.** "Must" is load-bearing — violating it can ruin a tank. "Should" is a
default you may override with a reason. "Consider" is optional.

## Keeping docs truthful

A document that lies is worse than no document. When your change invalidates something here, fix it
in the same PR. Specifically:

* **Pin numbers and versions change** → update [02](02-hardware-and-wiring.md) /
  [05](05-firmware-workflow.md), and update the source citation next to the value.
* **A protocol packet changes** → bump the protocol version in
  [07-wireless-protocol.md](07-wireless-protocol.md) and update both the byte layout and the test
  vectors. Both apps must agree; that is what the version field is for.
* **A milestone is finished** → tick its acceptance criteria in
  [09](09-roadmap-and-milestones.md) and note the date.
* **You reverse a decision** → do not quietly edit history. Add a superseding entry to
  [10-decision-log.md](10-decision-log.md) that says what changed and why.

Reviewers: a PR that changes behaviour described here but does not touch these docs is incomplete.
