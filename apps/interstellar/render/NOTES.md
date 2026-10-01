# interstellar_render — as-built notes

This directory holds the per-frame render path for Interstellar: which clips are live at time t
(`ActiveSet`), the grade step (`GradeEngine` over `arstro::EditEngine`), the frame cache
(`FrameCache` + `ParamHash`), and the composite (`Composite`). It does not depend on the project
model. Every input is a plain struct, so a render stays a pure function of what it is given
(R-RENDER-2), and the code can be tested and measured while the model is still changing.

Everything is in namespace `arstro::interstellar::render` (see Deviations, item 1).

## Build and run

```sh
B=<scratch>/build-render
cmake -S apps/interstellar/render -B $B -DCMAKE_BUILD_TYPE=Release
cmake --build $B -j --target interstellar_render_tests interstellar_render_bench
$B/interstellar_render_tests                       # or: ctest --test-dir $B -R interstellar_render
$B/interstellar_render_bench all --frames 31       # composite | grade | stages | loop-* (for perf)
```

There are two ways to use the CMakeLists:
- **Standalone.** It runs `project()` and `enable_testing()`, then pulls `arstro_image` in itself
  through `add_subdirectory(../../../core/ImageProcessing)`.
- **From an umbrella `add_subdirectory`.** `project()` and `enable_testing()` are skipped, and so is
  `arstro_image` if a target with that name already exists.

Both modes were checked: the umbrella mode with a scratch top-level CMakeLists that adds
ImageProcessing and then this directory, which built and passed `ctest`.

The build produces three targets:
- `interstellar_render`: a static library. Its public include directories are this directory,
  `../core` (for `Raster.h`) and `core/ImageProcessing/src`, and it links `arstro_image`
  publicly.
- `interstellar_render_tests`: registered with `add_test` as `interstellar_render`.
- `interstellar_render_bench`: not a test. It produces every number in this file.

## As-built API

### Composite.h
```cpp
struct Geom { double x=0, y=0, scale=1, rotation=0, anchorX=.5, anchorY=.5, cropX=0, cropY=0, cropW=1, cropH=1; };
enum class Blend { Normal, Multiply, Screen, Overlay, Add, Subtract, Difference };
enum class Fit   { Contain, Cover, Stretch, None };
struct Layer { const Raster *src=nullptr; Geom geom; Fit fit=Fit::Contain; double opacity=1; Blend blend=Blend::Normal;
               bool dissolveWithPrevious=false; };          // <- addition, see Deviations
double blendChannel(Blend mode, double base, double over);   // 0..1, the reference maths
void   placeLayer(const Layer &, Raster &out);               // out already allocated
void   compose(const std::vector<Layer> &bottomToTop, int w, int h, Raster &out);  // allocates + clears
```

The geometry is applied in this order: crop (normalised on the source), then fit, then
`scale` (which applies to **every** fit, including Stretch), then rotation about the anchor, then
translation.
- **Anchor.** Normalised on the *drawn* rectangle. The anchor point lands at the raster centre
  plus `(x, y)`.
- **Rotation.** In degrees. A positive angle turns clockwise on screen.
- **Blending.** Colour is composited over black, and alpha tracks coverage. Blend modes follow the
  W3C separable rule: `mixed = (1-αb)·over + αb·B(base, over)`. This means a blend-mode layer over
  an empty area shows the layer itself.

### ActiveSet.h
```cpp
struct ClipSpan       { std::string id; int trackOrder; bool audio; double at, in, out, speed; };
struct TransitionSpan { std::string clipA, clipB; double dur; bool linear = true; };
struct Active { std::string id; double localTime; long long sourceFrame; double weight; bool held; double progress;
                bool dissolveWithPrevious = false; };        // <- addition, see Deviations
std::vector<Active> activeAt(const std::vector<ClipSpan> &, const std::vector<TransitionSpan> &, double t, double fps);
```

**Time and source frame.**
- `localTime = (t - at)·speed + in`.
- `sourceFrame = floor(localTime·fps + 1e-6)`. The epsilon is Deviation 4.
- A clip is live on the half-open interval `[at, at + (out-in)/speed)`.
- `progress` is `(t - at)` divided by that duration. It goes above 1 while the clip is held.

**Transitions.**
- A transition starts at the incoming clip's `at` and lasts `dur`.
- The outgoing weight is `1-f` and the incoming weight is `f`, so the pair always sums to 1.
- `f` is linear. With `linear=false` it becomes smoothstep instead, and the sum is still 1.
- The outgoing clip is **held**: it stays live past its out-point, and its `localTime` runs on
  into the handles.

**Ordering.** The output is sorted bottom track first, then by `at`, then by `id`. Audio clips are
excluded.

**Clips and transitions that are ignored.**
- A clip with `speed <= 0`, with `out <= in`, or with a NaN field is ignored.
- A transition is ignored unless both of its clips are found among the video spans.

### FrameCache.h
```cpp
class FrameCache {
  static constexpr uint64_t kUngraded = 0;
  struct Key { std::string source; long long sourceFrame; uint64_t paramHash; int level; };
  struct Stats { size_t residentBytes, entries; uint64_t hits, misses, evictions; };
  explicit FrameCache(size_t capBytes = 512 MiB);
  void setCapBytes(size_t); size_t capBytes() const;
  bool get(const Key &, Raster &out);          // COPIES out, promotes
  void put(const Key &, const Raster &);       // a frame larger than the whole cap is not cached
  void invalidate(const std::string &source, long long fromFrame, long long toFrame);  // inclusive
  void clear();
  size_t residentBytes() const; size_t entries() const; uint64_t hits() const, misses() const, evictions() const;
  Stats stats() const;                          // one consistent snapshot
};
```

How it behaves:
- It is a byte-capped LRU, and it is thread-safe (one mutex).
- `put` copies the frame *before* it takes the lock.
- An oversize `put` also drops any older entry under the same key, because that entry is stale by
  definition. It never evicts other entries.
- The index is keyed on the `Key` struct. The first version built a string for every lookup.

### GradeEngine.h
```cpp
class GradeEngine {
  bool render(const Raster &in, const EditParams &p, bool hasParams, int longEdge, Raster &out);
  void releaseScratch();                        // <- addition: frees the engine's parked scratch
  static bool isIdentity(const EditParams &);   // serializeParams(p) == serializeParams(EditParams{})
};
```

- **Identity.** `!hasParams || isIdentity(p)` makes `out = in`, byte for byte, at the input size,
  even when a proxy edge was asked for.
- **Everything else.** It runs `EditEngine::fromEncodedBytes` and then
  `renderImage(linear, p, longEdge > 0 ? longEdge : max(w,h))`. This is Deviation 2.
- **Histograms.** Both intermediate histogram taps are switched off.
- **Threads.** It is not thread-safe. Use one instance per render thread.

### ParamHash.h
```cpp
uint64_t fnv1a64(const std::string &bytes);    // pinned to the published FNV-1a 64 test vectors
uint64_t hashParams(const EditParams &);       // fnv1a64(serializeParams(p)); never returns 0 (= kUngraded)
```

## Decisions (and why)

**Composite**

1. **One inverse affine per layer, a bounding box, and per-row analytic spans.**
   - All the trigonometry happens once per layer.
   - Only the layer's destination box is scanned.
   - Each row's covered run is solved from the two linear inequalities, then each end is checked
     against the exact "centre maps inside the crop" rule. Rounding therefore never decides
     coverage differently from the definition.
   - The inner loop has no coverage test, no per-pixel switch and no virtual call. There is one
     template instantiation per (blend, dissolve, sampling kind).
   - Rows are processed in parallel with `par::parallelFor`.

2. **Bilinear sampling, because it is cheap.** It uses 8-bit fixed-point weights and 32.32
   fixed-point coordinates. Taps are clamped to the crop rectangle, so a crop never bleeds in the
   pixels it removed (tested). There are three sampling kinds, and all of them give the bilinear
   answer:
   - **Copy:** unit scale, integer offset. It reads the source pixel itself.
   - **Axis:** scaled but not rotated. The x taps are computed once per layer into a table, and
     the y taps once per row.
   - **General:** rotated.

   A test compares every kind pixel by pixel against an independent double-precision bilinear and
   requires a difference of at most 1/255, with identical coverage.

3. **Alpha in 16 bits.** With 16 bits, the two weights of a dissolve still sum to 1 within
   1/65535. With 8 bits they could miss by 1/255.

4. **Quarter turns are snapped.** cos(90°) is 6e-17, not 0. Snapping makes 90°, 180° and 270°
   exact pixel permutations (tested).

5. **A zero-area crop draws nothing.** The first version clamped such a crop to 1e-6 and drew a
   one-pixel sliver stretched across the frame. NaN geometry also draws nothing.

**ActiveSet and FrameCache**

6. **Dissolve groups** (`dissolveWithPrevious`). See Deviation 1 for why they exist. For a group,
   `compose` snapshots the raster at the group's first layer and mixes every member against that
   snapshot: `base + Σ (mixᵢ − base)·αᵢ`. This costs one frame copy, and only on transition frames.

7. **Holding the outgoing clip** (R-TL-4) is done exactly the way the fixed version in `69b91eb`
   did it. A transition covering t forces the outgoing clip live and sets `held`.

8. **`hashParams` never returns 0**, so `FrameCache::kUngraded = 0` can key the ungraded source
   frames. The R-VOL-5 temporal volume runs over those frames, and graded layer frames can share
   the same cache.

## Deviations from the brief (each with its reason)

1. **Additions, not changes, to the contracted structs.**
   - **`Layer::dissolveWithPrevious` and `Active::dissolveWithPrevious`** (both default `false`;
     aggregate initialisation with the contracted fields still compiles). The weights summing to 1
     is not enough to make a dissolve right on screen:
     - **Stacked with ordinary over-compositing,** A at opacity `1-f` over black followed by B at
       `f` gives `A(1-f)² + Bf`. That is 75 % brightness at the midpoint, the dip through every
       cut that R-TL-4 exists to forbid.
     - **"A at 1, B at f"** fixes full-frame overlap. But where B does not cover (a letterboxed or
       picture-in-picture incoming clip), A stays at full strength and then pops off when the
       transition ends.
     - **A real dissolve** mixes both clips against the same base. `activeAt` sets the flag on the
       incoming entry whenever its outgoing partner is the entry immediately before it, so the
       caller only copies the flag across.
     - **Proof that the tests depend on it:** a mutant `compose` that ignores the flag fails the
       dissolve test.
   - **`GradeEngine::releaseScratch()`.** See item 2.
   - **`FrameCache::Stats`, `stats()`, `capBytes()` and `setCapBytes()`.**
   - **`fnv1a64()`.** It is exposed so a test can pin it to the published vectors. Pinning paid off
     during the build, when a dropped digit in the offset basis was caught at once.
   - **The namespace `arstro::interstellar::render`.** The old project model defined `Geom`,
     `Blend` and `Fit` in `arstro::interstellar`. If the model being written now does the same, a
     translation unit that includes both would fail to compile. The nested namespace avoids that.

2. **GradeEngine calls `renderImage`, not the slot sequence**
   (`clearImages → addImage → selectImage → applyParams → renderFull | setPreviewSize+renderPreview`).
   - **Same pixels.** The output is **byte-identical** to the slot sequence, both at full
     resolution and at a proxy edge. The test
     `grade: renderImage path is byte-identical to the slot sequence` pins this on every run, and
     the bench re-checks it at 1920×1080.
   - **Faster.** `renderFull` is the photo editor's one-shot export path, and it frees all of its
     scratch after every call. Per video frame that meant re-faulting about 200 MB:
     - 49K page faults per frame, against 18K;
     - 58–64 ms per 1080p frame, against 33–35 ms.
   - **Documented seam.** EditEngine's own source describes `renderImage` as "the seam a video
     editor reuses frame after frame at a FIXED size", and says `renderFull` "must not park"
     buffers.
   - **The cost.** About 200 MB of scratch stays parked between frames at 1080p (measured: RSS
     +206 MB after 5 full frames, 8 MB of which is the caller's output raster; back to +8 MB after
     `releaseScratch()` and `malloc_trim`). Peak RSS is about
     the same either way (256 MB vs 279 MB over 10 frames). `releaseScratch()` gives the parked
     memory back, and rebuilding the engine afterwards costs about 0.01 ms.
   - **Side benefit.** `renderImage` passes `p` straight to a GPU accelerator. The slot path would
     have used the slot's params, which `applyParams` never sets.

3. **`linear = false` means smoothstep.** The brief did not define the non-linear case, so I chose
   smoothstep. Its weights still sum to 1.

4. **`sourceFrame = floor(localTime·fps + 1e-6)`, not a bare floor.** A time of `t = k/fps`
   computed in floating point can land a hair *below* `k/fps`. A bare floor then reads frame
   `k−1`, which is a cut between frames (R-TL-5). The test checks that t = k/24 and t = k/29.97 read
   frame k exactly for k < 5000.

5. **Hostile inputs are ignored rather than half-applied.** This covers a clip with `speed <= 0`,
   one with `out <= in`, one with NaN fields, and a transition naming a missing clip. Reverse
   playback and freeze are temporal effects (R-FX-2), not the sign of a number.

## Test output (Release, `-O3 -DNDEBUG` with NDEBUG undone in the test TU)

```
[PASS] blend modes on known operands (reference + 8-bit kernels + backdrop rule)
[PASS] a half-scale Contain layer lands centred, corners untouched
[PASS] a 90 degree rotation puts a known source pixel where expected
[PASS] crop selects the right source region, with no bleed at its edges
[PASS] each Fit mode: Contain, Cover, Stretch, None (and scale applies to all)
[PASS] a dissolve group mixes against one base: constant brightness, A fades outside B
[PASS] sampling (axis, rotated, 180 deg) matches a reference bilinear within 1/255
[PASS] hostile geometry (3000 random layers, NaN) stays in bounds
[PASS] serial and parallel composites are byte-identical
[PASS] activeAt: the right source frame at 2x, frame-exact at k/fps, audio excluded, bottom first
[PASS] activeAt: a transition holds the outgoing clip; weights 1->0 / 0->1 sum to 1
[PASS] FrameCache: repeat hit, byte cap + LRU, oversize not cached, range invalidation
[PASS] grade: identity is a byte-identical passthrough
[PASS] grade: +exposure raises the mean channel value (-exposure lowers it), full and proxy
[PASS] grade: renderImage path is byte-identical to the slot sequence (full and proxy)
[PASS] param hash: FNV-1a vectors, equal grades equal, any change differs, never 0
interstellar_render: all tests passed

1/1 Test #1: interstellar_render ..............   Passed    0.02 sec
100% tests passed, 0 tests failed out of 1
```

### Checks beyond the suite

**Asserts are live in Release.** The first run, before the FNV basis fix, aborted on
`Assertion 'fnv1a64("") == 0xcbf29ce484222325ULL' failed` under `-O3 -DNDEBUG`.

**The transition test fails if the outgoing clip is not held — checked.** `ActiveSet.cpp` was
copied to the scratch directory, and the line
`if (!live[ia->second]) live[ia->second] = held[ia->second] = 1;` was deleted. The real test file
was then linked against that mutant (the repository was not touched):
```
t_ActiveSet_nohold: renderTests.cpp:512: test_active_transition_holds_outgoing_and_weights_sum_to_one():
    Assertion `a.size() == 2' failed.          (exit 134 — "AT the cut both are active")
```

**The on-screen dissolve test fails if dissolve groups are ignored — checked.** The same method was
used with `member = false` in `compose`:
```
t_Composite_nogroup: renderTests.cpp:286: test_dissolve_group_sums_to_one_on_screen():
    Assertion `is(out, 8, 8, 200, 200, 200, 255, 1)' failed.
```

**Clean under ASan, UBSan and LeakSanitizer.** The whole suite was run that way, including the
3000-layer hostile geometry sweep (`-fsanitize=address,undefined`, `halt_on_error=1`).

**No warnings.** `-Wall -Wextra -Wshadow` is clean on every file in this directory.

## Measurements

All figures come from `interstellar_render_bench`: Release (`-O3`), synthetic textured frames, the
median of 31 runs after a warm-up.
- **Machine:** AMD Ryzen 9 9900X, 12 cores / 24 threads, `par::threads()` = 24, `Pixel = float`.
- **Shared box.** Load average was 5.5–8.8 throughout, because other engineers were building in
  parallel. Absolute numbers are a few percent noisy. Every ratio quoted was stable across three
  runs.

### Composite: 2 layers into 1920×1080

| scenario | previous algorithm (nearest, full-raster scan, per-pixel trig, serial) | new, 1 thread | new, 24 threads |
|---|---:|---:|---:|
| A: 1080p bottom (copy path) + rotated 0.4× Screen PiP @ 0.8 | 46.74 ms | 7.94 ms | **0.85 ms** |
| B: 720p bottom upscaled (bilinear axis path) + same PiP | 46.79 ms | 15.86 ms | **1.55 ms** |

| new path, 24 threads | ms |
|---|---:|
| the rotated PiP layer alone | 0.46 |
| 50 % dissolve of two 1080p frames (snapshot + two group layers) | 2.69 |

- **Single thread.** The old algorithm took 46.7 ms; the new one takes 7.9 ms (scenario A) or
  15.9 ms (scenario B). That is 3–6× faster while also switching from nearest to bilinear
  sampling. Most of the gain comes from the bounding box and per-row spans: the PiP no longer pays
  for the whole raster.
- **All 24 threads.** 30–55× faster than the old algorithm.
- **Axis path.** Before the tap table was added, scenario B took 23.5 ms on one thread and 2.14 ms
  on 24.

### Grade: exposure 0.5, contrast 20, temp 5200, S-curve (0,0)(.25,.19)(.75,.83)(1,1), vibrance 15

| | 24 threads | 1 thread |
|---|---:|---:|
| **GradeEngine, 1920×1080 full** | **35.5 ms (28 fps)**; repeat runs 33.0 / 35.6 | **144.9 ms (6.9 fps)** |
| **GradeEngine, 1280 proxy edge (→1280×720)** | **12.2 ms (82 fps)** | **65.0 ms (15.4 fps)** |
| slot sequence, full (`renderFull`): the "before" | 63.8 ms; repeat runs 58.6 / 63.1 | 175.2 ms |
| slot sequence, 1280 (`renderPreview`) | 13.9 ms | 68.4 ms |
| identity short-circuit (copy) | 0.31 ms | |
| split of one GradeEngine full frame | ingest 1.83 + `renderImage` 21.3 + copy-out 0.47 ms | |

**Page faults dominate the full-resolution frame.** With glibc's default dynamic thresholds, every
33 MB float buffer that is freed goes back to the OS, and the next frame faults it in again.

| | page faults per frame | GradeEngine full | slot sequence full |
|---|---:|---:|---:|
| default malloc | 18K (~72 MB per frame), against 49K for the slot path | 33–35 ms | 58–64 ms |
| `GLIBC_TUNABLES=glibc.malloc.mmap_threshold=2000000000:glibc.malloc.trim_threshold=4000000000` | 2.6K | **23.4 ms** | 25.4 ms |

With the thresholds pinned, the proxy edge takes 12.1 ms through GradeEngine and 12.7 ms through
the slot sequence. On one thread the full frame takes 133.8 ms (GradeEngine) and 132.9 ms (slot
sequence).

The ~12 ms per frame that remains in GradeEngine comes from two 33 MB allocations per frame, both
outside this directory:
- `fromEncodedBytes` returns a fresh `Image`;
- `renderImage` makes a `downscaleLinear` copy even when no downscale is needed.

See the recommendations below.

### Per-pixel virtual dispatch in `PointProcessor`: evidence

**The five stages one at a time,** over a 1920×1080 linear RGBA float image:

| stage | 1 thread | 24 threads |
|---|---:|---:|
| Exposure | 5.40 ms | 1.14 ms |
| Contrast | 5.83 | 1.05 |
| WhiteBalance | 5.02 | 1.00 |
| **ToneCurve** | **51.13** | **4.97** |
| Vibrance | 20.75 | 2.24 |
| sum | 88.1 | 10.4 |

**The same Exposure kernel, reached three ways.** The memory traffic is identical in all three
(read one image, write one image):

| | PointProcessor (virtual call per pixel) | direct (qualified, non-virtual, not inlined) | inlined loop |
|---|---:|---:|---:|
| 1 thread | 4.98 ms | 4.25 ms | 1.65 ms |
| 24 threads | 1.21 ms | 1.13 ms | 1.05 ms |

**Profile of the shipped full-resolution path** (`perf record`, 30 frames, 24 threads; user-space
samples only, because `perf_event_paranoid=2`, so page-fault time is invisible here):

| share | symbol |
|---:|---|
| 14.6 % | `color::srgbDecode` |
| 13.2 % | `ToneCurve::processPixel` |
| 9.3 % | `Contrast::processPixel` |
| 9.2 % | `Exposure::processPixel` |
| 8.9 % | `WhiteBalance::processPixel` |
| 7.6 % | `color::srgbEncode` |
| 6.2 % | `encodeInPlace` |
| 5.1 % | `fromEncodedBytes` |
| 4.5 % | RGBA8 pack |
| 3.4 % | `Histogram::compute` |
| 3.1 % | `rgbToHsl` |
| 3.1 % | `Vibrance::processPixel` |
| 3.1 % | `hslToRgb` |
| 3.0 % | `ImageProcessor::getProperty` |
| 2.4 % | the PointProcessor row loop |

**Verdict.**
- **At full thread count, per-pixel virtual dispatch is *not* significant for this grade.** The
  cheap point stages are memory-bandwidth-bound: 66 MB moved in about 1.1 ms. The virtual, direct
  and inlined variants come out at 1.21, 1.13 and 1.05 ms, all within noise.
- **On one thread it is real.** It costs about 3.3 ms per cheap stage, and matches R-VOL-3's
  "~4 ms per stage". Only about 0.7 ms of that is the indirect call itself. The rest is lost
  inlining and vectorisation. Across the three cheap stages that is about 10 ms of a 145 ms frame
  (around 7 %). It matters on a 4–8-core board, not on this machine.
- **Bigger levers, in order of size:**
  1. Per-frame page faults (above).
  2. `ToneCurve::processPixel`, which makes 3 out-of-line calls per channel per pixel
     (`srgbEncode`, a LUT sample, `srgbDecode`), plus a per-channel LUT that is sampled even when it
     is identity. ToneCurve plus the sRGB calls account for about 35 % of user CPU, and ToneCurve
     is 51 ms of the single-thread frame.
  3. `Vibrance::processPixel`, which calls `getProperty()` twice per pixel: 3 % of CPU on a value
     that never changes within a frame.

I did not modify ImageProcessing. The recommendations follow.

## Known limits

**Composite**
- Coverage edges are hard (no edge anti-aliasing).
- A downscale beyond about 2× aliases, because there is no mip or area filter.
- Bilinear interpolates *straight* alpha. That is exact for opaque sources, which every decoded
  video frame is. A source with soft alpha would get a slight fringe; premultiplied interpolation
  would fix it.

**ActiveSet**
- If a clip is the incoming side of one transition *and* the outgoing side of another at the same
  instant (a clip shorter than its transitions), the weights multiply and no longer sum to 1. The
  model should reject such timelines.
- A transition longer than the incoming clip leaves that clip unheld past its own end.
- A pair that is not adjacent in the output (something between them) falls back to ordinary
  stacking. That is the expected behaviour for clips on different tracks with a layer between.

**ParamHash**
- `serializeParams` writes floats with 7 significant digits. Two grades that differ only beyond the
  7th digit therefore hash equal, and `isIdentity` cannot tell them apart. This is a property of
  the codec, which the brief mandates.

**GradeEngine**
- Identity output keeps the input size at a proxy edge, by design; the composite's Fit absorbs it.
- The engine never upscales.
- Not thread-safe.

**FrameCache**
- `invalidate` is O(entries). That is fine for hundreds of frames; a per-source index would be
  needed for tens of thousands.

## Recommendations for owners outside this directory

These are not done here, because they are outside my directory.

1. **Host (interstellar main): set the malloc thresholds at start-up.** Set
   `mallopt(M_MMAP_THRESHOLD, …)` and `mallopt(M_TRIM_THRESHOLD, …)` (or the `GLIBC_TUNABLES`
   above). Measured, this takes a full-resolution grade from 33–35 ms to 23.4 ms per frame. It is a
   process-wide policy, so it belongs to the host, not to a library.
2. **ImageProcessing: two allocation changes.**
   - Add a `fromEncodedBytes(…, Image &into)` overload.
   - Make `renderImage` skip its `downscaleLinear` copy when no downscale is needed.

   Together these remove the last ~66 MB per frame that GradeEngine still allocates.
3. **ImageProcessing: two kernel changes.**
   - Inline (or table) the sRGB conversions inside `ToneCurve::processPixel`, and skip identity
     channel LUTs.
   - Hoist `getProperty()` out of `Vibrance::processPixel`.

   These are the largest CPU costs in a typical grade. Devirtualising `PointProcessor` is worth
   doing only after these two.
