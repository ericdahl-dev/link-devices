#pragma once
// KitchenSync audio glue (P4-006): the ES8311 codec + I2S tone-burst side of the
// metronome. Pure scheduling (when to click / accent) lives in the host-tested
// X32Link/metronome.c; this file is the platform-specific sound. Thin per
// ADR-0003. See metronome_audio.c for the ES8311/I2S pin assumptions (logged at
// boot) — verified against the Waveshare ESP32-P4-NANO 12_I2SCodec example.
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif

// Bring up the ES8311 codec + I2S TX and spawn the click-player task. Call once
// at boot (only when the metronome is enabled). On any codec/I2S failure it logs
// and leaves the feature disabled — the rest of the firmware runs unaffected.
void metronome_audio_start(int volume, int voice);

// True once the codec + I2S came up successfully.
bool metronome_audio_ready(void);

// Enqueue one click for the player task. Non-blocking (drops if the queue is
// full, which shouldn't happen at musical tempos); a no-op until _ready().
// accent = the louder/higher bar-downbeat tone.
void metronome_audio_click(bool accent);

#ifdef __cplusplus
}
#endif
