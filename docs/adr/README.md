# Architecture Decision Records

Numbered, append-only decision log for the KitchenSync platform. New decisions
**continue the numbering** — never restart it — and supersede/amend earlier ones
by reference rather than editing history. This is the platform-wide record; the
`KitchenSync-iOS_App` repo defers to it and keeps only app-local decisions of its
own.

| ADR | Title | Status |
|---|---|---|
| [0003](0003-firmware-pure-c-glue-split.md) | ESP32 firmware splits pure-C logic from thin Arduino glue | accepted (amended by 0007) |
| [0004](0004-touch-realtime-web-config.md) | Touch carries real-time controls; static config lives in the web UI | accepted |
| [0005](0005-per-target-firmware-framework.md) | Per-target firmware framework: Arduino for the S3, ESP-IDF for the P4 | amended by 0009 |
| [0006](0006-shared-pure-c-lives-in-arduino-sketch-root.md) | Shared pure C lives in the X32Link sketch root | superseded by 0007 |
| [0007](0007-shared-pure-c-compiled-by-path.md) | Shared pure C is compiled by path, not symlinked | superseded by 0009 |
| [0008](0008-p4-web-ui-is-the-brand-reference.md) | The P4 web page is the brand reference; per-firmware pages mirror it | accepted |
| [0009](0009-converge-the-clock-box-on-esp-idf.md) | Converge the clock box on ESP-IDF; the shared engine moves out of X32Link | accepted |
| [0010](0010-kitchensync-platform-identity.md) | KitchenSync is a synchronization platform; Ableton Link is one input protocol | accepted |
| [0011](0011-control-plane-boundary.md) | Firmware owns musical time; applications are control planes | accepted |
| [0012](0012-configuration-lifecycle.md) | Configuration lifecycle: live-safe vs reboot-required | accepted |
| [0013](0013-clock-source-arbitration.md) | Clock source arbitration | proposed |
| [0014](0014-repository-evolution.md) | Repository evolution toward a `ks-*` model | accepted (direction) |

Notes:
- **0001–0002** were never committed to this repo (the earliest recorded decision
  is 0003). Do not reuse those numbers.
- The broader architecture narrative lives in
  [`../architecture/system-overview.md`](../architecture/system-overview.md); ADRs
  record *decisions*, the overview records *state*.
