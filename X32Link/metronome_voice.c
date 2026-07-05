#include "metronome_voice.h"

void metronome_voice_params(int voice, float* click_hz, int* click_ms,
                            float* accent_hz, int* accent_ms) {
    switch (voice) {
    case METRO_VOICE_CLICK:   // tight, high, short
        *click_hz = 2000.0f; *click_ms = 22; *accent_hz = 3000.0f; *accent_ms = 26;
        break;
    case METRO_VOICE_WOOD:    // woody, lower
        *click_hz = 1200.0f; *click_ms = 32; *accent_hz = 1500.0f; *accent_ms = 36;
        break;
    case METRO_VOICE_TONE:    // soft sine (default)
    default:
        *click_hz = 1000.0f; *click_ms = 45; *accent_hz = 1760.0f; *accent_ms = 55;
        break;
    }
}
