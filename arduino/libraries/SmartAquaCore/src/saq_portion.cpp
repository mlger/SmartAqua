/**
 * saq_portion.cpp — see saq_portion.h.
 */

#include "saq_portion.h"

uint32_t saq_portion_ms(uint16_t portion_g, uint16_t ms_per_g) {
    uint32_t ms;
    if (ms_per_g == 0) {
        ms_per_g = SAQ_MS_PER_G_DEFAULT; /* refuse a nonsense calibration */
    }
    if (portion_g == 0) {
        return 0;
    }
    /* 32-bit headroom: 10 g * 65535 ms/g would overflow u16 math only. */
    ms = (uint32_t)portion_g * (uint32_t)ms_per_g;

    if (ms > SAQ_MAX_FEED_MS) {
        ms = SAQ_MAX_FEED_MS;
    }
    if (ms < SAQ_MIN_FEED_MS) {
        ms = SAQ_MIN_FEED_MS;
    }
    return ms;
}

bool saq_portion_will_clamp(uint16_t portion_g, uint16_t ms_per_g) {
    if (ms_per_g == 0) {
        ms_per_g = SAQ_MS_PER_G_DEFAULT;
    }
    return (uint32_t)portion_g * (uint32_t)ms_per_g > SAQ_MAX_FEED_MS;
}

uint16_t saq_portion_clamped_g(uint16_t portion_g, uint16_t ms_per_g) {
    if (!saq_portion_will_clamp(portion_g, ms_per_g)) {
        return portion_g;
    }
    if (ms_per_g == 0) {
        return portion_g;
    }
    uint32_t g = SAQ_MAX_FEED_MS / (uint32_t)ms_per_g;
    if (g > SAQ_PORTION_MAX_G) {
        g = SAQ_PORTION_MAX_G;
    }
    return (uint16_t)g;
}
