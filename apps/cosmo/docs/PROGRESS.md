# cosmo — Progress Ledger

**This file is the single source of truth for what is done and what to do next.** It is committed and
travels between computers. The four cosmo skills read the **NEXT** line below, do one task, update this
file, and commit.

- Implement: `.claude/skills/arstro.cosmo.core.implement/` · `.claude/skills/arstro.cosmo.design.implement/`
- Debug: `.claude/skills/arstro.cosmo.core.debug/` · `.claude/skills/arstro.cosmo.design.debug/`
- Defects: [`DEFECTS.md`](DEFECTS.md) · Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) ·
  As-built: [`requirements.md`](requirements.md) · Backlog: [`../PARITY.md`](../PARITY.md)
- Checkbox legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but
  UNVERIFIED (say why in Verification notes).

---

## NEXT

**Phase P0 — build the agent-drivable harness.** cosmo is feature-rich but nearly opaque from a shell:
no flags, no CLI, no headless UI render, no UI logging, and a log level that is never checked. Until P0
is done, every skill in this family has to work around blind spots (see D-2 … D-7 in `DEFECTS.md`).

**► P0.0 — write the `R-AGENT-*` requirements** in `../REQUIREMENTS.md` before any harness code:
the CLI contract, the debug-mode contract (levels, categories, env vars), the scripted-input grammar,
the UI-logging contract, and the headless-render contract. Conflict-check them against `R-NFR` and
`R-G-1…R-G-3`. Then P0.1 onward, in order.

Last updated: 2026-08-16 · Last commit: the four cosmo skills + this ledger + `DEFECTS.md`.

---

## Milestone status at a glance

| M | Milestone | State |
|---|---|---|
| P0 | Agent harness — CLI, headless render, debug logging, scripted input | **not started** |
| P1 | Doc-drift cleanup (D-1) and requirement coverage for what already shipped | not started |
| P2 | PARITY backlog: crop overlay (#3), preset picker (#4), settings (#5), split-drag (#6) | see `PARITY.md` |
| P3 | Known gaps: R-ZOOM-5 eased zoom (belongs in Artboard), unwired seams | not started |

---

## P0 — the agent harness

Each task is one session. Specs: `arstro.cosmo.core.implement` §5–§6 (A1–A12) and
`arstro.cosmo.design.implement` §6–§7.

- [ ] **P0.0** `R-AGENT-*` requirements written and conflict-checked *(do this first — V-model)*
- [ ] **P0.1** `COSMO_APP_NOMAIN` in `apps/cosmo/CMakeLists.txt` (A11) — one `list(REMOVE_ITEM … linux_main.cpp)`; it structurally blocks every headless UI harness
- [ ] **P0.2** `cosmo_shots` — headless PNG renders of every named app state, 2+ window sizes, mid-transition (A12)
- [ ] **P0.3** `cosmo_ui_tests` — headless assertions over the assembled app: non-overlap, reachability, text fit, reflow (A12)
- [ ] **P0.4** `Log`: honour the level, add categories, `COSMO_LOG_LEVEL` / `COSMO_LOG_CATEGORIES` / `COSMO_LOG_FILE`, wire up the dead `setStderrEcho()`, route GLib through `g_log_set_default_handler`, Windows backtrace via dbghelp (closes D-2, D-3, D-4)
- [ ] **P0.5** UI + input debug logging in `App.cpp` and the widgets (closes D-5)
- [ ] **P0.6** `--dump-ui` — Segment tree with rects, visibility, opacity, scroll offsets, hover
- [ ] **P0.7** `cosmo.exe` flags: `--project` (closes D-6), `--headless`, `--script`, `--shot`, `--log-level`, `--debug`, `--exit-after`
- [ ] **P0.8** `cosmo-cc` skeleton + `info` + `backends` + `check` (A1, A2, A7, A8)
- [ ] **P0.9** `cosmo-cc render` + `params` + `project` (A3, A4, A6)
- [ ] **P0.10** `cosmo-cc export` + `bench` (A5, A9)
- [ ] **P0.11** `docs/DEVELOPING.md` — the cold-start guide: where things are, which document decides
      what, the invariants, the worked commands, the mistakes already made (model it on
      `apps/genesis/docs/DEVELOPING.md`, which is the best example in this repo)
- [ ] **P0.12** `configDir()` on Windows outside MSYS2 (D-8) — decide and document the rule

**P0 is done when** a session on a fresh machine can, from a shell alone: build, run every test, render
any screen to a PNG, dump the UI tree, replay a scripted interaction, drive the whole engine headlessly,
and read a debug log that explains what the UI did.

---

## Decisions & deviations log (newest first)

- **2026-08-16 — The harness is a requirement, not tooling.** Anything an agent cannot reach from a
  shell is treated as a product defect in cosmo, filed in `DEFECTS.md`, and given an `R-AGENT-*`
  requirement — not left as an informal wish. Rationale: this project is developed across machines and
  sessions by agents; unreachable behaviour is untestable behaviour.
- **2026-08-16 — Copy Genesis, do not invent.** `genesis-cc`, `genesis_shots`, `genesis_ui_tests` and
  `GENESIS_APP_NOMAIN` already solve exactly these problems in this repo. cosmo's harness mirrors them
  so the two apps stay learnable from each other.
- **2026-08-16 — Two requirement tiers stay.** `../REQUIREMENTS.md` (`R-*`, intent + history) and
  `requirements.md` (`DR-*`, as-built + `file:line`) are not merged; the split is deliberate and both
  files state it. Every change touches both.

---

## Verification notes

- Nothing in P0 has been built or verified yet; every claim above comes from reading the source on
  2026-08-16.
- The host used for that reading is Windows 10 + MSYS2 MinGW64. `build/` (Debug, Ninja) and
  `build-mingw64/` (Release, Ninja) both contain a working `cosmo.exe`; `build/` is the one wired to
  `.vscode`. The distinction between the two trees is undocumented — resolve it in P0.11.
- `${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/` exists on this host but is **empty** — no log, no
  settings, no recents — so the log has never been confirmed to be written here in practice. Confirm
  before trusting it as a debug channel (see D-8).
