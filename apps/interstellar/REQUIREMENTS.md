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
  (**AMENDED 2026-10-05, user request "implement all the features you suggest" for film work:** a
  grading node graph (R-CLR-3), window tracking (R-CLR-2), captions (R-DLV-1), and audio playback,
  waveforms, meters and a mix in renders (R-AUD-6..9) are now in scope. Stabilisation, optical-flow
  retiming and multi-user editing stay out.)
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
  (**AMENDED 2026-10-05, user request "the effect panel should be able to collapse an item":** the
  effect panel shows every effect of the node as a COLLAPSIBLE section — a header with its name, its
  on/off and a disclosure arrow, its parameters under it — the selected one open; opening and
  closing is eased and is presentation, never a command.)
- **R-FX-6 Blur, in kinds** (added 2026-10-05, user request): Gaussian, Box, Directional (motion:
  length + angle), Zoom (amount + centre) and Spin (angle + centre). Sizes are in SOURCE pixels and
  scale with the proxy, so a preview and a render blur alike.
- **R-FX-4 A clip's `geom.crop` and a source's `xform.crop` are different things.** The source's is
  its framing — one per source, part of the look. The clip's is a reframe of the graded result, per
  shot. They sit on opposite sides of the colour stage, which is why they are not one address.

---

## R-ANIM — keyframes — ✅ IMPLEMENTED (added 2026-10-05, user request; DR-ANIM-1 the curves, DR-ANIM-2 the key lane, DR-ANIM-3 shapes, DR-ANIM-4 the graph and the lane's size; R-ANIM-3/4 amended and R-ANIM-6..8 added the same day)

- **R-ANIM-1 Every numeric parameter can be animated**: a colour key of a rack node, an effect's
  parameter, a clip's opacity and geometry. An animation is a curve of keyframes stored with its
  own unique id against the NODE's id (rename-safe) and the key. A rack node's and an effect's
  curves run in SOURCE time — the footage's own clock, shared by every clip of it; a clip's run on
  the CLIP's own footage clock (its in-point plus the offset times its speed, as Premiere and Resolve
  key a clip), so moving a clip moves its animation and trimming or splitting it leaves every key on
  the frame it was set on.
- **R-ANIM-2 Interpolation is a spline you can shape**: each keyframe is linear, bezier or hold;
  a bezier keyframe has an incoming and an outgoing SPEED (units per second) and INFLUENCE (% of
  the segment) — After Effects' model, so "ease in 33 %" means what an editor expects. Presets:
  Linear, Ease, Ease In, Ease Out, Hold.
- **R-ANIM-3 Setting an animated parameter keys it**: once a parameter has a curve, a `set` (a
  slider) writes a keyframe at the current time instead of the static value. A diamond beside each
  parameter adds or removes a keyframe at the current time.
  (**AMENDED 2026-10-05, user request "the animation for a property should only happen in the
  timeline, not Grade":** animation is AUTHORED in the timeline only. Grade sets a still look and
  shows no diamond and no curve; its sliders edit an animated value where Grade stands (the
  reference frame), which is editing, not animating. The diamonds live in the Cut tab's key lane,
  beside every property of the selected clip — its own (opacity, geometry), its source's colour keys
  and its source's effects — and key at the PLAYHEAD, on the clip's footage clock. A source's colour
  and effect curves stay the source's (R-ANIM-1, law 1): the lane shows them under the clip, keys
  under the frames they key, so every clip of that source shows the same curve.)
- **R-ANIM-4 A graph editor**: every animated parameter of the selection can be shown as its value
  curve over time, keyframes dragged in time and value, bezier handles dragged to shape speed and
  influence, and a right-click on a keyframe types the incoming / outgoing speed and influence or
  picks a preset. The graph runs on the same time axis the reference-frame slider uses (source
  time) for rack nodes and effects, and inside the clip for a clip.
  (**AMENDED 2026-10-05, with R-ANIM-3:** the graph lives in the timeline's key lane only, under the
  selected clip, for every animated property of it — the Grade deck has no curves face.)
- **R-ANIM-6 Shapes animate too** (added 2026-10-05, user request; ✅ DR-ANIM-3): a tone curve, a colour wheel
  (hue, saturation, luminance — hue takes the short way round) and the crop are keyed like numbers;
  between two keys a curve's points are interpolated point by point (resampled when their counts
  differ). A shape key is still the rack's (law 2): a version cannot carry its own.
- **R-ANIM-7 The graph edits like an editor's** (added 2026-10-05, user request; ✅ DR-ANIM-4): several curves at
  once (each normalised to its own range, the selected one in front), box-select of keys across
  them, moving a selection together, and copy/paste of keys to the playhead or another property.
- **R-ANIM-8 The key lane is usable at 1024×640** (added 2026-10-05, user request; ✅ DR-ANIM-4): its height is
  dragged at its top edge (remembered), its property list scrolls, and it never hides the tracks
  completely.
- **R-ANIM-5 Animation is the rack's, like colour** (law 2): a curve is not a scalar, so a derived
  version cannot carry its own — it inherits the base's curves live, and a scalar `#tlgrade` delta
  still adds on top of the animated value.

---

## R-PLAY — smooth preview — ✅ IMPLEMENTED (added 2026-10-05, user request "playback directly is a disaster"; DR-PLAY-1..3)

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
  the preview cache (R-PLAY-1) and H.264/H.265 renders encode on the GPU's video unit (VA-API) when it
  is on and one is present, falling back to software with a note when it is not — never failing a
  render because a device is missing. A render may override the setting (`--encoder`); ProRes, DNxHR
  and PNG have no hardware encoder and are refused one. (Wording follows R-PLAY-1's amendment: the
  request's "proxies" are the graded preview cache.)

---

## R-AUD — the audio project format — ✅ IMPLEMENTED (schema + placement: DR-AUD-1; the mix in renders: DR-AUD-2; heard, drawn, metered: DR-AUD-3)

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
  (**AMENDED 2026-10-05, user request "no sound" in the film review:** it MIXES — clip gain and
  fades, track gain, mute and solo summed to a stereo master — and plays it, draws it, meters it and
  delivers it (R-AUD-6..9). It still does not record, and processing beyond gain and fades
  (EQ, dynamics, plugins) stays Solaris's.)
- **R-AUD-6 Playback is heard, in sync** (added 2026-10-05): the master plays through the host's
  audio device while the timeline plays; the audio clock drives the picture when it plays, and a
  scrub plays a short grain. — ✅ (DR-AUD-3; PulseAudio/PipeWire in the GTK host). Without a sound
  server, or with nothing to hear, playback is picture only on the wall clock, as before.
- **R-AUD-7 Waveforms** on audio clips (added 2026-10-05): peak envelopes computed once per file and
  kept beside the project. — ✅ (DR-AUD-3; `<stem>.peaks/`, 100 peaks a second, linear in
  amplitude).
- **R-AUD-8 Meters** (added 2026-10-05): the master's peak and RMS per channel while playing, with a
  held peak and a clip indicator. — ✅ (DR-AUD-3; in the transport, −48…0 dB, what is heard rather
  than what was last mixed).
- **R-AUD-9 Renders carry the mix** (added 2026-10-05): every video render muxes the master (AAC for
  H.264/H.265, 24-bit PCM for ProRes/DNxHR), sample-accurate to the picture. — ✅ (DR-AUD-2). Said:
  a timeline with nothing sounding renders picture only; a PNG sequence carries no sound (no `.wav`
  beside it yet); varispeed sound follows the clip's speed in pitch, as tape does; buses, sends and
  instruments of the suite schema sum straight to the master; a mono file sounds at its own level in
  both channels.

---

## R-RENDER — delivery — ✅ IMPLEMENTED (DR-RENDER-1, -2a, -5, -6; the mix muxed: DR-AUD-2)

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
  (**AMENDED 2026-10-05, user request "the waveform should have a colour-split option":** the
  waveform has a **Luma | RGB** switch; RGB overlays the three channels' waveforms in one plot, each
  in its channel colour, additive — white where they agree, a colour where one channel runs away.)
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

## R-SET — engine settings — ✅ IMPLEMENTED (DR-SET-1..3, DR-PLAY-3)

- **R-SET-1 Cosmo's Engine Settings dialog, reused**: Screen scale, Preview quality, CPU threads,
  CPU limit, GPU acceleration (cosmo's Input row is hidden — Interstellar has no touch shell). Each
  chip is a `settings set` line; the service validates, applies and **persists** them.
- **R-SET-2 One CPU limit for the whole app.** The share of cores is one budget: the hosted Cosmo's
  decode pool and the engine threads Interstellar's own frame path runs on both come from it.
- **R-SET-3 Preview quality caps the monitor**, never a render or an export, and never upscales.
- **R-SET-4 Hardware video and Preview cache** (added 2026-10-05) — R-PLAY-3's and R-PLAY-1's switches,
  persisted like the others; rows Interstellar adds after cosmo's (cosmo's own dialog is unchanged).
  DR-PLAY-1, DR-PLAY-3.

---

## R-COLOR — colour science — ✅ IMPLEMENTED (added 2026-10-05, user request "implement all the features you suggest")

- **R-COLOR-1 Deliveries keep more than 8 bits.** A render decodes, grades, composites and encodes
  at 16 bits per channel, so a 10-bit ProRes, DNxHR or H.265 carries 10 bits of picture; the monitor
  and the preview cache may stay 8-bit. — ✅ (DR-COLOR-1). Stays 8-bit, said: a layer with a
  temporal effect (its frames come from the 8-bit volume; widened before the composite), a PNG
  sequence and a still export (8-bit PNG), an 8-bit codec (H.264, H.265 8-bit, DNxHR LB/SQ/HQ).
- **R-COLOR-2 Input transforms per source**: a source says what it is — Rec.709, sRGB, linear, ARRI
  LogC3/LogC4, Sony S-Log3 (S-Gamut3.Cine), Panasonic V-Log, Canon Log 3, RED Log3G10 (RWG),
  Blackmagic Film Gen 5 — and is converted to the working space before Cosmo grades it. — ✅
  (DR-COLOR-2). It is the media's interpretation (`<bind>.input`, saved on the `#rackobj`), never a
  grade: Cosmo still owns every colour value (law 1). A log curve reads the file as video-range
  YCbCr, as cameras write it.
- **R-COLOR-3 A working space**: Rec.709 (display-referred, the default and Cosmo's own) or ACEScct
  (scene-referred), chosen per project. — ✅ (DR-COLOR-2; `colour working`, the Colour menu). In
  ACEScct Cosmo grades ACEScct values as its encoded input, and the monitor shows the Rec.709 output.
  The tone map from scene to display is this program's (one function, stated in DR-COLOR-2), not
  ACES's RRT — OpenColorIO is not available on this build machine.
- **R-COLOR-4 Output transforms, HDR included**: Rec.709 (2.4), sRGB, P3-D65, Rec.2100 PQ and
  Rec.2100 HLG, chosen per render; an HDR render is 10-bit H.265 (or ProRes) tagged with its
  primaries, transfer and mastering metadata. — ✅ (DR-COLOR-3). AMENDED (2026-10-05, while building
  it): "Rec.709" is two outputs — `rec709`, the graded code values as the monitor showed them
  (today's renders, unchanged), and `rec709-2.4`, display light re-encoded with a pure 2.4 power for
  a BT.1886 master — because the family's monitor convention is sRGB-encoded and silently re-encoding
  every existing render would change it. Said, not hidden: PQ's MaxCLL/MaxFALL are written as 0
  ("unknown" — one pass cannot know them); ProRes HDR carries its Rec.2100 tags but no mastering box
  (FFmpeg 4.4's MOV muxer cannot write one) — the H.265 render carries it in the bitstream; an HDR
  render is encoded in software (the mastering SEI is libx265's).
- **R-COLOR-5 LUTs in**: a `.cube` (1D or 3D) as an input LUT on a source and as an effect in the
  image-processing stack. — ✅ (DR-COLOR-4). The input LUT follows the input transform; a file that
  does not read is refused, naming the line; an edited file on disk is read again.
- **R-COLOR-6 LUTs out**: the colour of a source's grade baked to a 33-point `.cube` (the spatial
  stages — clarity, sharpening, noise reduction, grain, lens — cannot live in a LUT and are left
  out, said in the file's header). — ✅ (DR-COLOR-4). Also left out and said: texture, dehaze, crop
  and rotation, masks; the colour mixer's neighbourhood spread is taken as 0. A LUT cannot hold a
  hard clip inside one lattice cell: measured on testsrc2, mean error 0.32 code values, 99.9 % within
  3, worst 27 where the grade drives a saturated colour into a clip in two channels (23 at 65 points).

## R-GPU — the grade on the GPU — ✅ IMPLEMENTED (added 2026-10-05; DR-GPU-1: every Cosmo stage on the GPU, masks finished on the CPU)

- **R-GPU-1 With Use GPU on, the grade runs on Cosmo's GPU backend** (OpenGL compute) for previews,
  playback, the preview cache and renders, falling back to the CPU when the backend declines a job;
  its output matches the CPU reference within a stated tolerance, measured by a test.

## R-XCH — interchange — ✅ IMPLEMENTED (added 2026-10-05; DR-XCH-1)

- **R-XCH-1 EDL** (CMX 3600) export and import of a timeline's video cut, with source reel names and
  record/source timecodes. — ✅ One video track per EDL (`--track`), drop-frame at 29.97/59.94,
  dissolves and M2 speed; FROM/TO clip comments as OTIO and Resolve write them.
- **R-XCH-2 FCPXML** (1.9) export and import — what Final Cut Pro, Premiere and Resolve read. — ✅
  Spine offsets count from the sequence's `tcStart` (Apple's model; OTIO's fcpx adapter ignores
  `tcStart` and so reads a 01:00:00:00 timeline with an hour of gap in front — said). Titles,
  generators and compound clips are not read in v1.
- **R-XCH-3 OpenTimelineIO** (`.otio`) export and import. — ✅ Verified against OpenTimelineIO
  0.18.1's own readers (cmx_3600, fcpx_xml, otio_json) and its writers.
- **R-XCH-4 AAF**: no AAF library exists on this platform's build; until one is chosen, AAF is
  reached through OTIO's converters outside the app, and the app says so. — ✅ (said: `interchange
  export --format aaf` refuses, naming `otioconvert` and the otio-aaf-adapter).
- **R-XCH-5 Source timecode and reel names** are read from the media (container/stream timecode,
  reel tags), shown, and used by every interchange format. — ✅ (the clip inspector's Reel and
  Source TC rows; `rack[].timecode`/`reel`).

## R-MEDIA — media management — ✅ DONE (added 2026-10-05; 1 ✅ DR-MEDIA-1, 2 ✅ DR-MEDIA-2, 3 ✅ DR-MEDIA-3)

- **R-MEDIA-1 Camera RAW**: CinemaDNG sequences through LibRaw. ARRIRAW, R3D and BRAW need their
  vendors' SDKs, which are licensed per user and not in this build: a decoder seam takes them when
  installed, and the app names the missing SDK instead of failing silently. — ✅ (DR-MEDIA-1). A CinemaDNG
  clip is added as its folder (or as `name_%06d.dng`), developed at 16 bits with the camera's white
  balance and colour matrix to Rec.709 and one exposure for the whole clip; its rate and timecode
  come from its tags (24 fps and none without them, said). A vendor RAW is refused at `rack add`
  naming its SDK — and a project that already names one shows it offline with that reason. RAW plays
  at the speed LibRaw develops a frame; a proxy (R-MEDIA-2) is the way to smooth playback (said).
- **R-MEDIA-2 Proxies, offline/online**: a proxy (ProRes Proxy or H.264 at a chosen size) is made
  per source; a project switch picks proxies or originals for the monitor; renders always use
  originals. — ✅ (DR-MEDIA-2). "The monitor" is everything that shows a picture to work by: playback,
  the preview cache, Grade, the source viewer, capture; export-still is a deliverable and decodes
  originals like a render. A proxy carries the original's code values, so its colour is the
  original's; it is referenced only once complete, and proxies sit outside undo, as renders do.
- **R-MEDIA-3 Relink**: offline media listed in one place, relinked one by one or by searching a
  folder for matching names. — ✅ (DR-MEDIA-3). The grade, this session's unsaved colour edits and
  every clip stay. A relink reloads the rack, so it is refused — saying why and what to do — if the
  rack's groups or names changed since its last save (Cosmo cannot save while a source is offline,
  D-2). Opened in Cosmo alone, a relinked source still names its old path (Cosmo cannot rename a slot;
  said).

## R-CLR — colourist tools — 🔶 IN PROGRESS (added 2026-10-05; 1 ✅ DR-CLR-1, 2 windows ✅ DR-CLR-1 — tracking next, 3–5 next)

- **R-CLR-1 Qualifiers**: an HSL key (hue, saturation, luminance ranges with softness) that limits a
  node's grade to what it selects, with a matte view. — ✅ (DR-CLR-1). A qualifier is a plugin in the node's stack
  (`qualifier.hsl`) that grades nothing: it keys where the node's grade reaches, read from the node's
  input; on a group it limits the group's own contribution. Colour stays Cosmo's. The matte view is
  Grade's monitor (`view matte`, Shift+H); a matted layer grades at source size (said).
- **R-CLR-2 Windows and tracking**: a node's circle/rectangle window can be tracked — its position
  follows a feature through the shot (forward and backward from the playhead), the track stored as
  keyframes (R-ANIM). — 🔶 windows ✅ (DR-CLR-1): `window.shape`, a circle or a rectangle, feathered,
  inverted, animatable by keys like any plugin parameter; tracking next.
- **R-CLR-3 A node graph**: the rack's serial structure (a group's grade over its members') shown as
  nodes and links, with serial and parallel nodes added and wired there; Cosmo stays the colour
  authority — a node is a Cosmo grade.
- **R-CLR-4 A stills gallery**: grab the monitor's graded frame with its grade; apply a still's
  grade to another source.
- **R-CLR-5 Split-screen wipe**: the monitor compares the current frame with a still or another
  version, split horizontally or vertically, the split dragged.

## R-EDT — editing — ✅ DONE (added 2026-10-05; 1–2 ✅ DR-EDT-1, 3 ✅ DR-EDT-2, 4 ✅ DR-EDT-3, 5 ✅ DR-EDT-4)

- **R-EDT-1 Three-point editing**: a source viewer with its own In/Out, timeline In/Out, and Insert
  (ripples) or Overwrite to the target track. — ✅ (DR-EDT-1). One monitor: in Cut the viewer shows
  the source (double-click it in the bin; Escape returns), scrubbed and stepped — it does not play
  on its own (said). Insert ripples every track and every placed sound, so sync holds.
- **R-EDT-2 J/K/L shuttle**: L plays forward, J backward, repeated presses speed up (1×, 2×, 4×), K
  pauses; K+J/L steps a frame. — ✅ (DR-EDT-1). Sound is heard at 1× forward only (said).
- **R-EDT-3 Speed ramps**: a clip's speed is animatable (R-ANIM); the source frame is the integral
  of the speed curve, so a ramp is continuous. — ✅ (DR-EDT-2). The speed is keyed on the clip's
  FOOTAGE clock like its other curves (v at source time s, so no circularity); the clip lasts ∫ ds/v,
  its stored speed the average that makes that length. With a ramp, a key's place in the key lane is
  its source frame's share of the clip, not its timeline instant (said).
- **R-EDT-4 Nested sequences**: a timeline used as a clip in another timeline. — ✅ (DR-EDT-3). The
  clip shows the nested timeline LIVE — an edit inside it reaches every timeline that places it — and
  trims, moves, speeds, fades and transforms like footage; its sound joins the outer mix through the
  clip's window. A timeline may never end up inside itself: placing one is refused when it is the
  timeline, a version of it, or contains either at any depth (a version inherits the clip). A
  timeline that another one places cannot be deleted from under its clip. Interchange export does not
  carry a nested clip — it says how many it left out (export the nested timeline on its own). A ramped
  nested clip's sound plays at the ramp's average speed (said).
- **R-EDT-5 Multicam**: sources synced by timecode (or by their in-points) form a multicam clip whose
  angle is switched at the playhead, each switch a cut. — ✅ (DR-EDT-4). A multicam is a timeline (a
  nested sequence, R-EDT-4) with one video track per source — the angles, in the order given — lined
  up by timecode or by in-points, carrying ONE source's sound (chosen; it does not follow the angle,
  said). Its clip shows one angle; switching at the playhead cuts the clip there (at its first frame
  it changes the whole clip). Switched from the monitor's angle bar in Cut or Alt+1…9 — while paused
  or playing; the bar names each angle by its source, without per-angle pictures (said).

## R-DLV — delivery and safety — ⏳ NOT STARTED (added 2026-10-05)

- **R-DLV-1 Captions**: SRT import, shown on the monitor, burned in or carried as a subtitle track
  (MP4 mov_text, MKV SRT) or written as a sidecar `.srt`.
- **R-DLV-2 Burn-ins**: timecode, clip name, source name and free text, positioned, on a render.
- **R-DLV-3 Render presets**: a render's whole spec saved by name and applied in one step; a few
  built in (YouTube 1080p, ProRes HQ master, Review H.264).
- **R-DLV-4 DCP and IMF**: a DCP (JPEG 2000 XYZ in MXF with CPL, PKL and ASSETMAP) and an IMF App 2E
  package. Neither can be validated here (no cinema server, no DCP/IMF validator in this build) — the
  app says so on every package it writes.
- **R-DLV-5 Autosave**: the project is saved beside itself every minute while it has unsaved changes.
- **R-DLV-6 Crash recovery**: opening a project whose autosave is newer than its file offers the
  autosave.

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
