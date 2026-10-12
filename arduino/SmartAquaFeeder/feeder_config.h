/**
 * feeder_config.h — SmartAqua feeder node configuration.
 *
 * Pin plan and rationale: docs/02-hardware-and-wiring.md § "Feeder node —
 * pin plan". Everything here is bench-facing; the fail-safe constants that
 * outrank configuration live in SmartAquaCore/saq_portion.h.
 */

#pragma once

/* ------------------------------------------------------------------ */
/* Identity                                                            */
/* ------------------------------------------------------------------ */

#define FEEDER_FW_MAJOR 1
#define FEEDER_FW_MINOR 1
#define SAQ_NODE_ID     1u /* reported in BEACON / PAIR_REQ */

/* ------------------------------------------------------------------ */
/* Pins (classic ESP32, docs/02)                                       */
/* ------------------------------------------------------------------ */

/*
 * Relay IN — GPIO 26: safe output, not a strapping pin, no boot glitch,
 * no ADC2 conflict. Stays low and quiet through reset (M2 acceptance:
 * "relay silent during reset and boot").
 */
#define FEEDER_RELAY_PIN         26
#define FEEDER_RELAY_ACTIVE_HIGH 1 /* docs/02 recommends active-high modules */

/* Manual feed button — GPIO 27, to GND, internal pull-up. */
#define FEEDER_BUTTON_PIN 27

/* Status LED — GPIO 2 is the onboard LED on most devkits. */
#define FEEDER_LED_PIN 2

/* Optional DS3231 RTC (recommended, docs/02 BOM) on the default I2C pins.
 * Set FEEDER_HAVE_DS3231 to 1 and install an RTC module to survive power
 * cuts with the clock intact (design rule 3). Without it, the node re-syncs
 * from the HMI's PCF85063A after pairing. */
#define FEEDER_HAVE_DS3231 0
#define FEEDER_RTC_SDA     21
#define FEEDER_RTC_SCL     22

/* ------------------------------------------------------------------ */
/* Link — Mode A, fixed channel, no router (docs/07 § deployment modes).
 * Keys + channel come from the shared SmartAquaCore library
 * (saq_keys.h) so both firmwares cannot drift apart. */
#include "saq_keys.h"

/* ------------------------------------------------------------------ */
/* Feeder behaviour                                                    */
/* ------------------------------------------------------------------ */

/* Button: held this long → pairing window (docs/07 pairing flow). */
#define FEEDER_BUTTON_HOLD_MS 3000u

/* NVS namespace (docs/07 uses saq_cfg.peer_mac — keep that exact key). */
#define FEEDER_NS_CFG "saq_cfg"
#define FEEDER_NS_LOG "saq_log"

/* Retained log entries. 32 entries x 14 bytes = 448 B rewritten per feed;
 * NVS wear-levelling makes that comfortable for years of feeding. */
#define FEEDER_LOG_RING 32u
