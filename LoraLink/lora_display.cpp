#include "lora_display.h"
#include "lora_config.h"
#include <U8g2lib.h>
#include <Arduino.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, OLED_RST, OLED_SCL, OLED_SDA);

void lora_display_begin() {
    pinMode(VEXT_CTRL_PIN, OUTPUT);
    digitalWrite(VEXT_CTRL_PIN, LOW);  // Vext ON (active-low on this board family)
    delay(50);
    u8g2.begin();
    u8g2.setFont(u8g2_font_6x10_tf);
}

void lora_display_show_sender(int peers, float bpm, bool link_active) {
    char line[32];
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "LoraLink: SENDER");
    if (link_active) {
        snprintf(line, sizeof(line), "Link: %d peers", peers);
        u8g2.drawStr(0, 28, line);
        snprintf(line, sizeof(line), "%.2f BPM -> TX", bpm);
        u8g2.drawStr(0, 44, line);
    } else {
        u8g2.drawStr(0, 28, "No Link session");
    }
    u8g2.sendBuffer();
}

void lora_display_show_receiver(float bpm, bool stale) {
    char line[32];
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "LoraLink: RECEIVER");
    if (stale) {
        u8g2.drawStr(0, 28, "No signal");
    } else {
        snprintf(line, sizeof(line), "%.2f BPM", bpm);
        u8g2.drawStr(0, 28, line);
    }
    u8g2.sendBuffer();
}
