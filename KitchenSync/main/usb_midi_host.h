#pragma once
// KitchenSync glue: USB-MIDI host on the P4 Type-A OTG port. Enumerates a USB-MIDI
// device, claims its MIDIStreaming interface, and sends 4-byte USB-MIDI event
// packets out the bulk OUT endpoint. Proven in the scratchpad p4_midi_clock
// spike (P4-003). Packet encoding is the pure, host-tested usb_midi_pack.c.
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Install the USB host stack and start enumerating. Non-blocking.
void usb_midi_host_start(void);

// True once a USB-MIDI device is enumerated and its interface claimed.
bool usb_midi_host_ready(void);

// Submit a pre-encoded USB-MIDI event packet (typically 4 bytes) to the device.
// No-op if no device is ready. Non-blocking.
// Submit one bulk transfer (up to 64 bytes = 16 USB-MIDI events). Returns false when
// the endpoint is busy or not ready -- the caller keeps its batch and retries next tick
// (P4-034). Do NOT call this once per output: batch the tick's events (usb_midi_batch)
// and submit them together, or all but the first output get dropped.
bool usb_midi_host_send(const uint8_t* data, int len);

// Packets refused because a transfer was already in flight. Surfaced in /status so a
// silent drop can never hide again (P4-034).
uint32_t usb_midi_host_dropped(void);

// Count of 0xF8 timing-clock bytes seen on the device's IN endpoint — nonzero
// only if the device loops USB-in back to USB-out (a Midihub loopback preset),
// giving an end-to-end send+receive proof.
uint32_t usb_midi_host_rx_clocks(void);

// Count of clock packets sent out to the device.
uint32_t usb_midi_host_tx(void);

#ifdef __cplusplus
}
#endif
