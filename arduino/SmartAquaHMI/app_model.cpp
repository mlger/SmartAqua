/**
 * app_model.cpp — see app_model.h.
 */

#include "app_model.h"
#include "saq_portion.h"
#include "saq_rtc.h"

#include <Arduino.h>
#include <Preferences.h>
#include <string.h>

app_model_t g_app;

static Preferences prefs;

void model_init(void) {
    memset(&g_app, 0, sizeof(g_app));

    g_app.tz_h = 8; /* sensible default, change in Settings */
    g_app.default_portion_g = 3;
    g_app.ms_per_g = SAQ_MS_PER_G_DEFAULT;

    prefs.begin(HMI_NS_CFG, true);
    g_app.tz_h = prefs.getInt("tz_h", 8);
    g_app.default_portion_g = prefs.getUShort("portion_g", 3);
    g_app.ms_per_g = prefs.getUShort("ms_per_g", SAQ_MS_PER_G_DEFAULT);
    prefs.end();

    if (g_app.default_portion_g < SAQ_PORTION_MIN_G) {
        g_app.default_portion_g = SAQ_PORTION_MIN_G;
    }
    if (g_app.default_portion_g > SAQ_PORTION_MAX_G) {
        g_app.default_portion_g = SAQ_PORTION_MAX_G;
    }
    if (g_app.ms_per_g == 0) {
        g_app.ms_per_g = SAQ_MS_PER_G_DEFAULT;
    }

    /* History */
    prefs.begin(HMI_NS_HIST, true);
    g_app.hist_count = (uint16_t)prefs.getUShort("count", 0);
    g_app.hist_last_seq = prefs.getUInt("last_seq", 0);
    if (g_app.hist_count > HMI_HIST_MAX) {
        g_app.hist_count = 0;
    }
    prefs.getBytes("ring", g_app.hist, (size_t)g_app.hist_count * sizeof(saq_hist_entry_t));
    prefs.end();

    /* Schedule cache starts empty; it is pushed to the feeder after
     * pairing (see the sync note in arduino/README.md). */
    memset(&g_app.sched, 0, sizeof(g_app.sched));

    g_app.clock_ok = rtc_get_utc(NULL);

    (void)model_load_peer();
}

uint32_t model_epoch(void) {
    uint32_t e = 0;
    if (!rtc_get_utc(&e)) {
        return 0; /* unsynced — callers show "Time?" */
    }
    return e;
}

int32_t model_tz_s(void) {
    return g_app.tz_h * 3600;
}

uint32_t model_local_epoch(void) {
    const uint32_t utc = model_epoch();
    if (utc == 0) {
        return 0;
    }
    return utc + (uint32_t)model_tz_s();
}

bool model_hist_add(const saq_hist_entry_t *e) {
    if (e == NULL) {
        return false;
    }
    if (e->log_seq != 0 && e->log_seq <= g_app.hist_last_seq) {
        return false; /* already merged (dedupe by log_seq) */
    }
    if (e->log_seq != 0) {
        g_app.hist_last_seq = e->log_seq;
    }
    /* newest first */
    if (g_app.hist_count == HMI_HIST_MAX) {
        memmove(&g_app.hist[1], &g_app.hist[0], (size_t)(HMI_HIST_MAX - 1) * sizeof(saq_hist_entry_t));
    } else {
        memmove(&g_app.hist[1], &g_app.hist[0], (size_t)g_app.hist_count * sizeof(saq_hist_entry_t));
        g_app.hist_count++;
    }
    g_app.hist[0] = *e;

    prefs.begin(HMI_NS_HIST, false);
    prefs.putUShort("count", g_app.hist_count);
    prefs.putBytes("ring", g_app.hist, (size_t)g_app.hist_count * sizeof(saq_hist_entry_t));
    prefs.putUInt("last_seq", g_app.hist_last_seq);
    prefs.end();
    return true;
}

void model_set_tz(int32_t tz_h) {
    if (tz_h < -12) {
        tz_h = -12;
    }
    if (tz_h > 14) {
        tz_h = 14;
    }
    g_app.tz_h = tz_h;
    prefs.begin(HMI_NS_CFG, false);
    prefs.putInt("tz_h", (int)tz_h);
    prefs.end();
}

void model_set_cfg(uint16_t ms_per_g, uint16_t default_portion_g) {
    g_app.ms_per_g = ms_per_g;
    g_app.default_portion_g = default_portion_g;
    prefs.begin(HMI_NS_CFG, false);
    prefs.putUShort("ms_per_g", ms_per_g);
    prefs.putUShort("portion_g", default_portion_g);
    prefs.end();
}

bool model_load_peer(void) {
    uint8_t mac[6] = {0};
    prefs.begin(HMI_NS_CFG, true);
    const size_t got = prefs.getBytes("peer_mac", mac, 6);
    prefs.end();
    if (got != 6) {
        g_app.paired = false;
        return false;
    }
    bool all_zero = true;
    for (int i = 0; i < 6; i++) {
        if (mac[i] != 0) {
            all_zero = false;
        }
    }
    if (all_zero) {
        g_app.paired = false;
        return false;
    }
    memcpy(g_app.peer_mac, mac, 6);
    g_app.paired = true;
    return true;
}

void model_save_peer(const uint8_t mac[6]) {
    memcpy(g_app.peer_mac, mac, 6);
    g_app.paired = true;
    prefs.begin(HMI_NS_CFG, false);
    prefs.putBytes("peer_mac", mac, 6);
    prefs.end();
}
