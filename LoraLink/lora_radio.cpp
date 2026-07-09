#include "lora_radio.h"
#include "lora_config.h"
#include <RadioLib.h>
#include <Arduino.h>

static SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
static volatile bool s_rx_flag = false;

// ISR set via radio.setDio1Action() below — mirrors RadioLib's
// SX126x_Receive_Interrupt example; DO NOT call radio.* methods from here.
static void onDio1Action() {
    s_rx_flag = true;
}

void lora_radio_begin() {
    int state = radio.begin(LORA_FREQ_MHZ, LORA_BANDWIDTH_KHZ,
                             LORA_SPREADING_FACTOR, LORA_CODING_RATE,
                             LORA_SYNC_WORD, LORA_TX_POWER_DBM);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.printf("[LoraLink] radio.begin failed: %d\n", state);
    }
    radio.setDio2AsRfSwitch(true);  // required on most SX1262 breakout boards
    radio.setDio1Action(onDio1Action);
    radio.startReceive();
}

bool lora_radio_send(const uint8_t *buf, int len) {
    int state = radio.transmit((uint8_t *)buf, len);
    radio.startReceive();  // re-arm RX after the blocking TX
    return state == RADIOLIB_ERR_NONE;
}

bool lora_radio_try_receive(uint8_t *buf, int buf_len, int *out_len) {
    if (!s_rx_flag) return false;
    s_rx_flag = false;

    size_t len = radio.getPacketLength();
    if (len == 0 || (int)len > buf_len) {
        radio.startReceive();
        return false;
    }
    int state = radio.readData(buf, len);
    radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) return false;

    *out_len = (int)len;
    return true;
}
