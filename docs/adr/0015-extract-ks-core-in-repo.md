# 15. Extract `ks-core` in-repo: move the shared engine out of `X32Link/`

Date: 2026-07-16
Status: accepted — **Option A** (bundle the move with ESP-026 / ADR-0009)

## Context

`X32Link/` is two things wearing one folder:

1. **A shipping product** — the Behringer X32/XR18 mixer bridge. Its own audience
   (mixer users), its own code: `osc_in`/`osc_out`, `touch_ui`, the mixer `app_config`
   (`mixer_ip`/`model`/`fx_slot`/`fdr`), `x32_form`, the X32 web UI.
2. **The home of the platform's shared pure-C engine** — `link_protocol`,
   `link_measurement*`, `link_phase`, `session_timeline`, `beat_source`, `clock_ticker`,
   `clock_output`, `swing`, `master_clock`, `bar`, `transport_launch`, `ks_config*`,
   `ks_status`, `ui_chrome`, and the rest of the host-tested core.

Every other product reaches **into this product's folder** for the shared core:

- **KitchenSync (the P4 — the flagship)** compiles ~30 files straight out of `X32Link/`
  via `set(X32LINK_DIR .../../X32Link)` in `KitchenSync/main/CMakeLists.txt`. Zero
  symlinks, but the flagship's build **literally depends on a niche product's directory.**
- **KitchenSyncTouch** carries **53 symlinks** into `X32Link/`.
- **LoraLink**, the host **test suite**, and **X32_emulator** all reference it too.

This is a dependency **inversion**: the platform core is owned by a secondary product, and
the primary product depends on it. It is not academic — the conflation has produced real,
shipped defects, most recently in one week:

- **ESP-042** — the shared config (`ks_config`) lives in `X32Link/` and is symlinked into
  every sketch; growing it (the Touch's fields) meant editing a file inside the mixer
  product and re-checking a fleet of symlinks.
- **ESP-040** — X32Link's own `/save` silently zeroed legal-zero fields; the fix had to
  distinguish X32Link-product form handling from the shared grammar, which is hard when
  both live in one folder.

And the code-memory index makes the blindness concrete: over `X32Link/`, Leiden clustering
finds **12 modules and labels all 12 "X32Link"** — because the folder *is* the package
boundary — even though half are the shared core (`link_proto_parse`/`find_peer`,
`clock_output_step`, `ks_config_decode`/`migrate_v1`, `link_measure_pump`) and half are the
mixer product (`osc_in_parse`, `osc_send_bpm`, `draw_keyboard`, `x32_form_merge`). No tool —
and no new contributor — can tell the platform core from a mixer feature, because they share
an address.

[ARC-009](../../tasks/ARC-009.md) proposed exactly this move and was **declined** as
premature. [ADR-0014](0014-repository-evolution.md) gates the *separate* `ks-core` **repo**
on ESP-IDF convergence (ADR-0009 / ESP-026) plus "a second `X32Link`-conflation defect." The
convergence is in flight, not complete — but the second (and third) defect has now landed,
and the product owner has clarified that **X32Link is a distinct product for a distinct
audience** (mixer users who happen to run Link), which sharpens why the core must not live
inside it.

## Decision

**Extract the shared engine out of `X32Link/` into a neutral, product-agnostic
`shared/` directory, *in the same repo*.** No new repository. This is a directory move
plus a re-point of build references — **no logic changes** — and it is the reversible,
in-repo precursor to the eventual `ks-core` repo ([ADR-0014](0014-repository-evolution.md)
step 2), not that extraction itself.

After the move:
- `shared/` is owned by no product. KitchenSync (P4), KitchenSyncTouch, X32Link, and
  LoraLink all consume it **as peers**.
- The flagship's CMake points at `shared/`, not `X32Link/` — **KitchenSync no longer builds
  against the mixer product.**
- The code-memory index can label the core its own package, restoring the wider image.

### The trade-off, stated honestly

The roadmap's caution is real: extracting the core while the **Arduino** consumers
(X32Link, and the Touch until ESP-026 lands) still compile only their sketch root means the
core must reach them by **symlink**. Today X32Link *owns* the files (0 symlinks); after the
move it becomes a symlink consumer like everyone else, so **X32Link gains ~30 symlinks** and
the Touch's 53 **re-point** from `../X32Link/` to `../shared/`. Net symlink count rises
until the Arduino consumers move to ESP-IDF (path-based compilation needs no symlinks). The
P4, already ESP-IDF, pays nothing — for it the move is a one-line CMake change and a pure win.

So the *symlink mechanics* don't improve now; the *ownership* does. Two sequencings:

- **Option A — bundle with ESP-026 (recommended).** Do the `shared/` move as part of, or
  immediately after, the Touch's ESP-IDF convergence. The Touch's 53 symlinks are *deleted*
  (replaced by ESP-IDF path refs) at the same moment the source relocates — so the move is
  symlink-neutral for the Touch and only X32Link (if it stays Arduino) keeps symlinks. No
  wasted churn.
- **Option B — standalone now.** Take the flagship-decoupling and the index clarity
  immediately; accept that X32Link gains symlinks and the Touch's 53 re-point now and get
  deleted again by ESP-026 later. More churn, faster ownership fix.

**Recommendation: A**, unless decoupling the flagship from X32Link is urgent enough to
justify B's throwaway churn. Either way the destination (`shared/`) and manifest are the
same; only the timing differs.

**Decision (2026-07-16): Option A.** The `shared/` extraction is bundled with the Touch's
ESP-IDF convergence (ADR-0009 / ESP-026): when the Touch's 53 symlinks are deleted in favour
of ESP-IDF path refs, the shared source relocates to `shared/` in the same change, so the
move is symlink-neutral. This ADR therefore does not schedule a standalone move — it makes
`shared/` the agreed destination and folds the mechanics into ESP-026's landing. The
migration plan below is the checklist for that combined step.

## Migration plan

1. **Compute the manifest mechanically, then review it before moving anything.** A file is
   **shared** iff more than one product references it (P4 `CMakeLists` `X32LINK_DIR` set ∪
   Touch symlinks ∪ test `Makefile` shared refs ∪ LoraLink/emulator refs). The remainder is
   **X32Link-only**. Clear-cut core (moves to `shared/`): `link_protocol`,
   `link_measurement`, `link_measurement_session`, `link_measure_pump`, `link_phase`,
   `session_timeline`, `beat_source`, `beat_clock`, `clock_ticker`, `clock_output`, `swing`,
   `master_clock`, `bar`, `transport_launch`, `ks_config`, `ks_config_json`, `ks_config_nvs`,
   `ks_status`, `ks_hostname`, `ks_form`, `config_persist`, `ui_chrome`, `wifi_conn_policy`,
   `bpm_tracker`, `midi_bpm_calc`, `usb_midi_pack`, `usb_midi_batch`, `metronome`,
   `metronome_voice`, `metro_strip`, `swing`, plus their headers. Clear-cut X32Link-only
   (stays): `osc_in`, `osc_out`, `app_config` (mixer), `touch_ui`, `x32_form`, `web_config`,
   the X32 sketch. **Boundary cases to decide in review:** `transport.c`, `web_status_json.c`,
   `tempo_snapshot.c`, `battery_gauge.c`, `led_phase.c`, `beat_synth.c`, `wifi_down_blink.c`
   — resolve each by "does >1 product compile it?" The manifest is an artifact of the PR, not
   a guess.
2. **Move with history.** `git mv X32Link/<f>.c shared/<f>.c` (and `.h`) for every manifest
   entry, so `git blame`/`log --follow` survive. `shared/` gets its own `README` naming it
   the platform core (eventual `ks-core`).
3. **Re-point references:**
   - **P4:** `KitchenSync/main/CMakeLists.txt` — `X32LINK_DIR` → `SHARED_DIR = .../../shared`.
     (The flagship stops depending on `X32Link/`.)
   - **Touch:** regenerate the shared symlinks to target `../shared/` (a scripted `ln -sf`
     over the manifest). Under Option A this step is instead "delete the symlinks; add
     ESP-IDF path refs."
   - **X32Link:** add symlinks (Arduino) to `../shared/` for the shared files it uses —
     X32Link becomes a peer consumer.
   - **test/`Makefile`:** rewrite the shared `../X32Link/*.c` paths to `../shared/*.c`.
   - **LoraLink, X32_emulator, tools/linkcli:** update any shared refs.
4. **Verify — nothing changes behaviourally, so everything must still pass:**
   - Full host suite (`cd test && make`) green — same count.
   - Compiles: P4 (`idf.py build`, esp32p4), both Touch variants, X32Link (both), LoraLink,
     X32_emulator.
   - No dangling symlinks (`find . -xtype l`).
5. **Re-index code-memory.** After the move, `get_architecture` clusters split into a
   `shared` package (the core) and an `X32Link` package (the product) — the wider image the
   index can't produce today. Consolidate the duplicate `link-devices` index while here.
6. **Bookkeeping.** Supersede [ARC-009](../../tasks/ARC-009.md) (its trigger has now fired);
   update [ADR-0014](0014-repository-evolution.md) step 2 to note the in-repo precursor is
   done and the *repo* extraction still waits for its own trigger (a second engine consumer
   or release-cadence pressure).

## Alternatives considered

- **Leave the core in `X32Link/`.** Rejected: the inversion has caused repeated defects
  (ESP-040, ESP-042), the flagship depends on a niche product, and the index cannot see the
  real module boundary. "It still compiles" is not "it is owned correctly."
- **Jump straight to a separate `ks-core` repo.** Rejected as premature per
  [ADR-0014](0014-repository-evolution.md): pays CI/versioning/cross-repo-latency cost with
  no second-consumer pressure yet. The in-repo `shared/` gets 90% of the ownership benefit
  at ~none of that cost, and makes the eventual repo extraction a clean `git` move.
- **Merge X32Link into KitchenSync.** Rejected, emphatically: X32Link is a **separate
  product for a different audience** (Behringer mixer users). It is a *peer* consumer of the
  core, not a part of KitchenSync. Merging would repeat the conflation in the other direction.
- **Do nothing until ESP-026 fully lands.** Viable — this is Option A above, and the
  recommended sequencing. Rejected only as a *permanent* answer: the ownership fix should
  not wait indefinitely on the Touch's display port, the one real unknown in ESP-026.

## Consequences

- KitchenSync (P4) no longer builds against `X32Link/`; the core has a neutral home.
- The code-memory index regains a true core/product boundary (the wider image).
- Under Option A: symlink-neutral. Under Option B: a temporary symlink increase on X32Link
  (and re-pointed Touch symlinks) until the Arduino consumers reach ESP-IDF.
- The separate `ks-core` **repo** still waits for its own trigger; this ADR does not create
  one. Each later extraction gets its own ADR when its trigger fires.
- No behaviour changes: this is a move + re-point, gated on the full suite and every firmware
  target still building.
