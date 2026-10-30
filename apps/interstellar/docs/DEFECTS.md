# Interstellar — Defect list

`D-<n>`, sequential, **never reused**, nothing deleted. A resolved entry moves whole to `## Closed`
with its commit hash and the test that now guards it.

The numbering restarted at D-1 with this specification; the first build's D-1…D-8 are in git history
at `69b91eb` and are not carried forward as ids, because an id that pointed at deleted code would be
worse than no id.

**What IS carried forward is the lessons**, promoted into requirements so they cannot be relearned:

| the first build's defect | now |
|---|---|
| D-6, a dissolve faded the incoming clip up over black | **R-TL-4** — a transition holds the outgoing clip past its out-point, weights summing to 1 |
| D-2, a suite printed `[PASS]` while `NDEBUG` disabled its assertions | **R-TEST-1** — both suites undefine `NDEBUG` before `<cassert>` |
| D-5, `eval --explain` printed an input that contradicted its own result | **R-G-3** — one authority per fact, including the one a trace reads |
| D-7, `rack add` refused when nothing could be decoded | **R-RACK-7** — a source registers and reads offline; registering and decoding are different things |
| D-3, every automation link in the first 190 px was culled behind a label gutter | **`arstro.design.rule` gotcha 15** — derive the second copy of a fact from the first |

## Entry format

Defined in `arstro.cosmo.core.debug` §4 and not duplicated here beyond the shape: **Area · Status ·
Severity · Found · Reproduce (pasteable) · Expected · Actual · Evidence (a measurement, not an
argument) · Judgement · Cause (`file:line`) · Requirement · Recommended fix · Guarded by.**

## Open

### D-18 — `clip add`'s grammar says `--track`, `--in` and `--at` are optional; the parser requires all three
- **Area:** service grammar / API document · **Status:** Confirmed (reproduced) · **Severity:** S3 ·
  **Found:** 2026-10-30, driving Interstellar through `interstellar-cc mcp` from the API document.
- **Reproduce:** `interstellar-cc project new /tmp/a.isp : rack add photo.jpg : clip add --src ro_1`
  → `--in is required: clip add [--track <trk>] [--src <rackobj|timeline>] [--in <t>] …`; with
  `--in 0` → `--at is required`; with `--at 0` on a new project → `clip add: no track named ` (empty).
- **Expected:** what docs/API.md and the refusal's own usage line say — brackets mean optional; the
  description even says "Without --out, the rest of the source from --in" (a drag drops at the
  playhead on the selected track).
- **Actual:** all three are required, and a new project has no track to default to.
- **Judgement:** **defect** — the generated document and the parser disagree, which is exactly what an
  agent reading `interstellar_api` trips on (arstro.rule §5: discover from the document).
- **Recommended fix:** either default them (`--in 0`, `--at` the playhead, `--track` the first video
  track — made if none) or drop the brackets in the command table so the document says the truth.
- **Guarded by:** a service test that `clip add --src ro_1` on a fresh project either succeeds or is
  refused with a usage line that shows no brackets around what it requires.

### D-17 — A project that was empty when the Cut tab first laid out stays at the maximum zoom
- **Area:** ui · **Status:** Open · **Severity:** S3 · **Found:** 2026-10-27, driving the web face's
  Cut tab over the real service (R-SVC-4 amended).
- **Reproduce:** `project new`, open Cut (the timeline lays out with nothing on it), then `rack add` a
  6 s clip and drop it: the timeline shows 480 px/s — two seconds across the deck — not the clip fitted.
- **Expected:** a timeline that gets its first clip fits it, as one opened with clips does
  (`fitPps`, "zoom to fit"); a user should not need Workspace › Reset Workspace to see the cut.
- **Evidence:** `Timeline::advance` sets the zoom ONCE, on the first frame after the first bind
  (`mPpsInit`, `Timeline.cpp:741-747`), from `fitPps()` = lanes width / max(1, duration) clamped to
  2…480 (`:330-334`); with duration 0 that is 1 000+ px/s → 480, and nothing re-fits when the duration
  later grows from 0.
- **Cause:** `app/widgets/Timeline.cpp:741-747`.
- **Recommended fix:** re-run the first fit when the duration goes from 0 to > 0 (and on a project or
  version switch), unless the user has zoomed since. The web face (`web/js/cut/timeline.js`) keeps the
  native behaviour until then; a ui test: an empty project, a clip added, `ppsTarget() == fitPps()`.

### D-16 — The scrubber thickens when the CAPTURE button is hovered, not when the scrubber is
- **Area:** ui · **Status:** Open · **Severity:** S4 · **Found:** 2026-10-27, porting Transport to the
  web face (R-SVC-4 amended).
- **Reproduce:** Cut tab; hover the scrubber's track → it stays 3 px; hover the camera button → the
  track grows to 4 px and the thumb to 5.5 px.
- **Expected:** the track and thumb grow under the pointer ON THE SCRUBBER (the comment at
  `Transport.cpp:290`, "hover is already 1 under a press").
- **Evidence:** `handleGesture` hovers the scrubber as id `kButtons` = 4 (`Transport.cpp:139`) since
  the capture button became button 3 (R-UI-11, `kButtons = 4`, `:15`); `onPaint` still reads
  `mHover.amount(3)` for the scrubber (`:289`) — the capture button's.
- **Cause:** `app/widgets/Transport.cpp:289` (an id not renumbered when a button was added).
- **Recommended fix:** a named id for the scrubber (`kScrubId = kButtons`) used by both; a ui test that
  hovers the track and reads the thumb radius. The web face (`web/js/edit/transport.js`) keeps the
  native behaviour and cites this entry, so the two faces agree until the fix.

### D-15 — A command refused on Home or Loading is said where nobody can see it
- **Area:** ui · **Status:** Open · **Severity:** S3 · **Found:** 2026-10-26, porting App.cpp to the
  web face (R-SVC-4 amended).
- **Reproduce:** on Home, click a recent project whose file was moved away (the card dispatches
  `project open <path>`); or type nothing else — any refusal while `screen` is Home or Loading.
- **Expected:** the refusal is SAID (R-G-4: "a refusal is said, never dropped"), on the screen showing.
- **Actual:** nothing visible happens.
- **Evidence:** `App::dispatch` (`app/App.cpp:521-529`) always calls `mEdit->showRefusal(...)`; the
  chip is EditScreen's overlay, drawn at the Edit screen's opacity, which is 0 on Home and Loading
  (`App::screenOpacity`, `App.cpp:501-509`). HomeScreen and LoadingView have no refusal display.
- **Judgement:** defect (the requirement holds; the chip was placed on one screen).
- **Cause:** `app/App.cpp:527`.
- **Requirement:** R-G-4.
- **Recommended fix:** a refusal chip the App owns, drawn over whichever screen is active (the same
  look: popover, destructive border, the warn glyph), or one per screen. The web face shows the
  refusal on every screen meanwhile (its shell's toast), and says so in `web/js/shell.js`.
- **Guarded by:** — (a `ui` test: dispatch a refused line on Home, the chip's amount reaches 1 over Home).

### D-2 — Cosmo's save deletes an offline source and its grade (Cosmo D-66), and D-1 makes every video offline in Cosmo
- **Area:** rack / cross-app · **Status:** Open — **mitigated** here, root cause in Cosmo ·
  **Severity:** S1 · **Found:** 2026-10-01, binding the rack's `.cmp` entries to `#rackobj`.
- **Reproduce:** `apps/cosmo/docs/DEFECTS.md` D-66 (a pasteable cosmo-cc script).
- **Actual:** Cosmo's `saveWorkspaceAs` skips every image without a slot, so an offline source — and
  its params and history — is deleted from the `.cmp` on the next save. Cosmo's `add` saves on finish,
  and Interstellar's `project save`, `rack frame`, `rack duplicate`, `timeline pin` and `rebase` all
  make Cosmo save.
- **Compounding:** with D-1 (Cosmo alone cannot decode a video), opening an Interstellar rack in Cosmo
  and saving it there deletes **every video source's grade**. R-RACK-2's "open it in Cosmo" path is
  therefore read-only in practice until either is fixed.
- **Mitigation (this build):** `InterstellarService::rackSaveBlocked` refuses each command that would
  make Cosmo save while any rack node is offline, naming the offline sources and this defect. The
  `.isp` still saves. Guarded by `a save is refused while a source is offline` (`interstellar_service_l2`).
- **Recommended fix:** Cosmo D-66 (keep and re-write failed entries). Then D-1's shared host decoder.

### D-1 — Cosmo, opened by itself, shows a video source as failed
- **Area:** rack / cross-app · **Status:** Open · **Severity:** S3 · **Found:** 2026-10-01, running
  P1's gate.
- **Reproduce:**
  ```bash
  interstellar-cc rack new look.cmp : rack add a.png "clip.mp4#t=2.0" : rack set 2 exposure=0.35 : rack save
  cosmo-cc project look.cmp --print | grep clip
  ```
- **Expected:** R-RACK-2 — "open that `.cmp` in Cosmo and the edit is there."
- **Actual:** `node=3 … kind=failed slot=-1 … name=clip.mp4#t=2.0`. The grade **is** in the file
  (`exposure=0.35 temp=5200`) and round-trips; Cosmo cannot display the source's pixels.
- **Cause:** the video-aware decoder is installed by Interstellar's host through
  `CosmoService::setDecoderFactory`. Cosmo's own host installs `NativeImageDecoder`, which has no
  video path.
- **Judgement:** requirement gap, not a code defect in either app. R-RACK-3 promised Cosmo needs no
  change *to grade* a video source inside Interstellar — true — but R-RACK-2 implicitly promised Cosmo
  could *show* one on its own, which needs Cosmo's host to carry a video decoder too.
- **Recommended fix:** move `VideoFrameDecoder` + `FrameSourceFFmpeg` to a shared host-layer library
  both apps link, and have cosmo's host install it. **Owner: `arstro.cosmo.core.implement`**, in
  Cosmo's own commit and requirements. **Do NOT** teach `cosmo_core` itself about video — the decoder
  belongs at the host, which is the only layer allowed a codec.
- **Requirement:** R-RACK-2 needs a sentence scoping it; amended when the fix lands.

## Closed

### D-14 — An autosave written in the same clock tick as its file was not offered
- **Area:** service · **Status:** Closed (same commit) · **Severity:** S3 (a script that saves, edits and autosaves
  at once loses the recovery; a person cannot type that fast) · **Found:** 2026-10-21, `interstellar_service_l2`
  failing 2 runs in 4 alone and most runs under `ctest -j8`, while landing Solaris work.
- **Reproduce:** `project save`, `set a.basic.exposure=0.9`, `project autosave` with no pause, then a new session's
  `project open` → `recoveryAvailable` false.
- **Expected:** R-DLV-6 — an autosave written after the file is offered.
- **Actual:** the autosave and the file share a modification time (the kernel stamps files from a coarse clock, a
  tick of milliseconds), and `newer` asked for strictly later.
- **Cause:** `newer` compared `ta > tb` (`core/service/ServiceSafety.cpp:54`).
- **Fix:** a tie is newer — a save and a deliberate close remove the autosave (DR-DLV-1), so one beside the file was
  written after it. Guarded by `interstellar_service_l2` (recovery, discarded): the autosave's time set to the file's,
  red with `>` on every run.

### D-13 — Clearing one curve deleted the next curve's keys
- **Area:** model · **Status:** Closed (same commit) · **Severity:** S1 (silently loses another
  parameter's animation) · **Found:** 2026-10-07, while building R-ANIM-10: a tracked window's
  centre, cleared and marked again, came back with 25 old keys.
- **Reproduce:** animate `a.basic.exposure` and then `a.basic.contrast` (two keys each), then
  `key clear a.basic.exposure`. Contrast's curve is still listed but has no keys, and exposure's
  two keys stay in the file as orphans; the next curve to get the id `an_1` inherits them.
- **Expected:** R-ANIM-1 — one curve per parameter, and clearing one touches no other.
- **Cause:** `Project::dropAnim(const NodeId &animId)` was called as `dropAnim(a->id)`, a reference
  into `anims` itself. Erasing the curve moved the next one into its slot, so the key erase that
  followed read the NEXT curve's id (`model/Project.cpp:476`, since DR-ANIM-1).
- **Fix:** `dropAnim` copies the id before erasing. Guarded by model `animModes` (two curves, the
  first dropped by its own id: its keys gone, the other's kept), which is red with the copy made a
  reference again.

### D-12 — A scrolled effect panel paints its section headers over the plugin list
- **Area:** ui · **Status:** Closed (same commit) · **Severity:** S4 (cosmetic; the row under it
  stays clickable) · **Found:** 2026-10-05, looking at the `grade_lut_effect` shot at 1024×640.
- **Reproduce:** a node with three effects, the last one selected, at 1024×640: the panel scrolls
  to reveal its section and the first header is drawn over the list's last row.
- **Expected:** R6 (design rule) — content that scrolls is clipped to its container.
- **Cause:** `EffectPanel` set `clipToBounds`, which in Artboard clips the CHILDREN only
  (`Segment::renderContent`); the headers are the panel's own paint (DR-UI-16, commit 758dceb).
- **Fix:** the headers are painted inside `clipRect(0, 0, w, h)` (`app/widgets/EffectPanel.cpp`).
  Guarded by UI `LUTs in and out`: the plugin list is pixel-identical whether the panel below is
  scrolled or not — red with the clip removed.

### D-11 — A ProRes master said nothing about its colour
- **Area:** host · **Status:** Closed (same commit) · **Severity:** S2 (an untagged master in a
  colour tool: a reader guesses) · **Found:** 2026-10-05, probing R-COLOR-4's HDR renders.
- **Reproduce:** `render --timeline main --format prores --out r.mov`, then `ffprobe -show_entries
  stream=color_primaries,color_transfer,color_space r.mov` → `unknown` ×3 (H.264 in .mp4 said
  bt709).
- **Expected:** D-9's fix — "the stream tagged BT.709"; R-COLOR-4 — a render tagged with what it is.
- **Cause:** FFmpeg 4.4's ProRes encoder writes its frame header's colour bytes from each AVFrame,
  not from the encoder context, and the writer's frames never set them, so every ProRes frame said
  "unspecified"; a reader trusts the frame over the MOV's `colr` atom (which was right).
- **Fix:** each frame carries the encoder's primaries, transfer, matrix and range
  (`host/FrameWriterFFmpeg.cpp`). Guarded by `interstellar_render_codecs` (ProRes 422 bt709, ProRes
  PQ bt2020/smpte2084, P3 smpte432); with the fix removed those three go red. A `movflags=write_colr`
  change was tried first and proved unnecessary (the muxer writes `colr` whenever colour is set).

### D-10 — `render --quality` was parsed through a dangling pointer
- **Area:** service · **Status:** Closed (same commit) · **Severity:** S3 (undefined behaviour;
  happened to work) · **Found:** 2026-10-05, when the same pattern made `effect move --to 0` refuse
  a valid index.
- **Cause:** `std::strtol(c.flag("quality").c_str(), &end, 10)` — `Command::flag` returns a
  `std::string` BY VALUE, so the temporary dies at the end of the expression and `end` points into
  freed memory; the `*end` check that follows reads it (`core/service/ServiceRender.cpp`, R-RENDER-6,
  commit 118bf4b).
- **Fix:** bind the flag to a local string first, in both places. Guarded by L2 `effect move ef_2
  --to 0` (which failed with the pattern) and the render-spec refusals; the lesson is in the
  implement skill's gotchas.

### D-9 — Renders were converted to YUV with BT.601 coefficients and left untagged
- **Area:** render / host · **Status:** Closed (same commit) · **Severity:** S2 (wrong colour in a
  colour tool) · **Found:** 2026-10-02, reading `FrameWriterFFmpeg` for the user's "detail options
  for render".
- **Reproduce:** render a saturated flat field (`color=c=0xD83A1E`) to H.264, then decode it the way
  an HD player does (BT.709): the centre pixel reads (229,71,25) against the still's (216,59,30).
- **Expected:** R-RACK-2 / R-RENDER-5 — the colour Cosmo made is the colour delivered.
- **Cause:** `sws_getCachedContext` without `sws_setColorspaceDetails` converts with swscale's
  default BT.601 matrix, and the encoder context set no `color_primaries`/`trc`/`colorspace`, so the
  file did not say which matrix it used; HD players assume BT.709.
- **Fix:** BT.709 coefficients, full-range RGB in, video-range YUV out, and the stream tagged
  BT.709 (`host/FrameWriterFFmpeg.cpp`). Guarded by `interstellar_render_codecs` — the H.264 render
  decodes to the still's colour within 4; with the matrix removed it is 13 levels off.

### D-8 — `rack add --group <node>` is accepted and silently ignored
- **Area:** service · **Status:** Closed (flag removed) · **Severity:** S3 · **Found:** 2026-10-02,
  reading `rack duplicate` for the user's "duplicate source" request.
- **Reproduce:** `project new g.isp --res 320x180 : rack add a.mp4 : rack group new look --nodes a :
  rack add b.mp4 --group look : state print --json` → `b` has `parent: -1` (the top), not `look`.
- **Expected:** R-SVC-3 / implement-skill law 5 — "never accept a flag you do not honour".
- **Cause:** the grammar row declared `group=<node>` (`core/service/Command.cpp`, `rack add`) and the
  handler never read it; Cosmo has no command that moves a node into an existing group (it can only
  make a NEW group from a selection, `EditSession::createGroupFromSelection`).
- **Fix:** the flag is removed, so the line is refused naming it. Placing a node into an existing
  group — for `rack add`, a duplicated variant and drag-to-regroup — needs a Cosmo `Move` command
  first; recorded as a gap (R-UI "drag-to-regroup"). Guarded by the L2 variant test's
  `rack add … --group` refusal.

### Requirement gap (closed) — no multi-selection, no right-click menu on rack items (user request, 2026-10-02)
Cosmo selects a range with Shift-click and toggles with Ctrl-click, then groups the selection, and
offers a right-click menu on a photo (Add, Group Selection, Ungroup, Enable/Disable Filter, Rename,
Information, Delete). Interstellar's rack is single-select with no context menu. Recommended: R-RACK-8
(selection, Shift = range / Ctrl = toggle, in the rack tree and the filmstrip; Group Selection) and
R-UI-9 (cosmo's ContextMenu on rack rows and filmstrip cells).
Closed by R-RACK-8 and R-UI-9 (DR-RACK-8, DR-UI-10).

### D-3 — An MKV whose first timestamp is not zero plays back frozen on one frame
- **Area:** host / frame source · **Status:** Closed (fixed in the commit that adds
  `interstellar_host`) · **Severity:** S2 · **Found:** 2026-10-01, user report "can not playback
  mkv file".
- **Reproduce:** `ffmpeg -f lavfi -i testsrc2=size=640x360:rate=24:duration=4 -c:v libx264
  -output_ts_offset 3.5 offset.mkv`, cut it onto a timeline, `export-still --at 0`, `--at 1`,
  `--at 2` → all three PNGs hashed `803e44fe…` (one frame).
- **Cause:** `FrameSourceFFmpeg` turned a timestamp into a frame index as `pts × timebase × fps`,
  from timestamp 0 rather than the stream's `start_time`, and sought to `frame / fps` likewise; every
  frame before the first timestamp (3.5 s × 24 = 84 of them) decoded as the first frame. The frame
  count also came from the container's declared duration, which for such an MKV includes the offset
  (180 frames for 96). Cameras, OBS and stream recorders write such files; mp4s with an edit list too.
- **Fix:** frame 0 is the stream's first timestamp — `mStart`, subtracted in `indexOf` and added in
  `seekTo` (`host/FrameSourceFFmpeg.cpp:70`, `:131`); a stream with no frame count is MEASURED
  (`probeEndPts`, `:89`: last keyframe → last packet end) instead of trusting a declared duration.
- **Guarded by:** `interstellar_host` — mp4, mov, mkv (H.264, HEVC 10-bit, VP9), 29.97, and two
  offset files: every sequential frame differs from the last, five seeks land on the frame
  sequential decoding produced. Red before the fix (offset.mkv: 180 frames), green after; the
  offset file's 0 s and 2 s now hash equal to the plain encode's frames 0 and 48.

### D-4 — Choosing a reference frame reloaded the whole rack
- **Area:** service / rack · **Status:** Closed (same commit) · **Severity:** S3 · **Found:**
  2026-10-01, user report "when I choose a ref frame it reloads again".
- **Cause:** `rack frame` saved the `.cmp` and re-opened it so Cosmo's slot would decode the new
  frame — every source re-decoded and showed its spinner, for pixels nothing in Interstellar reads
  (the monitor, filmstrip and renders all decode the source themselves).
- **Fix:** `rack frame` updates the `.isp` and the frame selector and reloads nothing; Cosmo's slot
  picks the new frame up at the next rack load. Also lands on a frame of the SOURCE's rate.
- **Guarded by:** `choosing a reference frame reloads nothing and the Grade monitor shows it`
  (`interstellar_service_l2`) — confirmed red with the reload put back.

### D-5 — MKV preview is "really bad": scrubbing and the reference strip freeze the window
- **Area:** render / host / ui · **Status:** Closed (fixed in the commit after `bc31c06`) · **Severity:** S2 · **Found:**
  2026-10-02, user report ("seeking frame take forever"; `[matroska,webm] File is broken, keyframes
  not correctly marked!`).
- **Reproduce:** an OBS-like stream — one keyframe every 10 s:
  ```bash
  ffmpeg -f lavfi -i testsrc2=size=1920x1080:rate=60:duration=60 -c:v libx264 -preset veryfast \
    -g 600 -keyint_min 600 -sc_threshold 0 -bf 2 -pix_fmt yuv420p obs.mkv
  cmake --build build --target interstellar_source_bench
  build/apps/interstellar/host/interstellar_source_bench obs.mkv
  ```
- **Actual:** `scrub 8 seeks 1043.9 ms (worst 194.2 ms)`, `4 thumbnails 470.8 ms (117.7 ms each,
  fresh source each)` at 1080p; 4K is ~4× that. Every one of those runs ON THE UI THREAD: the
  monitor's `renderFrame` decodes synchronously per scrub step, and the Grade deck asks the
  `thumbnail` hook for ~13 reference-strip stills + one per filmstrip cell in a single frame, each a
  FRESH `HostFrameSource` (open + seek + decode from the previous keyframe).
- **Cause:** (1) `InterstellarService::renderFrame` (`core/service/ServiceRender.cpp`) renders on
  the caller's thread; (2) `interstellar_host::Thumbnailer::get` (`host/Thumbnailer.cpp`) decodes
  synchronously with a new decoder per call; (3) a seek in a long-GOP stream decodes every frame
  from the keyframe before the target, and `FrameSourceFFmpeg` seeks for any jump > 24 frames even
  inside the current GOP, where decoding forward is cheaper.
- **The FFmpeg message** is the Matroska demuxer saying the frame a seek landed on is not flagged as
  a keyframe — the file's cues or keyframe flags are off (common in captures). Decoding stays
  correct (checked: the frame at 30 s of an intra-refresh MKV matches `ffmpeg -ss 30`); the message
  is noise on stderr, not the slowness.
- **Requirement:** R2 of `arstro.design.rule` (a visible response this frame; heavy work off the UI
  thread), R-VOL.
- **Recommended fix:** preview frames rendered on a worker (plan on the UI thread, decode + grade +
  compose on the worker, latest request wins, the monitor keeps the last frame until the next is
  ready); thumbnails decoded on a host worker with one persistent decoder per file and a signal
  the app re-asks on; seek only when the target is beyond ~1.5 s ahead; quiet FFmpeg's demuxer log.
  Guard: an L2 test that the async monitor returns immediately and delivers the frame after a pump.
- **Fixed:** the monitor renders on a worker — the UI thread PLANS a frame (resolve, grades, frame
  indices, geometry; `planFrame`, `core/service/ServiceRender.cpp:191`), the worker decodes, grades
  and composites it (`executePlan`, `:348`; `previewLoop`, `:433`), the latest plan wins, and
  `renderFrame` (`:401`) answers at once with the newest finished frame; `pump` raises `frameSeq`
  when the requested one lands. Thumbnails decode on the host's `Thumbnailer` worker with one
  persistent decoder per file (`host/Thumbnailer.cpp:81`, `:107`) and the app re-asks on
  `AppHooks::thumbnailEpoch`. Forward targets within ~1.5 s decode on instead of seeking
  (`host/FrameSourceFFmpeg.cpp:211`); FFmpeg's stderr is set to fatal-only.
- **Guarded by:** `the async monitor answers at once and delivers the synchronous pixels`
  (`interstellar_service_l2`). Measured after: the slowest UI frame of the D-6 open is 38.5 ms
  (was 973 ms); thumbnail hook calls cost 1 ms in total on the UI thread.

### D-6 — The loading screen stalls just before the project appears
- **Area:** ui / host · **Status:** Closed (same commit) · **Severity:** S3 · **Found:** 2026-10-02,
  user report ("when almost loaded, the loading indicator lags").
- **Reproduce:** a 7-source project of the D-5 stream; `interstellar_live_shots --project p.isp
  --outdir shots --time 1`.
- **Actual:** `frames 65, total 1341 ms; monitor frames 1 in 19 ms; thumbnails 19 in 950 ms;
  slowest: 973.1 ms edit @224ms` — the FIRST Edit frame takes 973 ms, mid cross-fade from Loading,
  because the Grade deck decodes 19 thumbnails synchronously in it. The service side is not it:
  `interstellar_open_bench` shows the slowest pump at 0.6 ms (rack.loaded).
- **Cause:** D-5 (2) — synchronous thumbnails on the UI thread.
- **Recommended fix:** D-5's asynchronous thumbnails; the cells fade their picture in when it lands.
- **Fixed:** by D-5's asynchronous thumbnails; late stills fade in (strip frames, Home covers).
  `interstellar_live_shots --time 1`: `slowest: 38.5 ms edit @240ms` (was `973.1 ms`).

### D-7 — A GROUP's grade-weight bar does nothing
- **Area:** rack / render / ui · **Status:** Closed (same commit) · **Severity:** S3 · **Found:**
  2026-10-02, from the user's question "what is the slider inside an item in the rack for?"
- **Reproduce:** `project new gw.isp --res 320x180 : rack add w.mp4 : rack group new G --nodes w :
  set g.basic.exposure=1.5 : … : export-still --at 1` with `g.weight` 1 then 0 → both stills hash
  `9971a468…`; `w.weight=0` → `080bff89…`.
- **Cause:** the frame path applies only the clip SOURCE's weight (`mixWeight` of `ro->weight` in
  `ServiceRender.cpp`); a group's weight is stored, shown as a bar, and never read.
- **Requirement:** R-RACK-4 says "a node carries a continuous grade weight".
- **Recommended fix:** a group's weight fades its OWN contribution to its members — apply it in the
  fold (the source's pixels mix ungraded→graded with the product of weights up its chain is
  simplest and matches "a continuous bypass"). And label the bar: it is unexplained in the UI.
- **Fixed:** a group with weight < 1 fades its OWN contribution: the frame is graded with those
  groups bypassed (Cosmo's bypass, exactly — `gradeForBypassing`, `ServiceRender.cpp:158`) and with
  them on, and the two mix by the product of their weights before the source's own weight applies.
  Weight 0 renders byte-identical to the group bypassed. Guarded by `a group's weight fades the
  group's own contribution` — red with the mix disabled.

