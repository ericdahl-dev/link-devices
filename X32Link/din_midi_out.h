#pragma once
// ESP-016: hardware DIN MIDI OUT for KitchenSync Touch (ESP32-S3). A UART TX at
// 31250 8N1 on a GPIO -> 5-pin DIN jack, mirroring the USB-MIDI clock X32Link
// already emits -- so the S3 can drive DIN gear (Dan's RC-505) with no host.
//
// Thin Arduino glue over HardwareSerial; the byte TIMING is the pure, host-tested
// midi_clock_out engine (the same one that feeds the USB path). Same shape as the
// P4's midi_uart_out (ESP-015). Keep this header free of Arduino includes so the
// host test build can glob it without pulling in the core.
#include <stdint.h>

// MIDI TX pin on the Waveshare ESP32-S3-Touch-LCD-1.47: GPIO11 is free and broken
// out on the right header next to 3V3 + GND (see the s3-touch pinout / ESP-016).
// Any free header GPIO works -- UART routes to any pin via the matrix.
#if defined(BOARD_ESP32_DEVKIT)
// ESP-025 bench rig (classic ESP32 DevKit): GPIO17 = UART2 TX, broken out on the
// screw terminals. Do NOT use the terminals marked TX/RX (GPIO1/3) -- on this board
// those ARE the CP2102 console, so MIDI would spray into the serial log and the log
// would spray into the MIDI wire.
#define MIDI_TX_GPIO 17
#else
#define MIDI_TX_GPIO 11
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Bring up UART1 (UART0 is the console) at 31250 8N1, TX on tx_gpio, no RX.
void din_midi_out_begin(int tx_gpio);

// Write one raw MIDI status byte to the wire. On a DIN wire a System Real-Time
// message (0xF8 clock / 0xFA start / 0xFC stop) is a single byte -- no USB-MIDI
// packing. Fire-and-forget; a no-op until _begin().
void din_midi_out_byte(uint8_t status);

#ifdef __cplusplus
}
#endif
