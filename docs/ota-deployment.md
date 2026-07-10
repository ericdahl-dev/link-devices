# Over-the-air (OTA) firmware deployment

Both shipped firmware targets push new images **over WiFi** once a unit is on the
network. USB is only needed for the **first** flash (or to recover a bricked
device). The device serves a `/update` page on the same HTTP server as the config
UI (port 80).

| Target | Task | Transport | POST body |
|--------|------|-----------|-----------|
| **X32Link** (ESP32-S3) | LNK-034 | `multipart/form-data`, field name `update` | App `.bin` only |
| **KitchenSync** (ESP32-P4) | P4-017 | Raw POST body | `kitchensync.bin` |

There is **no authentication** on `/update` today — anyone on the LAN can flash
the device. Treat the rack network accordingly.

## Prerequisites

### X32Link partition scheme

OTA needs **two app slots**. On the Adafruit QT Py ESP32-S3 the default FQBN
scheme (`tinyuf2_noota`) has no second slot — `/update` fails with
`Partition Could Not be Found`.

Compile and **first USB flash** with `PartitionScheme=min_spiffs`:

```bash
FQBN='esp32:esp32:adafruit_qtpy_esp32s3_n4r2:PartitionScheme=min_spiffs'
arduino-cli compile  --fqbn "$FQBN" --export-binaries X32Link
arduino-cli upload   --fqbn "$FQBN" -p /dev/ttyACM0 X32Link
```

Changing the partition scheme relocates NVS → WiFi creds and config reset to
defaults (AP fallback). **Back up** settings from the device's `/` page before
repartitioning; restore with one **Write & Reboot** (`POST /save`) afterward.

Other S3 boards (Super Mini, Waveshare touch) use the `esp32s3` FQBN with
`FlashSize=16M` in CI; confirm the chosen scheme has `app0` + `app1` before
relying on OTA.

### KitchenSync partition table

KitchenSync ships with `CONFIG_PARTITION_TABLE_TWO_OTA` (`ota_0` + `ota_1` +
`otadata`). No extra step — OTA works after the first `idf.py flash`.

### Device must be reachable on WiFi

- **STA mode:** use the IP shown on the config page footer or serial boot log.
- **AP fallback** (`X32Link-Setup` / KitchenSync setup AP): connect to the AP,
  browse `http://192.168.4.1/`.

## Build the OTA image

### X32Link

Set board flags in `X32Link/build_opt.h` for the target unit (empty at HEAD =
headless Super Mini profile). Then:

```bash
FQBN='esp32:esp32:adafruit_qtpy_esp32s3_n4r2:PartitionScheme=min_spiffs'
# Example board flags (edit for the unit in hand):
# echo '-DBOARD_QTPY_ESP32S3' > X32Link/build_opt.h

arduino-cli compile --fqbn "$FQBN" --export-binaries X32Link
BIN=X32Link/build/esp32.esp32.adafruit_qtpy_esp32s3_n4r2/X32Link.ino.bin
```

Use **`X32Link.ino.bin`** (app partition only). Do **not** upload
`X32Link.ino.merged.bin` — that image is for full USB flash, not OTA.

The build subfolder name tracks the FQBN; adjust `BIN` if you use a different
board profile.

### KitchenSync

```bash
cd KitchenSync
idf.py build
BIN=build/kitchensync.bin
```

## Push firmware (no USB)

### Browser

1. Open `http://<device-ip>/update`.
2. Choose the `.bin` file.
3. Upload — the device flashes the **inactive** OTA slot and reboots on success.
   A failed upload leaves the running firmware untouched.

Footer link on the main config page also reaches `/update`.

### CLI (scriptable — preferred for agents)

**X32Link** — multipart field must be named `update`:

```bash
DEVICE=192.168.1.42   # replace with the unit's IP
curl -f -F "update=@${BIN}" "http://${DEVICE}/update"
```

**KitchenSync** — raw body:

```bash
DEVICE=192.168.1.42
curl -f --data-binary @"${BIN}" "http://${DEVICE}/update"
```

`-f` makes curl exit non-zero on HTTP 4xx/5xx (bad image, no OTA partition, etc.).

After a successful push the device reboots (~5–10 s). The result page polls `/`
and redirects when the server is back.

## Verify the deployed version

`FW_VERSION` in `X32Link/fw_version.h` is the single source of truth (shared by
KitchenSync via include path). Bump it and `git tag v<FW_VERSION>` on release
commits so distributed `.bin` files trace back to source.

**Before flashing**, check what is running:

```bash
curl -s "http://${DEVICE}/status"
# JSON field "fw" — e.g. "2.2.0"
```

**After reboot**, confirm the new version:

```bash
curl -s "http://${DEVICE}/status" | python3 -c "import sys,json; print(json.load(sys.stdin)['fw'])"
```

The `/update` GET page and config footer also show `FW_VERSION` + `FW_BUILD`
(compile stamp).

## First flash vs OTA (agent checklist)

| Step | USB | OTA |
|------|-----|-----|
| Blank or wrong partition table | Yes | No — fix partitions over USB first |
| Normal version upgrade | No | Yes — build `.bin`, `curl` to `/update` |
| Recovery after bad OTA image | Yes — previous slot may still boot; else USB | — |
| Board-specific `build_opt.h` flags | Set before **compile** | Same — OTA image must match hardware |

## Emulator note

`X32_emulator/` uses **ArduinoOTA** (mDNS `x32-emulator`, Arduino IDE /
`espota.py` protocol) — not the `/update` web upload path documented here.

## Related tasks

- LNK-034 / LNK-038 — X32Link web OTA + version identity
- P4-017 — KitchenSync web OTA
- P4-014 (todo) — NVS config versioning on struct layout changes (manual Write
  & Reboot workaround until then)
