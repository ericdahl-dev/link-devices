#pragma once
// Pure USB-MIDI event batcher (P4-034). Packs a tick's 4-byte USB-MIDI event packets
// into ONE bulk transfer instead of submitting one transfer per output.
//
// Why this exists: usb_midi_host_send() keeps a single in-flight bulk transfer and
// DROPS anything handed to it while busy. The clock fan-out called it once per output,
// back-to-back inside the same 1 ms tick, and a USB full-speed transfer occupies ~1 ms
// -- so with 4 outputs enabled roughly 3 of every 4 clock packets were discarded and
// only the first output ever reached USB gear. The DIN mirror hid it (the UART has a
// TX ring), so it took a logic analyzer to see: two outputs on one cable produced a
// 1.004x pulse ratio where 2.0x was expected.
//
// A USB-MIDI event packet is 4 bytes and a bulk transfer carries 64, so 16 events fit
// -- four outputs is nowhere near the limit. Batching also puts every output's clock in
// the SAME USB frame, so the outputs stay tightly aligned to each other.
//
// No Arduino/ESP-IDF dependency. Host-tested in test/test_usb_midi_batch.c.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USB_MIDI_BATCH_MAX 64   // one full-speed bulk transfer = 16 four-byte events

typedef struct {
    uint8_t  buf[USB_MIDI_BATCH_MAX];
    int      len;       // bytes staged so far
    uint32_t dropped;   // events refused because the transfer was full (running count)
} UsbMidiBatch;

// Stage one 4-byte USB-MIDI event packet. Returns false (and counts a drop) when the
// transfer is already full -- callers get an honest signal instead of silence.
bool usb_midi_batch_add(UsbMidiBatch* b, const uint8_t pkt[4]);

// Empty the staged bytes. `dropped` is a running total and is NOT cleared: it is a
// health counter, and the whole point of this module is that drops stop being invisible.
void usb_midi_batch_reset(UsbMidiBatch* b);

#ifdef __cplusplus
}
#endif
