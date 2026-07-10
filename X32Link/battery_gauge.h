// Pure parse for the MAX17048 LiPo fuel gauge (Adafruit "LiPo BFF", STEMMA QT,
// I2C addr 0x36 — the board stacked under the QT Py ESP32-S3's battery
// connector). Host-testable: no I2C/Wire here — the caller reads the two
// 16-bit big-endian registers below and hands the raw bytes to
// battery_gauge_parse(). Register layout is the MAX17048/49 datasheet's
// ModelGauge output pair, not anything board-specific:
//   VCELL (reg 0x02): 16-bit, 78.125uV/LSB cell voltage.
//   SOC   (reg 0x04): 16-bit, MSB = whole percent, LSB = percent/256.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BATTERY_GAUGE_I2C_ADDR   0x36
#define BATTERY_GAUGE_VCELL_REG  0x02
#define BATTERY_GAUGE_SOC_REG    0x04

typedef struct {
    float volts;    // cell voltage, e.g. 3.85f
    float percent;  // 0..100 (MAX17048 can report slightly over 100)
} battery_reading_t;

// Decode one big-endian VCELL register pair into volts.
float battery_gauge_volts(uint8_t hi, uint8_t lo);

// Decode one big-endian SOC register pair into a 0..100 percent estimate.
float battery_gauge_percent(uint8_t hi, uint8_t lo);

// Convenience: decode both registers into *out. Returns false (leaves *out
// untouched) if out == NULL.
bool battery_gauge_parse(uint8_t vcell_hi, uint8_t vcell_lo, uint8_t soc_hi, uint8_t soc_lo,
                          battery_reading_t *out);

#ifdef __cplusplus
}
#endif
