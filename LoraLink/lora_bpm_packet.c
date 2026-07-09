#include "lora_bpm_packet.h"

void lora_bpm_packet_encode(const lora_bpm_packet_t *pkt, uint8_t *buf) {
    buf[0] = pkt->msg_type;
    buf[1] = pkt->seq;
    buf[2] = (uint8_t)(pkt->bpm_x100 & 0xFF);
    buf[3] = (uint8_t)((pkt->bpm_x100 >> 8) & 0xFF);
}

bool lora_bpm_packet_decode(const uint8_t *buf, int len, lora_bpm_packet_t *out) {
    if (len < LORA_BPM_PACKET_SIZE) return false;
    out->msg_type = buf[0];
    out->seq      = buf[1];
    out->bpm_x100 = (uint16_t)(buf[2] | (buf[3] << 8));
    return true;
}
