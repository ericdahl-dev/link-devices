#include "ks_status.h"
#include <stdio.h>

int ks_status_json(char* buf, size_t len, float bpm, float midi_bpm, int peers, bool usb, uint32_t tx,
                   const char* fw, bool follow_enabled, float follow_bpm, float follow_confidence,
                   bool follow_valid) {
    return snprintf(buf, len,
                    "{\"bpm\":%.1f,\"min\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu,\"fw\":\"%s\","
                    "\"follow_enabled\":%s,\"follow_bpm\":%.1f,\"follow_confidence\":%.1f,\"follow_valid\":%s}",
                    bpm, midi_bpm, peers, usb ? "true" : "false", (unsigned long)tx, fw,
                    follow_enabled ? "true" : "false", follow_bpm, follow_confidence,
                    follow_valid ? "true" : "false");
}
