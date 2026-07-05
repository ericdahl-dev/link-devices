#pragma once
// Pure USB-MIDI 1.0 event-packet encoding (P4-005). A USB-MIDI endpoint carries
// 4-byte event packets, not raw MIDI bytes: byte0 = (cable << 4) | CIN, then up
// to 3 MIDI data bytes. This is shared by every USB-MIDI-host message the P4Hub
// sends (clock 0xF8, start 0xFA, stop 0xFC, continue 0xFB) and is host-tested in
// test/test_usb_midi_pack.c. No Arduino/ESP-IDF dependency.
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Pack a single-byte System Real-Time / Common message `status` (e.g. 0xF8) onto
// virtual `cable` (0-15) into `out[4]`. CIN 0xF ("single byte") is the correct
// code index for one-byte system messages. Unused data bytes are zeroed.
void usb_midi_pack_single(uint8_t cable, uint8_t status, uint8_t out[4]);

#ifdef __cplusplus
}
#endif
