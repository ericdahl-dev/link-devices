#pragma once
// Pure wire-format for the LoraLink BPM packet — encode/decode only, no
// radio/Arduino deps, host-testable. See
// docs/plans/2026-07-09-loralink-design.md.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LORA_BPM_PACKET_SIZE 4

typedef enum {
    LORA_MSG_BPM        = 1,  // bpm_x100 holds the current session tempo
    LORA_MSG_NO_SESSION = 2,  // no Link session found; bpm_x100 is 0/unused
} lora_msg_type_t;

typedef struct {
    uint8_t  msg_type;  // lora_msg_type_t
    uint8_t  seq;       // wraps 0-255; receiver-side gap display only, no retry
    uint16_t bpm_x100;  // BPM * 100, e.g. 12000 = 120.00 BPM
} lora_bpm_packet_t;

// Encodes `pkt` into `buf[0..LORA_BPM_PACKET_SIZE)`; buf must have at least
// LORA_BPM_PACKET_SIZE bytes. bpm_x100 is packed little-endian.
void lora_bpm_packet_encode(const lora_bpm_packet_t *pkt, uint8_t *buf);

// Decodes LORA_BPM_PACKET_SIZE bytes from `buf` into `out`. Returns false
// (leaving `out` untouched) if `len` is too short for a packet.
bool lora_bpm_packet_decode(const uint8_t *buf, int len, lora_bpm_packet_t *out);

#ifdef __cplusplus
}
#endif
