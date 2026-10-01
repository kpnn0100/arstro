# Interstellar — Defect list

`D-<n>`, sequential, **never reused**, nothing deleted. A resolved entry moves whole to `## Closed`

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

