#pragma once
// ESP-015: hardware MIDI OUT — a DIN-style byte stream on a GPIO, mirroring the
// USB-MIDI clock/transport the P4 already emits. MIDI on a wire is just UART at
// 31250 baud, 8N1, and a System Real-Time message (0xF8 clock / 0xFA start /
// 0xFB continue / 0xFC stop) is a single status byte -- no USB-MIDI 4-byte
// packing. This is thin glue over the UART peripheral; the bytes come from the
// pure clock engine (usb_midi_pack owns the USB side, this owns the wire).
//
// It also drives a "downbeat strobe" GPIO: one pulse per bar (from
// ks_tick's plan.downbeat), so a logic analyzer can trigger on the bar line and
// measure how far the emitted 0xFA sits from it (ESP-011, never measured).
//
// Impure (ESP-IDF UART + GPIO); not host-tested. The decisions it acts on
// (plan.downbeat, the byte values) are pure and tested in ks_tick / usb_midi_pack.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up UART1 at 31250/8N1 with TX on `tx_gpio`, and configure `strobe_gpio`
// as a plain output for the analyzer trigger. Pins route through the GPIO matrix,
// so any free pin works. Pass strobe_gpio < 0 to skip the strobe.
void midi_uart_out_start(int tx_gpio, int strobe_gpio);

// Mirror one MIDI status byte onto the wire. Fire-and-forget: copies into the
// UART driver's TX ring and returns without waiting for the shift-out, so it
// never blocks the 1 ms clock task. A no-op until _start().
void midi_uart_out_byte(uint8_t status);

// Drive the downbeat strobe to `level`. The caller passes plan.downbeat every
// tick, so the line is high for the one ~1 ms tick that crossed the bar line and
// low otherwise -- a clean rising edge to trigger on. A no-op if no strobe pin.
void midi_uart_out_strobe(bool level);

#ifdef __cplusplus
}
#endif
