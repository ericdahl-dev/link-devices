// Pure USB-MIDI event batcher — see usb_midi_batch.h (P4-034).
#include "usb_midi_batch.h"
#include <string.h>

bool usb_midi_batch_add(UsbMidiBatch* b, const uint8_t pkt[4])
{
    if (b->len + 4 > USB_MIDI_BATCH_MAX) { b->dropped++; return false; }
    memcpy(b->buf + b->len, pkt, 4);
    b->len += 4;
    return true;
}

void usb_midi_batch_reset(UsbMidiBatch* b)
{
    b->len = 0;   // `dropped` deliberately survives: it is a running health counter
}
