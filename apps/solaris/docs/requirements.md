# Solaris — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements ([`../REQUIREMENTS.md`](../REQUIREMENTS.md)). An
entry with a dead anchor is a defect, and a behaviour with no entry does not ship.

## Conformance

Rung **0** of `arstro.rule` §5 — specified, no code yet. The ladder and where each rung lands:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | ← **here** (S0) |
| 1 | core split out; `Command`/`Event`/model with one text codec | V1 |
| 2 | registered with the root `ctest`; L2 headless service tests | V1 |
| 3 | a real CLI that is the whole app without a window | V2 |
| 4 | generated API document, committed, drift-tested | V3 |
| 5 | control socket + the GUI/headless equivalence test | X4 |

## Entries

*None yet. The first entries land with D1.*
