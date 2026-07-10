// Thin I2C glue for the MAX17048 fuel gauge on the QT Py's STEMMA QT bus —
// see battery_gauge.h for the pure register decode. HAS_BATTERY_GAUGE-gated
// (opt-in per unit: not every QT Py has the LiPo BFF attached).
#pragma once
#include "battery_gauge.h"

#ifdef __cplusplus
extern "C" {
#endif

// Wire.begin() on the QT Py ESP32-S3's STEMMA QT pins (SDA=7, SCL=6).
void battery_gauge_io_begin(void);

// Reads VCELL + SOC over I2C and parses them into *out. Returns false (leaves
// *out untouched) on any I2C error, e.g. the LiPo BFF isn't attached.
bool battery_gauge_io_read(battery_reading_t *out);

#ifdef __cplusplus
}
#endif
