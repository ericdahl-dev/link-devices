// Pure metronome scheduler — see metronome.h.
#include "metronome.h"

void metronome_reset(Metronome* m) {
    clock_ticker_reset(&m->beat);
    bar_reset_reset(&m->bar);
}

MetroClick metronome_update(Metronome* m, double beats_now, double quantum, int max_burst) {
    // Both engines observe the SAME beats value every call, so the downbeat
    // detection lands on the exact update that also crosses a beat (a bar
    // boundary is an integer beat). Call both unconditionally to keep BarReset
    // advancing in lockstep with the beat grid.
    int  due      = clock_ticker_ticks_due(&m->beat, beats_now, 1, max_burst);
    bool downbeat = bar_reset_due(&m->bar, beats_now, quantum);
    if (due <= 0) return METRO_NONE;              // mid-beat / backward / re-primed jump
    return downbeat ? METRO_ACCENT : METRO_CLICK;
}
