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
    }

    const double begin = s->next_begin;
    s->next_begin = begin + block_beats;  // continuity: next block abuts exactly
    return begin;
}
