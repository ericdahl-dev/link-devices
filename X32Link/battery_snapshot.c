#include "battery_snapshot.h"

#if defined(ARDUINO) || defined(ESP_PLATFORM)
  #include "freertos/FreeRTOS.h"
  static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
  #define BS_ENTER() portENTER_CRITICAL(&s_mux)
  #define BS_EXIT()  portEXIT_CRITICAL(&s_mux)
#else
  #define BS_ENTER() ((void)0)
  #define BS_EXIT()  ((void)0)
#endif

static BatterySnapshot s_snap = { 0.0f, 0.0f, false };

void battery_snapshot_publish(float volts, float percent, bool present) {
    BS_ENTER();
    s_snap.volts   = volts;
    s_snap.percent = percent;
    s_snap.present = present;
    BS_EXIT();
}

void battery_snapshot_read(BatterySnapshot* out) {
    if (!out) return;
    BS_ENTER();
    *out = s_snap;
    BS_EXIT();
}
