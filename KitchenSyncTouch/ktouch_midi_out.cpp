// KitchenSync Touch MIDI writer — DIN clock (ESP-016).
//
// DIN-ONLY, deliberately. Dan's RC-505 is driven over a 5-pin DIN jack, so this
// product has no reason to be a USB-MIDI device -- and being one actively hurts:
// USB.begin() with a MIDI interface turns the S3 into a composite CDC+MIDI device,
// and that re-enumeration knocks the native USB-CDC serial/upload port off the
// host (diagnosed on the bench -- the port only returns via the ROM download mode).
// Keeping USB plain CDC = stable serial. USB-MIDI can be an opt-in feature later.
//
// The tick TIMING is the pure, host-tested clock_output engine (over
// clock_ticker) — the same derivation KitchenSync's plan loop uses (ARC-019),
// so nudge/swing/division and the reset-on-invalid rule have one tested owner.
#include "ktouch_midi_out.h"
#include "clock_output.h"     // ClockOutput: swing + nudge + ppqn -> pulses due
#include "din_midi_out.h"
#include "tempo_source.h"
#include "transport_launch.h"   // bar-quantized PLAY/STOP (ESP-011 pure engine)
#include "ktouch_transport.h"   // one-slot press mailbox
#include "app_config.h"         // quantum_beats
#include <Arduino.h>

extern AppConfig g_config;

// Burst cap + reset-on-invalid live in clock_output (CLOCK_OUTPUT_MAX_BURST),
// shared with X32Link's writer instead of copy-pasted into each task (ARC-019).
static ClockOutput     s_out;
static TransportLaunch s_tl;

// 1 ms writer task: quantize the current Link beat to 24 PPQN, emit due 0xF8 on
// the DIN wire. Resets when phase isn't valid so we never clock off a stale/absent
// timeline. Transport (0xFA/0xFC) joins this single writer in Inc2b.
/* ESP-018 tick-health probe. The analyzer caught the DIN clock stalling ~50 ms and then
 * emitting a compressed catch-up burst -- and because the catch-up is capped by
 * MAX_BURST, that means pulses are DROPPED, not merely delayed. On the RC-505 that is
 * an audible stutter.
 *
 * Do not guess which of the two possible causes it is -- measure. `gap` (previous tick's
 * end -> this tick's start) vs `work` (time spent inside the tick) separates them:
 *
 *   gap  >> 1ms  -> the task was never scheduled. The culprit is OUTSIDE this loop:
 *                   starvation by something else on core 1, or a flash-cache stall
 *                   (which freezes BOTH cores regardless of priority).
 *   work >> 1ms  -> a call in here blocked; the per-stage numbers say which.
 *
 * Worth knowing: this task is pinned to core 1, and Arduino's loopTask runs on core 1
 * too (CONFIG_ARDUINO_RUNNING_CORE=1). So the MIDI writer SHARES a core with the web
 * server, the LovyanGFX SPI blits and the I2C touch reads. It out-prioritises them
 * (6 vs 1), so plain preemption should cover it -- which is exactly why a large gap
 * would be interesting: it would mean something preemption cannot fix.
 *
 * Published, never logged: an ESP_LOGx in a 1 ms real-time task is a blocking UART
 * write, and that WAS the bug on the P4 (P4-033). The first version of that probe even
 * logged on every overrun, so the log delayed the next tick, which logged again --
 * 3945 phantom overruns in 40s. It manufactured the fault it measured. */
static volatile uint32_t s_max_gap_us  = 0;
static volatile uint32_t s_max_work_us = 0;
static volatile uint32_t s_overruns    = 0;
static volatile uint32_t s_bursts      = 0;   // ticks that emitted >1 clock = catch-up
static volatile int      s_core        = -1;  // which core this task actually runs on
// Per-stage worst case, captured on the tick that set max_work. `work` exploded from
// 124us (idle) to 2431us the moment a Link session came up -- so a call in here starts
// BLOCKING once there is a real timeline. These say which one.
static volatile uint32_t w_beats = 0, w_clock = 0, w_tport = 0;

uint32_t ktouch_midi_max_gap(void)  { return s_max_gap_us; }
uint32_t ktouch_midi_max_work(void) { return s_max_work_us; }
uint32_t ktouch_midi_overruns(void) { return s_overruns; }
uint32_t ktouch_midi_bursts(void)   { return s_bursts; }
uint32_t ktouch_midi_w_beats(void)  { return w_beats; }
uint32_t ktouch_midi_w_clock(void)  { return w_clock; }
uint32_t ktouch_midi_w_tport(void)  { return w_tport; }
int      ktouch_midi_core(void)     { return s_core; }

static void writer_task(void*) {
    s_core = xPortGetCoreID();   // ESP-018: confirm, don't assume
    clock_output_reset(&s_out);
    transport_launch_reset(&s_tl);
    uint32_t prev_end = 0;
    /* vTaskDelayUntil, NOT vTaskDelay (ESP-018).
     *
     * vTaskDelay(1) sleeps one tick FROM NOW, so the period re-bases every wake: the
     * loop body's time and any preemption latency are ADDED to each cycle instead of
     * absorbed. Measured on the bench, that produced gaps of up to 5.7 ms even with the
     * board completely idle and no HTTP traffic -- which is exactly the +-5 ms clock
     * jitter the analyzer saw (533us stdev, vs the P4's 331us).
     *
     * vTaskDelayUntil holds an absolute 1 ms cadence: if a tick runs late it fires
     * immediately rather than adding another whole tick on top. The clock grid stops
     * inheriting the scheduler's slop. */
    TickType_t next_wake = xTaskGetTickCount();
    for (;;) {
        uint32_t tk0 = micros();
        uint32_t gap = prev_end ? (tk0 - prev_end) : 0;

        double beats = tempo_source_beats_now();   // <0 when phase not valid
        uint32_t t_beats = micros();
        // One call owns the whole derivation (ARC-019): reset-on-invalid +
        // dropped banking + burst cap inside clock_output_step; nudge trims
        // only the CLOCK phase (+ve = ahead), tempo-relative so it holds
        // across tempo changes — transport START stays on the true bar.
        // ppqn 24 / swing 0 until the Touch grows RATE + SWING config fields
        // (ESP-017 parity; the derivation already supports both).
        int n = clock_output_step(&s_out, beats, 24, g_config.nudge_mbeats, 0);
        if (n > 1) s_bursts++;   // more than one pulse in a 1ms tick = catching up

        /* TRANSPORT FIRST, THEN CLOCK -- and the order is the whole point (ESP-023).
         *
         * The MIDI spec is explicit: a slave begins playing on the FIRST CLOCK IT
         * RECEIVES AFTER a Start. So the Start must arrive BEFORE the clock byte that
         * marks the downbeat. Emit the clock first and the slave discards it (it has not
         * started yet) and begins on the NEXT one -- exactly one 24-PPQN tick late, on
         * every single start, forever: 20.8 ms at 120 BPM, 17.2 ms at 145. That is what
         * "the drum machine falls behind when I start it from stopped" IS.
         *
         * ESP-023 measured this on the analyzer and fixed it on the P4 (ks_main.c): the
         * downbeat 0xF8 went out at +0.018 ms from the bar strobe and the 0xFA followed
         * it at +0.338 ms -- correct to the microsecond, and in the wrong ORDER. The Touch
         * had the identical bug and never got the fix. Both bytes go out the same UART in
         * the order written, so swapping these two blocks is the entire fix. Stop (0xFC)
         * is order-insensitive; it rides along because it shares the branch. */
        double q = (double)g_config.quantum_beats;
        TransportLaunchOut o = transport_launch_step(&s_tl, ktouch_transport_take(),
                                                     beats, q, beats >= 0.0);
        if (o.action == TL_START) din_midi_out_byte(0xFA);
        else if (o.action == TL_STOP) din_midi_out_byte(0xFC);
        ktouch_transport_publish_state(o.state);   // stopped/armed/running -> display

        uint32_t t_tport = micros();

        for (int i = 0; i < n; i++) din_midi_out_byte(0xF8);

        uint32_t tk1  = micros();
        uint32_t work = tk1 - tk0;
        if (gap  > s_max_gap_us)  s_max_gap_us  = gap;
        if (work > s_max_work_us) {
            s_max_work_us = work;
            // Stage split re-labelled to match the new order -- the probe must keep
            // measuring the stage it names, or ESP-018's next diagnosis reads a lie.
            w_beats = t_beats  - tk0;       // tempo_source_beats_now()
            w_tport = t_tport  - t_beats;   // clock_output_step + transport byte + publish
            w_clock = tk1      - t_tport;   // DIN clock bytes
        }
        if (gap > 5000 || work > 5000) s_overruns++;
        prev_end = tk1;

        vTaskDelayUntil(&next_wake, 1);   // absolute 1 ms cadence, no drift
    }
}

// Pulses the scheduler THREW AWAY (realign past the burst cap). The banking
// across resets lives in clock_output now (ARC-019); this stays the number
// that matters: a stall long enough to trip the realign leaves no burst and
// no gap on the wire, so before this counter existed it was undetectable.
uint32_t ktouch_midi_dropped(void) { return clock_output_dropped(&s_out); }

void ktouch_midi_out_begin(int tx_gpio) {
    din_midi_out_begin(tx_gpio);   // UART1 TX @ 31250 8N1 on the DIN pin
    /* ESP-018: priority 19, core 1. Both numbers are measured, not guessed.
     *
     * PRIORITY. The writer used to run at 6 -- BELOW lwIP's tcpip_task (18) and WiFi
     * (23). Those legally preempt it, so under heavy web traffic the network stack
     * descheduled the clock generator mid-tick. The stage probe caught it precisely:
     * the writer's one call into the Link engine, tempo_source_beats_now(), measured
     * 62us idle and 5038us under load -- an 80x blowup with no lock anywhere in that
     * path. It was never blocking; it was being PREEMPTED.
     *
     * On the wire, at 118 BPM under heavy web load:
     *     priority  6:  1 stall (40ms), 1 burst   <- pulses bunched
     *     priority 19:  0 stalls, 0 bursts        <- clean
     * and wbeats fell 5038us -> 87us.
     *
     * 19 sits ABOVE lwIP (18) and BELOW WiFi (23): the clock outranks the network
     * stack, but never the radio itself. The task is ~200us of work per 1ms tick, so
     * it cannot starve anything.
     *
     * This is why the P4 never had this bug: its WiFi lives on a separate C6
     * co-processor over SDIO, so its CPU never runs the network stack at all. The S3
     * runs WiFi on-die and has to out-rank it. (It is also most of why the P4 measures
     * 331us of jitter and the S3 ~500us.)
     *
     * CORE. Core 1, deliberately -- alongside Arduino's loopTask, not away from it:
     *     core 1:  gap 5749us, work  124us
     *     core 0:  gap 8927us, work 5952us   <- core 0 is where WiFi lives. Worse.
     */
    xTaskCreatePinnedToCore(writer_task, "ks_midi_out", 2048, NULL, 19, NULL, 1);
}
