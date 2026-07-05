#include "midi_clock_in.h"
#include "midi_bpm_calc.h"

// Ring of the last WINDOW pulse timestamps + a running total. The newest sits at
// (head-1); the oldest of the last WINDOW at head once the ring has filled.
static int64_t  s_ts[MIDI_CLOCK_IN_WINDOW];
static int      s_head    = 0;   // next write slot
static uint32_t s_count   = 0;   // total pulses since reset
static int64_t  s_last_us = 0;   // timestamp of the newest pulse

void midi_clock_in_reset(void) {
    s_head    = 0;
    s_count   = 0;
    s_last_us = 0;
}

void midi_clock_in_pulse(int64_t ts_us) {
    s_ts[s_head] = ts_us;
    s_head = (s_head + 1) % MIDI_CLOCK_IN_WINDOW;
    s_last_us = ts_us;
    s_count++;
}

uint32_t midi_clock_in_pulse_count(void) {
    return s_count;
}

float midi_clock_in_bpm(int64_t now_us) {
    if (s_count < (uint32_t)MIDI_CLOCK_IN_WINDOW) return 0.0f;
    if (now_us - s_last_us > MIDI_CLOCK_IN_TIMEOUT_US) return 0.0f;   // clock stopped

    // The WINDOW timestamps span (WINDOW-1) intervals. Newest is the slot just
    // written (s_head-1); oldest of the window is the current s_head slot (it is
    // about to be overwritten next, i.e. WINDOW-1 pulses back).
    int newest = (s_head - 1 + MIDI_CLOCK_IN_WINDOW) % MIDI_CLOCK_IN_WINDOW;
    int oldest = s_head;
    int64_t span = s_ts[newest] - s_ts[oldest];
    if (span <= 0) return 0.0f;

    return midi_bpm_calc((uint32_t)span, MIDI_CLOCK_IN_WINDOW);   // pure, host-tested
}
