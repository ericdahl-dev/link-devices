// Pure quantized-launch state machine — see transport_launch.h (ESP-011).
#include "transport_launch.h"
#include <math.h>

void transport_launch_reset(TransportLaunch* t) {
    t->state      = TL_STOPPED;
    t->have_last  = false;
    t->last_beats = 0.0;
}

// Did `beats` reach or pass a multiple of `quantum` since `last`? Also true when
// `beats` lands exactly on one, so pressing play on the downbeat fires now
// rather than waiting a whole bar.
static bool crossed_boundary(double last, double beats, double quantum) {
    if (quantum <= 0.0) return true;
    return floor(beats / quantum) > floor(last / quantum) || fmod(beats, quantum) == 0.0;
}

TransportLaunchOut transport_launch_step(TransportLaunch* t, TransportLaunchIntent intent,
                                         double beats, double quantum, bool have_beat) {
    TransportLaunchOut o = { TL_NONE, t->state };

    switch (intent) {
        case TL_INTENT_STOP:
            // Immediate, never quantized. Stopping while merely ARMED disarms
            // silently: we never started, so there is nothing to stop.
            if (t->state == TL_RUNNING) o.action = TL_STOP;
            t->state = TL_STOPPED;
            break;

        case TL_INTENT_PLAY:
            if (t->state == TL_RUNNING) break;   // no double Start
            if (!have_beat) {                    // nothing to quantize to
                t->state  = TL_RUNNING;
                o.action  = TL_START;
            } else if (fmod(beats, quantum) == 0.0) {   // already on the grid
                t->state  = TL_RUNNING;
                o.action  = TL_START;
            } else {
                t->state = TL_ARMED;
            }
            break;

        case TL_INTENT_NONE:
            if (t->state == TL_ARMED) {
                // Beat grid vanished under us: start rather than hang armed.
                if (!have_beat) {
                    t->state = TL_RUNNING;
                    o.action = TL_START;
                } else if (t->have_last && crossed_boundary(t->last_beats, beats, quantum)) {
                    t->state = TL_RUNNING;
                    o.action = TL_START;
                }
            }
            break;
    }

    t->last_beats = beats;
    t->have_last  = true;
    o.state       = t->state;
    return o;
}
