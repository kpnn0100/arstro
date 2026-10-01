# Interstellar — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements. An entry with a dead anchor is a defect
(cosmo's D-1), and a behaviour with no entry does not ship.

**There is no code.** The first build's as-built tier is withdrawn with the build it described
(git history, `69b91eb`); carrying it forward would be a document describing a seam that no longer
exists, which is exactly the failure this tier exists to prevent.

## Conformance

Rung **0** of `arstro.rule` §5 — spec only. The ladder, and what each rung will add here:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | ← **here** |
| 1 | core split out; `Command`/`Event`/model with one text codec | P3 |
| 2 | registered with the root `ctest`; L2 headless service tests | P2 |
| 3 | a real CLI that is the whole app without a window | P3 |
| 4 | **generated API document, committed, drift-tested** | P3 — and **no app in the suite has this** |
| 5 | control socket + the GUI/headless equivalence test | post-P7 |

## Entries

*(none — the first will be `DR-RACK-1`: a colour edit reaching a real `.cmp`, which is P1's gate.)*
