#!/usr/bin/env bash
# Refresh the codebase-memory knowledge graph for this repo.
#
#   tools/reindex.sh            # fast (default) — no similarity/semantic edges
#   tools/reindex.sh full       # full — adds similarity/semantic edges, slower
#
# WHY THREE PROJECTS AND NOT ONE:
#
# The indexer carries a HARDCODED exclude list — `docs`, `tools`, `.claude`, `.gstack`,
# build dirs — and it is not configurable (`codebase-memory-mcp config list` exposes only
# auto_index / auto_index_limit / auto_watch / ui-lang). So indexing the repo root leaves
# the ADRs and linkcli invisible to every structural query.
#
# The workaround: index them as their OWN projects, where the excluded directory names are
# no longer in the path. Markdown is not second-class — the indexer parses headings into
# Section nodes and greps bodies — the ADRs were merely excluded, never unindexable.
#
# `tasks/` is NOT on the exclude list, so the tickets are already in the main project.
#
# Consequence to remember: a query must name the right project. There is no cross-project
# search. Decisions -> link-devices-docs. Firmware -> link-devices. The CLI -> the linkcli one.
set -euo pipefail

CM="${CM_BIN:-$HOME/.local/bin/codebase-memory-mcp}"
MODE="${1:-fast}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

[ -x "$CM" ] || { echo "codebase-memory-mcp not found at $CM (set CM_BIN)" >&2; exit 1; }

index() {  # index <name> <path>
  printf '==> %-24s %s\n' "$1" "${2#"$ROOT"/}"
  "$CM" cli index_repository --repo-path "$2" --name "$1" --mode "$MODE" 2>/dev/null \
    | sed -nE 's/.*"nodes":([0-9]+),"edges":([0-9]+).*/    \1 nodes, \2 edges/p'
}

index link-devices          "$ROOT"                 # the firmwares (tasks/ included)
index link-devices-docs     "$ROOT/docs"            # ADRs + design plans
index link-devices-linkcli  "$ROOT/tools/linkcli"   # the host-side Link peer

echo "done (mode: $MODE)"
