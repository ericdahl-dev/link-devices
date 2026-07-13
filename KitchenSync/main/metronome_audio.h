#pragma once

/* ESP-026: the onboard speaker (P4-006) exists only on the P4-NANO. On the S3 / classic ESP32 the
 * hardware is not fitted, so the header hands back no-op stubs and the APP CORE
 * compiles unchanged -- no #ifdefs sprayed through ks_main.c / ks_web.cpp. Same shape
 * as HAS_USB_MIDI / HAS_TOUCH_DISPLAY / LED_NONE elsewhere in this repo (ADR-0003:
 * the glue is thin and per-target; the logic is pure and shared). */
#include "sdkconfig.h"
#if CONFIG_IDF_TARGET_ESP32P4
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

// P4-029: re-apply volume (0..100) + voice preset at runtime, no reboot (the web
// /live path). No-op until _ready() — enabling a metronome that was off at boot
// still needs a reboot to bring up the codec.
void metronome_audio_set(int volume, int voice);

// Enqueue one click for the player task. Non-blocking (drops if the queue is
// full, which shouldn't happen at musical tempos); a no-op until _ready().
// accent = the louder/higher bar-downbeat tone.
void metronome_audio_click(bool accent);

#ifdef __cplusplus
}
#endif

#else  /* hardware not fitted on this board */
#include <stdbool.h>
static inline void metronome_audio_start(int vol, int voice) { (void)vol; (void)voice; }
static inline bool metronome_audio_ready(void)               { return false; }
static inline void metronome_audio_set(int vol, int voice)   { (void)vol; (void)voice; }
static inline void metronome_audio_click(bool accent)        { (void)accent; }

#endif
