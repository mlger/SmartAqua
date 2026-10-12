---
name: Bug report
about: Something is broken or behaving unexpectedly
labels: bug
---

## What happens

<!-- What did you observe? Be specific: which screen, which board, what you did. -->

## What you expected

## Steps to reproduce

1.
2.
3.

## Which board

- [ ] HMI (ESP32-S3)
- [ ] Feeder node (ESP32)
- [ ] Both — or the link between them

## Versions

| | |
| --- | --- |
| HMI firmware | <!-- from Settings → Device, or the release tag --> |
| Feeder firmware | |
| Protocol version | |
| ESP-IDF | `idf.py --version` |
| LVGL | 8.4.x |

## Log output

```
<!-- idf.py monitor output around the failure. Redact WiFi credentials and the full peer MAC. -->
```

## Hardware state

<!-- Only if it looks electrical. Resets with no software cause, relay chatter, or a dead screen. -->

- [ ] Relay is silent at boot (no click during reset)
- [ ] No ESP32 reset when the motor starts
- [ ] No ESP32 reset when the motor stops
- [ ] Occurred with the hopper loaded and the enclosure closed
- [ ] Occurred only on the bench

## Is this one of the known causes?

Check [docs/08-testing-and-bringup.md](../../docs/08-testing-and-bringup.md#symptom--cause) first —
several of the symptoms that look like firmware bugs have diagnosed electrical causes.
