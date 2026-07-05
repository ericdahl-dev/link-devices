#include "beat_clock.h"

void beat_clock_reset(BeatClock* s) {
    s->beats   = 0.0;
    s->last_us = 0;
    s->primed  = false;
}

double beat_clock_advance(BeatClock* s, int64_t now_us, int64_t micros_per_beat) {
    if (micros_per_beat <= 0) return s->beats;   // no valid tempo yet — hold
    if (!s->primed) {                            // anchor on the first advance
        s->primed  = true;
        s->last_us = now_us;
        return s->beats;
    }
    if (now_us > s->last_us) {                    // integrate forward at the current rate
        s->beats  += (double)(now_us - s->last_us) / (double)micros_per_beat;
        s->last_us = now_us;
    }
    return s->beats;
}
