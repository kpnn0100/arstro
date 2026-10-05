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

*Last updated: 2026-10-05 — the image-processing stack, scopes, read-ahead playback, hardware video.*

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
- [x] Grade without a transport (its monitor shows the Grade target at its reference frame); the
      capture button with Copy Frame / Save Frame… (DR-UI-3c, DR-UI-11a/b).
- [x] The reference-frame slider: fast seek over the whole source with a live graded preview, frame
      steps at the source's rate (DR-RACK-3c).
- [x] Browsing groups like cosmo: shut groups in the tree, one level in the strip, double-click to
      drill in, cosmo's breadcrumb back up (DR-UI-12).
- [x] Variants: shared file, own object, offered wherever a source is, selected once made; D-8 fixed
      (DR-RACK-9).
- [x] Monitor zoom like cosmo: Ctrl + wheel about the pointer, eased, pan, fit (DR-UI-13).
- [x] Deliver's whole output spec: H.264/H.265/ProRes/DNxHR/PNG, profiles, quality, speed, depth,
      size, exact rates, range; D-9 (BT.601, untagged) fixed (DR-RENDER-6).
- [x] Cutting like an editor: drop a source on the timeline, roll, slip, ripple delete, markers,
      clip copy/cut/paste, clip and lane menus (DR-UI-14, DR-TL-6).
- [~] The 2026-10-05 request: (1) the rack row's weight bar → the Cosmo plugin's Mix — DONE
      (DR-UI-16), (2) the image-processing stack with Blur kinds — DONE (DR-FX-5/6, DR-UI-16), (3) keyframes
      and a graph editor (R-ANIM), (4) scopes — DONE (DR-UI-15), (5) smooth preview: read-ahead — DONE (DR-PLAY-2);
      hardware video — DONE (DR-PLAY-3); the graded preview cache — DONE (DR-PLAY-1). Keyframes and the
      graph editor (R-ANIM) next.
- [ ] **The professional backlog** (2026-10-02, asked "what is missing for professional movie
      editing") — ranked, each a future R- line, none started:
      1. **Audio**: playback in the monitor, waveforms, meters, clip/track volume and fades, the master
         sum muxed into renders (R-AUD-5 today: placed, not heard).
      2. **Interchange**: EDL / FCPXML / AAF / OTIO import and export, so a cut travels to and from
         other suites.
      3. **Three-point editing**: a source viewer with its own In/Out, insert vs overwrite, J/K/L
         shuttle, I/O marks on the timeline, match frame.
      4. **Colour science**: scopes (waveform, parade, vectorscope), colour management (ACES/OCIO,
         camera log input transforms), HDR (PQ/HLG) delivery, LUT import/export.
      5. **Keyframes**: geometry, opacity and effect parameters over time; speed ramps and reverse.
      6. **Timeline power**: multi-select and group move, linked audio/video, track lock/target/sync
         lock, slide edits, nested sequences, multicam, subclips, a snapping toggle.
      7. **Media management**: proxy generation and online/offline switching, relink UI, source
         timecode/reel metadata, RAW camera formats, frame-rate conform.
      8. **Titles and generators**: text, solids, bars and tone.
      9. **Delivery extras**: render presets, burn-ins, alpha output, upscale/reframe, a persistent
         queue, captions (R-SCOPE-4 excludes subtitles in v1).
      10. **Safety and output**: autosave and crash recovery, a second-display / video-output monitor,
          customisable shortcuts; a Cosmo `Move` command for drag-to-regroup (D-8).
- [ ] **Run the window by hand** on a desktop and walk the brief: new project → add footage → grade →
      cut → version → render. (Built and headlessly verified; not yet driven by a person.)
- [ ] UI follow-ups from the app's contract requests: determinate load progress in the model,
      a `lint` list in the model for Deliver's checks,
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

**2026-10-05 — the preview cache is checked per frame by plan hash, built when idle, one segment at a
time.** A render cache keyed by "clip + grade" would miss half of what changes a frame (a group's
weight, an effect, a version's override, a trim); the plan key already names all of it — the
read-ahead ring trusts it — so the cache hashes it per frame and an edit invalidates exactly what it
changed. One-second segments keep a rebuild small and a seek cheap. Building waits for the user to
stop for 1.5 s and drops its segment the moment they start: a cache that makes grading feel slower
would defeat the point of having one. Only playback reads it; a paused frame stays exactly graded.

**2026-10-05 — hardware video is VA-API through FFmpeg, and never a reason to fail.** The video unit
is reached through the FFmpeg we already link (`h264_vaapi`/`hevc_vaapi`), not a vendor SDK, so it
works on AMD and Intel under Linux with nothing new to ship. Colour conversion stays on the CPU with
the same BT.709 matrix as software, so a hardware render is the same picture (measured within 1 code
value). A missing device downgrades to software with the reason in the job's spec — a render that
fails because a laptop has no video unit would be worse than a slow one. Interstellar's row joins
cosmo's settings dialog through an opt-in API rather than a fork of the dialog.

**2026-10-05 — no source proxies: the grade is the cost (R-PLAY-1 amended).** Measured before
building what was asked: a 1080p frame decodes in 4 ms and grades in 170 ms at a 1600-px preview; 4K
decodes in 11 ms and grades in 220 ms; Cosmo's GPU path is unavailable here (no change with it on).
A proxy re-encode of the sources would have saved the 4 ms. What works: read-ahead on a few workers
at a size the machine keeps up with (stepping 960 → 640), prescaling large sources in linear light
before the grade (4K at 640 px: 82 → 39 ms), and — next — caching the GRADED preview as H.264, which
is the request's own idea applied to the frames that are expensive to make. Frame-level parallelism
gains little beyond 2 workers because Cosmo's engine already spreads one frame over the cores.

**2026-10-05 — plugins are rack nodes' children in the `.isp`, Cosmo stays plugin one (R-FX-5).**
Colour is Cosmo's and is always first: a blur after a grade is what the eye expects, and letting a
plugin run before Cosmo would mean Cosmo's own stills (the .cmp) no longer describe the colour of
its input. A plugin's parameters are the node's open fields, validated by the service's catalog
rather than the format, so a new plugin is a catalog row, not a format change. Group plugins run
after the member's own, inner first — the order a group's grade stacks in.

**2026-10-02 — a render never upscales or reframes (R-RENDER-6).** Sizes are fractions of the
project, so the render path's long-edge proxy produces them exactly and a pixel is never invented;
a deliverable larger than the project, or in another aspect, needs an upscaler or a reframe, which
v1 does not have — refused, naming why, rather than quietly stretched. NTSC rates are sent as
fractions because "23.976" is not 24000/1001 and a drifting time base moves cuts.

**2026-10-02 — a variant starts at the top of the rack (R-RACK-5, D-8).** Placing it beside its
original, inside the original's group, would keep the group's look — but Cosmo cannot move a node
into an existing group, and the `rack add --group` flag that pretended to was removed. The variant
is selected instead, so it is in view, and the user groups it like any node. A Cosmo `Move` command
is the prerequisite for in-place duplicates and drag-to-regroup alike.

**2026-10-02 — groups start shut, and the selection opens what hides it (R-UI-12).** "When I group,
the items come inside the group" reads as the asked-for behaviour, so a group is shut until opened.
A tree that hid the Grade target would be a trap, so moving the target inside a shut group opens its
ancestors — once per move, so the user can still shut the group around it. Open/shut and the strip's
level are presentation, not project state: no command, nothing saved, as cosmo's own navigation.
The breadcrumb is cosmo's widget with one opt-in (`setMeasuredText`): its width estimate left wide
gaps in Roboto; cosmo keeps the estimate and its pixels are unchanged.

**2026-10-02 — "remove the play bar from Grade" and "a capture button next to next on the play bar"
(R-UI-3 amended, R-UI-11).** The two asks collide in Grade. Resolved by putting the capture button on
the transport (Cut, Deliver) AND on Grade's monitor caption, so both hold. Grade's monitor stopped
showing the cut at the playhead — without a transport there is no playhead to move — and shows the
Grade target alone at its reference frame, which is what cosmo shows and what the grade is judged on.
Copy Frame is a host hook rather than a command because a clipboard is a desktop's, not the core's
(R-SCOPE-3); Save Frame… is the `capture` command, so a script can do it.

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
