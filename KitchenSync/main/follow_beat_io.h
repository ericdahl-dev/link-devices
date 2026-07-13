#pragma once

/* ESP-026: the ES8311 mic (P4-020) exists only on the P4-NANO. On the S3 / classic ESP32 the
 * hardware is not fitted, so the header hands back no-op stubs and the APP CORE
 * compiles unchanged -- no #ifdefs sprayed through ks_main.c / ks_web.cpp. Same shape
 * as HAS_USB_MIDI / HAS_TOUCH_DISPLAY / LED_NONE elsewhere in this repo (ADR-0003:
 * the glue is thin and per-target; the logic is pure and shared). */
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
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

#else  /* hardware not fitted on this board */
#include <stdbool.h>
#include "follow_beat.h"    /* FollowBeatOut is PURE (ADR-0003) -- it exists on every target */
static inline void          follow_beat_io_start(void)  {}
static inline bool          follow_beat_io_ready(void)  { return false; }
static inline FollowBeatOut follow_beat_io_status(void) { FollowBeatOut o = {0}; return o; }

#endif
