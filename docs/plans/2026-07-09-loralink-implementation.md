# LoraLink Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Build `LoraLink/` — a shared Arduino sketch (role-selected at compile
time) where one ESP32-S3+SX1262 board joins the Ableton Link session over
WiFi and broadcasts the current BPM over LoRa, and the other receives it and
shows it on its OLED. No phase alignment, no ack/retry (see
`docs/plans/2026-07-09-loralink-design.md`).

**Architecture:** Pure-C wire-format/staleness logic (host-tested with Unity,
mirrors this repo's `wifi_down_blink.c` pattern) + thin Arduino glue for the
radio (RadioLib SX1262) and OLED (U8g2). The sender reuses `link_listener.*`
from `../X32Link` unchanged — no new Link-parsing code.

**Tech Stack:** Arduino-ESP32 core, RadioLib (SX1262), U8g2 (SSD1306 OLED),
Unity (host tests), arduino-cli.

---

## Before you start

This plan was written and validated against the repo at commit `2fe8977`
(design doc committed). You're working in the worktree
`/Users/edahl/Documents/GitHub/link-devices/.worktrees/loralink` on branch
`feature/loralink` — stay in this directory for all steps below. Baseline
`make -C test` passes clean (all suites, 0 failures) before this plan's
changes — if it doesn't in your session, stop and report rather than
proceeding.

Two things you can't verify from a laptop and must flag rather than guess
past:
- **Pin mapping** (`lora_config.h`) is written for the *stock* Heltec WiFi
  LoRa 32 V3 pinout, since these "N30" boards are sold as
  Meshtastic-compatible clones of that reference design. Confirm against the
  actual board silkscreen/schematic once you have it in hand — if it differs,
  this is the only file that needs to change.
- **RadioLib/U8g2 exact API surface** — this plan's code targets current
  RadioLib (`SX1262` class, `setDio1Action`) and U8g2 (`U8G2_SSD1306_128X64_NONAME_F_HW_I2C`)
  APIs as of when this plan was written. If `arduino-cli lib install` pulls a
  newer major version with breaking changes, adapt the glue code, not the
  pure modules.

---

### Task 1: `lora_bpm_packet` pure module (wire format)

**Files:**
- Create: `LoraLink/lora_bpm_packet.h`
- Create: `LoraLink/lora_bpm_packet.c`
- Test: `test/test_lora_bpm_packet.c`

**Step 1: Write the failing test**

```c
#include "unity.h"
#include "lora_bpm_packet.h"

void setUp(void)    {}
void tearDown(void) {}

void test_round_trip_typical_bpm(void) {
    lora_bpm_packet_t pkt = { LORA_MSG_BPM, 7, 12000 };  // 120.00 BPM, seq 7
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    lora_bpm_packet_encode(&pkt, buf);

    lora_bpm_packet_t out;
    TEST_ASSERT_TRUE(lora_bpm_packet_decode(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(LORA_MSG_BPM, out.msg_type);
    TEST_ASSERT_EQUAL_UINT8(7, out.seq);
    TEST_ASSERT_EQUAL_UINT16(12000, out.bpm_x100);
}

void test_round_trip_zero_bpm(void) {
    lora_bpm_packet_t pkt = { LORA_MSG_NO_SESSION, 0, 0 };
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    lora_bpm_packet_encode(&pkt, buf);

    lora_bpm_packet_t out;
    TEST_ASSERT_TRUE(lora_bpm_packet_decode(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(LORA_MSG_NO_SESSION, out.msg_type);
    TEST_ASSERT_EQUAL_UINT16(0, out.bpm_x100);
}

void test_round_trip_max_bpm_x100(void) {
    lora_bpm_packet_t pkt = { LORA_MSG_BPM, 255, 65535 };  // seq wraparound value + max u16
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    lora_bpm_packet_encode(&pkt, buf);

    lora_bpm_packet_t out;
    TEST_ASSERT_TRUE(lora_bpm_packet_decode(buf, sizeof(buf), &out));
    TEST_ASSERT_EQUAL_UINT8(255, out.seq);
    TEST_ASSERT_EQUAL_UINT16(65535, out.bpm_x100);
}

void test_decode_rejects_short_buffer(void) {
    uint8_t buf[LORA_BPM_PACKET_SIZE] = {1, 2, 3, 4};
    lora_bpm_packet_t out;
    TEST_ASSERT_FALSE(lora_bpm_packet_decode(buf, LORA_BPM_PACKET_SIZE - 1, &out));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_round_trip_typical_bpm);
    RUN_TEST(test_round_trip_zero_bpm);
    RUN_TEST(test_round_trip_max_bpm_x100);
    RUN_TEST(test_decode_rejects_short_buffer);
    return UNITY_END();
}
```

**Step 2: Run test to verify it fails**

Run: `cd test && gcc -Wall -Wextra -g -I../LoraLink -I../lib/unity/src test_lora_bpm_packet.c ../lib/unity/src/unity.c -o /tmp/t 2>&1 | head -20`
Expected: FAIL to compile — `lora_bpm_packet.h` doesn't exist yet.

**Step 3: Write minimal implementation**

`LoraLink/lora_bpm_packet.h`:
```c
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
```

`LoraLink/lora_bpm_packet.c`:
```c
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
```

**Step 4: Run test to verify it passes**

Run the same gcc command as step 2, then `/tmp/t`.
Expected: `4 Tests 0 Failures 0 Ignored / OK`

**Step 5: Commit**

```bash
git add LoraLink/lora_bpm_packet.h LoraLink/lora_bpm_packet.c test/test_lora_bpm_packet.c
git commit -m "feat(loralink): add lora_bpm_packet pure encode/decode module"
```

---

### Task 2: `lora_freshness` pure module (staleness check)

**Files:**
- Create: `LoraLink/lora_freshness.h`
- Create: `LoraLink/lora_freshness.c`
- Test: `test/test_lora_freshness.c`

**Step 1: Write the failing test**

```c
#include "unity.h"
#include "lora_freshness.h"

void setUp(void)    {}
void tearDown(void) {}

void test_stale_when_never_received(void) {
    TEST_ASSERT_TRUE(lora_freshness_is_stale(10000, 0, 5000, false));
}

void test_not_stale_before_threshold(void) {
    TEST_ASSERT_FALSE(lora_freshness_is_stale(4999, 0, 5000, true));
    TEST_ASSERT_FALSE(lora_freshness_is_stale(6000, 1500, 5000, true));  // elapsed 4500
}

void test_stale_after_threshold_elapsed(void) {
    // boundary: exactly at threshold counts as stale, not strictly greater
    // (mirrors wifi_down_blink_due()'s >= convention)
    TEST_ASSERT_TRUE(lora_freshness_is_stale(5000, 0, 5000, true));
    TEST_ASSERT_TRUE(lora_freshness_is_stale(7000, 1500, 5000, true));  // elapsed 5500
}

void test_stale_calc_survives_millis_rollover(void) {
    uint32_t last = 0xFFFFFFF0u;  // 16 ticks before rollover
    TEST_ASSERT_FALSE(lora_freshness_is_stale(100u, last, 5000, true));   // elapsed 116
    TEST_ASSERT_TRUE(lora_freshness_is_stale(5000u, last, 5000, true));   // elapsed 5016
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stale_when_never_received);
    RUN_TEST(test_not_stale_before_threshold);
    RUN_TEST(test_stale_after_threshold_elapsed);
    RUN_TEST(test_stale_calc_survives_millis_rollover);
    return UNITY_END();
}
```

**Step 2: Run test to verify it fails**

Run: `cd test && gcc -Wall -Wextra -g -I../LoraLink -I../lib/unity/src test_lora_freshness.c ../lib/unity/src/unity.c -o /tmp/t2 2>&1 | head -20`
Expected: FAIL to compile — `lora_freshness.h` doesn't exist yet.

**Step 3: Write minimal implementation**

`LoraLink/lora_freshness.h`:
```c
#pragma once
// Pure staleness check for the LoraLink receiver's last-seen BPM packet.
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// True if the last received packet is older than `threshold_ms`, or if
// `has_received` is false (no packet seen yet since boot). `now_ms` /
// `last_seen_ms` are millis()-style timestamps; unsigned subtraction makes
// this wraparound-safe across the ~49.7-day millis() rollover, same trick
// wifi_down_blink_due() relies on (see X32Link/wifi_down_blink.c).
bool lora_freshness_is_stale(uint32_t now_ms, uint32_t last_seen_ms,
                              uint32_t threshold_ms, bool has_received);

#ifdef __cplusplus
}
#endif
```

`LoraLink/lora_freshness.c`:
```c
#include "lora_freshness.h"

bool lora_freshness_is_stale(uint32_t now_ms, uint32_t last_seen_ms,
                              uint32_t threshold_ms, bool has_received) {
    if (!has_received) return true;
    return (uint32_t)(now_ms - last_seen_ms) >= threshold_ms;
}
```

**Step 4: Run test to verify it passes**

Run the same gcc command as step 2, then `/tmp/t2`.
Expected: `4 Tests 0 Failures 0 Ignored / OK`

**Step 5: Commit**

```bash
git add LoraLink/lora_freshness.h LoraLink/lora_freshness.c test/test_lora_freshness.c
git commit -m "feat(loralink): add lora_freshness pure staleness check"
```

---

### Task 3: Wire both pure modules into `test/Makefile`

**Files:**
- Modify: `test/Makefile`

**Step 1: Add variables** (near the other `../X32Link/*.c` variables, e.g. after `METROSTRIP`):

```makefile
LORAPKT    = ../LoraLink/lora_bpm_packet.c
LORAFRESH  = ../LoraLink/lora_freshness.c
```

**Step 2: Add to the `all` target's prerequisite list and run list**

Add `test_lora_bpm_packet test_lora_freshness` to the end of the `all:` line's
prerequisite list, and `./test_lora_bpm_packet` / `./test_lora_freshness` to
the end of its recipe (both go right before `./check_docs.sh`).

**Step 3: Add build rules** (near the other `test_*` rules):

```makefile
test_lora_bpm_packet: test_lora_bpm_packet.c $(LORAPKT) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@

test_lora_freshness: test_lora_freshness.c $(LORAFRESH) $(UNITY)
	$(CC) $(CFLAGS) $^ -o $@
```

**Step 4: Run the full suite**

Run: `make -C test`
Expected: every existing suite still passes, plus:
```
./test_lora_bpm_packet
... 4 Tests 0 Failures 0 Ignored OK
./test_lora_freshness
... 4 Tests 0 Failures 0 Ignored OK
doc check: every source file named in AGENTS.md exists
```

**Step 5: Commit**

```bash
git add test/Makefile
git commit -m "test(loralink): wire lora_bpm_packet + lora_freshness into host suite"
```

---

### Task 4: `lora_config.h` — pins, role, timing constants

**Files:**
- Create: `LoraLink/lora_config.h`

**Step 1: Write the file**

```c
#pragma once
// Board pins + role/timing constants for LoraLink. See
// docs/plans/2026-07-09-loralink-design.md.
//
// Pin mapping below is the STOCK Heltec WiFi LoRa 32 V3 pinout — the "N30"
// board is sold as a Meshtastic-compatible clone of that reference design.
// Verify against the actual board silkscreen/schematic; this is the only
// file that should need to change if the clone's pinout differs.

// --- OLED (SSD1306, I2C) ---
#define OLED_SDA       17
#define OLED_SCL       18
#define OLED_RST       21
#define VEXT_CTRL_PIN  36   // LOW = Vext (OLED/peripheral) power ON

// --- LoRa (SX1262, SPI) ---
#define LORA_NSS   8
#define LORA_SCK   9
#define LORA_MOSI  10
#define LORA_MISO  11
#define LORA_RST   12
#define LORA_BUSY  13
#define LORA_DIO1  14

// --- Radio parameters (point-to-point, not LoRaWAN) ---
#define LORA_FREQ_MHZ          915.0
#define LORA_BANDWIDTH_KHZ     125.0
#define LORA_SPREADING_FACTOR  9
#define LORA_CODING_RATE       5
#define LORA_SYNC_WORD         0x12   // private-network sync word (not the
                                       // 0x34 LoRaWAN public sync word)
#define LORA_TX_POWER_DBM      17

// --- Role selection (compile-time; each board is flashed once) ---
#define LORA_ROLE_SENDER    0
#define LORA_ROLE_RECEIVER  1

#ifdef LORA_ROLE_OVERRIDE
  #define LORA_ROLE LORA_ROLE_OVERRIDE
#else
  #define LORA_ROLE LORA_ROLE_SENDER   // flip via build_opt.h for the 2nd board
#endif

// --- Timing ---
#define LORA_LINK_POLL_MS             20    // sender: Link listener poll interval
#define LORA_HEARTBEAT_MS             1500  // sender: resend at least this often
#define LORA_BPM_CHANGE_EPSILON_X100  5     // sender: resend immediately if BPM
                                             // moves by >= 0.05 BPM
#define LORA_STALE_THRESHOLD_MS       5000  // receiver: "No signal" after this long
```

**Step 2: Commit**

```bash
git add LoraLink/lora_config.h
git commit -m "feat(loralink): add board pin map + role/timing config"
```

(No test step — this is pure constants, nothing to unit test.)

---

### Task 5: WiFi secrets template

**Files:**
- Create: `LoraLink/lora_secrets.h.example`
- Modify: `.gitignore`

**Step 1: Write the template**

`LoraLink/lora_secrets.h.example`:
```c
#pragma once
// Copy this file to lora_secrets.h (gitignored) and fill in your real
// credentials — needed by the sender role only, to join the WiFi network
// the Link session is running on.
#define WIFI_SSID     "your-ssid"
#define WIFI_PASSWORD "your-password"
```

**Step 2: Add to `.gitignore`**

Add this line under the existing `# ESP-IDF (KitchenSync)` block or its own
comment:

```
# LoraLink WiFi credentials (copy lora_secrets.h.example -> lora_secrets.h)
LoraLink/lora_secrets.h
```

**Step 3: Create your local copy** (not committed):

```bash
cp LoraLink/lora_secrets.h.example LoraLink/lora_secrets.h
# then edit LoraLink/lora_secrets.h with the real SSID/password
```

**Step 4: Commit the template + gitignore change**

```bash
git add LoraLink/lora_secrets.h.example .gitignore
git commit -m "feat(loralink): add WiFi secrets template"
```

---

### Task 6: `lora_radio` — RadioLib SX1262 glue

**Files:**
- Create: `LoraLink/lora_radio.h`
- Create: `LoraLink/lora_radio.cpp`

**Step 1: Write the files**

`LoraLink/lora_radio.h`:
```cpp
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
```

`LoraLink/lora_radio.cpp`:
```cpp
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
```

**Step 2: Commit** (compiles as part of Task 8's on-device check, not standalone)

```bash
git add LoraLink/lora_radio.h LoraLink/lora_radio.cpp
git commit -m "feat(loralink): add RadioLib SX1262 glue"
```

---

### Task 7: `lora_display` — OLED status glue

**Files:**
- Create: `LoraLink/lora_display.h`
- Create: `LoraLink/lora_display.cpp`

**Step 1: Write the files**

`LoraLink/lora_display.h`:
```cpp
#pragma once

void lora_display_begin();
void lora_display_show_sender(int peers, float bpm, bool link_active);
void lora_display_show_receiver(float bpm, bool stale);
```

`LoraLink/lora_display.cpp`:
```cpp
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
```

**Step 2: Commit**

```bash
git add LoraLink/lora_display.h LoraLink/lora_display.cpp
git commit -m "feat(loralink): add OLED status display glue"
```

---

### Task 8: `LoraLink.ino` — role dispatch, sender + receiver loops

**Files:**
- Create: `LoraLink/LoraLink.ino`

**Step 1: Write the file**

```cpp
// LoraLink — relays the Ableton Link session BPM over LoRa for out-of-WiFi-range
// FX tempo control. No phase alignment — see docs/plans/2026-07-09-loralink-design.md.
#include <WiFi.h>
#include <Arduino.h>
#include <stdlib.h>  // abs()

#include "lora_config.h"
#include "lora_secrets.h"   // WIFI_SSID / WIFI_PASSWORD — see lora_secrets.h.example
#include "lora_radio.h"
#include "lora_display.h"
#include "lora_bpm_packet.h"
#include "lora_freshness.h"

// Reused unchanged from ../X32Link (ADR-0003: pure-C-logic + thin-glue split).
#include "../X32Link/link_listener.h"

#if LORA_ROLE == LORA_ROLE_SENDER

static uint8_t s_seq = 0;

static void wifi_connect_blocking() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[LoraLink] connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(250);
        Serial.print('.');
    }
    Serial.printf("\n[LoraLink] WiFi up, ip=%s\n", WiFi.localIP().toString().c_str());
}

void setup() {
    Serial.begin(115200);
    lora_display_begin();
    lora_radio_begin();
    wifi_connect_blocking();
    link_listener_begin();
}

void loop() {
    link_listener_poll();
    link_listener_tick();

    int   peers  = link_listener_peers();
    bool  active = peers > 0;
    float bpm    = active ? (float)link_listener_bpm() : 0.0f;

    static uint32_t s_last_send_ms = 0;
    static uint16_t s_last_sent_bpm_x100 = 0xFFFF;  // force an initial send
    uint32_t now = millis();
    uint16_t bpm_x100 = (uint16_t)(bpm * 100.0f + 0.5f);

    bool changed = active &&
        (abs((int)bpm_x100 - (int)s_last_sent_bpm_x100) >= LORA_BPM_CHANGE_EPSILON_X100);
    bool heartbeat_due = (uint32_t)(now - s_last_send_ms) >= LORA_HEARTBEAT_MS;

    if (changed || heartbeat_due) {
        lora_bpm_packet_t pkt;
        pkt.msg_type = active ? LORA_MSG_BPM : LORA_MSG_NO_SESSION;
        pkt.seq      = s_seq++;
        pkt.bpm_x100 = active ? bpm_x100 : 0;

        uint8_t buf[LORA_BPM_PACKET_SIZE];
        lora_bpm_packet_encode(&pkt, buf);
        lora_radio_send(buf, sizeof(buf));

        s_last_send_ms = now;
        s_last_sent_bpm_x100 = bpm_x100;
    }

    lora_display_show_sender(peers, bpm, active);
    delay(LORA_LINK_POLL_MS);
}

#else  // LORA_ROLE_RECEIVER

static uint32_t s_last_seen_ms = 0;
static bool     s_has_received = false;
static float    s_last_bpm     = 0.0f;

void setup() {
    Serial.begin(115200);
    lora_display_begin();
    lora_radio_begin();
}

void loop() {
    uint8_t buf[LORA_BPM_PACKET_SIZE];
    int len = 0;
    if (lora_radio_try_receive(buf, sizeof(buf), &len)) {
        lora_bpm_packet_t pkt;
        if (lora_bpm_packet_decode(buf, len, &pkt) && pkt.msg_type == LORA_MSG_BPM) {
            s_last_bpm     = pkt.bpm_x100 / 100.0f;
            s_last_seen_ms = millis();
            s_has_received = true;
        }
    }

    bool stale = lora_freshness_is_stale(millis(), s_last_seen_ms,
                                          LORA_STALE_THRESHOLD_MS, s_has_received);
    lora_display_show_receiver(s_last_bpm, stale);
    delay(50);
}

#endif
```

**Step 2: Compile-check both roles on the dev box** (requires arduino-cli + ESP32 core + libs installed locally — see Task 9 for the exact install commands if not already present):

```bash
cp LoraLink/lora_secrets.h.example LoraLink/lora_secrets.h   # if not done in Task 5
arduino-cli lib install RadioLib U8g2
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi LoraLink
```
Expected: compiles clean (sender role, the default).

```bash
echo '-DLORA_ROLE_OVERRIDE=LORA_ROLE_RECEIVER' > LoraLink/build_opt.h
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi LoraLink
git checkout -- LoraLink/build_opt.h
```
Expected: compiles clean (receiver role).

If either fails on a RadioLib/U8g2 API mismatch, fix the glue file (Task 6/7),
not this file.

**Step 3: Commit**

```bash
git add LoraLink/LoraLink.ino
git commit -m "feat(loralink): add role-dispatched sender/receiver sketch"
```

---

### Task 9: CI — compile LoraLink (both roles)

**Files:**
- Modify: `.github/workflows/ci.yml`

**Step 1: Add a new job**, modeled on the existing `firmware-compile` (X32Link) job:

```yaml
  loralink-compile:
    name: Firmware compile (LoraLink)
    runs-on: ubuntu-latest
    env:
      FQBN: esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi
    steps:
      - uses: actions/checkout@v4

      - name: Cache arduino-cli data
        uses: actions/cache@v4
        with:
          path: |
            ~/.arduino15
            ~/Arduino
          key: arduino-esp32-lora-${{ runner.os }}-v1

      - name: Install arduino-cli
        run: |
          curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh -s
          echo "$GITHUB_WORKSPACE/bin" >> "$GITHUB_PATH"

      - name: Install ESP32 core + LoRa/OLED libs
        run: |
          arduino-cli config init --overwrite --additional-urls https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
          arduino-cli core update-index
          arduino-cli core install esp32:esp32
          arduino-cli lib install RadioLib U8g2

      - name: Provide placeholder WiFi secrets for CI compile
        run: cp LoraLink/lora_secrets.h.example LoraLink/lora_secrets.h

      - name: Compile LoraLink (sender role)
        run: arduino-cli compile --fqbn "$FQBN" LoraLink

      - name: Compile LoraLink (receiver role)
        run: |
          echo '-DLORA_ROLE_OVERRIDE=LORA_ROLE_RECEIVER' > LoraLink/build_opt.h
          arduino-cli compile --fqbn "$FQBN" LoraLink
          git checkout -- LoraLink/build_opt.h
```

Add this job as a new top-level entry alongside `firmware-compile` and
`kitchensync-compile` (order doesn't matter, GitHub Actions runs jobs in
parallel by default).

**Step 2: Verify locally as best you can**

CI itself is the real verification (no GitHub Actions runner locally), but
re-run Task 8 Step 2 once more to make sure both compiles still pass before
pushing.

**Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "ci: compile LoraLink (sender + receiver roles)"
```

---

### Task 10: Docs — `LoraLink/README.md` + root README update

**Files:**
- Create: `LoraLink/README.md`
- Modify: `README.md`

**Step 1: Write `LoraLink/README.md`**, matching the style of `X32Link` /
`KitchenSync`'s READMEs (purpose, hardware, build/flash, config):

```markdown
# LoraLink — LoRa BPM relay

Relays the current Ableton Link session tempo over LoRa, for FX tempo control
out of range of the venue WiFi network. No phase/beat alignment — BPM value
only. See [`docs/plans/2026-07-09-loralink-design.md`](../docs/plans/2026-07-09-loralink-design.md)
for the full design.

## Hardware

- 2x ESP32-S3 + SX1262 915MHz LoRa dev board w/ onboard SSD1306 OLED
  ("N30" Meshtastic-compatible clone of the Heltec WiFi LoRa 32 V3 form factor).
- One board is flashed as **sender** (joins WiFi + the Link session, transmits
  BPM), the other as **receiver** (listens, displays BPM).

## Build / flash

```sh
cp LoraLink/lora_secrets.h.example LoraLink/lora_secrets.h   # fill in your WiFi creds
arduino-cli lib install RadioLib U8g2
arduino-cli compile --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi LoraLink
arduino-cli upload  --fqbn esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashSize=16M,PSRAM=opi -p <port> LoraLink
```

To flash the **receiver** board, override the role before compiling:

```sh
echo '-DLORA_ROLE_OVERRIDE=LORA_ROLE_RECEIVER' > LoraLink/build_opt.h
arduino-cli compile ... LoraLink   # then upload as above
git checkout -- LoraLink/build_opt.h   # restore sender default for next time
```

## Architecture

Pure-C wire format (`lora_bpm_packet.*`) and staleness check
(`lora_freshness.*`) are host-tested with Unity (`make -C test`). The sender
reuses `link_listener.*` from `../X32Link` unchanged to join the Link session
— no new Link-parsing code. Radio (`lora_radio.*`, RadioLib) and display
(`lora_display.*`, U8g2) are thin Arduino glue, verified on-device.

## Status

Milestone 1: receiver only displays BPM. FX/MIDI/CV output from the received
BPM is a follow-on, not yet built.
```

**Step 2: Update root `README.md`**

In the `## Devices` section, add a bullet after the `X32_emulator` entry:

```markdown
- **LoraLink** (`LoraLink/`) — a pair of ESP32-S3+SX1262 LoRa boards that relay
  the Link session BPM out of WiFi range for loose (non-phase-accurate) FX
  tempo control. See [`LoraLink/README.md`](LoraLink/README.md).
```

**Step 3: Commit**

```bash
git add LoraLink/README.md README.md
git commit -m "docs(loralink): add device README + root README entry"
```

---

### Task 11: On-device verification (manual, not automatable)

**Not host-testable — requires the physical hardware.** Once both boards are
flashed (Task 8 sender + receiver builds):

1. Power the sender within range of the venue WiFi + an active Link session
   (e.g. a laptop running Ableton Live or another Link peer). Confirm its
   OLED shows `Link: N peers` and a plausible BPM.
2. Power the receiver nearby. Confirm its OLED shows the same BPM within ~2s.
3. Change tempo on the Link session (e.g. drag the BPM in Live). Confirm the
   receiver's displayed BPM updates within ~2s.
4. Stop the Link session (or disconnect the sender's WiFi). Confirm the
   sender shows "No Link session" and the receiver eventually shows "No
   signal" (within `LORA_STALE_THRESHOLD_MS`, default 5s).
5. Walk the receiver out of range, then back in. Confirm it recovers to a
   fresh BPM display once back in range.
6. Note the practical LoRa range achieved for the design doc's follow-on
   notes (useful if `LORA_SPREADING_FACTOR`/`LORA_TX_POWER_DBM` need tuning).

Report results back rather than silently marking this task done — this is
the step most likely to surface a wrong pin or a RadioLib/board mismatch.
