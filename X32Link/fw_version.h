// LNK-038: firmware version identity — the one source of truth for what a
// device reports on serial, the web UI, /status JSON, and the /update page.
// KitchenSync compiles with X32Link/ on its include path (ADR-0003) and
// X32_emulator symlinks this file, so every firmware reports the same version.
//
// Release process: bump FW_VERSION, tag the commit (git tag v<FW_VERSION>),
// then any distributed .bin traces back to source. Guarded so CI can inject
// a richer string via -DFW_VERSION="\"$(git describe --tags)\"" later.
#pragma once

#ifndef FW_VERSION
#define FW_VERSION "2.2.0"
#endif

// Invariants an injected FW_VERSION must keep: charset [A-Za-z0-9.-] only —
// it is interpolated unescaped into /status JSON and web-UI JS strings — and
// short enough that the /status snprintf buffers never truncate (asserted
// below; both firmwares' status handlers rely on it).
#ifdef __cplusplus
static_assert(sizeof(FW_VERSION) <= 24, "FW_VERSION too long for /status buffers");
#else
_Static_assert(sizeof(FW_VERSION) <= 24, "FW_VERSION too long for /status buffers");
#endif

// Compile stamp — tells dev builds apart between version bumps. Note: expands
// per translation unit, so serial vs web stamps can differ on an incremental
// rebuild; clean release builds agree.
#define FW_BUILD __DATE__ " " __TIME__
