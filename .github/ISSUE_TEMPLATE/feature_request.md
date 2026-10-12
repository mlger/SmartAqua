---
name: Feature request
about: Something the feeder should do that it does not
labels: enhancement
---

## The problem

<!-- What is awkward or impossible today? Describe the situation, not the solution. -->

## What you would like

## Which part of the system

- [ ] HMI UI — which screen?
- [ ] Feeder node behaviour
- [ ] Wireless protocol
- [ ] Hardware / wiring
- [ ] Docs and tooling

## Does this change a design rule?

The five rules in [docs/01-architecture.md](../../docs/01-architecture.md#design-rules) are
constraints, not preferences. If this request touches one — autonomy, the fail-safe relay, time
discipline, motor power, or switching location — say which, and argue the case. That is a legitimate
thing to do; doing it by accident is not.

- [ ] No design rule is affected
- [ ] This affects a design rule: <!-- which, and why -->

## Would this need a protocol change?

- [ ] No
- [ ] Yes — new packet type or a new field at the end of an existing payload (minor version bump)
- [ ] Yes — changes an existing field's meaning or layout (major version bump, **both boards must be
      reflashed together**)

## Anything else

<!-- Alternatives considered. Whether this is in the "deliberately not planned" list in
     docs/09-roadmap-and-milestones.md — if so, say why now is the time. -->
