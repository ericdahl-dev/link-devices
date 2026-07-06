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
