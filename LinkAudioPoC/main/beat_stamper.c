#include "beat_stamper.h"
#include <math.h>

void beat_stamper_reset(BeatStamper* s)
{
    s->anchored = false;
    s->next_begin = 0.0;
}

double beat_stamper_stamp(BeatStamper* s, double measured_end_beats,
                          double bps, uint32_t frames, uint32_t rate)
{
    const double block_beats = (double)frames / (double)rate * bps;
    const double measured_begin = measured_end_beats - block_beats;

    if (!s->anchored || fabs(measured_begin - s->next_begin) > BEAT_STAMPER_RESYNC_BEATS) {
        s->next_begin = measured_begin;   // anchor / resync to what the session says
        s->anchored = true;
    } else {
        // Servo: the anchor came from ONE boot-time measurement, and whatever
        // scheduling/network error that instant carried would otherwise be
        // frozen in for the whole run (observed on hardware: +/-25ms run-to-
        // run placement wander with +/-0.05ms stability WITHIN each run,
        // 2026-07-10). Walking 1/128 of the remaining error out per block
        // converges a 20ms anchor bias in seconds and tracks crystal drift,
        // while staying ~two orders too slow to follow per-block jitter.
        s->next_begin += (measured_begin - s->next_begin) * (1.0 / 128.0);
    }

    const double begin = s->next_begin;
    s->next_begin = begin + block_beats;  // continuity: next block abuts exactly
    return begin;
}
