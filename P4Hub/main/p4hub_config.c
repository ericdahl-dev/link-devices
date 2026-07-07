#include "p4hub_config.h"
#include <string.h>
#include <stdlib.h>

void p4hub_config_defaults(P4HubConfig* c) {
    memset(c, 0, sizeof(*c));
    c->clock_out_enable = 1;
    c->metronome_enable = 0;   // audible: default off so a fresh board is silent
    c->metronome_accent = 1;   // when enabled, accent the bar-1 downbeat
    c->metronome_volume = 80;  // ES8311 codec volume
    c->metronome_voice  = 0;   // Tone (default)
    c->led_enable       = 0;   // visual metronome on the strip: default off
    c->led_brightness   = 60;  // % (P4-019)
    c->led_mode         = 0;   // chase
    c->led_fade         = 55;  // pulse dim across a beat
    c->led_beat_color   = 0x00B400;   // green
    c->led_accent_color = 0xDC6E00;   // amber
    // P4-010: output 0 is the default 24-PPQN MIDI clock on cable 0; rest off.
    for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) {
        c->clock[i].enable       = (i == 0) ? 1 : 0;
        c->clock[i].cable        = i;
        c->clock[i].ppqn         = 24;
        c->clock[i].phase_mbeats = 0;
        c->clock[i].swing_mbeats = 0;   // straight by default (P4-013)
    }
}

bool p4hub_config_valid(const P4HubConfig* c) {
    if (c->clock_out_enable != 0 && c->clock_out_enable != 1) return false;
    if (c->metronome_enable != 0 && c->metronome_enable != 1) return false;
    if (c->metronome_accent != 0 && c->metronome_accent != 1) return false;
    if (c->metronome_volume < 0 || c->metronome_volume > 100) return false;
    if (c->metronome_voice < 0 || c->metronome_voice > 2) return false;
    if (c->led_enable != 0 && c->led_enable != 1) return false;
    if (c->led_brightness < 0 || c->led_brightness > 100) return false;
    if (c->led_mode < 0 || c->led_mode > 2) return false;
    if (c->led_fade < 0 || c->led_fade > 100) return false;
    if (c->led_beat_color   < 0 || c->led_beat_color   > 0xFFFFFF) return false;
    if (c->led_accent_color < 0 || c->led_accent_color > 0xFFFFFF) return false;
    for (int i = 0; i < P4HUB_CLOCK_OUTPUTS; i++) {
        const ClockOutputCfg* o = &c->clock[i];
        if (o->enable != 0 && o->enable != 1) return false;
        if (o->cable < 0 || o->cable > 3) return false;
        if (o->ppqn < 1 || o->ppqn > 48) return false;
        if (o->phase_mbeats < -250 || o->phase_mbeats > 250) return false;
        if (o->swing_mbeats < 0 || o->swing_mbeats > 250) return false;
    }
    return true;
}

// Copy a bounded string; returns false if it wouldn't fit (config unchanged).
static bool copy_field(char* dst, size_t cap, const char* src) {
    if (strlen(src) >= cap) return false;
    strcpy(dst, src);
    return true;
}

bool p4hub_config_set(P4HubConfig* c, const char* key, const char* value) {
    if (strcmp(key, "wifi_ssid") == 0) {
        return copy_field(c->wifi_ssid, sizeof(c->wifi_ssid), value);
    }
    if (strcmp(key, "wifi_pass") == 0) {
        if (value[0] == '\0') return true;   // blank = keep current
        return copy_field(c->wifi_pass, sizeof(c->wifi_pass), value);
    }
    if (strcmp(key, "clock_out") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->clock_out_enable = v;
        return true;
    }
    if (strcmp(key, "metronome") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->metronome_enable = v;
        return true;
    }
    if (strcmp(key, "metro_accent") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->metronome_accent = v;
        return true;
    }
    if (strcmp(key, "metro_vol") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 100) return false;
        c->metronome_volume = v;
        return true;
    }
    if (strcmp(key, "metro_voice") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 2) return false;
        c->metronome_voice = v;
        return true;
    }
    if (strcmp(key, "led") == 0) {
        int v = atoi(value);
        if (v != 0 && v != 1) return false;
        c->led_enable = v;
        return true;
    }
    if (strcmp(key, "led_bright") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 100) return false;
        c->led_brightness = v;
        return true;
    }
    if (strcmp(key, "led_mode") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 2) return false;
        c->led_mode = v;
        return true;
    }
    if (strcmp(key, "led_fade") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 100) return false;
        c->led_fade = v;
        return true;
    }
    // Colours arrive from an HTML <input type="color"> as "#rrggbb" (the '#' is
    // url-decoded before we see it); accept with or without the leading '#'.
    if (strcmp(key, "led_beat") == 0 || strcmp(key, "led_accent") == 0) {
        const char* hex = (value[0] == '#') ? value + 1 : value;
        char* end = NULL;
        long v = strtol(hex, &end, 16);
        if (end == hex || *end != '\0' || v < 0 || v > 0xFFFFFF) return false;
        if (key[4] == 'b') c->led_beat_color   = (int)v;   // "led_beat"
        else               c->led_accent_color = (int)v;   // "led_accent"
        return true;
    }
    // P4-010 per-output fields: "clk<N>_en|cable|ppqn|phase" (N = 0..3).
    if (strncmp(key, "clk", 3) == 0 && key[3] >= '0' && key[3] <= '9' && key[4] == '_') {
        int idx = key[3] - '0';
        if (idx < 0 || idx >= P4HUB_CLOCK_OUTPUTS) return false;
        const char* f = key + 5;
        int v = atoi(value);
        ClockOutputCfg* o = &c->clock[idx];
        if (strcmp(f, "en") == 0)    { if (v != 0 && v != 1) return false; o->enable = v; return true; }
        if (strcmp(f, "cable") == 0) { if (v < 0 || v > 3)   return false; o->cable = v; return true; }
        if (strcmp(f, "ppqn") == 0)  { if (v < 1 || v > 48)  return false; o->ppqn = v; return true; }
        if (strcmp(f, "phase") == 0) { if (v < -250 || v > 250) return false; o->phase_mbeats = v; return true; }
        if (strcmp(f, "swing") == 0) { if (v < 0 || v > 250)    return false; o->swing_mbeats = v; return true; }
        return false;
    }
    return false;
}
