# Interstellar — Requirements (as-built tier)

**This tier describes what the code contractually does today. Today, that is nothing.**

There is no Interstellar code. This file exists from the first day rather than being created with
the first commit, because `arstro.rule` §2 makes the two-tier discipline a precondition rather than
a milestone, and because the suite already has a unit that skipped it: *"Solaris has a third id
convention (`SR-` + phase tags) and no as-built tier"* is recorded as a real task in that file's
appendix, not as a formatting complaint. Interstellar starts with both tiers so it never has to
acquire one.

| | intent tier | **as-built tier (this file)** |
|---|---|---|
| answers | what was asked for, why, and what happened to it | what the code contractually does **today** |
| written | **before** the code | **with** the code, in the same commit |
| voice | normative — shall / must / may not | descriptive present, with `file:line` anchors |
| status | in the section heading | none — an entry is current or it is a bug |
| history | never deleted; amended in place | rewritten in place to the new truth |
| lives in | [`../REQUIREMENTS.md`](../REQUIREMENTS.md) | here |

## Conformance

Rung **0** of `arstro.rule` §5's ladder — *spec only*, alongside pulsar. The rungs above, and what
each will require of this file:

| rung | what it means | what lands here |
|---|---|---|
| 0 | spec only | ← **Interstellar is here** |
| 1 | core split out; `Command`/`Event`/model with one text codec | `DR-SVC-*`, `DR-FMT-*` |
| 2 | registered with the root `ctest`; L2 headless service tests | `DR-TEST-*` |
| 3 | a real CLI front end that is the whole app without a window | `DR-CLI-*` |
| 4 | generated API document, committed, drift-tested | `DR-PARAM-3`, and Interstellar is asked to be **first in the suite** here |
| 5 | control socket + the equivalence test | `DR-SVC-8/9` |

## The rules this file obeys, once code exists

1. **One `DR-` entry per contractual behaviour**, in the descriptive present, citing the `R-` tag it
   implements and anchored with `file:line`. An entry with a dead anchor is a defect.
2. **Ids are stable, never renumbered, never reused.** Insert with a letter suffix (`DR-AUTO-2a`),
   never by shifting the numbers below.
3. **Rewritten in place** to the new truth when the code changes. This tier keeps no history — the
   intent tier does.
4. **The heading areas mirror the intent tier's**: `DR-SVC`, `DR-COSMO`, `DR-RACK`, `DR-CUT`,
   `DR-COMP`, `DR-PARAM`, `DR-AUTO`, `DR-BIND`, `DR-EVAL`, `DR-PLAY`, `DR-RENDER`, `DR-FMT`,
   `DR-VCS`, `DR-CLI`, `DR-UI`, `DR-TEST`.
5. **A behaviour with no `DR-` entry does not ship**, and neither does a `DR-` entry the code no
   longer matches (Cosmo's D-1: a doc describing a seam the code no longer has is a defect, not
   untidiness).

## Entries

*(none — no code exists. The first entries arrive with P1: `DR-FMT-1` for the `.isp` reader/writer
and its fixed-point round-trip, `DR-SVC-1/2/3` for the service seam, and `DR-CLI-1` for
`interstellar-cc`.)*
