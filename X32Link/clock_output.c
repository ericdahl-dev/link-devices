#include "clock_output.h"

int clock_output_due(ClockTicker* t, double beats_now, int ppqn, int phase_mbeats, int max_burst) {
    // Positive phase fires earlier: advance the grid's view of the beat position.
    double shifted = beats_now + (double)phase_mbeats / 1000.0;
    return clock_ticker_ticks_due(t, shifted, ppqn, max_burst);
}
