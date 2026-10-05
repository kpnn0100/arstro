# Interstellar — Requirements (intent tier)

**What Interstellar is asked to do, why, and what happened to each request.** `R-<AREA>-<n>`; status
in the section heading. Read before any code; conflict-check before accepting a new one
(`arstro.rule` §2). As-built is [`docs/requirements.md`](docs/requirements.md) (`DR-`).

> **This is the SECOND specification.** The first (commit `903b121`, built at `69b91eb`) is withdrawn
> whole. It was not wrong in its parts — the address space, the automation model and the binding
> language were sound and some return here — it was wrong in its **centre**: it made the timeline the
> app and colour an embed, so two builds shipped a cutting tool whose colour authority was a test
> fake. **Interstellar is a colour tool that can cut**, not a cutting tool that can grade. Every
> requirement below is ordered by that sentence, and `R-RACK` comes before `R-TL` on purpose.

**Status legend:** `📋 SPECIFIED` · `🚧 IN PROGRESS` · `✅ IMPLEMENTED` · `⚠️ REOPENED (D-n)` ·
`❌ WITHDRAWN`.

---

## Global rules — 📋 SPECIFIED

- **R-G-1 The design law binds this app by reference.** `.claude/skills/arstro.design.rule` — every
  visible change animates, tokens only, R1–R6. **No value from it is restated here**; cosmo's own
  `R-G-1` predates the shared law and now duplicates it, and a third copy is how a rule becomes
  three rules.
- **R-G-2 Cosmo is the reference implementation, and the divergence is ONE token.** Interstellar
  *aliases* cosmo's radii, fonts, spacing and every neutral colour, and declares only the **accent**
  itself (R-UI-2). A forked token file is a divergence with a delay fuse; a forked *accessor* is a
  deliberate, auditable four lines.
- **R-G-3 One authority per fact.** A source's colour exists once. A clip's position exists once. A
  derived timeline stores a *delta*, never a copy of what it derived from.
- **R-G-4 Nothing is reachable only by clicking.** Every behaviour is a `Command`, observable in the
  `AppModel` or the `Event` stream, assertable from a shell with no display.

---

## R-SCOPE — what this is — ✅ IMPLEMENTED (Home + Edit{Grade, Cut, Deliver}; core UI-free)

- **R-SCOPE-1** Interstellar is a **video colour tool with an editor attached**. Its subject is
  grouping and blending colour across the footage of a production; its timeline exists so a graded
  look can be cut, versioned and delivered.
- **R-SCOPE-2 Four surfaces, and no more in v1:** a **Home** page of projects, and an **Edit** page
  of three tabs — **Grade** (grouping + colour), **Cut** (arrangement), **Deliver** (render).
- **R-SCOPE-3** `interstellar_core` is UI-free: no Artboard, no GTK, no codec, no OS paths. The CLI,
  the GUI and the tests are peers over one service.
- **R-SCOPE-4 Not in v1**, stated so it is not silently attempted: node-graph compositing, motion
  tracking, stabilisation, optical-flow retiming, subtitles, multi-user editing, and any audio
  capability beyond placing and gain-staging a clip (R-AUD-5).
- **R-SCOPE-5** v1 platform is Linux. Windows follows cosmo's MSYS2 path.

---

## R-RACK — grouping and colour: the centre of the app — ✅ IMPLEMENTED (DR-RACK-1..8, DR-RACK-3b/3c; limits D-1, D-2)

**This is the requirement the first version failed. It is first, and nothing else is accepted as
done while its authority is a fake.**

- **R-RACK-1 The rack is Cosmo's group tree, hosted — not re-implemented.** Interstellar owns a real
  `cosmo::CosmoService` in-process. Sources and groups are its nodes; a group's parameters compose
  onto its descendants through Cosmo's own `composeParams`. Interstellar stores **no `EditParams`,
  no curve, no mixer, no mask and no LUT reference**, ever.
- **R-RACK-2 A colour edit from Interstellar IS an edit to the Cosmo project.** It enters Cosmo's
  history and is saved into the `.cmp`. Opening that `.cmp` in Cosmo afterwards shows it. There is
  no import, no export and no synchronisation step, because there is nothing to synchronise.
- **R-RACK-3 A video source is graded on a reference frame.** It appears in the rack as an image
  node whose pixels are one extracted frame; the frame is selectable and changing it alters no
  parameter. **Cosmo grows no concept of time** — the extraction happens on Interstellar's side of
  Cosmo's own decoder seam, so Cosmo needs no change at all.
  (**AMENDED 2026-10-02, user request:** the frame is chosen with a **fast-seek slider over the whole
  source** — dragging previews the graded frame live in the monitor without committing, releasing
  commits it — plus **frame steps** (one frame back / forward) for the exact frame. The preview
  is "for choosing the correct frame"; the thumbnail strip stays as the slider's track.)
- **R-RACK-4 Grouping is the blending mechanism.** A group's offsets stack onto every descendant;
  nesting is the user's way of saying "these shots share a look". A node carries a continuous
  **grade weight** (0..1) — a scalable `bypass` — which is Interstellar's, not Cosmo's.
  (**AMENDED 2026-10-05, user request "remove the weight slider in Grade":** the weight is no longer
  a bar on every rack row — it is the **Mix** of the node's Cosmo plugin in its image-processing
  stack (R-FX-5), where every plugin has an on/off and a mix. The address `<node>.weight` and its
  rendering are unchanged, so a project that set it renders the same.)
- **R-RACK-5 A variant is a duplicated rack node**, not a per-clip override. One shot needing two
  looks is two nodes on one file. A per-clip grade would put colour in two places (R-G-3).
  (**AMENDED 2026-10-02, user request "duplicate source — same source but a different blending,
  taking no space, treated as its own object":** a variant references the **same file** — nothing
  is copied — and the cut decodes that file through **one** decoder and cache however many variants
  use it (Cosmo keeps one reference still per node, its own). It is its own object everywhere: its
  own bind name, grade, weight, bypass and group, its own clips, its own row in the rack, the strip
  and the source bin. It is offered wherever a source is — the rack, the strip and the source bin's
  right-click menus, Edit › Duplicate as Variant, Ctrl+D — and is selected once made, so it is in
  view. A row says **shared file** when another node uses the same file. It starts at the top of
  the rack: Cosmo has no "move into a group" yet (D-8), so it is grouped like any node.)
- **R-RACK-6 Rack identity is Interstellar's.** A bind name (`gr1`, `s_day01`) — legal, unique,
  stable once assigned — plus the media path and reference-frame time. Cosmo names a node after its
  file or after what the user typed, and neither is addressable.
- **R-RACK-8 Select like Cosmo** (added 2026-10-02, user request): Shift-click selects a range,
  Ctrl-click toggles, in the rack tree and the filmstrip; **Group Selection** (Ctrl+G, the Edit menu,
  the right-click menu) groups it; a source no clip uses can be removed from the rack.
- **R-RACK-7 An offline source reads as missing, never as a stall**, and the project stays openable.

---

## R-VER — a timeline is a version — ✅ IMPLEMENTED (DR-VER-1..3, DR-FMT-1)

The headline of this specification, and the reason it is not the first one.

- **R-VER-1 A project holds many timelines; a timeline may declare a `base`.** `main` is the root. A
  derived timeline is *"my base, plus my changes"* — it stores a **delta**, never a copy.
- **R-VER-2 One inheritance model: inherit live, override to diverge, pin to stop, rebase to
  reconcile.** A version is *"my base, resolved live, with my deltas on top"* — and **colour and
  arrangement behave identically**, which is the whole reason the model is learnable. A regrade or a
  re-cut on the base reaches every open version the moment it lands, except where that version
  overrides, and an override always wins.
  (**AMENDED while writing [`docs/project-format.md`](docs/project-format.md) §3.2, 2026-10-01.**
  This first said colour inherits live while arrangement inherits by a delta *replayed on demand*,
  to protect the idea that a cut "must not change under the editor's hands". Writing the resolution
  rules showed that to be two models where one does: **a delta resolved live against its base IS
  inheritance**, and naming the same mechanism twice bought nothing but a second set of rules. The
  editorial worry is real and is answered by `cut=frozen` — now the same lever as `colour=pin`
  rather than a different one.)
- **R-VER-3 A version may PIN its base's colour and FREEZE its arrangement**, which is what a
  delivery needs: both stop inheriting, deliberately and visibly, through the same lever. A pinned
  version is read-only for colour, refused with the commit named — a pin that yields is not a pin.
- **R-VER-4 `rebase` reconciles and advances.** Because resolution is live, a version is already
  current — so rebase does the two things liveness cannot: **prune or report deltas whose target the
  base has deleted** (dangling), and **advance a pin or thaw a freeze** to the base's present state.
  It never discards a delta silently; a dangling one is named.
- **R-VER-5 Every version is renderable and selectable by name** (R-RENDER-1). `main` is not
  privileged except by being the default base.
- **R-VER-6 Versioning is per-timeline, not per-project.** A project is one body of footage with one
  rack; its versions are cuts of that footage. (**This replaces the first specification's
  project-level Nebula branching**, which put the version boundary around the colour authority and
  so made "rebase to get the base's colour" impossible to express.)

---

## R-TL — the timeline — ✅ IMPLEMENTED (DR-VER-1, DR-TL-4, DR-TL-6); every op reachable in the Cut tab (DR-UI-14)

- **R-TL-1 Node types: `track`, `clip`, `transition`, `marker`.** A track is `video | audio`. A clip
  references a **rack node** and carries its source range, timeline position, speed, geometry,
  opacity and blend.
- **R-TL-2 A clip carries no colour**, of any spelling. Such a field is a validation **error**, not
  an ignored key: ignoring it would silently discard a user's edit.
- **R-TL-3 The cut operations**, each a `Command`: add, trim, split, move, roll, slip, ripple delete,
  set speed, snap.
- **R-TL-4 A transition HOLDS the outgoing clip past its out-point** for its duration, reading its
  handles, with the two weights summing to 1. Two adjacent clips leave nothing to dissolve *from* —
  the first version shipped that and the picture went dark through every cut (D-6, carried forward
  as a requirement rather than relearned).
- **R-TL-5 Frames are the authority**; a cut that lands between frames is a bug.
- **R-TL-6 A clip can be copied and pasted** (added 2026-10-02, user request "all edit features"):
  `clip copy <clip>` keeps the clip's source range, speed, geometry, opacity and blend in the
  service; `clip paste [--at <t>] [--track <trk>]` places a new clip from it (default: the playhead,
  the copied clip's track) — so cut-then-paste works after the original is gone.

---

## R-VOL — video is a volume — ✅ IMPLEMENTED (DR-VOL-1..3; every timeline frame is read through it, DR-RENDER-2a)

- **R-VOL-1 A clip's source is a VOLUME — (x, y, t) — and the volume is lazy.** It is an interface,
  not storage: frames materialise on demand behind it. A 10-second 1080p clip is 6 GB in linear
  float and a 4K one is 24 GB, so a resident volume is not a thing that can exist.
- **R-VOL-2 The accessor is a WINDOW, never a point.** `window(t0, t1)` materialises a range and
  hands back a view; there is no `at(x, y, t)` scalar accessor. A per-pixel `t` lookup hides an
  unbounded decode behind an innocent call, which is lazy evaluation's classic trap.
- **R-VOL-3 A view is raw memory.** Resolved once per window, hoisted once per row, plain pointer
  arithmetic per pixel — the style `Sharpen` already uses, not the per-pixel virtual
  `PointProcessor` uses. At 2.07 M pixels a frame, a per-pixel virtual call is ~4 ms per stage per
  frame and ~670 ms/s of dispatch across seven stages: fine for one photo, fatal at 24 fps.
- **R-VOL-4 A processor declares its temporal footprint** — radius 0 (all seventeen of today's
  stages, unchanged), radius *k*, or whole-clip. The engine unions the chain's footprints and
  materialises one window, so **residency is bounded by the largest radius, not by the clip
  length**.
- **R-VOL-5 The volume is over SOURCE frames, ungraded.** Source frames because a 2× clip's timeline
  neighbours are two source frames apart, which is wrong for anything temporal; ungraded because a
  cached graded neighbour would make the answer depend on the grade and force every neighbour's
  parameters into the cache key.
- **R-VOL-6 A photo is the T=1 case**, so Cosmo is untouched and the shared engine is strengthened
  rather than threatened.
- **R-VOL-7 Residency is invisible.** Same `t`, same pixels, whatever the access order — a render
  stays a pure function of (project, timeline, range, spec), which is what every golden test rests
  on.

---

## R-FX — basic video effects — 🚧 IN PROGRESS (DR-FX-2, DR-FX-3, DR-RENDER-2; R-FX-5, R-FX-6 in progress)

- **R-FX-1 The seventeen Cosmo stages apply per frame**, unchanged, because they are radius 0.
  Exposure, contrast, tone regions, curve, white balance, vibrance, mixer, grade, dehaze, grain,
  texture, clarity, sharpen, noise reduction, crop, rotate, lens.
- **R-FX-2 v1 adds exactly three temporal effects**, chosen because each is inexpressible per frame
  and each proves a different part of R-VOL: **temporal denoise** (radius *k*, the quality lever
  that matters most on real footage), **frame blend / shutter** (radius *k*, proves windowing), and
  **freeze** (radius 0 with a remapped `t`, proves the time axis is addressable).
- **R-FX-3 Geometry and composite per clip**: position, scale, rotation, anchor, crop, opacity,
  blend mode, fit.
- **R-FX-5 Every rack node has an image-processing STACK of plugins** (added 2026-10-05, user
  request). Selecting a source or a group shows its stack: **Cosmo** is plugin one — the colour,
  always first, on/off = the node's bypass, Mix = its weight — and effects follow in order, each
  with its own **unique, stable id** (`ef_<n>`, kept for the effect's whole life and never
  renumbered, so a project under version control diffs as "this effect changed"), an on/off, a
  mix, and its parameters as
  addresses (`ef_3.radius`). Effects can be added, removed and reordered. A group's effects apply
  to every source under it, after the source's own (inner first, the way a group's grade stacks).
  An effect belongs to the rack like a grade does: the same in every version; a version that needs
  a different one duplicates the source (R-RACK-5), the rule law 2 already gives curves.
- **R-FX-6 Blur, in kinds** (added 2026-10-05, user request): Gaussian, Box, Directional (motion:
  length + angle), Zoom (amount + centre) and Spin (angle + centre). Sizes are in SOURCE pixels and
  scale with the proxy, so a preview and a render blur alike.
- **R-FX-4 A clip's `geom.crop` and a source's `xform.crop` are different things.** The source's is
  its framing — one per source, part of the look. The clip's is a reframe of the graded result, per
  shot. They sit on opposite sides of the colour stage, which is why they are not one address.

---

## R-ANIM — keyframes — 🚧 IN PROGRESS (added 2026-10-05, user request)

- **R-ANIM-1 Every numeric parameter can be animated**: a colour key of a rack node, an effect's
  parameter, a clip's opacity and geometry. An animation is a curve of keyframes stored with its
  own unique id against the NODE's id (rename-safe) and the key. A rack node's and an effect's
  curves run in SOURCE time — the footage's own clock, shared by every clip of it; a clip's run in
  CLIP time, so moving a clip moves its animation.
- **R-ANIM-2 Interpolation is a spline you can shape**: each keyframe is linear, bezier or hold;
  a bezier keyframe has an incoming and an outgoing SPEED (units per second) and INFLUENCE (% of
  the segment) — After Effects' model, so "ease in 33 %" means what an editor expects. Presets:
  Linear, Ease, Ease In, Ease Out, Hold.
- **R-ANIM-3 Setting an animated parameter keys it**: once a parameter has a curve, a `set` (a
  slider) writes a keyframe at the current time instead of the static value. A diamond beside each
  parameter adds or removes a keyframe at the current time.
- **R-ANIM-4 A graph editor**: every animated parameter of the selection can be shown as its value
  curve over time, keyframes dragged in time and value, bezier handles dragged to shape speed and
  influence, and a right-click on a keyframe types the incoming / outgoing speed and influence or
  picks a preset. The graph runs on the same time axis the reference-frame slider uses (source
  time) for rack nodes and effects, and inside the clip for a clip.
- **R-ANIM-5 Animation is the rack's, like colour** (law 2): a curve is not a scalar, so a derived
  version cannot carry its own — it inherits the base's curves live, and a scalar `#tlgrade` delta
  still adds on top of the animated value.

---

## R-PLAY — smooth preview — 🚧 IN PROGRESS (added 2026-10-05, user request "playback directly is a disaster"; R-PLAY-2 DONE, DR-PLAY-2)

- **R-PLAY-1 A preview cache of GRADED frames** — (**AMENDED 2026-10-05, the same day, after
  measuring:** the request suggested pre-encoded H.264 *proxies* of the sources. Measured on this
  machine with `interstellar_play_bench`: decoding a frame costs 4 ms (1080p) and 11 ms (4K), the
  grade 170–220 ms at a 1600-px preview. A source proxy would speed up the 4 ms. So the pre-encoded
  H.264 is of the frames that are expensive to make — the GRADED preview.) The current timeline can
  be cached in the background as H.264 at the preview size, and playback reads a frame from the
  cache — at decode speed — whenever that frame has not changed since it was cached (each frame is
  checked by its plan key, so an edit invalidates only the frames it touches). A cache never
  reaches a render or an export.
- **R-PLAY-2 Playback reads ahead**: while playing, the frames after the playhead are decoded and
  graded on workers into a small ring, and the monitor shows the frame due NOW — dropping, never
  stalling, when the machine falls behind. The playing picture is graded at the size the machine keeps up
  with — stepped down (1280 → 960 → 640) when the read-ahead falls behind, up when it has headroom,
  said on the monitor's caption — and the paused frame is graded at the full preview size. Play
  pre-rolls (at most half a second) so the first frames are ready.
- **R-PLAY-3 Hardware video, as a setting**: Engine Settings gains **Hardware video** (off / on):
  proxies and H.264/H.265 renders encode on the GPU's video unit (VA-API) when it is on and one is
  present, falling back to software with a note when it is not — never failing a render because a
  device is missing.

---

## R-AUD — the audio project format — 🚧 IN PROGRESS (schema + placement: DR-AUD-1; the master sum is not built)

Designed now, for Solaris to adopt whole. Interstellar implements the subset it needs; the schema
reserves the rest so adopting it is not a migration.

- **R-AUD-1 One schema, two apps**, specified in [`../../docs/audio-format.md`](../../docs/audio-format.md)
  at suite level rather than inside this app — a format two apps read is not one app's property.
- **R-AUD-2 Node types:** `atrack` (`audio | instrument | bus`), `aclip` (sample: `src`/`in`/`out`/
  `at`/`gain`/`fade`; note: `at`/`length` + `note` children), `note`, `arack` (an ordered processor
  chain), `aeffect`, `aauto` (a parameter curve), `asend`. Interstellar implements `atrack
  kind=audio`, `aclip` sample form, `gain`, `fade` and the master sum; the rest **parse, round-trip
  and are preserved** so a Solaris project opens here without loss.
- **R-AUD-3 Time is shared with video and stated per project.** Seconds on a frame boundary in
  Interstellar; Solaris adds `bpm`/`sig` and beats, and the schema carries both without either app
  guessing.
- **R-AUD-4 Audio is referenced, never embedded** — `res:<hash>` plus a path hint, relinkable,
  offline-flagged rather than fatal.
- **R-AUD-5 Interstellar does not mix, record or process audio.** It places clips, stages gain and
  sums to a master so a cut can be watched and delivered with its bed. Sound design is Solaris's,
  and the format is the seam between them.

---

## R-RENDER — delivery — ✅ IMPLEMENTED for picture (DR-RENDER-1, -2a, -5, -6); audio not muxed

- **R-RENDER-1 A render names its TIMELINE.** `render --timeline social-30s --out …`. There is no
  implicit "current" timeline in a render, because a delivery that depended on which tab was open
  would be a delivery nobody can reproduce.
- **R-RENDER-2 A render is a pure function** of (project, timeline, range, output spec). No
  wall-clock, no unseeded randomness; grain seeded from (source, frame).
- **R-RENDER-3 Real codecs at the host**: H.264/MP4 and ProRes/MOV, plus a **PNG/PPM sequence**,
  which is kept deliberately because it is the only output a golden test can compare byte for byte.
  (**AMENDED 2026-10-02:** plus **H.265/HEVC** in MP4/MKV and **DNxHR** in MOV — R-RENDER-6.)
- **R-RENDER-4 A render runs a frame at a time and yields**, so it is cancellable and does not block
  a window.
- **R-RENDER-5 A still exported from Interstellar and the same frame exported from Cosmo are
  byte-identical.** The cheapest possible proof that R-RACK-2 actually holds.
  (**AMENDED 2026-10-05:** for a node with no effects after Cosmo and no animated parameter —
  Cosmo has neither, so a frame that uses them cannot equal a Cosmo still; the identity still binds
  everything else, and the test keeps proving it.)
- **R-RENDER-6 Deliver states the whole output spec** (added 2026-10-02, user request "Deliver needs
  detail options for render"). Every choice is a flag of `render`, shown in Deliver and named in the
  queue row: **codec** (H.264, H.265, ProRes Proxy/LT/422/HQ/4444, DNxHR LB/SQ/HQ/HQX/444, PNG
  sequence), **resolution** (the project's, or a fraction of it — never larger, never another
  aspect: a render does not upscale or reframe in v1), **frame rate** (the project's, or a standard
  rate, the NTSC ones as exact fractions; the timeline is SAMPLED at the output rate, so a render
  stays a pure function, R-RENDER-2), **quality** (a constant-quality level for H.264/H.265;
  the profile is the quality for ProRes/DNxHR), **encoder speed**, **bit depth** where the codec
  offers a choice (H.265 8/10), and **range** (the whole timeline, or an in/out set from the
  playhead). A combination the encoder cannot make is refused, naming why, before anything is
  queued. Audio is not muxed (R-AUD-5) and Deliver says so rather than leaving it to be discovered.
  Every video file is BT.709, video range, and tagged so (D-9).

---

## R-SVC / R-API — the service and its document — ✅ IMPLEMENTED (DR-SVC-1..3, DR-API-1) — rung 4

- **R-SVC-1** `InterstellarService`: `dispatch(Command)` / `pump(nowMs)` / `model()` / an `Event`
  sink. The GUI has no privileged path.
- **R-SVC-2 One way in, one way out, one codec.** A tagged `Command` with a generated text form; an
  `AppModel` of plain data plus an `Event` stream. `formatEvent()` output **is** the log line.
- **R-SVC-3 An unknown input is REJECTED, naming it**, with the nearest candidates. A command that
  accepts a key it does not understand and reports success is worse than one that crashes.
- **R-API-1 The API document is GENERATED, COMMITTED and DRIFT-TESTED.** `interstellar-cc api
  [--json]` prints every command with its grammar and argument hints, every event with its fields,
  every `AppModel` field, and the whole parameter address space with units, ranges and owners —
  from the same tables the parser and the registry use. `docs/api.json` and `docs/API.md` are
  committed, and **a test regenerates and diffs them**.
  **This is rung 4 of `arstro.rule` §5's ladder and no app in the suite has reached it.** The first
  version generated the document and never committed it, which is the half that does not count: a
  document an agent cannot read without building the app is a document an agent will not read.
- **R-API-2 An agent drives the app through the document.** Discover via `api --json`, reach via one
  `Command`, observe via an `AppModel` field or an `Event`, assert via a stable dump or `eval`.

---

## R-UI — the two screens — ✅ IMPLEMENTED (DR-UI-1..16, DR-UI-3c, DR-UI-11a/b; gap: drag-to-regroup, needs a Cosmo move — D-8)

- **R-UI-1 Home.** Recent projects as cards, newest first, with name, footage count and size; new,
  open, and a settings dialog. Cosmo's `HomeScreen` rhythm — this is the surface where "exactly the
  same design as cosmo" is most visible and least excusable to get wrong.
- **R-UI-2 The accent is purple-pink, and it is the ONLY forked token.** `primary = #CF5AED`,
  derived by taking cosmo's `#4F7EF7` into HSL (H 222°, S 91%, L 64%), rotating the hue to **288°**
  and easing saturation to 80% — so it carries the *same perceptual weight* and every alpha token
  built on it (`ring`, `primaryAlpha`, the 22% highlight wash) keeps working untouched. Everything
  else — background, card, foreground, muted, border, destructive, success, radii, fonts, the
  spacing ladder, the type ramp — is **aliased from cosmo, not copied**.
- **R-UI-3 Edit is three tabs over one monitor:** **Grade** (rack tree + Cosmo's own parameter
  panels), **Cut** (timeline + version switcher), **Deliver** (render queue + output spec). The
  monitor never leaves and never reloads on a tab change — a frame that looks different in two tabs
  is a defect.
  (**AMENDED 2026-10-02, user request "in Grade, remove the play/time bar — it is not necessary in
  grading":** Grade has **no transport**; its monitor shows the **Grade target's reference frame,
  graded** — the frame being graded, as in Cosmo — and takes the transport's room. Cut and Deliver
  show the current timeline at the playhead with the transport. The monitor is still one widget that
  never moves or reloads its layout; what changed is that Grade's subject is a source, not the cut.)
- **R-UI-4 The version switcher is chrome, not a panel.** Which version you are editing is as
  present as which project you are in, and switching it is one click from anywhere in Edit.
- **R-UI-5 Cosmo's widgets are reused as libraries**: `SliderRow`, `ParamPanel`, `MixerPanel`,
  `CurvePanel`, `GradePanel`, `XformPanel`, `HistogramWidget`, `Filmstrip`, `SegmentedControl`,
  `PillButton`, `IconButton`, `ConfirmDialog`. A copied widget is a divergence with a delay fuse.
  (**AMENDED 2026-10-05:** the histogram became one mode of the SCOPES panel (R-UI-15), which draws
  it to fill the scope body — cosmo's `HistogramWidget` has a fixed plot height. It is the same
  data in the same channel colours; nothing of cosmo's was copied.)
- **R-UI-6 Every state is drawn and shot**, empty and loading included, at two window sizes, and
  mid-transition as well as at rest.

- **R-UI-7 Cosmo's menu bar, in Interstellar's words** (added 2026-10-01, user request "file, edit,
  setting, workspace like cosmo"). Cosmo's `MenuStrip` after the wordmark: **File** (Home, Open,
  Save, Save As, Add Footage, Export Still, Render), **Edit** (Undo, Redo, Copy Grade, Paste Grade
  to Selected / to All, Group, Ungroup, Duplicate as Variant — cosmo's Develop + History),
  **Settings** (Engine Settings), **Workspace** (Grade / Cut / Deliver, Reset Workspace — the
  view, never data), **Preset** (Save, Import, and Apply for every preset in the library). Cosmo's
  accelerators: Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z, Ctrl+S, Ctrl+Shift+S, Ctrl+O, Ctrl+C / Ctrl+V. Every
  item is a command line or a host picker — the menus add no behaviour of their own (R-G-4).
- **R-UI-9 Cosmo's right-click menu on rack items** (added 2026-10-02, user request) — rack rows and
  filmstrip cells: Add Footage, Group / Group Selection, Ungroup, Enable/Disable Filter, Rename
  (inline, cosmo's morph), Duplicate as Variant, Copy Grade, Paste Grade (to Selection), Remove from
  Rack. Right-clicking outside the selection selects that node first; inside it, the selection
  stays — cosmo's rule. The grade-weight bar names itself on hover ("weight 80%").
- **R-UI-11 Capture the frame** (added 2026-10-02, user request). A capture button beside the
  transport's "next" button opens a menu — **Copy Frame** (the system clipboard) and **Save Frame…**
  (a PNG) — of the frame the monitor shows, at full resolution. Grade has no transport (R-UI-3), so
  there the same button sits on the monitor's caption: the request asked for it "on the playback
  bar" and also for that bar to leave Grade; this keeps both. The save is a command (`capture`).
- **R-UI-12 Browse groups like Cosmo** (added 2026-10-02, user request "grouping feels weird"). Grouped
  items go INSIDE their group: the rack tree collapses a group (a chevron opens and closes it,
  eased); the SOURCES strip shows the OPEN group's contents; double-clicking a group (chip or row)
  opens it; Cosmo's breadcrumb above the strip names the path and a click on a name goes back up.
- **R-UI-13 Zoom the monitor like Cosmo** (added 2026-10-02, user request "zoom in using Ctrl +
  scroll like cosmo"). Cosmo's R-ZOOM, on the monitor: Ctrl + wheel zooms about the pointer
  (wheel up = in), 1×–8×; drag pans while zoomed, clamped so the picture covers the monitor;
  double-click (or Workspace › Reset Workspace) returns to fit. The zoom EASES (cosmo's R-ZOOM-5,
  which cosmo itself has not built), a chip names the magnification while zoomed, and the monitor
  asks for a frame sharp enough for the magnification — within Engine Settings' preview cap, which
  still bounds the monitor (R-SET-3; raise Preview quality for full detail). Plain wheel over the
  monitor does nothing, as in Cosmo; Ctrl + wheel over the timeline keeps zooming time.
- **R-UI-14 Cut like an editor** (added 2026-10-02, user request "drag and drop source to timeline
  and all edit features"). Every R-TL-3 operation is reachable in the Cut tab, each a command line:
  **drag a source** from the source bin onto a lane (`clip add` — the whole source, snapped like a
  move; onto an empty timeline it first adds a video track); move and trim (as built); **roll**
  (Alt-drag the cut between two touching clips); **slip** (Alt-drag a clip's body); split at the
  playhead (S); delete and **ripple delete** (Delete / Shift+Delete); **speed**; a **dissolve** into
  the next touching clip; a **marker** at the playhead (M); **add a track**; **copy / cut / paste**
  a clip (Ctrl+C / Ctrl+X / Ctrl+V, R-TL-6). A right-click on a clip offers all of them. A
  modifier-drag says what it will do while it is held.
- **R-UI-15 Scopes, like Resolve's** (added 2026-10-05, user request "data visual graphs for
  checking the quality — clipping, bit depth"). Beside the monitor in Grade: **Histogram**,
  **Waveform** (luma), **Parade** (R G B), **Vectorscope** (with skin-tone line), computed from the
  frame the monitor shows. Each says what is wrong in words: the share of pixels CLIPPED at black
  and white per channel, and the **levels used** — how many of the 256 code values occur — so
  banding from a crushed or 8-bit-starved range is visible as a number, not only as gaps. A
  **clip warning** overlay on the monitor marks clipped pixels.
- **R-UI-8 Screen scale, eased** — cosmo's R-SCALE: the shell draws at 75–200 %, the scale ZOOMS
  (260 ms) with the layout re-derived from the drawn scale every frame, input maps through it, and
  the window minimum follows the target scale.

---

## R-EDIT — Cosmo's editing conveniences — ✅ IMPLEMENTED (DR-EDIT-1..3)

- **R-EDIT-1 One undo history across the rack and the project.** A grade, a version override, a
  cut, a version change — undone and redone in order. A slider drag is one step. A change to the
  rack's node set (add, group, ungroup, duplicate, import) is not undoable and starts a fresh
  history — exactly as in Cosmo, whose history is per node. Undo writes colour back THROUGH Cosmo.
- **R-EDIT-2 Copy and paste a grade** between rack nodes (cosmo's Copy Settings / Paste to Selected
  / Paste to All Images). Root timeline only — on a version a paste would be a second copy of
  colour; there the addresses are the tool.
- **R-EDIT-3 Presets are Cosmo's `.apf`**: save a node's grade to the library, import an `.apf`,
  apply one to a source (Cosmo applies presets to an image, not a group).

## R-SET — engine settings — ✅ IMPLEMENTED (DR-SET-1..3)

- **R-SET-1 Cosmo's Engine Settings dialog, reused**: Screen scale, Preview quality, CPU threads,
  CPU limit, GPU acceleration (cosmo's Input row is hidden — Interstellar has no touch shell). Each
  chip is a `settings set` line; the service validates, applies and **persists** them.
- **R-SET-2 One CPU limit for the whole app.** The share of cores is one budget: the hosted Cosmo's
  decode pool and the engine threads Interstellar's own frame path runs on both come from it.
- **R-SET-3 Preview quality caps the monitor**, never a render or an export, and never upscales.
- **R-SET-4 Hardware video** (added 2026-10-05) — R-PLAY-3's switch, persisted like the others.

---

## R-TEST — evidence — 🚧 IN PROGRESS (round trip, colour round trip, version table and Cosmo-equality are live; a golden timeline frame is not)

- **R-TEST-1** Every suite registers with the root `ctest`. A suite that cannot report red is not
  evidence — both suites undefine `NDEBUG` before `<cassert>` (cosmo's D-43, which bit this repo
  again on `gene_tests`' first run).
- **R-TEST-2 Pick the lowest level that proves it**, and prefer a measured assertion to an argued
  one. L2 — the real service, headless, with a fake decoder — is usually right.
- **R-TEST-3 The new behaviour needs a test that fails without the fix.** Checked, not assumed.
- **R-TEST-4 Four load-bearing suites:** the `.isp` round-trip fixed point; the **colour round trip
  through a real `.cmp`** (R-RACK-2's gate); the version/rebase table; and the golden frame.
