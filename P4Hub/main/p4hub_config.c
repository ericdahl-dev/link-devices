#include "p4hub_config.h"
#include <string.h>
#include <stdlib.h>

void p4hub_config_defaults(P4HubConfig* c) {
    memset(c, 0, sizeof(*c));
    c->clock_out_enable = 1;
    c->midi_cable       = 0;
    c->metronome_enable = 0;   // audible: default off so a fresh board is silent
    c->metronome_accent = 1;   // when enabled, accent the bar-1 downbeat
}

bool p4hub_config_valid(const P4HubConfig* c) {
    if (c->midi_cable < 0 || c->midi_cable > 3) return false;
    if (c->clock_out_enable != 0 && c->clock_out_enable != 1) return false;
    if (c->metronome_enable != 0 && c->metronome_enable != 1) return false;
    if (c->metronome_accent != 0 && c->metronome_accent != 1) return false;
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
    if (strcmp(key, "midi_cable") == 0) {
        int v = atoi(value);
        if (v < 0 || v > 3) return false;
        c->midi_cable = v;
        return true;
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
    return false;
}
