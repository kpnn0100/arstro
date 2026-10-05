# Interstellar — Architecture

> **Partly built.** The module map marks every row **[planned]** or **built**, and
> [`requirements.md`](requirements.md) carries the `DR-` entry with live `file:line` anchors for each
> built one. The first specification's architecture is withdrawn (see [`../REQUIREMENTS.md`](../REQUIREMENTS.md)).

## 1. Layers

```
┌────────────────────────────────────────────────────────────────────────────────┐
│ host     linux_main.cpp            the ONLY layer with OS code                  │
│          GTK3 window · dialogs · FFmpeg IFrameSource/IFrameWriter · audio out   │
│          · embedded fonts · binds a Cairo IRenderTarget                         │
├────────────────────────────────────────────────────────────────────────────────┤
│ app      arstro::interstellar_v1   App + widgets                                │
│          Home · Edit {Grade, Cut, Deliver} · the Segment tree. Dispatches       │
│          Commands; draws from AppModel. Reuses COSMO's panel widgets.           │
├────────────────────────────────────────────────────────────────────────────────┤
│ core     arstro::interstellar      UI-free, codec-free                          │
│          InterstellarService · Rack · ParamRegistry · Command/Event/Codec/Api   │
│   model  Project · Versions · Arrange          (no Cosmo, no pixels)            │
│  render  ActiveSet · Composite · GradeEngine · FrameCache  (no project model)   │
├────────────────────────────────────────────────────────────────────────────────┤
│ rack     arstro::cosmo             a REAL hosted CosmoService — THE colour      │
│          authority. Driven by cosmo::Command; read through cosmo::AppModel.     │
├────────────────────────────────────────────────────────────────────────────────┤
│ engines  arstro  ImageProcessing   EditEngine · EditParams · **Volume** (new)    │
│          gene    core/Gene         the shared expression language                │
└────────────────────────────────────────────────────────────────────────────────┘
```

The fourth row is the unusual one and it is the point of the app: **`interstellar_core` depends on
`cosmo_core`**, constructs a `CosmoService`, and dispatches `cosmo::Command`s into it. Everything
the user means by "grade like cosmo, and it really is the same project" falls out of that.

## 2. The seams

| seam | who fills it | why it is a seam |
|---|---|---|
| `artboard::IRenderTarget` | host binds `CairoTarget` | the app emits primitives and never calls a backend |
| `IFrameSource` / `IFrameWriter` | host, FFmpeg | the core carries no codec |
| `RackAccess` | the hosted `CosmoService` | lets the evaluator be tested with a three-line fake and no display |
| `Volume` | `arstro_image` | **new** — §3 |
| `cosmo::Command` / `cosmo::AppModel` | cosmo itself | the ONLY way colour is read or written |

### 2.1 How a video source reaches Cosmo without changing Cosmo

`CosmoService::setDecoderFactory` is injectable. Interstellar installs a decoder that, handed
`…/DSC01.MOV`, returns **one extracted frame** via the same FFmpeg source the timeline uses. Cosmo
then treats it as an ordinary image slot and grows no concept of time (R-RACK-3). The requirement
that looked like a change to another app turns out to be a seam that already exists.

## 3. Volume — the engine change

`arstro_image` gains a lazy 3D object. It is an **interface, not storage**: a 10 s 1080p clip is
6 GB in linear float and a 4K one 24 GB, so a resident volume cannot exist (R-VOL-1).

```cpp
struct VolumeView                 // plain data: no virtuals, no ownership       (src/volume/Volume.h:93)
{
    static constexpr int kMaxWindow = 33, kChannels = 4;   // RGBA8, always
    const uint8_t *frame[kMaxWindow];  // pointers INTO the cache's own buffers
    int   frames, width, height;
    long long t0;                      // source index of frame[0]; negative in a clip-end padded view
    ptrdiff_t rowStride;
    const uint8_t *row(int dt, int y) const    // header-inline; folds into the loop
    { return frame[dt] + (size_t)y * rowStride; }
};

class Volume                      // the lazy object                              (src/volume/Volume.h:111)
{
public:
    virtual VolumeExtent extent() const = 0;
    virtual bool window(long long t0, long long t1, VolumeView &out) = 0;   // INCLUSIVE, strict
};
// CachedVolume(extent, Provider, capBytes) — residency is EXACTLY the current window; a departing
// frame's buffer is recycled for the entering one; a window over the cap is REFUSED, never overshot.
```

Four decisions, each with a reason that cost something to learn:

- **A window, never a point.** A `at(x,y,t)` scalar accessor hides an unbounded decode behind an
  innocent call — lazy evaluation's classic trap. The window makes the cost visible at the call
  site and bounds residency by the declared radius.
- **An array of frame pointers, not one contiguous block.** A contiguous window means memcpy'ing
  each frame in: 8 MB of RGBA8 at 1080p, so a radius-2 window would copy 41 MB per output frame —
  more than the filter costs (denoise r2 measures 1.35 ms at 1080p; the copy alone would be ~4 ms). The frames already exist in the cache; pointing at them is free, and the
  indirection vanishes once the row pointers are hoisted.
- **Raw memory in the inner loop.** Resolved once per window, hoisted once per row. At 2.07 M
  pixels a frame, a per-pixel virtual is ~4 ms per stage per frame — fine for one photo, ~670 ms/s
  across seven stages at 24 fps. This is the style `Sharpen` already uses; `PointProcessor`'s
  per-pixel virtual is the style to keep away from the video path.
- **Source-space and ungraded.** A 2× clip's *timeline* neighbours are two source frames apart,
  which is wrong for anything temporal; and a graded cache entry would drag every neighbour's
  parameters into the key.

A processor declares `TemporalFootprint footprint()` — radius 0 for all seventeen of today's
stages, so they are untouched. The engine unions the chain and materialises one window.

## 4. Version resolution

The one piece of real algorithm in the core. `resolve(timeline)` walks the base chain and applies
deltas; the result is cached per `(timeline, projectRevision)` and nothing else holds a copy
(R-G-3).

```
resolve(tl) = pinned/frozen ? snapshot(tl) : resolve(tl.base)
              minus #tldrop … · with #tlset … · plus nodes declared in tl
colourOf(rackNode, tl) = rack.effective(rackNode)  ∘  #tlgrade deltas up the chain
```

A cycle in the base chain is refused at edit time, naming both ends.

## 5. Screens

```
Screen = Home | Loading | Edit          Tab = Grade | Cut | Deliver
```
Home is a standalone Segment outside the editor tree, as in cosmo. The **monitor** lives outside the
tab host: one widget, one frame, every tab — a frame that differs between tabs is a defect.

## 6. Module map

Every module, its layer, and the requirement that justifies it.

| path | layer | state |
|---|---|---|
| `core/ImageProcessing/src/volume/Volume.{h,cpp}` | engine | **built** — the lazy volume, `VolumeView`, `CachedVolume` (R-VOL, DR-VOL-1..3); notes in `core/ImageProcessing/docs/volume.md` |
| `core/ImageProcessing/src/volume/TemporalOps.{h,cpp}` | engine | **built** — denoise · blend · freeze remap · `renderTemporal` (R-FX-2, DR-FX-2) |
| `apps/interstellar/model/Project.{h,cpp}` + `Schema.h` | model | **built** — the `.isp`, canonical text, the fixed point, validation (R-FMT, DR-FMT-1) |
| `apps/interstellar/model/Versions.{h,cpp}` | model | **built** — resolution, deltas, grade deltas, pin/freeze, rebase, diff (R-VER, DR-VER-1) |
| `apps/interstellar/model/Arrange.{h,cpp}` | model | **built** — the cut operations, derived-aware (R-TL-3) |
| `apps/interstellar/render/ActiveSet.{h,cpp}` | render | **built** — clips live at t, transitions held (R-TL-4, DR-TL-4) |
| `apps/interstellar/render/ColourTransform.{h,cpp}` | render | **built** — camera curves, gamuts, working spaces (Rec.709, ACEScct), output transforms incl. PQ/HLG (R-COLOR-2..4, DR-COLOR-2/3) |
| `apps/interstellar/render/Lut.{h,cpp}` | render | **built** — `.cube` read (1D/3D), tetrahedral apply, write (R-COLOR-5/6, DR-COLOR-4) |
| `apps/interstellar/render/Composite.{h,cpp}` | render | **built** — geometry · fit · blend · the one-base dissolve (R-FX-3, DR-FX-3); 8- or 16-bit, one templated loop (DR-COLOR-1) |
| `apps/interstellar/render/Effects.{h,cpp}` | render | **built** — the plugin catalog and the Blur kinds (Gaussian, Box, Directional, Zoom, Spin), in source pixels scaled to the proxy (R-FX-5/6, DR-FX-5/6) |
| `apps/interstellar/render/GradeEngine.{h,cpp}` · `ParamHash` · `FrameCache` | render | **built** — EditEngine per frame, cached on the param hash (DR-RENDER-2) |
| `apps/interstellar/core/Rack.{h,cpp}` | core | **built** — the hosted `CosmoService`, async load, read-through own-params cache (R-RACK, DR-RACK-1..5) |
| `apps/interstellar/core/Colour.{h,cpp}` | core | **built** — the ONE fold, pin snapshots through Cosmo's reader, deltas (DR-RACK-4, DR-VER-2/3) |
| `apps/interstellar/core/FrameSelector.h` | core | **built** — the reference frame a stored video path decodes to (DR-RACK-3a) |
| `apps/interstellar/core/ParamRegistry.{h,cpp}` | core | **built** — the address space, owners, units (R-API-1) |
| `apps/interstellar/core/service/Command·Event·Json·AppModelCodec·ApiDoc` | core | **built** — the grammar table, the log line, the model dump, the generated document (DR-SVC-2, DR-API-1) |
| `apps/interstellar/core/service/InterstellarService.{h,cpp}` + `ServiceRender.cpp` | core | **built** — routing, binding, versions, arrangement, the frame path, the render queue (DR-SVC-1, DR-RENDER-*) |
| `apps/interstellar/model/Anim.h` | model | **built** — the keyframe evaluator, After Effects' bezier model; header-only, shared with the UI (R-ANIM-2, DR-ANIM-1) |
| `apps/interstellar/core/service/ServiceAnim.cpp` | core | **built** — addresses → curves and clocks, `key add|remove|set|clear`, `set` keying, curves on the render path, pins' curve snapshots (R-ANIM, DR-ANIM-1) |
| `apps/interstellar/core/service/ServiceCache.cpp` | core | **built** — the graded preview cache: plan-hash index, idle builder thread, segments read by playback (R-PLAY-1, DR-PLAY-1) |
| `apps/interstellar/core/service/ServiceEdit.cpp` | core | **built** — one undo history, grade clipboard, presets, engine settings (DR-EDIT-1..3, DR-SET-1..3) |
| `apps/interstellar/host/FrameSourceFFmpeg` · `FrameWriterFFmpeg` | host | **carried forward** from the first build; the writer now takes an `EncodeSpec` — H.264, H.265 8/10-bit, ProRes Proxy…4444, DNxHR LB…444, BT.709 tagged (DR-RENDER-6, D-9); a deep frame decodes and encodes as RGBA64 (DR-COLOR-1) |
| `apps/interstellar/host/VideoFrameDecoder` · `HostFrameSource` · `PngWriter` | host | **built** — Cosmo's decoder seam, the timeline's source (stills through Cosmo's own decoder, for R-RENDER-5), PNG out |
| `apps/interstellar/cli/main.cpp` | front end | **built** — `interstellar-cc`, argv/stdout only (DR-SVC-3) |
| `apps/interstellar/app/*` | app | **built** — Home · Edit · the three tabs over `AppHooks`; cosmo's panels, `MenuStrip` and `SettingsDialog` compiled in; screen scale (R-UI, DR-UI-1..9; `app/NOTES.md`) |
| `apps/interstellar/linux_main.cpp` | host | **built** — the GTK3 window: hooks bound to the service, pickers, frame tick (DR-UI-4) |
| `apps/interstellar/host/Thumbnailer.{h,cpp}` | host | **built** — the app's optional `thumbnail` hook, cached |
| `apps/interstellar/tests/liveShots.cpp` | test | **built** — the real app over the real service, headless (`interstellar_live`) |
| audio master sum | core | **[planned]** P6 (R-AUD-5) |
