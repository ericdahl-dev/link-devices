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
#include "transport_launch.h"   // bar-quantized PLAY/STOP (ESP-011 pure engine)
#include "ktouch_transport.h"   // one-slot press mailbox
#include "app_config.h"         // quantum_beats
#include <Arduino.h>

extern AppConfig g_config;

// A gap over this many 1/24-beat ticks between task iterations (a re-origin or a
// long stall) realigns instead of spraying a catch-up burst. Same as X32Link.
static const int MAX_BURST = 4;
static MidiClockOut    s_sched;
static TransportLaunch s_tl;

// 1 ms writer task: quantize the current Link beat to 24 PPQN, emit due 0xF8 on
// the DIN wire. Resets when phase isn't valid so we never clock off a stale/absent
// timeline. Transport (0xFA/0xFC) joins this single writer in Inc2b.
static void writer_task(void*) {
    midi_clock_out_reset(&s_sched);
    transport_launch_reset(&s_tl);
    for (;;) {
        double beats = tempo_source_beats_now();   // <0 when phase not valid
        if (beats < 0.0) {
            midi_clock_out_reset(&s_sched);
        } else {
            int n = midi_clock_out_ticks_due(&s_sched, beats, MAX_BURST);
            for (int i = 0; i < n; i++) din_midi_out_byte(0xF8);
        }

        // Transport: a touch press arms via transport_launch, which fires START on
        // the next bar line (STOP is immediate). Single writer, so no interleave
        // with the clock bytes above. DIN only.
        double q = (double)g_config.quantum_beats;
        TransportLaunchOut o = transport_launch_step(&s_tl, ktouch_transport_take(),
                                                     beats, q, beats >= 0.0);
        if (o.action == TL_START) din_midi_out_byte(0xFA);
        else if (o.action == TL_STOP) din_midi_out_byte(0xFC);
        ktouch_transport_publish_state(o.state);   // stopped/armed/running -> display

        vTaskDelay(1);
    }
}

void ktouch_midi_out_begin(int tx_gpio) {
    din_midi_out_begin(tx_gpio);   // UART1 TX @ 31250 8N1 on the DIN pin
    xTaskCreatePinnedToCore(writer_task, "ks_midi_out", 2048, NULL, 6, NULL, 1);
}
