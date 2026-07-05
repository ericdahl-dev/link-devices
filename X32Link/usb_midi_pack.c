#include "usb_midi_pack.h"

// USB-MIDI 1.0 event packet for a single-byte message: byte0 packs the virtual
// cable in the high nibble and the Code Index Number in the low nibble. CIN 0xF
// = "single byte", used for system real-time (clock/start/stop/continue) and
// single-byte system-common messages. The status byte goes in byte1; the two
// unused data bytes are zero.
void usb_midi_pack_single(uint8_t cable, uint8_t status, uint8_t out[4]) {
    out[0] = (uint8_t)((cable << 4) | 0x0F);
    out[1] = status;
    out[2] = 0x00;
    out[3] = 0x00;
}
