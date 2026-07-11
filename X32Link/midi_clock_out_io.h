#pragma once
// LNK-027 glue for Link → USB-MIDI clock OUT. Owns the high-priority timer task
// that reads the monotonic beat position from the tempo_source seam, runs the
// pure midi_clock_out.c scheduler, and writes 0xF8 ticks over USB-MIDI. Not
// host-testable (needs FreeRTOS + TinyUSB) — all timing math lives in the pure
// module. Call once, after USB + the phase pipeline are up, only when
// input_source == Link and midi_clock_out_enable.

#include <stdint.h>
#include <stdbool.h>
#include "web_status_json.h"   // WebTickHealth

void midi_clock_out_io_begin(void);

// ARC-024 tick health, surfaced in /status. X32Link is the firmware that has been
// running the LONGEST and was measured the LEAST — it carried no probe at all, on the
// same S3 silicon, the same on-die WiFi and the same lwIP priority that produced
// ESP-018 on the Touch. Every structural precondition for that bug is present here.
//
// The writer NEVER logs: an ESP_LOGx in a 1 ms real-time task is a blocking UART write
// (~10 ms at 115200), and that WAS the bug on the P4 (P4-033). Worse, the first version
// of this probe on the Touch logged on every overrun — the log delayed the next tick,
// which overran, which logged — manufacturing 3945 phantom overruns in 40 s. So the
// writer only ever PUBLISHES plain scalars and the web layer reads them.
//
// Returns false when the writer task was never started (clock-out disabled, or the
// device is on MIDI input rather than Link) — /status then omits the block entirely
// rather than publishing a row of zeroes that would read as "measured, all clean".
bool midi_clock_out_io_health(WebTickHealth* out);
