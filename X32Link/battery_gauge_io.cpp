// See battery_gauge_io.h.
#include "battery_gauge_io.h"
#include <Wire.h>

// QT Py ESP32-S3's STEMMA QT connector — the genuine adafruit_qtpy_esp32s3
// board variant's default Wire pins (pins_arduino.h: SDA=7, SCL=6). Explicit
// here rather than a bare Wire.begin() so this stays correct even compiled
// against a generic esp32s3 profile (same reasoning as touch_display.cpp's
// TP_SDA/TP_SCL).
#define BATTERY_SDA_PIN 7
#define BATTERY_SCL_PIN 6

void battery_gauge_io_begin(void) {
    Wire.begin(BATTERY_SDA_PIN, BATTERY_SCL_PIN);
}

static bool read_reg16(uint8_t reg, uint8_t *hi, uint8_t *lo) {
    Wire.beginTransmission(BATTERY_GAUGE_I2C_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;  // repeated START, no STOP

    if (Wire.requestFrom((int)BATTERY_GAUGE_I2C_ADDR, 2) != 2) return false;
    *hi = Wire.read();
    *lo = Wire.read();
    return true;
}

bool battery_gauge_io_read(battery_reading_t *out) {
    uint8_t vcell_hi, vcell_lo, soc_hi, soc_lo;
    if (!read_reg16(BATTERY_GAUGE_VCELL_REG, &vcell_hi, &vcell_lo)) return false;
    if (!read_reg16(BATTERY_GAUGE_SOC_REG, &soc_hi, &soc_lo)) return false;
    return battery_gauge_parse(vcell_hi, vcell_lo, soc_hi, soc_lo, out);
}
