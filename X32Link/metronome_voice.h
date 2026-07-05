#pragma once
// Pure metronome click-voice presets (P4-012): map a voice id to the tone
// parameters (pitch + length) for the plain click and the bar-1 accent. The
// audio glue renders bursts from these; keeping the mapping pure makes it
// host-testable and float-free at the config layer. No Arduino/ESP-IDF dep.
#ifdef __cplusplus
extern "C" {
#endif

enum {
    METRO_VOICE_TONE = 0,   // soft sine (default)
    METRO_VOICE_CLICK,      // tight, high, short
    METRO_VOICE_WOOD,       // woody, lower
    METRO_VOICE_COUNT,
};

// Fill the click/accent pitch (Hz) and length (ms) for `voice`. Out-of-range
// falls back to METRO_VOICE_TONE.
void metronome_voice_params(int voice, float* click_hz, int* click_ms,
                            float* accent_hz, int* accent_ms);

#ifdef __cplusplus
}
#endif
