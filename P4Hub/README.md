# P4Hub — ESP32-P4 hub-tier firmware

The "pro" tier that grows I/O the S3 `X32Link` can't: **USB-MIDI host** (drive
gear like a Blokas Midihub directly, P4-003/005), **Link over WiFi via the
onboard ESP32-C6** (P4-004), and later an audible metronome (P4-006). The S3
`X32Link` stays the small, lean Arduino box; the P4 is where the ambitious I/O
lives.

## Why ESP-IDF (not Arduino like X32Link)

The P4 hub features require ESP-IDF components the Arduino core doesn't expose:
the `usb_host` stack (USB-MIDI host) and `esp_hosted` / `esp_wifi_remote` (WiFi
through the C6 co-processor). So P4Hub is a **native ESP-IDF app**. This does not
break [ADR-0003](../docs/adr/0003-firmware-pure-c-glue-split.md): the interesting
logic stays **pure, host-tested C reused unchanged** from `../X32Link` (compiled
in by relative path — single source, no forks), and only the glue is ESP-IDF.

## Build / flash

Requires ESP-IDF v5.5+ (`. $IDF_PATH/export.sh`).

```sh
cd P4Hub
idf.py set-target esp32p4
idf.py -p <port> flash monitor
```

Note: this P4-NANO is early silicon (chip rev v1.3); `sdkconfig.defaults` drops
the ESP-IDF minimum P4 revision to v1.0 so it boots.

## Status

Scaffold (P4-002): boots and drives the reused pure `clock_ticker` with a
simulated tempo to prove the shared-module build. Proven spikes to fold in next:
Link-over-WiFi listener (P4-004) and USB-MIDI host clock out (P4-005), joined so
a live Link tempo drives `clock_ticker` out to the USB-MIDI host.
