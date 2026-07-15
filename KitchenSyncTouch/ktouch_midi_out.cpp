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
#include "beat_source.h"        // ESP-027: free-run through a lost GhostXForm (ARC-007)
#include "master_clock.h"       // ESP-027: originate a clock when solo (P4-040)
#include "link_protocol.h"      // link_proto_timeline()
#include "link_measurement.h"   // link_measurement_current_xform()
#include "esp_timer.h"          // esp_timer_get_time()
#include "ks_config.h"          // ESP-042: the shared fleet config
#include <Arduino.h>

extern KsConfig g_config;

// Burst cap + reset-on-invalid live in clock_output (CLOCK_OUTPUT_MAX_BURST),
// shared with X32Link's writer instead of copy-pasted into each task (ARC-019).
static ClockOutput     s_out;
static TransportLaunch s_tl;
static BeatSource      s_bs;   // ESP-027
static MasterClock     s_mc;   // ESP-027

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
// ESP-027 diagnostic: what beat position is the writer ACTUALLY seeing, and from where.
static volatile float s_dbg_beats = 0.0f;
static volatile int   s_dbg_locked = -1;
static volatile int   s_dbg_active = -1;

/* ESP-028: the only honest answer to "is the clock alive?" is BYTES ON THE WIRE.
 *
 * /status used to report `sync` = tempo_source_phase_valid() -- whether a phase ESTIMATE
 * exists. That says nothing about whether pulses reach the jack, and during ESP-027 it read
 * sync:1 peers:1 clock:1 drop:0 with ZERO bytes on the wire for 138 seconds. Every counter
 * green. It did not merely fail to report the fault; it actively said the device was healthy,
 * which is why finding it needed a logic analyzer instead of a glance at /status.
 *
 * The writer is the only thing that knows. It counts what it emits. */
static volatile uint32_t s_pulses        = 0;   // lifetime 0xF8 written to the UART
static volatile uint32_t s_last_pulse_ms = 0;   // when the last one went out

/* ESP-028: the SAME lie, one field over. /status reported bpm from tempo_source_bpm() ->
 * link_proto_bpm(), which zeroes the moment peers drop. So a device free-running perfectly
 * at 120 BPM on the master_clock reported bpm 0.0 -- while emitting a 120 BPM clock.
 *
 * Take it from the timeline the WRITER is actually clocking off (the arbiter's, which
 * survives peer loss), exactly as P4-039 did for the P4: "derive from the settled timeline,
 * which survives peer loss -- not link_proto_bpm(), which a caller may zero independently." */
static volatile float s_bpm = 0.0f;

uint32_t ktouch_midi_max_gap(void)  { return s_max_gap_us; }
uint32_t ktouch_midi_max_work(void) { return s_max_work_us; }
uint32_t ktouch_midi_overruns(void) { return s_overruns; }
uint32_t ktouch_midi_bursts(void)   { return s_bursts; }
uint32_t ktouch_midi_w_beats(void)  { return w_beats; }
uint32_t ktouch_midi_w_clock(void)  { return w_clock; }
uint32_t ktouch_midi_w_tport(void)  { return w_tport; }
int      ktouch_midi_core(void)     { return s_core; }
float    ktouch_midi_beats(void)    { return s_dbg_beats; }
int      ktouch_midi_locked(void)   { return s_dbg_locked; }
int      ktouch_midi_bs_active(void){ return s_dbg_active; }

// ESP-028. A monotonic pulse count makes "is the wire alive?" answerable by polling, with
// no analyzer. And the state answers it in one word.
uint32_t ktouch_midi_pulses(void) { return s_pulses; }
float    ktouch_midi_bpm(void)    { return s_bpm; }

// At 120 BPM a pulse is due every ~20.8 ms; the slowest tempo we allow is far inside 500 ms.
// So half a second of silence is not slow -- it is STOPPED.
#define KTOUCH_SILENT_AFTER_MS 500

const char* ktouch_midi_clock_state(void) {
    // Never emitted anything at all: not "locked", not "free". Silent.
    if (s_pulses == 0) return "silent";
    if ((uint32_t)(millis() - s_last_pulse_ms) > KTOUCH_SILENT_AFTER_MS) return "silent";
    return s_dbg_locked == 1 ? "locked" : "free";
}

static void writer_task(void*) {
    s_core = xPortGetCoreID();   // ESP-018: confirm, don't assume
    clock_output_reset(&s_out);
    transport_launch_reset(&s_tl);
    beat_source_reset(&s_bs);   // ESP-027
    master_clock_reset(&s_mc);  // ESP-027
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
    int s_applied_tempo_mbpm = -1;   // ESP-037
    for (;;) {
        uint32_t tk0 = micros();
        uint32_t gap = prev_end ? (tk0 - prev_end) : 0;

        /* ESP-037: apply the user's set tempo to the master clock. The WRITER owns s_mc,
         * so it reads g_config.tempo_mbpm (which the web task writes) and pushes it in on
         * CHANGE -- no cross-thread write to s_mc, same live-read pattern as nudge/ppqn.
         * This covers both cold boot (starts -1, so the configured tempo seeds the very
         * first tick -> a standalone box free-runs at YOUR tempo with no Link ever) and a
         * live /live edit. A Link session still wins: the arbiter ignores s_mc when a peer
         * is present. */
        if (g_config.tempo_mbpm != s_applied_tempo_mbpm) {
            master_clock_set_bpm(&s_mc, (float)g_config.tempo_mbpm / 1000.0f);
            s_applied_tempo_mbpm = g_config.tempo_mbpm;
        }

        /* ESP-027: the beat basis comes from beat_source (ARC-007), NOT straight from the
         * Link phase estimate.
         *
         * It used to call tempo_source_beats_now(), which returns -1 the moment the
         * committed GhostXForm is invalid -- and clock_output's reset-on-invalid rule then
         * emits NOTHING. So any loss of the xform silenced the DIN wire. Measured on the
         * analyzer: drop the Link session and the clock stops DEAD and never returns, while
         * /status still cheerfully reports sync:1 peers:1 clock:1. Zero bytes in 6 s.
         *
         * beat_source is what the P4 has always used and the Touch never got: while a
         * session exists it prefers the phase-locked position, and when the xform goes away
         * it falls back to a FREE-RUNNING accumulator at the last known tempo. The clock
         * keeps playing. It signals `reprime` on a basis switch so the tick grid realigns
         * instead of dumping a catch-up burst.
         *
         * have_session stays true here off the gossiped timeline alone -- that is the whole
         * point: the timeline is still arriving, only the local host<->ghost mapping is
         * momentarily gone, and a clock box must never go silent for that. */
        LinkTimeline   link_tl;
        bool link_have_session = link_proto_timeline(&link_tl) && link_tl.micros_per_beat > 0;
        LinkGhostXForm link_xform = link_measurement_current_xform();

        /* ESP-027 / P4-040: the arbiter, NOT the raw Link state.
         *
         * beat_source alone was not enough: it free-runs only while a session EXISTS but the
         * xform is invalid. On TOTAL session loss (the last peer leaves) have_session goes
         * false, beat_source stops, and the wire goes silent -- measured: kill the only Link
         * peer and the DIN clock dies within one beat and NEVER returns, while /status still
         * reports sync:1 peers:1 clock:1.
         *
         * For a CLOCK BOX that is indefensible. Close the laptop and the drum machine must
         * keep playing. master_clock_arbiter defers to Link whenever a peer is present and,
         * on the peers>0 -> 0 edge, seeds an internal tempo from whatever Link last showed --
         * so the output is continuous across the transition and needs no new UI.
         *
         * The P4 has had this since P4-040. The Touch never got it: master_clock.h said
         * "KitchenSync (P4) only", written when the Touch was not yet the clock box. It is
         * now the thing driving the RC-505, so it originates too. */
        MasterArbiterOut mc = master_clock_arbiter(&s_mc, link_proto_peers(),
                                                   link_have_session, link_tl, link_xform);
        BeatSourceOut  bs = beat_source_step(&s_bs, mc.have_session, mc.xform, mc.tl,
                                             esp_timer_get_time());
        if (bs.reprime) clock_output_reset(&s_out);   // realign, don't burst
        s_dbg_beats = (float)bs.beats; s_dbg_locked = bs.locked; s_dbg_active = bs.active;
        s_bpm = (bs.active && mc.tl.micros_per_beat > 0)          // ESP-028
                  ? (float)(60.0e6 / (double)mc.tl.micros_per_beat) : 0.0f;
        double beats = bs.active ? bs.beats : -1.0;
        uint32_t t_beats = micros();
        // One call owns the whole derivation (ARC-019): reset-on-invalid +
        // dropped banking + burst cap inside clock_output_step; nudge trims
        // only the CLOCK phase (+ve = ahead), tempo-relative so it holds
        // across tempo changes — transport START stays on the true bar.
        // ESP-030 pt3: rate + swing come from config now. The derivation always
        // supported both (clock_output.c is the P4's, symlinked); the Touch simply
        // had no fields to drive it, so it hardcoded 24/0. Defaults are 24/0, so a
        // migrated device emits the exact clock it emitted yesterday.
        int n = clock_output_step(&s_out, beats, g_config.clock[0].ppqn,
                                  g_config.clock[0].phase_mbeats, g_config.clock[0].swing_mbeats);
        if (n > 1) s_bursts++;   // more than one pulse in a 1ms tick = catching up

        /* TRANSPORT FIRST, THEN CLOCK -- and the order is the whole point (ESP-023).
         *
         * The MIDI spec is explicit: a slave begins playing on the FIRST CLOCK IT
         * RECEIVES AFTER a Start. So the Start must arrive BEFORE the clock byte that
         * marks the downbeat. Emit the clock first and the slave discards it (it has
         * not started yet) and begins on the NEXT one -- exactly one 24-PPQN tick late,
         * on every single start, forever: 20.8 ms at 120 BPM. That is what "the drum
         * machine falls behind when I start it from stopped" IS.
         *
         * ESP-023 measured this on the analyzer and fixed it on the P4 (ks_main.c).
         * The Touch had the same bug and never got the fix -- it emitted clock at the
         * top of the tick and transport at the bottom. Both bytes go out the same UART
         * in the order written, so the order of these two blocks is the entire fix. */
        double q = (double)g_config.quantum_beats;
        TransportLaunchOut o = transport_launch_step(&s_tl, ktouch_transport_take(),
                                                     beats, q, beats >= 0.0);
        switch (o.action) {
            case TL_START:   din_midi_out_byte(0xFA); break;
            case TL_STOP:    din_midi_out_byte(0xFC); break;
            case TL_RESTART:
                // ESP-025 realign: Stop then Start, in that order, on the bar line.
                // 0xFA is "play from the top", so the slave's pattern restarts at step 1
                // aligned to the bar -- without ever passing through a stopped state.
                din_midi_out_byte(0xFC);
                din_midi_out_byte(0xFA);
                break;
            case TL_NONE:
            default: break;
        }
        ktouch_transport_publish_state(o.state);   // stopped/armed/running -> display
        ktouch_transport_publish_realign(o.realign_armed);   // ESP-025 -> lit button

        uint32_t t_tport = micros();

        // Clock, AFTER transport (see above).
        for (int i = 0; i < n; i++) din_midi_out_byte(0xF8);
        if (n > 0) { s_pulses += (uint32_t)n; s_last_pulse_ms = millis(); }   // ESP-028

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
