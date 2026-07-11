// KitchenSync Touch tempo seam — Link ONLY (ESP-016). Implements tempo_source.h
// so the reused clock task (midi_clock_out_io) and the display talk to one
// interface. The Link path is lifted verbatim from X32Link/tempo_source.cpp; the
// MIDI-in adapter and its deps (midi_bpm / beat_synth) and the OSC coupling are
// dropped. Do NOT symlink tempo_source.cpp too — this replaces it.
#include "tempo_source.h"
#include "config.h"
#include "app_config.h"
#include "link_listener.h"
#include "link_measurement_io.h"
#include "link_protocol.h"
#include "link_measurement.h"
#include "link_phase.h"
#include "din_midi_out.h"        // MIDI_TX_GPIO
#include "ktouch_midi_out.h"     // DIN-only clock writer (no USB-MIDI composite)
#include <Arduino.h>
#include "esp_timer.h"

extern AppConfig g_config;   // owned by the .ino

extern "C" void tempo_source_select(int /*kind*/) { /* Link-only product */ }

// DIN-only product: no USB-MIDI, so nothing to enumerate before WiFi. Leaving USB
// as plain CDC keeps the serial/upload port stable (a composite CDC+MIDI drops it).
extern "C" void tempo_source_pre_net(void) { /* no USB-MIDI (see ktouch_midi_out) */ }

// Link joins multicast after WiFi; the measurement client keeps a GhostXForm warm.
extern "C" void tempo_source_begin(void) {
    link_listener_begin();
    link_measurement_io_begin();
    if (g_config.clock_enable) ktouch_midi_out_begin(MIDI_TX_GPIO);   // DIN 0xF8 task
}

extern "C" void tempo_source_poll(void) {
    link_listener_poll();
    link_listener_tick();
    link_measurement_io_poll();
}

extern "C" float tempo_source_bpm(void)    { return (float)link_listener_bpm(); }
extern "C" bool  tempo_source_active(void) { return link_listener_peers() > 0; }
extern "C" bool  tempo_source_beat(void)   { return false; }   // Inc1 display doesn't beat-flash

extern "C" bool tempo_source_phase_valid(void) {
    LinkTimeline t;
    return link_measurement_have_phase_estimate() && link_proto_timeline(&t);
}

// Monotonic non-wrapping beats — the clock generator's tick basis (LNK-027).
extern "C" double tempo_source_beats_now(void) {
    if (!tempo_source_phase_valid()) return -1.0;
    LinkTimeline timeline;
    link_proto_timeline(&timeline);
    LinkGhostXForm xform = link_measurement_current_xform();
    int64_t ghost_now_us = link_ghost_xform_host_to_ghost(xform, esp_timer_get_time());
    return link_phase_beats_now(timeline, ghost_now_us);
}

extern "C" float tempo_source_phase(float quantum) {
    double b = tempo_source_beats_now();
    if (b < 0.0) return -1.0f;
    return (float)link_phase_from_beats(b, (double)quantum);
}

extern "C" float    tempo_source_threshold(void) { return LINK_BPM_THRESHOLD; }
extern "C" uint32_t tempo_source_poll_ms(void)   { return LINK_POLL_MS; }
