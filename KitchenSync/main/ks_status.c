#include "ks_status.h"
#include <stdio.h>

int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw) {
    return snprintf(buf, len,
                    "{\"bpm\":%.1f,\"min\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu,\"fw\":\"%s\"}",
                    bpm, midi_bpm, peers, usb ? "true" : "false", (unsigned long)tx, fw);
}
