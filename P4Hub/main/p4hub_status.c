#include "p4hub_status.h"
#include <stdio.h>

int p4hub_status_json(char* buf, size_t len, float bpm, int peers, bool usb, uint32_t tx) {
    return snprintf(buf, len,
                    "{\"bpm\":%.1f,\"peers\":%d,\"usb\":%s,\"tx\":%lu}",
                    bpm, peers, usb ? "true" : "false", (unsigned long)tx);
}
