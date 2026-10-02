# Interstellar — Progress Ledger

**The only authority on what is done and what is next.** Committed, so work resumes on any machine.
A session reads **NEXT**, does one task, updates this file, and commits — in the same commit.

- Rules: `.claude/skills/arstro.rule` · `.claude/skills/arstro.design.rule`
- Skills: `.claude/skills/arstro.interstellar.implement` · `.claude/skills/arstro.interstellar.debug`
- Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) · As-built: [`requirements.md`](requirements.md)
  · Format: [`project-format.md`](project-format.md) · Audio: [`../../../docs/audio-format.md`](../../../docs/audio-format.md)
  · Architecture: [`architecture.md`](architecture.md) · UI: [`ui-brief.md`](ui-brief.md)
  · Defects: [`DEFECTS.md`](DEFECTS.md)
- Legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but UNVERIFIED

*Last updated: 2026-10-01 — cosmo's menus, settings (CPU limit), undo/redo and presets; MKV playback (D-3) and the reference-frame reload (D-4) fixed.*

---

## NEXT

**► Integration of four parallel streams, then the service.** The second build is being made by four
agents in disjoint directories (each builds standalone, none touches shared CMake, none commits) and
one integrator who moves, wires, tests and commits each stream:

| stream | directory | state |
|---|---|---|
| volume + temporal ops | `core/ImageProcessing/src/volume/` | `[x]` integrated — DR-VOL-1..3, DR-FX-2 |
| render path (active set, composite, grade, cache) | `apps/interstellar/render/` | `[x]` integrated — DR-TL-4, DR-FX-3, DR-RENDER-2 |
| `.isp` model + versions | `apps/interstellar/model/` | `[x]` integrated — DR-FMT-1, DR-VER-1 |
| UI (Home, Edit: Grade/Cut/Deliver) | `apps/interstellar/app/` | `[x]` integrated — DR-UI-1..6; GTK host `linux_main.cpp` |
| service, grammar, codec, API doc, CLI | `apps/interstellar/core/service/`, `cli/` | `[x]` DR-SVC-1..3, DR-API-1, DR-VER-2/3, DR-RENDER-* |

- [x] `InterstellarService`: Rack + model + render + volume behind `dispatch` / `pump` / `model()` /
      `renderFrame`; `set` routed by owner; pins as content-addressed `.cmp` snapshots.
- [x] `interstellar-cc` on the grammar; `docs/api.json` + `docs/API.md` committed and drift-tested.
- [x] R-RENDER-5: an Interstellar still == the same frame from Cosmo (`interstellar_still_equals_cosmo`).
- [x] **Integrate the UI stream** and write the GTK host (`linux_main.cpp`); `interstellar_live`
      renders the real app over the real service.
- [x] Cosmo's File / Edit / Settings / Workspace / Preset menus, accelerators, Engine Settings with
      the CPU limit, screen scale, undo/redo, copy/paste grade, presets (DR-EDIT-*, DR-SET-*, DR-UI-7..9).
- [x] D-5/D-6/D-7 fixed (async monitor + thumbnails, group weight); Shift/Ctrl selection, Group
      Selection, cosmo's right-click menu on rack items, the weight bar's caption (DR-RACK-8, DR-UI-10).
- [ ] **Run the window by hand** on a desktop and walk the brief: new project → add footage → grade →
      cut → version → render. (Built and headlessly verified; not yet driven by a person.)
- [ ] UI follow-ups from the app's contract requests: determinate load progress in the model,
      `RackNodeModel::mediaDuration`, a `lint` list in the model for Deliver's checks,
      `ClipModel::danglingReason`; drag a source onto the timeline (`clip add`); drag to regroup.
- [x] Rewrite `arstro.interstellar.implement` / `.debug` for this specification (every command they
      name exists; the debug skill's sample script was run as written).
- [ ] P6: the audio master sum, muxed into a render (R-AUD-5).
- [ ] D-2 belongs to Cosmo (D-66); until it lands Interstellar refuses saves with an offline source.

---

## Phases

| phase | status |
|---|---|
| **P0** specification | `[x]` requirements, format, audio format, architecture, UI brief. First build removed |
| **P1** the rack — colour reaching a real `.cmp` | `[x]` gate passed from a shell on a video source; read back by a second CosmoService. D-1 filed (Cosmo alone cannot show a video source) |
| **P2** `.isp` + versions | `[x]` model stream integrated: 431 checks; DR-FMT-1, DR-VER-1 |
| **P3** service + API document | `[x]` rung 4 — `docs/api.json` + `docs/API.md` committed and drift-tested; 14 L2 tests on the real service |
| **P4** arrange + composite + render | `[x]` a named timeline renders to H.264/ProRes/PNG-seq; Interstellar still == Cosmo still |
| **P5** Volume + temporal effects | `[x]` every timeline frame reads through the volume; `#fx` denoise/blend/freeze wired |
| **P6** audio | `[~]` schema parsed and preserved; audio tracks/clips placed; **master sum not built** |
| **P7** UI | `[x]` Home + Edit (Grade/Cut/Deliver) in cosmo's design, purple-pink; 62 shots, 137 UI checks; GTK host; live harness over the real service |

---

## Decisions log (newest first)

**2026-10-02 — the monitor and the thumbnails moved off the UI thread (D-5, D-6).** Decoding a
capture's long GOP costs 100–200 ms per seek at 1080p, and no tuning of a decoder makes that a
frame's worth. So the work moved: the UI thread plans (cheap), a worker decodes (expensive), the
latest request wins and the monitor keeps its last frame meanwhile; thumbnails likewise, with a
persistent decoder per file. A non-reference-frame skip was tried for seeks and dropped: it saved
~9 % and broke frame exactness in `interstellar_host`.

**2026-10-02 — a group's weight is a continuous bypass of the GROUP (D-7).** Implemented as a pixel
mix between Cosmo's two exact answers (group bypassed / group on), not by scaling parameters toward
neutral — which would have been a colour computation Cosmo never makes.

**2026-10-01 — Cosmo's menus and settings, reused rather than re-drawn (R-UI-7, R-SET).** The menu
bar is cosmo's `MenuStrip`; Engine Settings is cosmo's `SettingsDialog`. Each needed one small,
opt-in addition (`MenuStrip::setItems` + read-only geometry; `SettingsDialog::setInputRowShown` +
`appearAmount`) and cosmo's two pure `AppSettings` scale helpers moved inline into the header so
the app links no cosmo_core — cosmo's 41 shots are byte-identical before and after. The menus are
named for this app (cosmo's Develop + History became **Edit**; **Workspace** holds the pages and a
view reset that never touches data). Interstellar's own one-row settings dialog (Reduce motion) is
retired: cosmo's dialog has no such row and cosmo honours the OS setting, so this app does too.

**2026-10-01 — undo restores STATES, written back through Cosmo.** Aligning Interstellar's undo
with Cosmo's own per-node, time-coalesced history would mean predicting Cosmo's coalescing; instead
an edit records the `.isp` text and the affected nodes' params before and after, and undo writes the
earlier params back as an ordinary `set` — Cosmo's history sees a forward edit, the `.cmp` stays the
authority, and nothing here becomes a second copy of colour.

**2026-10-01 — a reference frame is the Grade monitor's business, not a rack reload (D-4).** The
first build re-opened the rack so Cosmo's slot would hold the chosen frame. Nothing Interstellar
draws reads that slot — every source pixel comes from Interstellar's own frame source — so the
reload bought only spinners. Cosmo's slot now catches up at the next load.

**2026-10-01 — how a version's colour is stored, and what a pin is.** A derived timeline's colour
edit becomes a `#tlgrade` **delta on the colour source's own value** — so "my look = the base's look
+ my changes", and a base regrade still arrives (the user's "rebase to get the latest colour of the
base branch" is simply liveness; `rebase` is for dangling deltas and for advancing a pin). The
format stores numbers, so a curve/wheel override on a version is refused, pointing at the base or
`rack duplicate` — a second look is a second rack node (R-RACK-5), not a per-version curve. A **pin**
is a byte copy of the `.cmp`, content-addressed, read back through Cosmo's own static reader: a
frozen historical copy the user asked for, read-only by construction, never a second authority.

**2026-10-01 — the grammar removed two flags it could not honour.** `eval --at` and
`timeline diff --against` were specified but are not implementable in v1 (values do not vary over
time yet; the model's diff is against the base). R-SVC-3 forbids accepting a flag that does nothing,
so they were removed and §8 amended rather than accepted and ignored.

**2026-10-01 — Cosmo deletes offline images on save (Cosmo D-66), so Interstellar refuses to make it
save while a source is offline (D-2).** Found by binding the rack; measured with a cosmo-cc script.
It is Cosmo's to fix; the mitigation is a refusal that names the offline sources, never a silent
partial save.

**2026-10-01 — the render path is its own library and knows no project.** `interstellar_render`
takes plain structs (`ClipSpan`, `Layer`, `Raster`, `EditParams`) and nothing else, so a render is a
pure function of what it was handed (R-RENDER-2) by construction: it cannot quietly read a project
field it was never given. The model and the service translate into it. A dissolve needs a flag the
first contract lacked (`dissolveWithPrevious`) — weights summing to 1 are not enough if the layers
are stacked rather than mixed against one base.

**2026-10-01 — the volume landed as RGBA8, not linear float.** The architecture sketch had float
pixels; the build uses straight RGBA8 frames because the temporal ops run on decoded source frames
(R-VOL-5: ungraded) and the grade — which needs float — runs after them, per frame, in EditEngine.
A float window would cost 4× the residency to hold values that are about to be quantised anyway.
The view is a fixed 33-slot array of frame pointers, so it is a stack value that never allocates.

**2026-10-01 — the second specification, and what it changes.**

1. **The centre moved.** The first specification made the timeline the app and colour an embed, and
   two builds shipped a cutting tool whose colour authority was a test fake. Interstellar is a
   **colour tool that can cut**. `R-RACK` is now the first requirement and P1 the first phase, and
   the phase gate is a `.cmp` read back through `cosmo-cc`.
2. **A timeline IS a version** (R-VER), replacing project-level Nebula branching. Branching the
   whole project put the version boundary around the *colour authority*, which made "rebase to get
   the base's latest colour" impossible to express. One project, one rack, many cuts of it.
3. **One inheritance model, found by writing the format.** R-VER-2 first had colour inherit live and
   arrangement inherit by a replayed delta. Writing §3.2's resolution rules showed those to be two
   names for one mechanism; `cut=frozen` is now the same lever as `colour=pin`. Amended in place.
4. **The audio format is a suite document, not an app document** — two apps read it, so it is not
   one app's property. Interstellar implements a subset and **parses, preserves and refuses** the
   rest: a Solaris project opens here without loss, and a part it cannot render makes the render
   refuse rather than quietly go missing.
5. **The accent is derived, not picked.** `#CF5AED` is cosmo's blue rotated to hue 288° at the same
   lightness, so `ring`, `primaryAlpha` and the selection wash need no re-tuning. It is the **only**
   forked token.
6. **The FFmpeg source and writer were kept** across the removal. They are concept-independent
   plumbing, tested, and the new design needs them identically; deleting and retyping them would
   have been waste dressed as a clean slate.

---

## Verification notes

Nothing is built. The first build's evidence does not transfer: it was taken against a `FakeRack`
and a per-frame engine, and both are gone.

**The three claims that will need evidence rather than assertion, recorded now so they are not
quietly skipped:**

1. **P1's gate** — a value set in Interstellar, read back out of the `.cmp` by `cosmo-cc`. No
   architecture argument substitutes for that output.
2. **P5's measurement** — ms per frame for a radius-2 temporal denoise at 1080p and at 4K, which is
   what decides whether the volume's window size is affordable. The first build never measured the
   graded path at all, because identity params short-circuited the engine.
3. **R-API-1's drift test** — the document regenerated and diffed in CI. Generating it is the half
   that is easy; committing and diffing it is the half that makes it trustworthy.
