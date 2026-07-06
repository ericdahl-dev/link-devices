#pragma once
// Pure MIDI-clock-IN tracker (P4-011): accumulates 24-PPQN timing pulses (0xF8)
// arriving on the USB-MIDI host IN endpoint and derives a stable BPM. The BPM
// math is the reused, host-tested midi_bpm_calc.c (24-PPQN pulse span -> BPM);
// this module owns the pulse ring + total count + stop-timeout. No ESP-IDF deps,
// so it is host-tested in test/test_midi_clock_in.c. The thin glue in
// usb_midi_host.c calls midi_clock_in_pulse() from its IN callback; the web
// status reads midi_clock_in_bpm(). (Publishing that tempo into Link is P4-011
// stage 2.)
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Pulses averaged for a BPM reading: exactly one beat of 24 PPQN. midi_bpm_calc
// bakes in "window intervals == one beat", so this MUST be 24 for correct BPM (a
// wider window would scale the tempo). us-precise esp_timer timestamps on the P4
// make one beat of averaging enough; the S3's jitter smoothing isn't needed.
#define MIDI_CLOCK_IN_WINDOW      24
// No pulse for this long -> the clock is considered stopped (BPM 0).
#define MIDI_CLOCK_IN_TIMEOUT_US  2000000

void     midi_clock_in_reset(void);

// Record one 24-PPQN timing pulse observed at ts_us (a monotonic microsecond
// clock, e.g. esp_timer_get_time()). Cheap + lock-free: a single writer.
void     midi_clock_in_pulse(int64_t ts_us);

// Total pulses seen since reset (a running 24-PPQN count -> beat = count/24).
uint32_t midi_clock_in_pulse_count(void);

// Current BPM, or 0.0f if fewer than MIDI_CLOCK_IN_WINDOW pulses have arrived or
// the last pulse is older than MIDI_CLOCK_IN_TIMEOUT_US (clock stopped). now_us
// is the same clock domain as midi_clock_in_pulse().
float    midi_clock_in_bpm(int64_t now_us);

#ifdef __cplusplus
}
#endif
