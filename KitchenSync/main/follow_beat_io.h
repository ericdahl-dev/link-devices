#pragma once
// KitchenSync mic capture glue (P4-020) -- consumes the shared I2S RX channel
// from i2s_audio_bus.c (ES8311 mic-in, DIN=GPIO11) and feeds the pure
// follow_beat.c tempo detector. Thin per ADR-0003: this file only gets samples
// off the wire; the BPM decision lives in follow_beat.c.
#include <stdbool.h>
#include "follow_beat.h"

#ifdef __cplusplus
extern "C" {
#endif

// Spawn the capture task, which reads from the shared bus's RX channel and
// feeds follow_beat_push_sample(), republishing the latest FollowBeatOut. Call
// once at boot (only when follow_beat_enable is set), after audio_bus_init().
// No-op (logs + returns) if the bus isn't ready.
void follow_beat_io_start(void);

bool follow_beat_io_ready(void);

// Latest published estimate. Single-writer (capture task) / single-reader
// (status poll) via a short critical section -- FollowBeatOut is a small POD
// struct, so this is cheap and avoids a torn read.
FollowBeatOut follow_beat_io_status(void);

#ifdef __cplusplus
}
#endif
