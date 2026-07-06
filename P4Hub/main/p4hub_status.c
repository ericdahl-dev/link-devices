#include "p4hub_status.h"
#include <stdio.h>

int p4hub_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx) {
    return snprintf(buf, len,
                    "{\"bpm\":%.1f,\"min\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu}",
                    bpm, midi_bpm, peers, usb ? "true" : "false", (unsigned long)tx);
}
