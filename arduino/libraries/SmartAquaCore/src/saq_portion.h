/**
 * saq_portion.h — grams to milliseconds, and the hard clamp that outranks
 * everything else (docs/01-architecture.md rule 2).
 *
 * Pure C99. The clamp constant lives here so the feeder actuation layer, the
 * HMI editor warning, and the docs all quote one number.
 */

#ifndef SAQ_PORTION_H
#define SAQ_PORTION_H

#include "saq_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hard maximum energised duration. NOT configurable from the UI — that is
 * the entire point (rule 2: no malformed command, hung task, or reboot can
 * run the motor indefinitely). If your auger legitimately needs longer,
 * change the constant here, in one place, and re-flash both apps.
 */
#define SAQ_MAX_FEED_MS 30000u

/* SAQ_PORTION_MIN_G / SAQ_PORTION_MAX_G live in saq_protocol.h. */

/* Default calibration: milliseconds of auger run per gram. 6 s/g is a
 * typical starting point for a small screw feeder; refine with the guided
 * calibration flow on the HMI Settings screen. */
#define SAQ_MS_PER_G_DEFAULT 6000u

/* Shortest run we will ever command — avoids a relay click with no food
 * moved when calibration is aggressive. */
#define SAQ_MIN_FEED_MS 250u

/**
 * Milliseconds the auger should run for `portion_g` grams at the given
 * calibration, clamped to [SAQ_MIN_FEED_MS, SAQ_MAX_FEED_MS].
 */
uint32_t saq_portion_ms(uint16_t portion_g, uint16_t ms_per_g);

/**
 * True when saq_portion_ms() would clamp — the editor shows a warning
 * before saving rather than discovering truncation in a log later.
 */
bool saq_portion_will_clamp(uint16_t portion_g, uint16_t ms_per_g);

/**
 * Grams actually delivered if clamped at SAQ_MAX_FEED_MS — what "Fed X g"
 * should say when the clamp bit into the request.
 */
uint16_t saq_portion_clamped_g(uint16_t portion_g, uint16_t ms_per_g);

#ifdef __cplusplus
}
#endif

#endif /* SAQ_PORTION_H */
