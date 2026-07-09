#pragma once
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
#include "driver/i2s_std.h"
#include "es8311.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_BUS_SAMPLE_RATE   16000
#define AUDIO_BUS_MCLK_MULTIPLE 384

// Bring up I2S_NUM_0 (full duplex) + the ES8311 codec (I2C addr 0x18). Call
// once at boot, before either metronome_audio_start() or follow_beat_io_start()
// -- both now require this to have already run. Idempotent: a second call is a
// no-op (logs and returns) if already ready.
void audio_bus_init(void);

bool audio_bus_ready(void);

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
