// Pure logic for the Ableton Link measurement (ping/pong) protocol.
// No WiFi/Arduino dependency — testable on host. See link_measurement.h for
// the wire format and algorithm references.

#include "link_measurement.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static const uint8_t MAGIC[8] = {'_','l','i','n','k','_','v', 1};

static const uint8_t MSG_PING = 1;
static const uint8_t MSG_PONG = 2;

static const uint32_t HOST_TIME_KEY  = 0x5f5f6874u;  // '__ht'
static const uint32_t GHOST_TIME_KEY = 0x5f5f6774u;  // '__gt'
static const uint32_t PREV_GHOST_KEY = 0x5f706774u;  // '_pgt'
// SessionMembership ('sess') is present in real pongs but ignored here —
// we don't track session identity changes in this path.

/* ---------------------------------------------------------------------- */
/* Byte helpers (BE), same conventions as link_protocol.c                 */
/* ---------------------------------------------------------------------- */

static int64_t be64(const uint8_t* p) {
    int64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

static void put_be32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}

static uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static void put_be64(uint8_t* p, int64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = (uint8_t)v; v >>= 8; }
}

// Append one TLV (key + 8-byte int64 value) at buf[i], return new offset.
static int put_tlv_i64(uint8_t* buf, int i, uint32_t key, int64_t value) {
    put_be32(buf + i, key);     i += 4;
    put_be32(buf + i, 8);       i += 4;
    put_be64(buf + i, value);   i += 8;
    return i;
}

/* ---------------------------------------------------------------------- */
/* GhostXForm                                                              */
/* ---------------------------------------------------------------------- */

int64_t link_ghost_xform_host_to_ghost(LinkGhostXForm xform, int64_t host_us) {
    return host_us + xform.intercept_us;
}

/* ---------------------------------------------------------------------- */
/* Ping build/parse                                                        */
/* ---------------------------------------------------------------------- */

int link_measurement_build_ping(uint8_t* buf, int buf_cap,
                                 int64_t host_time_us,
                                 bool has_prev_ghost, int64_t prev_ghost_us) {
    if (!buf) return 0;
    int need = 8 + 1 + 16 + (has_prev_ghost ? 16 : 0);
    if (buf_cap < need) return 0;

    int i = 0;
    memcpy(buf, MAGIC, 8); i += 8;
    buf[i++] = MSG_PING;
    i = put_tlv_i64(buf, i, HOST_TIME_KEY, host_time_us);
    if (has_prev_ghost) i = put_tlv_i64(buf, i, PREV_GHOST_KEY, prev_ghost_us);
    return i;
}

bool link_measurement_parse_ping(const uint8_t* buf, int len, LinkPingFields* out) {
    if (!buf || len < 9) return false;
    if (memcmp(buf, MAGIC, 8) != 0) return false;
    if (buf[8] != MSG_PING) return false;
    if (out) memset(out, 0, sizeof(*out));

    const uint8_t* p   = buf + 9;
    const uint8_t* end = buf + len;
    while (p + 8 <= end) {
        uint32_t key  = be32(p); p += 4;
        uint32_t size = be32(p); p += 4;
        if (p + size > end) break;
        if (key == HOST_TIME_KEY && size >= 8 && out) {
            out->has_host_time = true;
            out->host_time_us  = be64(p);
        } else if (key == PREV_GHOST_KEY && size >= 8 && out) {
            out->has_prev_ghost_time = true;
            out->prev_ghost_time_us  = be64(p);
        }
        p += size;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Pong parse                                                              */
/* ---------------------------------------------------------------------- */

bool link_measurement_parse_pong(const uint8_t* buf, int len, LinkPongFields* out) {
    if (!buf || len < 9) return false;
    if (memcmp(buf, MAGIC, 8) != 0) return false;
    if (buf[8] != MSG_PONG) return false;
    if (out) memset(out, 0, sizeof(*out));

    const uint8_t* p   = buf + 9;
    const uint8_t* end = buf + len;
    while (p + 8 <= end) {
        uint32_t key  = be32(p); p += 4;
        uint32_t size = be32(p); p += 4;
        if (p + size > end) break;
        if (key == GHOST_TIME_KEY && size >= 8 && out) {
            out->has_ghost_time = true;
            out->ghost_time_us  = be64(p);
        } else if (key == HOST_TIME_KEY && size >= 8 && out) {
            out->has_host_time = true;
            out->host_time_us  = be64(p);
        } else if (key == PREV_GHOST_KEY && size >= 8 && out) {
            out->has_prev_ghost_time = true;
            out->prev_ghost_time_us  = be64(p);
        }
        // unknown keys (e.g. SessionMembership 'sess') skipped via p += size below
        p += size;
    }
    return true;
}

/* ---------------------------------------------------------------------- */
/* Sample buffer / median                                                  */
/* ---------------------------------------------------------------------- */

static double s_samples[LINK_MEASUREMENT_MAX_SAMPLES];
static int    s_sample_count = 0;
static int    s_sample_next  = 0;  // ring write cursor

void link_measurement_samples_reset(void) {
    s_sample_count = 0;
    s_sample_next  = 0;
}

int link_measurement_samples_count(void) { return s_sample_count; }

static void push_sample(double v) {
    s_samples[s_sample_next] = v;
    s_sample_next = (s_sample_next + 1) % LINK_MEASUREMENT_MAX_SAMPLES;
    if (s_sample_count < LINK_MEASUREMENT_MAX_SAMPLES) s_sample_count++;
}

/* P4-038 phase-health counters. Lifetime; reset only by link_measurement_reset(). */
static uint32_t s_ph_commits;
static uint32_t s_ph_last_step_us;
static uint32_t s_ph_max_step_us;
static uint32_t s_ph_rtt_min_us = UINT32_MAX;
static uint32_t s_ph_rtt_max_us;

void link_measurement_phase_health(LinkPhaseHealth* out) {
    if (!out) return;
    out->commits      = s_ph_commits;
    out->last_step_us = s_ph_last_step_us;
    out->max_step_us  = s_ph_max_step_us;
    out->rtt_min_us   = (s_ph_rtt_min_us == UINT32_MAX) ? 0 : s_ph_rtt_min_us;
    out->rtt_max_us   = s_ph_rtt_max_us;
}

/* P4-038: the RTT that produced each sample, so the commit can throw out the queued ones. */
static uint32_t s_sample_rtt[LINK_MEASUREMENT_MAX_SAMPLES];

int link_measurement_add_pong_samples(int64_t h_recv, const LinkPongFields* pong) {
    if (!pong || !pong->has_ghost_time || !pong->has_host_time) return 0;

    // LNK-026 bug 2: drop a pong whose round trip is negative or implausibly
    // large — it's a stale/mismatched reply (e.g. left in the RX buffer across
    // the idle gap between attempts), and its ~2s-old echoed host_time would
    // poison the median by ~1s (~2 beats). See LINK_MEASUREMENT_MAX_RTT_US.
    int64_t rtt = h_recv - pong->host_time_us;
    if (rtt < 0 || rtt > LINK_MEASUREMENT_MAX_RTT_US) return 0;

    /* P4-038: the RTT of an ACCEPTED sample is the direct predictor of how far this
     * commit can throw the origin -- the midpoint estimate below assumes a symmetric
     * delay, so it carries ~rtt/2 of error when the delay is not. */
    if ((uint32_t)rtt < s_ph_rtt_min_us) s_ph_rtt_min_us = (uint32_t)rtt;
    if ((uint32_t)rtt > s_ph_rtt_max_us) s_ph_rtt_max_us = (uint32_t)rtt;

    int added = 0;
    double g      = (double)pong->ghost_time_us;
    double h_sent = (double)pong->host_time_us;
    double h_r    = (double)h_recv;

    double sample_a = g - (h_r + h_sent) / 2.0;
    if (s_sample_count < LINK_MEASUREMENT_MAX_SAMPLES) s_sample_rtt[s_sample_count] = (uint32_t)rtt;
    push_sample(sample_a);
    added++;

    if (pong->has_prev_ghost_time) {
        double prev_g    = (double)pong->prev_ghost_time_us;
        double sample_b  = (g + prev_g) / 2.0 - h_sent;
        if (s_sample_count < LINK_MEASUREMENT_MAX_SAMPLES) s_sample_rtt[s_sample_count] = (uint32_t)rtt;
        push_sample(sample_b);
        added++;
    }

    return added;
}

static int cmp_double(const void* a, const void* b) {
    double da = *(const double*)a, db = *(const double*)b;
    return (da > db) - (da < db);
}

bool link_measurement_median(double* out_median) {
    if (s_sample_count == 0) return false;

    /* P4-038: only the low-RTT samples vote. A sample delayed well past the best one in
     * this attempt was queued, so its midpoint estimate is asymmetric, so it is biased by
     * ~rtt/2 -- and that bias is what threw the origin 500 ms on the bench. Every sample
     * at the same RTT (the host tests, and an idle radio) survives the filter unchanged. */
    uint32_t best = UINT32_MAX;
    for (int i = 0; i < s_sample_count; i++)
        if (s_sample_rtt[i] < best) best = s_sample_rtt[i];

    double tmp[LINK_MEASUREMENT_MAX_SAMPLES];
    int n = 0;
    for (int i = 0; i < s_sample_count; i++)
        if (s_sample_rtt[i] <= best + LINK_MEASUREMENT_RTT_SLACK_US) tmp[n++] = s_samples[i];
    if (n == 0) return false;   /* cannot happen: the best sample always survives */

    qsort(tmp, (size_t)n, sizeof(double), cmp_double);
    double m = (n % 2 == 1) ? tmp[n / 2]
                            : (tmp[n / 2 - 1] + tmp[n / 2]) / 2.0;
    if (out_median) *out_median = m;
    return true;
}

/* ---------------------------------------------------------------------- */
/* Attempt lifecycle                                                       */
/* ---------------------------------------------------------------------- */

static bool           s_active = false;
static LinkGhostXForm s_xform  = {0, false};

void link_measurement_reset(void) {
    link_measurement_samples_reset();
    s_active = false;
    s_xform.intercept_us = 0;
    s_xform.valid = false;
    /* P4-038: the gauge is module state too. A reset that leaves it behind reports a step
     * count from a previous life -- ks_tick_reset forgot exactly this and the host test
     * caught it as a phantom +1. */
    s_ph_commits      = 0;
    s_ph_last_step_us = 0;
    s_ph_max_step_us  = 0;
    s_ph_rtt_min_us   = UINT32_MAX;
    s_ph_rtt_max_us   = 0;
}

void link_measurement_attempt_begin(void) {
    link_measurement_samples_reset();
    s_active = true;
}

void link_measurement_attempt_end(bool success) {
    if (success) {
        double m;
        if (link_measurement_median(&m)) {
            int64_t next = (int64_t)llround(m);

            if (s_xform.valid) {
                int64_t d   = next - s_xform.intercept_us;
                int64_t mag = d < 0 ? -d : d;

                /* P4-038: gauge the step the MEASUREMENT ASKED FOR, before the clamp trims
                 * it. That is the diagnostic: it says how badly the estimate is behaving
                 * even when the slew is successfully protecting the bar line. Gauging the
                 * APPLIED move instead would just report the clamp back to us, and the
                 * moment the fix worked we would go blind to the thing it is fixing. */
                s_ph_last_step_us = (uint32_t)mag;
                if ((uint32_t)mag > s_ph_max_step_us) s_ph_max_step_us = (uint32_t)mag;

                /* SLEW: bound how far one commit may move an ESTABLISHED origin. A genuine
                 * re-origin (>= 1 s; link_phase.c has seen ~510 s) is adopted whole -- the
                 * clamp rejects measurement noise, it does not fight the session. */
                if (mag < LINK_MEASUREMENT_REORIGIN_US && mag > LINK_MEASUREMENT_MAX_SLEW_US) {
                    next = s_xform.intercept_us +
                           (d > 0 ? LINK_MEASUREMENT_MAX_SLEW_US : -LINK_MEASUREMENT_MAX_SLEW_US);
                }
            }
            s_ph_commits++;
            s_xform.intercept_us = next;
            s_xform.valid = true;
        }
        // empty sample buffer despite a "success" call: leave existing
        // xform (if any) untouched rather than committing garbage.
    }
    s_active = false;
}

LinkGhostXForm link_measurement_current_xform(void) { return s_xform; }
bool           link_measurement_active(void)        { return s_active; }

// A committed xform is our phase estimate. Deliberately independent of
// s_active: an attempt in flight never makes phase valid, and a committed
// estimate stays trustworthy across the next attempt (which leaves it
// untouched on failure). See ARC-002.
bool link_measurement_have_phase_estimate(void) { return s_xform.valid; }
