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
