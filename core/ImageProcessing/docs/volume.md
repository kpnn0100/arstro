# Volume engine + temporal ops (R-VOL-1..7, R-FX-2)

Lives in `core/ImageProcessing/src/volume/` (part of `arstro_image`). Built in a staging directory
by a parallel agent and moved here at integration, 2026-10-01; this file is that agent's notes,
kept because the measurements and decisions below are the as-built record.

```
cmake --build build --target volume_tests volume_bench
ctest --test-dir build -R '^volume$'
build/core/ImageProcessing/volume_bench        # not a test — ms per output frame for the ledger
```

The view is `const uint8_t *` straight RGBA8, always 4 channels, and the extent type is
`VolumeExtent` — `apps/interstellar/docs/architecture.md` §3 is synced to this. A radius-2 window
at 1080p is 41 MB of RGBA8 (the float figure of 125 MB in the first sketch is for a layout that was
not built).

## As-built API (namespace `arstro`)

Everything in the task contract is present with the contract's signatures. Items marked *(added)*
are extras.

`Volume.h`

| item | notes |
|---|---|
| `struct FrameRGBA { std::vector<uint8_t> rgba; int width, height; }` | Straight RGBA8, tightly packed: the decoder's own layout |
| `struct TemporalFootprint { int before, after; }` | In source frames. `{0,0}` means point/spatial |
| `uniteFootprints(a, b)` *(added)* | Takes the larger reach on each side. Correct for ops that all read the **source**. Ops that read each other's output need footprints **added** instead |
| `struct VolumeExtent { int width, height; long long frames; double fps; }` | |
| `struct VolumeView` | Contract fields plus `kChannels = 4` *(added)*, and `kMaxWindow = 33` (radius 16) |
| `class Volume { extent(); window(t0, t1, out); }` | |
| `class CachedVolume : Volume` | `CachedVolume(ext, Provider, capBytes)`, `residentBytes()`, `decodes()`, plus *(added)* `residentFrames()`, `capBytes()`, `frameBytes()`, `clear()` |

`TemporalOps.h`

| item | notes |
|---|---|
| `class TemporalOp { footprint(); process(view, centre, out); }` | |
| `kMaxTemporalRadius = 16` *(added)* | |
| `TemporalDenoise(int radius = 2, float strength = 0.5f)` | `setRadius`/`setStrength` clamp the values; `kGateAtFullStrength = 48` |
| `FrameBlend(int radius = 1)` | `setRadius` clamps the value |
| `freezeRemap(t, freezeAt)` | Inline: `t < freezeAt ? t : freezeAt` |
| `temporalWindow(vol, t, fp, out, centre)` *(added)* | The driver's window step, exposed so a chain can unite its footprints and take one window |
| `renderTemporal(vol, op, t, out)` | |

## Decisions made inside the contract

1. **`window(t0, t1)` is inclusive at both ends.** `frames = t1 - t0 + 1`, so footprint `{b, a}`
   around `t` is just `window(t-b, t+a)`.
2. **`window()` is strict and does no clamping.** It refuses `t0 < 0`, `t1 >= frames`, `t1 < t0`,
   more than 33 frames, or a window bigger than the cap. When it refuses it returns false and
   leaves `out` as an empty view. The edge policy lives in the driver, so a caller bug that walks
   off the clip fails loudly instead of quietly repeating the last frame.
3. **Edge policy: the edge frame is replicated.** `temporalWindow` takes the part of the
   footprint that exists as a strict window, then builds a padded view of exactly `b + 1 + a`
   slots. Slots outside the clip point at the edge frame's cache buffer, so the padding is
   pointers only and costs nothing.
   - The centre slot is always `before`.
   - `t0` in a padded view may be negative. `frame[i]` holds source frame
     `clamp(t0 + i, 0, frames - 1)`.
   - Consequence: near a clip end the edge frame carries more weight. For example, FrameBlend r2 at
     `t = 0` is `(3·f0 + f1 + f2) / 5`.
   - Ops also clamp their taps into whatever view they are given, so a narrower or hand-built view
     replicates the same way instead of reading past the array.
   - A centre `t` outside the clip is refused (false). Only neighbours are clamped.
4. **Lifetime rule:** a view is valid until the next `window()` call on the same volume, or until
   the volume is destroyed. `window()` resets `out` before doing anything else, so a failed call
   also ends the previous view.
5. **Residency is exactly the current window.** After `window(t0, t1)` the resident set is
   `[t0, t1]` and nothing else. That makes it bounded by the footprint and independent of clip
   length (R-VOL-4).
   - The only eviction happens before the first decode, and only for frames outside the new
     window. So the cache can never evict a frame inside the window it is building or handing out.
   - Missing frames are decoded in ascending order, so a forward walk decodes each frame exactly
     once and never makes a sequential decoder seek.
   - The buffer of a frame leaving the window is **recycled**: the entering frame is decoded into
     it. That avoids an munmap/mmap plus page faults per output frame (8 MB at 1080p, 33 MB at 4K).
   - Peak residency during a call is max(previous window, new window).
6. **The cap is a hard refusal, not a soft LRU budget.** A window larger than `capBytes` makes
   `window()` return false. It never overshoots, so the published bound is a real number.
   `residentBytes()` counts buffer **capacity**, which is real memory.
7. **TemporalDenoise weighting: a difference-gated temporal average. It is not motion-compensated.**
   ```
   d   = (|ΔR| + 2|ΔG| + |ΔB|) / 4        code values, Δ = neighbour − centre, per pixel
   h   = strength · 48                     the gate in code values (rounded to 0.25)
   w_n = max(0, 1 − (d/h)²)²               Tukey biweight; the centre has weight 1
   out = (centre + Σ w_n·n) / (1 + Σ w_n)  per RGB channel, accumulated in float
   ```
   - One distance per pixel, so all channels get the same weight and hue cannot shift.
   - Absolute differences are summed before weighting, so an isoluminant change still counts.
   - The biweight's compact support means an edge with more than 48/255 contrast contributes
     **exactly** zero: no faint ghost.
   - Alpha is copied from the centre frame. `strength = 0` or `radius = 0` gives an exact copy.
   - The loop computes the same weight in integer units as `((G−m)(G+m)/G²)²` with
     `m = min(4d, G)` and `G = round(4h)`. This keeps the clamp branch-free so GCC vectorises the
     loop: one float `max()` had kept it scalar and cost about 2x (see the measurements).
8. **FrameBlend** is the plain mean of the 2k+1 frames, on all four channels. It sums exactly in
   `uint16` and rounds to nearest. With an odd tap count the mean is never exactly .5, so the
   result is the exact rounded mean, and the test asserts it with zero tolerance.
9. **Freeze is "play, then hold"** (`min(t, freezeAt)`), like an NLE's "add frame hold".
   - Remap `t` **before** taking the window. A held frame that is also denoised then uses its real
     neighbours.
   - The held window stays resident, so a held stretch costs no decodes after its first frame
     (tested).
10. **Colour domain:** ops work on the decoder's 8-bit code values, i.e. source-space and ungraded
    (R-VOL-5).
11. **Not thread-safe:** one renderer drives one volume. Each op is internally parallel over row
    bands with `par::parallelFor`, and every band writes only its own output rows, so the output
    is byte-identical however the bands are scheduled.

## Test output (Release, `-O3 -DNDEBUG`; asserts are live because NDEBUG is undefined before `<cassert>`)

```
PASS  test_window_returns_right_frames_and_t0
PASS  test_window_refuses_what_it_cannot_serve
PASS  test_driver_clamps_at_both_clip_ends
PASS  test_residency_bounded_by_footprint_not_clip_length
PASS  test_forward_walk_decodes_each_frame_once
PASS  test_cache_never_evicts_inside_the_window
PASS  test_residency_is_invisible
      static noise stddev: input 7.999 -> radius 2: 3.605 (x2.22), radius 4: 2.700 (x2.96)
PASS  test_denoise_reduces_noise_on_static_sequence
      moving square: trailing ghost denoise -0.040, leading -0.095, frame-average 54.005; square interior 219.98 (true 220)
PASS  test_denoise_does_not_smear_moving_square
PASS  test_frame_blend_equals_analytic_mean
PASS  test_freeze_remap
volume_tests: 11 passed (assertions live)

ctest -R volume:  1/1 Test #1: volume ...........................   Passed    0.04 sec
```

What the tests assert:

- **Frame index in the pixels.** The synthetic provider writes R,G = frame index, B = x, A = y, so
  every window and clamping check is an exact integer comparison.
- **Clamping:** exact slot sequences at both clip ends, e.g. `t = 0` gives `{0,0,0,1,2}` and
  `t = 99` gives `{97,98,99,99,99}`. Also covers an asymmetric `{3,1}` footprint and a one-frame
  clip.
- **Residency:** a 1000-frame walk with footprints {2,2}, {4,4} and {3,0}. After every frame,
  residency is ≤ the cap (64 frames) **and** ≤ (before+after+1)×frameBytes. Peak residency is
  identical on a 1,002-frame clip and a 10¹²-frame clip.
- **Decodes:** a forward walk gives `decodes() == 1000` and a provider call log of exactly
  `0, 1, …, 999` in order. This holds with the cap at one window and at 64. A backward walk also
  decodes 1000. One step back costs exactly one decode.
- **Never evicts inside the window:** with the cap at exactly one window, `window(8,12)` then
  `window(10,14)` decodes only 13 and 14. Frames 10–12 come back as the **same buffer pointers**,
  i.e. no copy and no re-decode.
- **Denoise, static scene:** σ = 8 noise. The measured stddev must drop below 0.55× at radius 2
  (√5 bound: 0.447; measured 0.451) and below 0.40× at radius 4 (bound 0.333; measured 0.338).
  Bias must be under 0.5 code values.
- **Denoise, moving square:** a 220-on-40 square moving 4 px/frame. The trailing and leading
  ghosts must stay under 2 code values (measured −0.04 and −0.10). The same probe on FrameBlend r2
  must read over 30 (measured 54.005; analytic value 54), which shows the probe can fail. The
  square's interior must stay within 2 of 220.
- **FrameBlend:** exact rounded analytic mean for k = 1, 2, 3, at interior `t` and at both
  replicated clip ends, on all four channels.
- **R-VOL-7:** frame 120 rendered after a 120-frame walk is byte-identical to frame 120 rendered
  on a fresh volume, and to frame 120 reached by a jump.

### I checked that the tests fail when the code is broken

I applied each break to the source, built it into a separate mutant build directory, ran the
suite, and restored the file. `diff` against the backup confirmed the restore was byte-identical.
This was done on the final kernel.

| mutant | first failing assertion |
|---|---|
| A. Cache disabled (every window drops all resident frames) | `test_forward_walk_decodes_each_frame_once`: `vol.decodes() == N` |
| B. Denoise returns the centre frame | `test_denoise_reduces_noise_on_static_sequence`: `outSd < 0.55 * inSd` (stddev 7.999 → 7.999) |
| C. Gate removed (every weight 1, i.e. a plain average) | `test_denoise_does_not_smear_moving_square`: `fabs(dnTrail) < 2.0` (ghost 54.0, interior 166) |

## Measurements (Release `-O3`, x86-64 baseline: SSE2 only, no `-march`)

- **Machine:** AMD Ryzen 9 9900X, 12 cores / 24 threads, 64 MB L3.
- **`par::threads()` = 24** (auto, from `hardware_concurrency`).
- The load average was about 5–9.5 during the runs because other work was running on the
  checkout, so the numbers are slightly pessimistic.
- The tool is `volume_bench`. The op is timed on a view pointed at pre-generated noisy frames, so
  **provider cost is excluded**. Each figure is the median of 60 runs (30 at 4K) after warm-up. The
  bench was run twice.

| op | size | median ms/frame, run 1 | median ms/frame, run 2 | min |
|---|---|---|---|---|
| **TemporalDenoise r2** | 1920×1080 | **1.35** | **1.37** | 1.25 |
| **TemporalDenoise r2** | 3840×2160 | **5.69** | **5.90** | 5.45 |
| **FrameBlend r2** | 1920×1080 | **0.54** | **0.55** | 0.47 |
| FrameBlend r2 | 3840×2160 | 4.58 | 4.05 | 3.78 |
| TemporalDenoise r2, 1 thread | 1920×1080 | 13.68 | 13.98 | 13.56 |
| FrameBlend r2, 1 thread | 1920×1080 | 2.50 | 2.40 | 2.33 |

Full path: a `CachedVolume` forward walk where the provider is a memcpy from a pool, with the
provider timed separately (run 1 / run 2).

| op | size | total ms/frame | provider | cache + op | decodes |
|---|---|---|---|---|---|
| TemporalDenoise r2 | 1080p | 1.77 / 1.69 | 0.44 / 0.42 | 1.32 / 1.27 | 58 / 60 frames |
| TemporalDenoise r2 | 4K | 7.33 / 7.21 | 1.76 / 1.69 | 5.57 / 5.52 | 28 / 30 frames |
| FrameBlend r2 | 1080p | 1.06 / 1.14 | 0.42 / 0.44 | 0.63 / 0.70 | 58 / 60 frames |

What this table shows:

- "cache + op" matches the op-only figure, so the cache's own bookkeeping cost does not show up
  in the measurement.
- The decode count is one per new frame. The last two windows of the walk clamp at the clip end,
  so they need no new decode.

Caveats on these numbers:

- **1080p is helped by the L3 cache.** The radius-2 working set at 1080p is 50 MB, which fits in
  this CPU's 64 MB L3. FrameBlend's implied 90 GB/s is more than DRAM can deliver. In a real
  forward walk, 4 of the 5 input frames were read for the previous output frame, so that reuse is
  realistic **on this CPU**. It is not realistic on a board without a large L3 such as an RK3588.
  The 4K figures (199 MB per output frame, about 35–49 GB/s) are the honest DRAM-streaming
  numbers.
- **Before vectorisation:** the first build's denoise loop was scalar. It measured 2.53 ms at
  1080p, 11.41 ms at 4K, and 24.05 ms single-threaded at 1080p. FrameBlend was 0.94 ms at 1080p
  (its output loop was scalar). The loop fixes are described in decision 7 and in
  `TemporalOps.cpp`. The fixes were locals instead of by-reference captures, because stores
  through `uint8_t*` alias everything, plus integer min/abs instead of a float clamp.
- No AVX: the library builds for baseline x86-64. A `-march=x86-64-v3` build would likely widen
  the vectors, but that is a project-wide flag decision and was not taken here.

## Known limits

- **Denoise is not motion-compensated.**
  - Moving objects keep their own noise, because their neighbours are gated out.
  - Motion with contrast below the gate (48/255 at full strength) ghosts partially.
  - The gate is referenced to the centre pixel, so impulse noise on the centre survives: every
    neighbour is gated out. A pre-filtered or median reference would fix this.
  - Optical-flow alignment before the gate is the real fix and is not in v1.
- **FrameBlend averages code values, not linear light.** This follows the NLE "frame blending"
  convention. A physical shutter integrates linear light, so moving highlights would come out
  brighter than they do here. Straight-alpha translucent sources would need a premultiplied mean;
  decoded video is opaque.
- **No retention beyond the window.** One step backwards re-decodes one frame, which is a seek on
  a sequential decoder. An LRU retention allowance under the cap would be a small addition, but
  the brief asked for residency bounded by the footprint, so it was left out.
- **Not thread-safe.** Use one volume per rendering thread, or serialise access externally.
- **`uniteFootprints` is only right for ops that read the source.** Chained temporal ops, where
  one reads another's output, need footprints added, and their intermediate frames are not cached.
- **The window is capped at 33 frames** (radius 16). A wider footprint is refused, not truncated.
- **`extent().frames` must be known up front.** There is no streaming source of unknown length.
- **The cap must hold the largest window.** A smaller cap fails the render (by design) instead of
  degrading it.

## Deviations

- **From the task contract:** none. Every signature is as specified; additions are listed above.
- **From `architecture.md` §3:** the pixel type is `uint8_t` RGBA8 rather than `Pixel` float with
  a `channels` field, because the task contract fixes RGBA8 to match decoder output. The doc needs
  updating (see Integration).
