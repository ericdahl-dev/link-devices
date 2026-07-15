# 14. Repository evolution toward a `ks-*` model

Date: 2026-07-15
Status: accepted (as direction)

## Context

The platform lives in two historically-named repositories (`link-devices`,
`KitchenSync-iOS_App`) with a monorepo firmware family inside the first. The
architecture vision proposes a decomposed future: `ks-core`, `ks-devices`,
`ks-ios`, `ks-mac`, `ks-simulator`, `ks-protocol`, `ks-docs`, and a quarantined
`link-audio-poc`.

The decomposition is directionally right, but decomposition has real cost (CI,
versioning, cross-repo change latency, symlink/submodule plumbing), and the codebase
does not yet justify most of it. There is a live temptation — visible in prior ADRs
(ARC-008/009, declined) — to co-locate/extract for tidiness before the pressure
exists. That temptation is the failure mode to guard against.

## Decision

**Adopt the `ks-*` model as documented long-term direction, and gate each
extraction on a concrete trigger — ownership or release-cadence pressure — rather
than on the diagram looking cleaner.** No `ks-*` repository is created by this
decision; this ADR records the direction and the order.

Order and triggers (full detail in
[`../architecture/repository-roadmap.md`](../architecture/repository-roadmap.md)):

1. **Framework convergence** (Touch → ESP-IDF) — [ADR-0009](0009-converge-the-clock-box-on-esp-idf.md).
   Trigger already fired. Prerequisite for `ks-core` (makes the engine's move out of
   `X32Link/` symlink-neutral).
2. **`ks-core`** — extract the pure timing/protocol engine from `X32Link/`. Trigger:
   convergence complete **and** a second consumer or another `X32Link`-conflation
   defect.
3. **Repo renames** (`link-devices → ks-devices`, `KitchenSync-iOS_App → ks-ios`).
   Trigger: historical names actively confuse. Lowest urgency; cosmetic.
4. **`ks-protocol`** — versioned/generated schema for firmware + clients. Trigger:
   the contract churns enough that hand-mirroring is a recurring drift source.
5. **`ks-simulator`** — generalize `X32_emulator` + `linkcli`. Trigger: deterministic
   integration tests needed that hardware can't provide.
6. **`ks-mac` / future hardware** — trigger: a concrete user need. No code today.
7. **`link-audio-poc`** — stays isolated until the Ableton commercial Link license
   question is resolved (GPLv2).

## Alternatives considered

- **Decompose now to the target shape.** Rejected: pays every split's cost up front
  with no offsetting pressure; `ks-core` before convergence would *add* ~50 symlinks
  rather than remove them; empty `ks-protocol`/`ks-simulator` repos would be
  abstractions the code doesn't justify (the exact over-engineering the vision itself
  warns against).
- **Never split; stay a monorepo indefinitely.** Rejected as a blanket rule: the
  `X32Link`-as-library conflation already causes defects, so at least `ks-core` has a
  real future trigger. The monorepo is the right *default*, not a permanent
  commitment.
- **Lead with the repo renames** (cheap, visible). Rejected as the first move:
  renames are cosmetic and would front-run the structural work that actually reduces
  coupling. Renames come after `ks-core`.

## Consequences

- The target `ks-*` shape is documented and stable, so contributors know where new
  functionality will eventually live — without any repository being created
  prematurely.
- Each extraction is a reviewable decision with a stated trigger; "it would be
  cleaner" is explicitly not sufficient.
- The near-term work that matters (ADR-0009 convergence) is correctly sequenced ahead
  of the cosmetic renames.
- This ADR is *direction*; each actual extraction, when its trigger fires, gets its
  own ADR recording the concrete decision and consequences at that time.
