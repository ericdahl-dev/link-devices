// DIN MIDI OUT on the S3 (KitchenSync Touch) -- see din_midi_out.h (ESP-016).
#include "din_midi_out.h"
#include <Arduino.h>

// UART0 is the console (GPIO43/44); UART1 is free. TX routes to any GPIO through
// the matrix, RX unused (-1) -- this is clock/transport OUT only.
static HardwareSerial s_midi(1);
static bool s_ready = false;

void din_midi_out_begin(int tx_gpio) {
    // 31250 divides cleanly off the UART clock, so the byte rate is exact and the
    // analyzer decodes it at 31250 with no baud error (same as the P4, ESP-015).
    s_midi.begin(31250, SERIAL_8N1, -1 /*rx*/, tx_gpio /*tx*/);
    s_ready = true;
}

void din_midi_out_byte(uint8_t status) {
    // write() copies into the driver's TX ring and returns; the peripheral shifts
    // it out under interrupt, so this never blocks the 1 ms clock task.
    if (s_ready) s_midi.write(status);
}
