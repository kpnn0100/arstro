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

*Last updated: 2026-10-05 — the image-processing stack, scopes, read-ahead playback, hardware video, the preview cache, keyframes and the graph editor.*

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
- [x] The 2026-10-05 request: (1) the rack row's weight bar → the Cosmo plugin's Mix — DONE
      (DR-UI-16), (2) the image-processing stack with Blur kinds — DONE (DR-FX-5/6, DR-UI-16), (3) keyframes
      and a graph editor (R-ANIM), (4) scopes — DONE (DR-UI-15), (5) smooth preview: read-ahead — DONE (DR-PLAY-2);
      hardware video — DONE (DR-PLAY-3); the graded preview cache — DONE (DR-PLAY-1). Keyframes: curves,
      the grammar and the render path — DONE (DR-ANIM-1); the diamonds, the graph editor and the key menu —
      DONE (DR-ANIM-2). The 2026-10-05 request is complete.
- [~] **The 2026-10-05 film request** ("implement all the features you suggest" + three fixes). One
      commit per line, in this order — each line's R- tags are in REQUIREMENTS.md:
      1. [x] Animation authored in the timeline only: no diamonds/curves in Grade; the Cut key lane
             lists the clip's properties, its source's colour keys and effects (R-ANIM-3/4 amended).
      2. [x] Waveform Luma | RGB overlay (R-UI-15 amended).
      3. [x] Collapsible effect sections in the effect panel (R-FX-5 amended).
      4. [x] Keyframe limits: shapes animate (DR-ANIM-3); multi-curve graph, box-select, copy/paste
             keys, resizable lane (DR-ANIM-4).
      5. [x] The grade on the GPU (R-GPU-1): Cosmo's GL backend is a multi-pass pipeline porting every
             stage (DR-GPU-1, cosmo DR-GPU-8).
      6. [x] 16-bit delivery path (R-COLOR-1) — DR-COLOR-1.
      7. [x] Input/working/output transforms and HDR (R-COLOR-2..4) — DR-COLOR-2, DR-COLOR-3; D-11.
      7b. [x] LUTs in and out (R-COLOR-5, R-COLOR-6) — DR-COLOR-4; D-12.
      8. [x] Audio: the mix, muxed into every video render (R-AUD-5 amended, R-AUD-9) — DR-AUD-2.
      8b. [x] Audio: playback on the audio clock and scrub grains, waveforms, meters (R-AUD-6..8) — DR-AUD-3.
      9. [x] Interchange: EDL, FCPXML, OTIO, timecode and reels; AAF stated (R-XCH-1..5) — DR-XCH-1.
      10. [x] Editing: three-point Insert/Overwrite with a source viewer, J/K/L shuttle (R-EDT-1, R-EDT-2) — DR-EDT-1.
      10b. [x] Speed ramps (R-EDT-3) — DR-EDT-2.
      10c. [x] Nested timelines (R-EDT-4) — DR-EDT-3.
      10d. [x] Multicam (R-EDT-5) — DR-EDT-4.
      11. [x] Media: CinemaDNG through LibRaw, the vendor SDK seam (R-MEDIA-1) — DR-MEDIA-1.
      11b. [x] Proxies, offline/online (R-MEDIA-2) — DR-MEDIA-2.
      11c. [x] Relink (R-MEDIA-3) — DR-MEDIA-3.
      12. [x] Qualifiers, windows and the matte view (R-CLR-1, R-CLR-2 windows) — DR-CLR-1.
      12b. [x] Window tracking (R-CLR-2) — DR-CLR-2.
      12c. [x] Stills gallery and the split-screen wipe (R-CLR-4, R-CLR-5) — DR-CLR-3.
      12d. [x] The node graph (R-CLR-3) — DR-CLR-4.
      13. [x] Autosave and crash recovery (R-DLV-5, R-DLV-6) — DR-DLV-1.
      13b. [x] Render presets (R-DLV-3) — DR-DLV-2.
      13c. [x] Burn-ins (R-DLV-2) — DR-DLV-3.
      13e. [x] Captions (R-DLV-1) — DR-DLV-4.
      13d. [x] DCP and IMF, unvalidated and saying so (R-DLV-4) — DR-DLV-5.
- [ ] **The professional backlog** (2026-10-02, asked "what is missing for professional movie
      editing") — ranked, each a future R- line, none started:
      1. **Audio**: playback in the monitor, waveforms, meters, clip/track volume and fades, the master
         sum muxed into renders (R-AUD-5 today: placed, not heard).
      2. **Interchange**: EDL / FCPXML / AAF / OTIO import and export, so a cut travels to and from
         other suites.
      3. **Three-point editing**: a source viewer with its own In/Out, insert vs overwrite, J/K/L
         shuttle, I/O marks on the timeline, match frame.
      4. **Colour science**: ~~scopes~~ (DONE 2026-10-05, DR-UI-15), colour management (ACES/OCIO,
         camera log input transforms), HDR (PQ/HLG) delivery, LUT import/export.
      5. **Keyframes**: ~~geometry, opacity, colour and effect parameters over time~~ (DONE 2026-10-05,
         DR-ANIM-1/2); speed ramps and reverse remain.
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

**2026-10-05 — DCP and IMF track files come from our own MXF writer, laid out as asdcplib lays them.**
FFmpeg 4.4's MXF muxer writes no RGBA or JPEG 2000 sub-descriptor and refuses multichannel OP-Atom
sound, so it cannot make a DCP. Vendoring asdcplib would add a library and its OpenSSL dependency to the
build. Instead `host/MxfWriter` writes the reference implementation's layout set for set, with labels
taken from its dictionary and SMPTE's registers. asdcplib, ClairMeta and Photon were built or fetched in
a scratch area to check the output during development, and none is in the build. So the app still says,
on every package, that it is not validated here. The IMF's JPEG 2000 is signalled as the generic
ISO 15444-1 label it really is rather than a profile it does not meet. Photon reports that, and the note
names it.

**2026-10-05 — a caption is a timeline node, and a render's captions are one cue at a time.** Captions
could have lived in a sidecar file the project points at. They are `#caption` nodes instead, because a
version must be able to change a line without copying the file (law 2), and an undo must take back an
import. A subtitle stream (mov_text, SubRip) and a burned-in picture can show only one cue at a time.
So the render joins cues that start together and shortens an earlier cue that runs into the next; the
nodes themselves keep what the SRT said. SRT's HTML/ASS styling is dropped on import, because neither
the monitor nor the burn could honour it.

**2026-10-05 — the core decides what a burn-in says; the host draws it.** Drawing text needs a
rasteriser and a typeface, and law 8 keeps both out of the core. So the core works out each item's
text and place per frame, and the host draws it through `Host::drawText`, using the app's CairoTarget and
the fonts compiled into the binary. A render's burn-in then reads like the product. The CLI links the app
library to get it. A host without text refuses `--burnin` rather than rendering without the burn-ins.
The text is drawn after the output transform. In an HDR render its white is BT.2408's graphics white, not
the peak.

**2026-10-05 — an autosave is the .isp plus every grade, and recovery goes through Cosmo.** Cosmo's
`.cmp` can only be saved over itself — and while a source is offline not at all (D-2) — so "saved
beside itself" cannot mean a second `.cmp`. The undo machinery already captures exactly the state that
matters (the .isp text and each node's own params and bypass) and restores it through Cosmo, so the
autosave is that capture written to disk, and recovery is that restore. It is removed by a save and by
a deliberate close, so only a session that ended without either — a crash — leaves one to offer.

**2026-10-05 — the node graph is the rack, drawn; a parallel node is an empty variant added by its mix.**
Cosmo's groups already are serial nodes (a group grades its members' results), so the graph adds no
second structure — a serial node is a group, made by Cosmo. Cosmo has no parallel structure, so a
parallel node is a variant of the source (a real Cosmo grade on the same media) marked `parallelOf`,
and the render adds its difference from the input — Resolve's parallel mixer, chosen over a layer
mixer's average because an empty parallel node must change nothing, which is what lets one be added
without the picture jumping.

**2026-10-05 — a still is a picture and a snapshot of a grade; the wipe is drawn by the service.** A
gallery's grade must not become a second colour authority, so it is written beside the project as the
same text Cosmo's params serialise to, and applying it goes through Cosmo exactly as Paste Grade does.
The wipe is composed by the service into the monitor's picture rather than by the UI from two images:
it is then one picture, testable pixel for pixel, the same in every front end, and it can never leak
into a render because only the monitor's paths apply it.

**2026-10-05 — a qualifier and a window are mattes in the node's stack, not colour.** Colour is Cosmo's
and Cosmo has no HSL key; the grade weight already showed the way — Interstellar owning HOW MUCH of a
node's grade reaches, never what it does. A matte is that weight made per-pixel: a plugin type that
keys instead of processing, so it inherits the plugin stack's storage, undo, animation (a window
tracks by keys), panel and cache key for nothing. A source's key limits its whole look; a group's
limits the group's contribution, computed apart exactly as a partial group weight is (D-7) — which is
what a serial node's window means.

**2026-10-05 — a relink moves the .isp's path; Cosmo keeps its slot.** Cosmo's slot identity is the path
it was added with and nothing outside Cosmo may rewrite its file (law 1), so the `.isp` holds the
source's file and remembers the slot's (`cosmoPath`), and the decoder seam — which already answers
"which frame" for a stored path — also answers "which file". Cosmo re-decodes only on a load, and
while a source is offline its `.cmp` cannot be saved (D-2), so a relink reloads the rack and puts back
everything only in memory through Cosmo, as undo does; a structural change since the save cannot be
put back that way, so the relink is refused with the way round rather than losing it.

**2026-10-05 — proxies are the monitor's, outside undo, and only whole.** Every NLE draws the line
the same way: proxies serve the person working, never the deliverable — so a render and export-still
plan under "originals only" whatever the switch, and everything else a person looks at follows it.
A proxy stores the original's code values (not a graded or transformed picture), so the input
transform, LUT, grade and plugins apply to it unchanged and its colour is the original's at a lower
resolution; plugins keep their size in original pixels. It is referenced only once its last frame is
written, and undo leaves proxy references alone: a background job finishing between an edit and its
undo must not be undone with it.

**2026-10-05 — a RAW sequence is a pattern; vendor RAW is refused at the door.** A CinemaDNG clip is a
folder of numbered frames; naming it `name_%06d.dng` (FFmpeg's and Nuke's spelling) makes it one
source with one identity — the .isp, Cosmo's slot, the caches — with no new node kind, and Cosmo
grades it through the same decoder seam as video. Development is fixed per clip (camera white
balance, matrix, BT.709, no auto-brightening) so footage does not flicker; a log or wide-gamut
interpretation is the input transform's job as for any file. ARRIRAW, R3D and BRAW cannot be decoded
without their vendors' per-user SDKs, so `rack add` refuses them naming the SDK — accepting them would
let Cosmo's save-on-add drop the failed slot (D-2) — while a project made where the SDK exists opens
with the source offline and the reason shown.

**2026-10-05 — a multicam is a nested timeline with an angle, not a new kind of node.** Premiere's
multicam source sequence is a sequence whose tracks are the cameras, and a multicam clip is that
sequence showing one of them; switching cuts the clip. Built that way here it costs one integer on a
clip (`angle`, the placed timeline's k-th video track) and inherits everything nested timelines
already do — live edits inside, versions as deltas (a version may switch angles), the plan key, the
sound, loop refusal. The multicam's sound is one chosen source, held across angle switches, because
the usual production has one good recorder and a picture switch must not jump the dialogue. The
angle bar names angles by their sources; per-angle pictures would cost a full render per angle per
playhead move and are left for when the preview pool can afford them.

**2026-10-05 — a nested timeline is live, clear where empty, and never inside itself.** Resolve's
compound clips and Premiere's nests both show the nested sequence live, so an edit inside reaches
every placement — a copy would fork the cut silently. The nested picture is the working-space
composite (no view transform, so ACEScct is viewed once) with empty areas transparent, as both NLEs
do, so a nest on V2 overlays V1. The loop check counts versions: a version inherits the clip, so
placing `social` (a version of `main`) inside `main`, or `main` inside anything `social` shows, is
refused. A hand-edited loop is linted and drawn as nothing rather than refused at load, so the file
still opens to be fixed.

**2026-10-05 — curves: the root's, on the footage's clock, frozen by pins.** A rack node's curve
runs in SOURCE time so every clip of that footage animates alike and Grade's reference-frame slider
walks it; a clip's runs on the clip's own footage clock (Premiere's and Resolve's choice), so moving,
trimming or splitting a clip never slides a key off its frame. A curve is not a scalar, so law 2
keeps it on the root timeline: a version inherits it live and adds its `#tlgrade` delta on top. A
pin is "colour frozen", so it snapshots the rack's curves beside the `.cmp` and its commit hashes
both — otherwise a pinned delivery would quietly re-animate when the base changed a key.

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
