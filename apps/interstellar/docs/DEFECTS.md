# Interstellar — Defect list

`D-<n>`, sequential, **never reused**, nothing deleted. A resolved entry moves whole to `## Closed`
with its commit hash and the test that now guards it.

**No defects — there is no code.** The numbering restarts at D-1 with this specification; the first
build's D-1…D-8 are in git history at `69b91eb` and are not carried forward as ids, because an id
that pointed at deleted code would be worse than no id.

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

*(none)*
