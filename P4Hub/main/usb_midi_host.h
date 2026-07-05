#pragma once
// P4Hub glue: USB-MIDI host on the P4 Type-A OTG port. Enumerates a USB-MIDI
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
void usb_midi_host_send(const uint8_t* data, int len);

// Count of 0xF8 timing-clock bytes seen on the device's IN endpoint — nonzero
// only if the device loops USB-in back to USB-out (a Midihub loopback preset),
// giving an end-to-end send+receive proof.
uint32_t usb_midi_host_rx_clocks(void);

// Count of clock packets sent out to the device.
uint32_t usb_midi_host_tx(void);

// Callback invoked for every 0xF8 timing-clock byte received on the device's IN
// endpoint, with the monotonic microsecond timestamp of reception. Runs in the
// USB client-event context, so it MUST be minimal + non-blocking (e.g. push a
// timestamp into midi_clock_in_pulse()). P4-011 uses this to derive tempo from an
// incoming MIDI clock. Pass NULL to detach. Set before/independent of enumeration.
typedef void (*usb_midi_clock_cb_t)(int64_t ts_us);
void usb_midi_host_set_clock_cb(usb_midi_clock_cb_t cb);

#ifdef __cplusplus
}
#endif
