#include "clock_output.h"
#include "swing.h"

int clock_output_due(ClockTicker* t, double beats_now, int ppqn,
                     int phase_mbeats, int swing_mbeats, int max_burst) {
    // Swing the musical time first (delays each off-eighth), then apply the
    // constant phase nudge (positive fires earlier), then quantize to ppqn.
    double swung   = swing_warp(beats_now, swing_mbeats);
    double shifted = swung + (double)phase_mbeats / 1000.0;
    return clock_ticker_ticks_due(t, shifted, ppqn, max_burst);
}

void clock_output_reset(ClockOutput* o) {
    clock_ticker_reset(&o->t);
    o->dropped = 0;
}

int clock_output_step(ClockOutput* o, double beats_now, int ppqn,
                      int phase_mbeats, int swing_mbeats) {
    if (beats_now < 0.0) {
        // Phase not valid: bank the discarded-pulse count before reset zeroes
        // it, re-prime so the next valid beat starts a clean grid, stay quiet.
        o->dropped += o->t.dropped;
        clock_ticker_reset(&o->t);
        return 0;
    }
    return clock_output_due(&o->t, beats_now, ppqn, phase_mbeats, swing_mbeats,
                            CLOCK_OUTPUT_MAX_BURST);
}

uint32_t clock_output_dropped(const ClockOutput* o) {
    return o->dropped + o->t.dropped;
}
