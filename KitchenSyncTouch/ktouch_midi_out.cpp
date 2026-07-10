// KitchenSync Touch MIDI writer — DIN clock (ESP-016).
//
// DIN-ONLY, deliberately. Dan's RC-505 is driven over a 5-pin DIN jack, so this
// product has no reason to be a USB-MIDI device -- and being one actively hurts:
// USB.begin() with a MIDI interface turns the S3 into a composite CDC+MIDI device,
// and that re-enumeration knocks the native USB-CDC serial/upload port off the
// host (diagnosed on the bench -- the port only returns via the ROM download mode).
// Keeping USB plain CDC = stable serial. USB-MIDI can be an opt-in feature later.
//
// The tick TIMING is the pure, host-tested midi_clock_out engine (over clock_ticker).
#include "ktouch_midi_out.h"
#include "midi_clock_out.h"   // MidiClockOut scheduler: quantize beats -> 24-PPQN
#include "din_midi_out.h"
#include "tempo_source.h"
#include <Arduino.h>

// A gap over this many 1/24-beat ticks between task iterations (a re-origin or a
// long stall) realigns instead of spraying a catch-up burst. Same as X32Link.
static const int MAX_BURST = 4;
static MidiClockOut s_sched;

// 1 ms writer task: quantize the current Link beat to 24 PPQN, emit due 0xF8 on
// the DIN wire. Resets when phase isn't valid so we never clock off a stale/absent
// timeline. Transport (0xFA/0xFC) joins this single writer in Inc2b.
static void writer_task(void*) {
    midi_clock_out_reset(&s_sched);
    for (;;) {
        double beats = tempo_source_beats_now();   // <0 when phase not valid
        if (beats < 0.0) {
            midi_clock_out_reset(&s_sched);
        } else {
            int n = midi_clock_out_ticks_due(&s_sched, beats, MAX_BURST);
            for (int i = 0; i < n; i++) din_midi_out_byte(0xF8);
        }
        vTaskDelay(1);
    }
}

void ktouch_midi_out_begin(int tx_gpio) {
    din_midi_out_begin(tx_gpio);   // UART1 TX @ 31250 8N1 on the DIN pin
    xTaskCreatePinnedToCore(writer_task, "ks_midi_out", 2048, NULL, 6, NULL, 1);
}
