# Behringer Wing Support

This project does not target the Behringer Wing as a mixer for tempo/FX
control. Supported mixer targets are the X32 family (X32, XR18/X-Air).

## Why this is out of scope

The Wing is not an X32-family device. It speaks its own control protocol — an
OSC-style layer plus a text/`$`-style node command channel — with a different
address tree, a name-addressed node/param model instead of numeric
`/fx/N/par/MM` indices, and different value encodings. None of the X32-dialect
OSC layer (`osc_out`/`osc_in`, the model table mapping `MODEL_X32`/`MODEL_XR18`
to ports 10023/10024 and slot counts) carries over; Wing support means either
parameterizing that layer or writing a second dialect module behind the
tempo-source→OSC seam.

Two durable reasons to decline rather than defer:

1. **The project's scope is gear on the bench.** Every mixer feature shipped so
   far was validated against a real XR18 with live OSC dumps. Nobody involved
   owns a Wing, so the core research deliverables — real protocol dumps, FX
   topology confirmation, tempo-param encodings — cannot be produced, only
   transcribed from third-party docs and shipped unverified. That conflicts
   with the project's measure-before-building discipline.
2. **A second protocol stack is a real architectural cost.** Carrying a Wing
   dialect module means a second address model, second introspection path, and
   second unit-conversion table to keep correct, for hardware no user of this
   project has asked about since filing.

## What would change this

A Wing on the bench (or a collaborator supplying real control-channel dumps).
If that happens: delete this file and re-file the research ticket. The X32/XR18
FX research (LNK-029) is deliberately structured as the reusable template —
introspect FX type → map tempo param + encoding → fan out to N targets with
per-target note division — so the Wing track would re-answer those questions
rather than start from scratch.

## Prior requests

- LNK-030 — "Research: Behringer Wing support (different OSC protocol) for
  tempo/FX control" (filed 2026-07-04, declined in triage 2026-07-11)
