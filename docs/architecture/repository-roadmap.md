# KitchenSync — Repository Roadmap

> **Status:** direction, not backlog. This document records the long-term
> repository shape the platform is moving toward and — more importantly — the
> **conditions that should trigger each step**. Nothing here is scheduled work.
> See [ADR-0014](../adr/0014-repository-evolution.md).

## Guiding principle

> Extract a repository only when **ownership or release cadence** justifies it —
> not because a diagram looks cleaner.

Every split below has a real cost (CI, versioning, cross-repo change latency,
symlink/submodule plumbing). The current monorepo is the right default until a
specific pressure makes a boundary pay for itself. **Do not create empty `ks-*`
repositories ahead of the trigger.** Premature modularization is the failure mode
this roadmap exists to prevent.

## Current state (2026-07)

```
link-devices/                 KitchenSync-iOS_App/
├── KitchenSync/   (P4, primary, ESP-IDF)
├── KitchenSyncTouch/ (S3, migrating → ESP-IDF, ADR-0009)
├── X32Link/       (S3 mixer product + shared-pure-C home)
├── LoraLink/      (S3 LoRa relay)
├── X32_emulator/  (test double)
├── LinkAudioPoC/  (GPLv2 spike, quarantined)
└── tools/linkcli/ (host Link peer)
```

Two repositories, both historically named:
- `link-devices` — the firmware family (the name predates the platform framing).
- `KitchenSync-iOS_App` — the companion app.

## Target shape

```
ks-core       ← pure timing + protocol library (extracted out of X32Link/)
ks-devices    ← firmware targets (KitchenSync P4, Touch, X32Link, LoraLink)
ks-ios        ← iOS control plane           (rename of KitchenSync-iOS_App)
ks-mac        ← desktop control plane        (does not exist yet)
ks-simulator  ← virtual devices              (generalizes X32_emulator + linkcli)
ks-protocol   ← shared management schema     (the HTTP/Bonjour + Link contracts)
ks-docs       ← optional, only if docs outgrow the repos
link-audio-poc← the GPLv2 spike, kept isolated for licensing
```

## Steps, in dependency order, with triggers

Each step lists the **trigger** (the condition that makes it worth doing) and the
**cost** it pays. A step should not start before its trigger fires.

### 1. Converge the clock box on ESP-IDF (in flight)
- **What:** migrate `KitchenSyncTouch` to ESP-IDF as a board target of KitchenSync;
  collapse the triplicated clock writer to one. [ADR-0009](../adr/0009-converge-the-clock-box-on-esp-idf.md).
- **Trigger:** *already fired* — the duplicate writer caused real, shipped bugs.
- **Why first:** it is the prerequisite for `ks-core`. It makes the shared engine's
  move out of `X32Link/` symlink-neutral (ESP-IDF compiles by path; the Touch's ~50
  symlinks disappear).
- **Cost:** regression risk on working hardware; the display port (LovyanGFX +
  AXS5106L on ESP-IDF) is the one real unknown.

### 2. Extract `ks-core`
- **What:** move the pure, host-tested engine (`link_protocol`, `link_measurement`,
  `session_timeline`, `link_phase`, `beat_source`, `clock_ticker`, `clock_output`,
  `swing`, `bar`, `ks_config`, `ui_chrome`, the config model) out of `X32Link/` into
  a library owned by no single product.
- **Trigger:** step 1 complete **and** a second reason to want it independent —
  e.g. a non-firmware consumer (a simulator or desktop tool) needs the engine, or
  the `X32Link/`-as-both-product-and-library conflation causes another defect.
- **Why:** removes the "put the shared module in X32Link → put the feature in
  X32Link" trap ([ADR-0006/0007/0009](../adr/)); gives the engine its own test/release identity.
- **Cost:** the arduino-cli constraint (compiles only the sketch root) means
  Arduino consumers still need symlinks or a library layout — do this *with* the
  framework convergence, not before it.

### 3. Rename the repos (`link-devices → ks-devices`, `KitchenSync-iOS_App → ks-ios`)
- **Trigger:** the platform framing is stable and the historical names are actively
  confusing new contributors or users. This is the *lowest-cost, lowest-urgency*
  step — a rename with redirects, not a code move.
- **Cost:** URL churn (GitHub redirects handle most of it), CI/remote reconfig,
  documentation link updates.
- **Note:** the naming debt is cosmetic. It should not front-run the structural
  work (steps 1–2) that actually reduces coupling.

### 4. Extract `ks-protocol`
- **What:** the firmware↔client HTTP/Bonjour contract and the Link wire format as a
  versioned, possibly code-generated schema, shared by firmware and every client.
- **Trigger:** firmware and clients *share generated schemas* — i.e. the contract
  churns often enough that hand-mirroring it in Swift (and any future client) is a
  recurring source of drift. Today the contract is small and mirrored by hand + a
  partition test; that is cheap enough that extraction is not yet justified.
- **Cost:** codegen toolchain; a schema becomes a third thing to version.

### 5. Add `ks-simulator`
- **What:** deterministic virtual devices for integration testing, generalizing the
  existing `X32_emulator` (OSC console) and `tools/linkcli` (host Link peer).
- **Trigger:** integration tests need device behavior that hardware-in-the-loop
  can't give deterministically (CI without boards; fault injection; fleet-scale).
- **Cost:** a simulator is a second implementation of device behavior that must be
  kept honest against firmware.

### 6. `ks-mac` / desktop and future hardware
- **Trigger:** a concrete user need for a desktop control plane, or a new hardware
  tier. No code exists; pure direction. Reuses `ks-core` + `ks-protocol`.

### 7. `link-audio-poc`
- **Trigger:** keep isolated until the Ableton **commercial Link license** question
  is resolved. It links GPLv2 code and must never merge into product firmware or
  `ks-core` before that gate clears.

## What NOT to do

- Do not split repos to "look modular." Each split must clear its trigger.
- Do not extract `ks-core` before the framework convergence (step 1) — you would
  add ~50 symlinks, not remove them.
- Do not create `ks-protocol` while the contract is small enough to mirror by hand.
- Do not let the repo rename (step 3) substitute for the structural work.
