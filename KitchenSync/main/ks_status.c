#include "ks_status.h"
#include <stdio.h>

int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, bool follow_enabled, float follow_bpm, float follow_confidence,
                   bool follow_valid, const int launch[4], bool playing, bool link_owns) {
    return snprintf(buf, len,
                    "{\"bpm\":%.1f,\"min\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu,\"fw\":\"%s\","
                    "\"follow_enabled\":%s,\"follow_bpm\":%.1f,\"follow_confidence\":%.1f,\"follow_valid\":%s,"
                    "\"launch\":[%d,%d,%d,%d],\"playing\":%s,\"link_owns\":%s}",
                    bpm, midi_bpm, peers, usb ? "true" : "false", (unsigned long)tx, fw,
                    follow_enabled ? "true" : "false", follow_bpm, follow_confidence,
                    follow_valid ? "true" : "false",
                    launch[0], launch[1], launch[2], launch[3],
                    playing ? "true" : "false", link_owns ? "true" : "false");
}
