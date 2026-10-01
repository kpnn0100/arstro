# Interstellar — Requirements (as-built tier)

**What the code contractually does today.** `DR-<AREA>-<n>`, descriptive present, `file:line`
anchors, each citing the `R-` tag it implements. An entry with a dead anchor is a defect
(cosmo's D-1), and a behaviour with no entry does not ship.

The first build's as-built tier is withdrawn with the build it described (git history, `69b91eb`);
carrying it forward would be a document describing a seam that no longer exists. Every entry below
is the second build's.

## Conformance

Rung **0** of `arstro.rule` §5 — spec only. The ladder, and what each rung will add here:

| rung | means | lands with |
|---|---|---|
| 0 | spec only | ← **here** |
| 1 | core split out; `Command`/`Event`/model with one text codec | P3 |
| 2 | registered with the root `ctest`; L2 headless service tests | P2 |
| 3 | a real CLI that is the whole app without a window | P3 |
| 4 | **generated API document, committed, drift-tested** | P3 — and **no app in the suite has this** |
| 5 | control socket + the GUI/headless equivalence test | post-P7 |

## Entries

### DR-RACK-1 The rack is a real hosted CosmoService (R-RACK-1)
`Rack` (`core/Rack.h:1`) owns a `cosmo::CosmoService` constructed with Interstellar's
`ThreadBudget` by reference, and reaches it **only** through `cosmo::Command`s. A colour write is
the `Select` + `Set` pair `cosmo-cc` sends (`Rack::setParam`, `core/Rack.cpp`), so the CLI path and
any future GUI path are one path. `interstellar_core` links `cosmo_core` deliberately — a real
authority is worth a link dependency, and avoiding one is what let colour be a fake in two builds.

### DR-RACK-2 A colour edit reaches a real `.cmp` (R-RACK-2) — THE GATE
Guarded by `test_a_colour_edit_reaches_a_real_cmp`, which reads the saved file back with code that
shares **nothing** with the writer, and by `test_a_second_service_sees_the_edit_the_first_one_made`,
which opens the saved file in a second `CosmoService` and reads `exposure 0.450, temp 5200` back.
From a shell, on a **video** source:
```
interstellar-cc rack open look.cmp : rack set 3 exposure=0.35 : rack set 3 temp=5200 : rack save
→ the .cmp's third #image carries   exposure=0.35   temp=5200
```

### DR-RACK-3 A video source is graded on an extracted frame, with no change to Cosmo (R-RACK-3)
`VideoFrameDecoder` (`host/VideoFrameDecoder.cpp:1`) is an `IImageDecoder` installed through
`CosmoService::setDecoderFactory`: a video path — `…/clip.mp4#t=2.0` — becomes one extracted frame
via `FrameSourceFFmpeg`; anything else delegates to Cosmo's own `NativeImageDecoder`. Cosmo then
holds an ordinary image slot. `rack add a.png b.png clip.mp4#t=2.0` → `3 images in the rack`.
**Limit, filed as D-1:** Cosmo opened *by itself* has no such decoder and shows the video node as
`kind=failed` — its grade is preserved in the file, its pixels are not displayable there.

### DR-RACK-4 Grouping stacks through Cosmo's own composeParams (R-RACK-4)
`Rack::effectiveParams` returns `AppModel::params` (Cosmo's `effectiveEditParams`) after a
`Select`; `getParam` reads `ownParams` back through Cosmo's own serializer rather than a field
switch. Guarded by `test_a_group_offset_stacks_onto_its_members`: a group's +0.5 EV appears in its
member's effective params while the member's own value stays 0.

### DR-RACK-5 The load is pumped against the wall clock (R-RACK-1)
`Rack::pumpUntilLoaded` (`core/Rack.cpp`) sleeps 1 ms per 16 ms tick against a wall-clock deadline
and keeps its simulated clock **monotonic** as a member — cosmo's own `pumpUntilIdle` pattern, and
D-56's lesson. The first version spun simulated time and reported `0 images` for a project that was
loading fine, because 7 500 ticks elapsed before a worker opened the first file. A timeout is now
an error naming the path, never a silently empty rack.

### DR-VOL-1 Video is a lazy volume with a WINDOW accessor and no point accessor (R-VOL-1, R-VOL-2)
`Volume::window(t0, t1, VolumeView&)` (`core/ImageProcessing/src/volume/Volume.h:121`) materialises
source frames **[t0, t1] inclusive** and is strict: it refuses t0 < 0, t1 ≥ frames, an inverted
range, more than `VolumeView::kMaxWindow` (33) frames, or a frame the provider could not produce —
and a refused call leaves the view empty. There is no `at(x, y, t)`. A clip-end window is the
driver's job (`temporalWindow`, `TemporalOps.cpp:242`), which pads by pointing the extra slots at
the edge frame. Guarded by `test_window_refuses_what_it_cannot_serve` and
`test_driver_clamps_at_both_clip_ends` (`core/ImageProcessing/unittest/volumeTests.cpp`).

### DR-VOL-2 A view is raw memory: frame pointers into the cache, rows hoisted (R-VOL-3)
`VolumeView` (`Volume.h:93`) is plain data — `const uint8_t *frame[33]`, straight RGBA8, a row
stride and an inline `row(dt, y)` — so an op resolves a row once and walks pointers. The window is
**not** copied into a contiguous block. Measured (`volume_bench`, Release, 24 threads, op alone):
temporal denoise r2 **1.35 ms** at 1080p / **5.69 ms** at 4K; frame blend r2 **0.54 ms** / 4.58 ms.

### DR-VOL-3 Residency is bounded by the footprint, never by the clip, and is invisible (R-VOL-4, R-VOL-7)
`CachedVolume` (`Volume.h:152`, `window` at `Volume.cpp:52`) keeps **exactly** the current window
resident, recycles the departing frame's buffer for the entering one, decodes in ascending order (a
forward walk decodes each source frame once), and **refuses** a window that would exceed its byte
cap rather than overshooting. Guarded by `test_residency_bounded_by_footprint_not_clip_length`,
`test_forward_walk_decodes_each_frame_once`, `test_cache_never_evicts_inside_the_window`, and
`test_residency_is_invisible` (same t, same bytes, any access order). Limit: stepping BACKWARDS one
frame re-decodes one frame (a seek) — the cost of a bound that is a number.

### DR-FX-2 Three temporal effects, each proving a different part of R-VOL (R-FX-2)
`TemporalDenoise` (`TemporalOps.cpp:94`) — a per-pixel gate against the centre frame,
`w = max(0, 1 − (d/h)²)²` with `d = (|ΔR| + 2|ΔG| + |ΔB|)/4`, `h = 48·strength`; **not**
motion-compensated. `FrameBlend` (`TemporalOps.cpp:190`) — the mean of the window, in 8-bit code
values (not linear light, so not a physically exact shutter). `freezeRemap` (`TemporalOps.h:146`) —
`min(t, freezeAt)`, a remap of t at radius 0. Guarded by: static noise σ 7.999 → 3.605 at r2
(×2.22, ceiling √5 = ×2.236) and 2.700 at r4; a moving square leaves **no** ghost (a plain average
measures 54.0 on the same probe, so the probe can fail); blend equals the analytic mean. Each test
was confirmed to fail against a deliberately broken op (cache off; denoise returning the centre;
denoise ungated). **Not yet wired to clips** — the `#fx` node and the render pipeline land with the
service (P3).

### DR-TL-4 A transition holds the outgoing clip, and the pair mixes against one base (R-TL-4, R-TL-5)
`render::activeAt(clips, transitions, t, fps)` (`render/ActiveSet.cpp:37`) returns the clips live at
`t`, bottom track first, each with its source frame `floor(localTime·fps + 1e-6)` — the epsilon
because a bare floor reads frame k−1 at t = k/fps (checked for k < 5000 at 24 and 29.97 fps). Through
a transition the OUTGOING clip is **held past its out-point** (`held = true`) with weights 1→0 / 0→1
that sum to 1, and the incoming entry carries `dissolveWithPrevious`, which `render::compose`
(`render/Composite.cpp:417`) honours by mixing the pair against the SAME base —
`base + (A−base)·wA + (B−base)·wB` — because stacking them gives `A(1−f)² + Bf`, a 75 % dip at the
midpoint: the very darkening R-TL-4 forbids. Guarded by
`activeAt: a transition holds the outgoing clip` (confirmed to fail with the hold removed) and
`a dissolve group mixes against one base: constant brightness` (`render/tests/renderTests.cpp`).

### DR-FX-3 Geometry and composite per clip (R-FX-3)
`render::Layer` (`render/Composite.h:63`) carries crop, `Fit` (Contain | Cover | Stretch | None),
scale, rotation, anchor, translation, opacity and `Blend` (Normal, Multiply, Screen, Overlay, Add,
Subtract, Difference). `placeLayer` computes the inverse affine once per layer, scans only its
bounding box, solves each row's covered span, samples bilinear in 8-bit fixed point clamped to the
crop, and runs rows in parallel; serial and parallel output are byte-identical. Measured
(`interstellar_render_bench`): two layers into 1080p, one rotated and Screen-blended,
**0.85 ms** at 24 threads (7.9 ms on one). Limits: no edge anti-aliasing; >2× downscale aliases.

### DR-RENDER-2 The grade step is EditEngine, unchanged, and a frame cache keys on the params (R-RENDER-2, R-FX-1)
`render::GradeEngine::render` (`render/GradeEngine.cpp:36`) runs Cosmo's own `EditEngine` via
`renderImage` — byte-identical to the clear/add/render slot sequence at full and proxy size, which a
test checks on every run — and passes an identity grade through untouched. `render::FrameCache` is a
byte-capped, thread-safe LRU keyed on (media, source frame, `hashParams`, proxy level); `hashParams`
never returns 0, so 0 means an ungraded source frame (R-VOL-5). Measured, 1080p, a non-identity
grade: **33–35 ms** at 24 threads (145 ms on one), 12 ms at a 1280 proxy. Most of a full-res frame
is page faults from glibc returning each 33 MB float buffer to the OS; pinning the malloc
thresholds in the host takes it to 23.4 ms (recommended to the host, not yet done).
