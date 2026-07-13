#pragma once

/* ESP-026: the ES8311 codec (P4-006) exists only on the P4-NANO. On the S3 / classic ESP32 the
 * hardware is not fitted, so the header hands back no-op stubs and the APP CORE
 * compiles unchanged -- no #ifdefs sprayed through ks_main.c / ks_web.cpp. Same shape
 * as HAS_USB_MIDI / HAS_TOUCH_DISPLAY / LED_NONE elsewhere in this repo (ADR-0003:
 * the glue is thin and per-target; the logic is pure and shared). */
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
// KitchenSync shared I2S bus + ES8311 codec owner (P4-020). The ES8311 is one
// physical codec on one I2S bus (MCLK=GPIO13 BCLK=GPIO12 WS=GPIO10, DOUT=GPIO9
// speaker out, DIN=GPIO11 mic in) -- metronome_audio.c (TX) and follow_beat_io.c
// (RX) can't each independently call i2s_new_channel() as I2S_ROLE_MASTER: two
// masters can't share the same physical BCLK/WS pins (confirmed via embedded-
// firmware consult, see docs/plans/2026-07-09-p4-020-follow-beat-design.md
// addendum). This module does ONE i2s_new_channel() call for both directions
// (true full duplex, one shared clock generator) plus the one ES8311 I2C
// bring-up; metronome_audio.c and follow_beat_io.c become consumers that only
// call i2s_channel_enable()/disable() on the handles this module owns.
//
// Clock-driver asymmetry: TX and RX share one BCLK/WS pair, but only one
// direction actually drives that shared clock -- the other rides along as a
// passenger. TX is the driver here. Hardware validation (2026-07-09) found
// that gating TX-enable on the metronome FEATURE being on left RX with no
// clock at all whenever the metronome was off -- i2s_channel_read() just
// blocked forever, silently breaking the mic-only use case entirely. Fix:
// audio_bus_init() unconditionally enables TX itself once the bus comes up,
// independent of metronome_enable -- an always-running, nothing-queued TX
// just emits silence (auto_clear). metronome_audio.c no longer enables TX at
// all; it only renders/queues click bursts onto an already-running channel.
// Don't reintroduce a metronome_enable-gated TX-enable -- that's the bug.
//
// Known limitation: audio_bus_init()'s failure paths don't roll back partial
// state (a mid-sequence I2S/I2C failure leaks the already-created handle(s)
// and leaves re-init impossible for the process lifetime, since e.g.
// i2c_driver_install() errors on a port that's already installed). Acceptable
// today because init() has no retry caller and runs once at boot; revisit if
// Task 9's hardware bring-up needs a retry/reinit path.
#include <stdbool.h>
#include <stdint.h>
#include "driver/i2s_std.h"
#include "es8311.h"

#ifdef __cplusplus
extern "C" {
#endif

// KitchenSync's rate (metronome bursts + follow_beat are built around it).
// The bus itself is rate-parametric since P4-032: LinkAudioPoC runs the same
// codec at 44.1k. Callers that need the live rate use audio_bus_sample_rate().
#define AUDIO_BUS_SAMPLE_RATE   16000
#define AUDIO_BUS_MCLK_MULTIPLE 384

// Bring up I2S_NUM_0 (full duplex) + the ES8311 codec (I2C addr 0x18). Call
// once at boot, before either metronome_audio_start() or follow_beat_io_start()
// -- both now require this to have already run. Idempotent: a second call is a
// no-op (logs and returns) if already ready.
void audio_bus_init(uint32_t sample_rate);

bool audio_bus_ready(void);

// The rate audio_bus_init() was called with (0 before init).
uint32_t audio_bus_sample_rate(void);

// Reclock the running bus to a new sample rate (P4-032: adapt to the Link
// session's rate discovered off a received buffer). Disables both channels,
// reconfigures I2S clocks + the ES8311, re-enables TX (the shared clock
// driver -- see the header comment); the RX consumer re-enables its own side.
// Returns false (bus left ready at the OLD rate's config, channels disabled
// except TX best-effort) on failure.
bool audio_bus_reclock(uint32_t sample_rate);

// TX (speaker out) / RX (mic in) channel handles. NULL if audio_bus_init()
// hasn't run or failed -- callers must check audio_bus_ready() first.
i2s_chan_handle_t audio_bus_tx(void);
i2s_chan_handle_t audio_bus_rx(void);

// The one ES8311 handle, for volume/mic-path config that must go through the
// codec directly (metronome volume, e.g.).
es8311_handle_t audio_bus_codec(void);

#ifdef __cplusplus
}
#endif

#else  /* hardware not fitted on this board */
#include <stdbool.h>
#include <stdint.h>
#define AUDIO_BUS_SAMPLE_RATE   16000
#define AUDIO_BUS_MCLK_MULTIPLE 384
static inline void     audio_bus_init(uint32_t sr)        { (void)sr; }
static inline bool     audio_bus_ready(void)              { return false; }
static inline uint32_t audio_bus_sample_rate(void)        { return 0; }
static inline bool     audio_bus_reclock(uint32_t sr)     { (void)sr; return false; }

#endif
