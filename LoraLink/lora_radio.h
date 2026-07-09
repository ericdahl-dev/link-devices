#pragma once
#include <stdint.h>
#include <stdbool.h>

// Configures and starts the SX1262 in receive mode (both roles listen by
// default; the sender simply also transmits between receives).
void lora_radio_begin();

// Blocking transmit of `len` bytes from `buf`. Returns true on success.
// Re-arms receive mode afterward.
bool lora_radio_send(const uint8_t *buf, int len);

// Non-blocking poll: true if a packet arrived, with its length in *out_len
// (payload copied into `buf`, up to `buf_len` bytes). False if nothing new.
bool lora_radio_try_receive(uint8_t *buf, int buf_len, int *out_len);
