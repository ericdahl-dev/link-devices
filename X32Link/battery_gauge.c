// See battery_gauge.h.
#include "battery_gauge.h"

float battery_gauge_volts(uint8_t hi, uint8_t lo) {
    uint16_t raw = (uint16_t)((hi << 8) | lo);
    return (float)raw * 78.125e-6f;
}

float battery_gauge_percent(uint8_t hi, uint8_t lo) {
    return (float)hi + (float)lo / 256.0f;
}

bool battery_gauge_parse(uint8_t vcell_hi, uint8_t vcell_lo, uint8_t soc_hi, uint8_t soc_lo,
                          battery_reading_t *out) {
    if (!out) return false;
    out->volts   = battery_gauge_volts(vcell_hi, vcell_lo);
    out->percent = battery_gauge_percent(soc_hi, soc_lo);
    return true;
}
