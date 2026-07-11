// Shared pure clock-tick scheduler — see clock_ticker.h.
#include "clock_ticker.h"
#include <math.h>

void clock_ticker_reset(ClockTicker* s) {
    s->last_tick = 0;
    s->primed    = false;
    /* Zeroed here so the counter is always INITIALISED -- callers do
     * `ClockTicker t; clock_ticker_reset(&t);`, so leaving it alone means it starts as
     * stack garbage and can never be trusted. reset() only fires at startup and on Link
     * phase-invalid (no clock to drop anyway), so nothing real is lost. Accumulating a
     * LIFETIME total across resets is the glue's job: a pure struct cannot tell "first
     * init" from "re-prime", and that distinction is what the decision needs. */
    s->dropped   = 0;
}

int clock_ticker_ticks_due(ClockTicker* s, double beats_now, int ppqn, int max_burst) {
    if (ppqn <= 0) return 0;
    if (beats_now < 0.0) beats_now = 0.0;
    int64_t tick = (int64_t)floor(beats_now * (double)ppqn);

    if (!s->primed) {           // first observation: align the grid, no backlog
        s->primed    = true;
        s->last_tick = tick;
        return 0;
    }

    int64_t due = tick - s->last_tick;
    if (due <= 0) return 0;     // backward or same 1/ppqn-beat slot

    if (max_burst > 0 && due > (int64_t)max_burst) {
        // Big forward jump (tempo re-origin / long stall): realign to the new
        // position instead of flooding the output with a catch-up burst.
        //
        // ESP-018: this THROWS AWAY every pending pulse. Still the right musical call
        // -- a 20-pulse burst is worse than a gap -- but it used to happen silently,
        // so a stall long enough to trip it left no evidence anywhere. Count it.
        s->dropped  += (uint32_t)due;
        s->last_tick = tick;
        return 0;
    }

    s->last_tick = tick;
    return (int)due;
}

void bar_reset_reset(BarReset* s) {
    s->last_bar = 0;
    s->primed   = false;
}

bool bar_reset_due(BarReset* s, double beats_now, double quantum) {
    if (quantum <= 0.0) return false;
    if (beats_now < 0.0) beats_now = 0.0;
    int64_t bar = (int64_t)floor(beats_now / quantum);

    if (!s->primed) {
        s->primed   = true;
        s->last_bar = bar;
        return false;
    }

    int64_t delta = bar - s->last_bar;
    if (delta == 1) {           // clean crossing into the next bar
        s->last_bar = bar;
        return true;
    }
    if (delta != 0) {           // backward or multi-bar jump (re-origin): realign,
        s->last_bar = bar;      // don't fire a false downbeat
    }
    return false;
}
