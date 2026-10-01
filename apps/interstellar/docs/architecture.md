# Interstellar — Architecture

> **Planned.** Nothing below exists yet; the module map marks every row, and
> [`requirements.md`](requirements.md) gains the `DR-` entry with live `file:line` anchors as each
> lands. The first specification's architecture is withdrawn (see [`../REQUIREMENTS.md`](../REQUIREMENTS.md)).

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
│          InterstellarService · Project · Rack · Versions · Arrange · Evaluate   │
│          · Composite · ParamRegistry · FrameCache                               │
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
struct VolumeView                 // plain data: no virtuals, no ownership
{
    const Pixel *frame[kMaxWindow];   // pointers INTO the cache's own buffers
    int   frames, width, height, channels;
    long long t0;                      // source index of frame[0]
    ptrdiff_t rowStride;
    const Pixel *row(int dt, int y) const    // header-inline; folds into the loop
    { return frame[dt] + (size_t)y * rowStride; }
};

class Volume                      // the lazy object
{
public:
    virtual bool window(long long t0, long long t1, VolumeView &out) = 0;
    virtual Extent extent() const = 0;
};
```

Four decisions, each with a reason that cost something to learn:

- **A window, never a point.** A `at(x,y,t)` scalar accessor hides an unbounded decode behind an
  innocent call — lazy evaluation's classic trap. The window makes the cost visible at the call
  site and bounds residency by the declared radius.
- **An array of frame pointers, not one contiguous block.** A contiguous window means memcpy'ing
  each frame in: 25 MB at 1080p, so a radius-2 window copies 125 MB per output frame — more than
  the filter costs. The frames already exist in the cache; pointing at them is free, and the
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

Every planned file, its layer, and the requirement that justifies it. **[planned]** until it lands.

| path | layer | responsibility |
|---|---|---|
| `core/ImageProcessing/src/volume/Volume.{h,cpp}` | engine | **[planned]** the lazy volume + `VolumeView` (R-VOL) |
| `core/ImageProcessing/src/volume/TemporalOps.{h,cpp}` | engine | **[planned]** denoise · blend · freeze (R-FX-2) |
| `apps/interstellar/core/Project.{h,cpp}` | core | **[planned]** the `.isp` document, canonical text, the fixed point (R-FMT) |
| `apps/interstellar/core/Versions.{h,cpp}` | core | **[planned]** base chains, deltas, resolution, rebase (R-VER) |
| `apps/interstellar/core/Rack.{h,cpp}` | core | **[planned]** owns the hosted `CosmoService`; `RackAccess` over it (R-RACK) |
| `apps/interstellar/core/Arrange.{h,cpp}` | core | **[planned]** the cut operations (R-TL-3) |
| `apps/interstellar/core/Evaluate.{h,cpp}` | core | **[planned]** resolved values per frame, pure (R-VOL-7) |
| `apps/interstellar/core/Composite.{h,cpp}` | core | **[planned]** geometry · blend · transitions (R-FX-3) |
| `apps/interstellar/core/Audio.{h,cpp}` | core | **[planned]** the audio schema subset + master sum (R-AUD) |
| `apps/interstellar/core/FrameCache.{h,cpp}` | core | **[planned]** byte-capped LRU, the volume's residency layer |
| `apps/interstellar/core/ParamRegistry.{h,cpp}` | core | **[planned]** the generated address space + owner routing |
| `apps/interstellar/core/service/*` | core | **[planned]** Service · Command · Event · AppModel · Codec · ApiDoc (R-SVC, R-API) |
| `apps/interstellar/host/FrameSourceFFmpeg.{h,cpp}` | host | **carried forward** from the first build — concept-independent plumbing, tested |
| `apps/interstellar/host/FrameWriterFFmpeg.{h,cpp}` | host | **carried forward** — H.264/ProRes, chosen by extension |
| `apps/interstellar/Theme.h` | app | **[planned]** aliases cosmo; forks ONE token (R-UI-2) |
| `apps/interstellar/App.{h,cpp}` + `widgets/*` | app | **[planned]** Home · Edit · the three tabs (R-UI) |
| `apps/interstellar/cli/main.cpp` | front end | **[planned]** `interstellar-cc` — argv, stdout, no behaviour |
| `apps/interstellar/linux_main.cpp` | host | **[planned]** GTK3 window |
