#pragma once
// ARC-022: when does a live config edit get written to flash?
//
// A live edit is a real edit -- it must survive a power cycle. But config is
// stored as ONE nvs blob (nvs_set_blob(h, KEY, cfg, sizeof(*cfg))); there is no
// per-field write. A slider drag emits dozens of POSTs, so writing on every edit
// would blob-write flash tens of times per gesture and wear NVS for no benefit.
//
// So edits mark the config dirty and the blob is written once they settle. This
// module owns that decision and nothing else: no flash, no timer, no Arduino, no
// wall clock. Time is an argument, so the policy is host-testable and the glue is
// left with nothing to get wrong but mark() on edit and due() on a poll.
//
// The write must never land on the 1 ms MIDI writer task: a flash write suspends
// the cache and freezes BOTH cores regardless of priority. Poll due() from a
// low-priority task (the P4's status_task at prio 2, the Arduino loop()).
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Quiet window: a burst is settled once this long passes with no new edit. Long
// enough that a slider drag is one write; short enough that a user who changes a
// setting and immediately yanks power almost certainly still keeps it.
#define CONFIG_PERSIST_QUIET_MS      1500u

// Maximum deferral: a continuous stream of edits that never leaves a quiet gap is
// written this long after the FIRST edit of the burst. Debounce defers a write;
// it must not starve one. Without this bound, a control the browser polls (or a
// user who never stops dragging) would keep resetting the quiet window forever.
#define CONFIG_PERSIST_MAX_DEFER_MS  10000u

typedef struct {
    uint32_t first_edit_ms;   // when the current dirty burst began
    uint32_t last_edit_ms;    // most recent edit in it
    bool     dirty;           // an edit is pending a write
} ConfigPersist;

// Clear state: not dirty, nothing owed. Callers stack-allocate, so this must zero
// every field -- see clock_ticker.dropped, which read stack garbage until it did.
void config_persist_reset(ConfigPersist* p);

// An edit arrived (call from the /live handler, after the config is updated).
void config_persist_mark(ConfigPersist* p, uint32_t now_ms);

// Should the blob be written NOW? True exactly once per settled burst -- it
// clears the dirty flag, so a poll loop cannot re-write the same blob every pass.
// Returns false on a clean tracker at any time.
bool config_persist_due(ConfigPersist* p, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
