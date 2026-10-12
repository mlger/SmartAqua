/**
 * saq_keys.h — ESP-NOW deployment constants shared by both SmartAqua nodes.
 *
 * These are device-family constants in the sense of docs/07 § Encryption
 * ("compiled in as a device-family constant" is one of the two accepted
 * provisioning paths). They live here — once, in the shared library — so
 * the two firmwares cannot drift apart.
 *
 * CHANGE THEM BEFORE YOU DEPLOY. Anyone in radio range who knows these
 * bytes can command a feed. Keep them out of any public fork.
 */

#ifndef SAQ_KEYS_H
#define SAQ_KEYS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Mode A fixed channel (docs/07): 1, 6 or 11 — the non-overlapping ones.
 * Both nodes must agree; changing it means re-flashing both. */
#define SAQ_ESPNOW_CHANNEL     1

/* Primary Master Key — set once per device with esp_now_set_pmk(). */
static const uint8_t SAQ_PMK_BYTES[16] = {
    0x9a, 0x37, 0x4f, 0x21, 0xc8, 0x56, 0xd3, 0x0e,
    0x7b, 0x14, 0xa9, 0x62, 0x3d, 0xf0, 0x85, 0x2c,
};

/* Local Master Key — per-peer, used for all unicast traffic once both
 * sides have upgraded the peer after the pairing handshake. */
static const uint8_t SAQ_LMK_BYTES[16] = {
    0x51, 0xe3, 0x08, 0xd7, 0x46, 0x9c, 0x2a, 0xbd,
    0xf8, 0x63, 0x1d, 0x94, 0x07, 0xcb, 0x3e, 0x70,
};

#ifdef __cplusplus
}
#endif

#endif /* SAQ_KEYS_H */
