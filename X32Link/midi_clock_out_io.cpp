// LNK-027 glue: drive USB-MIDI clock OUT from the Link phase pipeline. See
// midi_clock_out_io.h. Timing math is in the pure, host-tested midi_clock_out.c;
// this file only owns the FreeRTOS task and the TinyUSB writes.
#include "midi_clock_out_io.h"
#include "midi_clock_out.h"
#include "midi_clock.h"      // midi_clock_send_f8()
#include "din_midi_out.h"    // ESP-016: mirror 0xF8 onto the DIN wire (S3 -> RC-505)
#include "tempo_source.h"    // tempo_source_beats_now()
#include <Arduino.h>

// A gap of more than this many 1/24-beat ticks between task ticks (a re-origin
// or a long stall) triggers a realign instead of a catch-up burst. At the 1ms
// task cadence, a normal step is 0-1 ticks even at high tempo, so 4 is generous
// slack for scheduling hiccups while still catching a real discontinuity.
static const int MAX_BURST = 4;

static MidiClockOut s_sched;

/* ARC-024 tick-health probe. This firmware has never been measured. The Touch's
 * identical writer, on identical silicon, turned out to be losing MIDI pulses under web
 * load (ESP-018) — and nothing about that was visible on serial, on the wire, or in any
 * counter. It took a probe to see it, and X32Link has not had one.
 *
 * Two candidate causes, and they are told apart by which number moves:
 *
 *   gap  >> 1ms  -> the task was never SCHEDULED. The cause is OUTSIDE this loop:
 *                   preemption by something higher (lwIP's tcpip_task sits at 18, this
 *                   task at 6), or a flash-cache stall, which freezes BOTH cores
 *                   regardless of priority or pinning.
 *   work >> 1ms  -> a call INSIDE the tick blocked. w_beats / w_clock say which.
 *
 * On the Touch the answer was gap, and the smoking gun was the stage split:
 * tempo_source_beats_now() measured 62us idle and 5038us under load — an 80x blowup
 * with no lock anywhere in that path. It was never blocking. It was being preempted.
 *
 * Published, never logged. See the header for why that is not a stylistic preference. */
static volatile uint32_t s_max_gap_us    = 0;
static volatile uint32_t s_max_work_us   = 0;
static volatile uint32_t s_overruns      = 0;
static volatile uint32_t s_bursts        = 0;   // ticks that emitted >1 pulse = catch-up
static volatile uint32_t s_dropped_total = 0;   // lifetime pulses discarded by the realign
static volatile int      s_core          = -1;  // which core this task ACTUALLY runs on
static volatile uint32_t s_w_beats = 0, s_w_clock = 0;   // worst-tick stage split

static volatile bool s_running = false;   // the writer task was actually created

bool midi_clock_out_io_health(WebTickHealth* out) {
    if (!out || !s_running) return false;
    out->max_gap_us  = s_max_gap_us;
    out->max_work_us = s_max_work_us;
    out->overruns    = s_overruns;
    out->bursts      = s_bursts;
    out->core        = s_core;
    out->w_beats     = s_w_beats;
    out->w_clock     = s_w_clock;
    // Banked total plus whatever the live ticker has counted since its last reset. This
    // is the number that matters most: a stall long enough to trip the realign leaves no
    // burst and no gap on the wire, so until this counter existed it was undetectable.
    out->dropped     = s_dropped_total + s_sched.dropped;
    return true;
}

// 1ms high-priority task: quantize the current Link beat position to 24 PPQN and
// emit any due 0xF8 ticks. Resets the scheduler whenever phase isn't valid
// (pre-sync, peer loss, mid re-measure) so we never spray ticks off a stale or
// absent timeline, and re-prime cleanly when it comes back.
static void midi_clock_out_task(void*) {
    s_core = xPortGetCoreID();   // ARC-024: confirm, don't assume. ESP-018's ticket was
                                 // WRONG about core assignment; only measuring caught it.
    midi_clock_out_reset(&s_sched);
    uint32_t prev_end = 0;
    /* vTaskDelayUntil, NOT vTaskDelay (ESP-018, ported here by ARC-024).
     *
     * vTaskDelay(1) sleeps one tick FROM NOW, so the period re-bases on every wake: the
     * loop body's own runtime and any preemption latency are ADDED to each cycle rather
     * than absorbed by it. Measured on the Touch, that alone produced gaps of up to
     * 5.7 ms with the board completely idle and no HTTP traffic.
     *
     * vTaskDelayUntil holds an ABSOLUTE 1 ms cadence: a tick that runs late fires
     * immediately instead of adding another whole tick on top, so the clock grid stops
     * inheriting the scheduler's slop. */
    TickType_t next_wake = xTaskGetTickCount();
    for (;;) {
        uint32_t tk0 = micros();
        uint32_t gap = prev_end ? (tk0 - prev_end) : 0;

        double beats = tempo_source_beats_now();   // <0 when phase not valid
        uint32_t t_beats = micros();
        if (beats < 0.0) {
            // Bank the discarded-pulse count before reset() zeroes it. reset() MUST zero
            // it (the ticker is stack/BSS-allocated, so an un-zeroed counter is garbage),
            // and a pure struct cannot tell "first init" from "re-prime" — so keeping the
            // LIFETIME total is the caller's job. After the first reset this adds 0, so
            // calling it every tick while phase is invalid is harmless.
            s_dropped_total += s_sched.dropped;
            midi_clock_out_reset(&s_sched);
        } else {
            int n = midi_clock_out_ticks_due(&s_sched, beats, MAX_BURST);
            if (n > 1) s_bursts++;   // more than one pulse in a 1 ms tick = catching up
            // Same code point for both wires: the DIN byte and the USB packet leave
            // microseconds apart, so DIN gear (ESP-016) is clocked in lockstep with
            // the USB path -- and DIN clocks even with no USB host attached.
            for (int i = 0; i < n; i++) { din_midi_out_byte(0xF8); midi_clock_send_f8(); }
        }

        uint32_t tk1  = micros();
        uint32_t work = tk1 - tk0;
        if (gap  > s_max_gap_us)  s_max_gap_us  = gap;
        if (work > s_max_work_us) {
            s_max_work_us = work;
            s_w_beats = t_beats - tk0;   // tempo_source_beats_now()
            s_w_clock = tk1     - t_beats;   // scheduling + DIN byte + USB packet
        }
        if (gap > 5000 || work > 5000) s_overruns++;
        prev_end = tk1;

        vTaskDelayUntil(&next_wake, 1);   // absolute 1 ms cadence, no drift
    }
}

void midi_clock_out_io_begin(void) {
    midi_clock_usb_begin();  // idempotent; USB.begin() is done in tempo_source_pre_net
    din_midi_out_begin(MIDI_TX_GPIO);   // ESP-016: DIN MIDI out on the S3 header
    /* ARC-024: priority 19, core 1 — the ESP-018 fix, ported to the firmware that never
     * got it. This task ran at 6, which is BELOW lwIP's tcpip_task (18): the network
     * stack could legally preempt the clock generator, and on the Touch, on identical
     * silicon, it did — costing real MIDI pulses under web load.
     *
     * 19 is the only defensible number, and it is a sandwich:
     *   - ABOVE lwIP (18), so serving the web UI cannot deschedule the clock.
     *   - BELOW WiFi (23), because starving the radio to feed the clock loses the Link
     *     session, and a perfectly-timed clock with no tempo is worth nothing.
     *
     * Core 1 is unchanged, and deliberately: Arduino's loopTask is on core 1 too
     * (CONFIG_ARDUINO_RUNNING_CORE=1), so the writer shares a core with the web server.
     * It now out-prioritises everything there. What priority CANNOT fix is a flash-cache
     * stall, which freezes both cores regardless — which is exactly why the config write
     * in ARC-022 is debounced and kept on a low-priority task. */
    xTaskCreatePinnedToCore(midi_clock_out_task, "midi_clk_out", 2048, NULL, 19, NULL, 1);
    s_running = true;   // ARC-024: /status omits tick health entirely until this is set,
                        // so a row of zeroes can never be mistaken for "measured, clean".
}
