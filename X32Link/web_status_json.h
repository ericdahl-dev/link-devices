// Pure builder for the /status JSON payload (LNK-022) — see web_config.cpp.
// Host-testable: no Arduino/WebServer dependency, just snprintf. Same
// "pull the formatting logic out of the .ino-adjacent file" pattern as
// led_phase.h/.c (LNK-021).
#pragma once
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

// ARC-024: tick health for the 1 ms MIDI writer. X32Link shipped with NO probe at
// all -- the firmware running the longest was the one measured the least, on the
// same S3 silicon and the same lwIP priority that produced ESP-018 on the Touch.
//
//   max_gap_us  >> 1000 -> the writer task was never SCHEDULED (preempted/starved,
//                          or a flash-cache stall, which freezes both cores).
//   max_work_us >> 1000 -> something INSIDE the tick blocked; w_* says which stage.
//   dropped             -> pulses the ticker THREW AWAY on a realign past MAX_BURST.
//                          These leave no burst and no gap on the wire, so without
//                          this counter a stall long enough to trip it is invisible.
//   core                -> which core the writer REALLY landed on. ESP-018's original
//                          premise about core assignment was wrong, and only the
//                          measurement caught it. Report it; never assume it.
typedef struct {
    uint32_t dropped;       // lifetime pulses discarded by the realign
    uint32_t bursts;        // ticks that emitted >1 pulse (catch-up)
    uint32_t max_gap_us;    // worst inter-tick gap
    uint32_t max_work_us;   // worst time spent inside a tick
    uint32_t overruns;      // ticks where gap or work blew past the threshold
    uint32_t w_beats;       // worst tick, stage: tempo_source_beats_now()
    uint32_t w_clock;       // worst tick, stage: scheduling + DIN/USB writes
    int      core;          // core the writer task actually runs on
    /* P4-038: times the beat grid was RE-PRIMED (phase went invalid, basis switched,
     * a GhostXForm re-commit moved the origin). This is the counter that tells a
     * SCHEDULING stall apart from a PHASE stall, and nothing else can.
     *
     * The probe proved KitchenSync's clock task was scheduled perfectly (gap 1.5 ms,
     * work 1.7 ms, zero overruns) while the analyzer watched its wire go silent for
     * 150 ms and then burst. A task that is never late cannot explain a wire that
     * stops -- unless the grid underneath it moved. A re-prime emits nothing and
     * counts no drops, so it is invisible in every other number here. */
    uint32_t reprimes;
} WebTickHealth;

// Formats {"bpm":F,"phase":F,"valid":bool,"quantum":N,"fw":"S"} into buf.
// `phase` and `quantum` are passed through as-is (caller decides what to do
// with tempo_source_phase()'s -1.0f "no reading yet" sentinel — this function
// doesn't special-case it, it just prints whatever float it's given;
// `valid` is the field JS actually gates on). `fw` is the firmware version
// string (LNK-038) — caller-supplied (glue passes FW_VERSION) so this builder
// stays pure and version-agnostic. `has_batt` gates two extra fields,
// `"batt_v":F,"batt_pct":F` — omitted entirely when false, so boards with no
// fuel gauge (i.e. most of them) get the same schema as before this field
// existed; the web JS feature-detects on `typeof d.batt_pct`. Returns the
// snprintf() return value (bytes that would have been written, excluding the
// terminator), so the caller can detect truncation the same way any snprintf
// caller would. `tick` (ARC-024) gates a further block of MIDI-writer tick-health
// fields, `"drop":N,"burst":N,"gap":N,"work":N,"over":N,"core":N,"w_beats":N,
// "w_clock":N` -- omitted entirely when NULL, so a build with no probe keeps the
// old schema byte for byte. Same opt-in shape as has_batt, and independent of it.
int web_status_json(char* buf, size_t buf_len, float bpm, float phase, bool valid, int quantum,
                    const char* fw, bool has_batt, float batt_v, float batt_pct,
                    const WebTickHealth* tick);

#ifdef __cplusplus
}
#endif
