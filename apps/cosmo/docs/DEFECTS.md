# cosmo — Defect List

**Committed, so it travels between machines and sessions.** Filed and closed by
`.claude/skills/arstro.cosmo.core.debug/` and `.claude/skills/arstro.cosmo.design.debug/`; the entry
format is defined in `arstro.cosmo.core.debug` §4 and is shared by both.

- IDs are `D-<n>`, sequential across both areas, **never reused**. Next free id: **D-46**.
- Status: `Open` · `Confirmed` · `Fixed` · `Not-a-defect` · `Unreproduced` · `Deferred`.
- Severity: `S1` data loss / crash / hang · `S2` wrong output or an unusable surface · `S3` wrong
  behaviour with a workaround · `S4` cosmetic or diagnostic.
- A resolved entry moves to `## Closed` with its commit hash and the test that now guards it. **Nothing
  is ever deleted.**

The entries below were found by reading the source on 2026-08-16, while writing the cosmo skills. They
are **confirmed in the code but not yet reproduced at runtime** — each says so. Most are observability
and reachability gaps, which is why `PROGRESS.md`'s NEXT is the P0 harness.

---

## Open

### D-45 — A 1600 px preview render costs ~600 ms with 16 threads, at default parameters
- **Area:** core / engine · **Status:** **Confirmed** (measured) · **Severity:** S2
- **Found:** 2026-08-23, while measuring D-44 — it is the floor D-44's warm hops could not get under.
- **Front end:** none — measured against `EditEngine` directly.
- **Reproduce:** `apps/cosmo/core/tests/fixtures/preview_hop_latency.cpp <one.ARW>`, stage 4.
- **Expected:** no requirement states a preview budget, which is itself the finding — see
  **Judgement**. As an order of magnitude: 1600x1067 is 1.7 Mpx, and seventeen point/area processors
  over it at 16 threads is tens of milliseconds of arithmetic, not six hundred.
- **Actual:**
  ```
  4. render again, proxy warm       601.4 ms      (engine threads = 16, EditParams all default)
  ```
  Every parameter is at its neutral value, so this is what cosmo costs to render a photo it has
  already cached with no edits applied at all. It is paid on **every** hop and **every** slider move
  — D-44 is the variance on top of it and this is the floor underneath.
- **Evidence:** the number above, taken with `par::setThreads(0)` pinned first, because earlier tests
  in the same binary leave a `ThreadBudget`'s count behind and an unpinned measurement would have
  been measuring somebody else's budget. It read 645 ms at whatever they left and 617/601 ms at 16,
  so the thread count is NOT the explanation.
- **Judgement:** **requirement gap.** Nothing in `REQUIREMENTS.md` says what an interactive preview
  may cost, so there is no line for this to be under — R-LOADPERF governs *opening a project* and
  R-CPU governs *how many threads*, and neither bounds the latency of the interaction the user
  performs most. Not closed while filing, deliberately: the right number is a product decision (100
  ms reads as instant, 600 ms does not) and it should be written with the user rather than guessed
  here. Filed against the gap so it is not lost.
- **Regression?** No. `git log -S "renderInto"` and `-S "ensurePreviewProxy"` on `EditEngine.cpp`
  both return only `af95c65` (the repo reorganisation): the render path is original and untouched by
  any of the last week's commits.
- **ATTRIBUTED 2026-08-24** — the measurement this entry asked for, run on a 24-thread Ryzen 9 9900X
  with the new fixture `apps/cosmo/core/tests/fixtures/preview_stage_cost.cpp`. **Both hypotheses were
  right, and a third one nobody had proposed is the largest single item.** At 1600x1066 (1.71 Mpx,
  27.3 MB per float-RGBA buffer), every `EditParams` field at its neutral value:

  ```
  threads=24  full chain (all default)   193.25 ms
  threads=24  ALL STAGES BYPASSED         61.02 ms   <- so 132 ms is stages doing nothing
  threads=8   full chain (all default)   221.30 ms
  threads=4   full chain (all default)   326.20 ms
  threads=1   full chain (all default)   980.13 ms
  ```

  Per stage, at identity, measured individually:

  ```
  ToneCurve   28.11   ColorGrading 12.40   ToneRegions 8.80   Crop      3.73
  ColorMixer  12.29   Exposure/Contrast/WhiteBalance/Vibrance 2.69 each  Rotate 1.90
  spatial early-out copy (x9, SERIAL std::copy of 27 MB)      1.46 each
  Histogram::compute  15.15 (encoded in) / 11.22 (linear in) x2   computeHue 4.51
  encodeInPlace 8.40   float->RGBA8 pack 1.10   parallelFor(empty body) 0.30 x ~13
  ```

  **(a) Processors run at identity — confirmed, and it is 132 of the 193 ms.** `ImageBlock::process`
  filters on `isBypassed()` only; there is no notion of a stage being *at its default*, so all
  seventeen run. `Crop` with `cropW=cropH=1` still copies the frame row by row, `Rotate` with angle 0
  still runs `quarterTurn` before its early-out, and the nine spatial stages that DO early-out do it
  with a **serial** `std::copy` of 27 MB each — which is why 24 threads (193 ms) barely beat 8 (221 ms)
  and only 5x beat 1 (980 ms). More cores cannot fix this; that is the whole SBC problem in one number.

  **(b) Per-pixel transcendentals — confirmed, and `ToneCurve` is the worst stage in the pipeline at
  28 ms.** `EditParams::curveLog` defaults to **true** (`EditParams.h:72`), so
  `ToneCurve::processPixel` calls `color::srgbEncode` + `srgbDecode` per colour channel
  (`ToneCurve.cpp:87,90`) — each a `std::pow(double, ...)` (`ColorSpace.cpp:18,29`) — **10.3 M pow
  calls per render, to look up an identity curve**. `encodeInPlace` and all three histogram taps pay
  the same `pow`; together those are ~39 ms more. `fromEncodedBytes` already solved exactly this with
  `kSrgbToLinear`; the encode direction never got the same treatment.

  **(c) NEW — `renderInto` allocates its working buffers on every call.** `Image preCurve, preMixer;`
  and `Image processed;` are **locals** (`EditEngine.cpp:542,568`), so each preview render freshly
  allocates and first-touches ~82 MB of anonymous memory. `ImageBlock`'s `mScratchA/mScratchB` are
  members and correctly reused — these three were simply missed. This is the bulk of the 61 ms
  all-bypassed floor: the floor is allocation, three histogram passes and one `pow`-based encode, none
  of which any stage is responsible for.

  **(d) `par::parallelFor` has no thread pool.** It spawns fresh `std::thread`s per call
  (`Parallel.h:44-58`), ~13 times per render, and splits into **equal** chunks. Cheap here (0.30 ms a
  call) and nearly free to fix, but on a big.LITTLE SBC equal chunks mean every parallel pass finishes
  at little-core speed — a 2-3x standing loss on exactly the hardware this matters for.

  Cost is very close to linear in pixels, which is what makes a coarse-first strategy work:
  `800px 54.85 ms` · `400px 15.07 ms` (threads=8).

- **RECOMMENDED FIX — not applied. Four contained changes, in this order, each independently measurable
  with `preview_stage_cost.cpp`:**
  1. **`ImageProcessor::isIdentity()`** — virtual, `return false` by default, each processor answers
     from its own properties; `ImageBlock::process` drops identity stages from the active list exactly
     as it already drops bypassed ones. Removes 132 of 193 ms at default params and costs nothing when
     a stage is in use. Guard: a test asserting an identity stage's output is bit-identical to its
     input AND that the chain reports it skipped.
  2. **LUT `srgbEncode`/`srgbDecode`** — a 4096-entry table plus linear interpolation is well under
     1/255 of error, mirroring `kSrgbToLinear`. Fixes `ToneCurve` (28 ms), `encodeInPlace` and all
     three histogram taps in one change. Guard: max-abs-error assertion against the `pow` reference.
  3. **Hoist `preCurve`/`preMixer`/`processed` to `EditEngine` members.** One-line-per-buffer change;
     removes ~30 ms of per-render page-faulting and makes the render allocation-free at steady state.
  4. **Make the pre-curve and pre-mixer histogram taps opt-in** — they exist for the Curve and Mixer
     panels, and cost 16 ms on every render whether or not either is open.

  Together these are expected to take 1600 px from ~200 ms to ~20 ms without changing a single output
  pixel. **They do not by themselves make a slider real-time on a weak SBC** — that needs the
  progressive-resolution requirement this entry filed as the gap; see `PROGRESS.md`'s SBC plan.

### D-38 — The touch editor draws its action bar over its own controls, and landscape is unusable
- **Area:** design / touch shell · **Status:** Confirmed (rendered) · **Severity:** S2
- **Found:** 2026-08-20, on the first frames `cosmo_touch_shots` ever produced — the phone UI had
  never been rendered anywhere but on a device, which is why this shipped through M3.
- **Reproduce:** `cosmo_touch_shots --outdir /tmp/t --only portrait` and `--only landscape`, then
  look at `cosmo-touch-editor-*`.
- **Expected:** R-TOUCH-2 — no component overlaps another; R-TOUCH-3 — both orientations work.
- **Actual:** **Portrait:** the Save / Import / Export action bar sits on top of the last slider row
  ("Blacks" is cut in half behind the Save button), and the tray's row list is clipped mid-row rather
  than ending above the bar. **Landscape (852×393):** the photo is a thin strip at the top and the
  tray covers the rest, with the section chips, the action bar and the 5-tab tool bar all drawn into
  the same band — three layers of controls in the same pixels, most of them unreachable.
- **Cause:** the tray is laid out as an overlay that rises **over** the photo (the design brief's own
  model, now amended), its content height is not measured against the space left after the action bar
  and tool bar, and there is no landscape layout at all — `resize()` applies one set of portrait
  metrics whatever the aspect.
- **RECOMMENDED FIX:** the R-TOUCH-2/3 work: give the tray its own box that the photo's box shrinks
  to make room for (no overlay), measure the row list against `bodyTop()..actionBarTop()` and scroll
  it (R6), and add the landscape two-pane layout — photo left, tray a fixed right-hand panel — so
  neither orientation stacks controls. Guard it in `cosmo_touch_shots --assert` with a sibling-rect
  intersection check once the tray is a real box rather than an overlay.

### D-36 — An adjustment outside the UI's range crashes the render worker (NaN through a clamp)
- **Area:** core / engine · **Status:** Confirmed (crashed under gdb) · **Severity:** S1 (crash)
- **Found:** 2026-08-20, while adding the `editor-dissolve` shot for R-VIEW-1. The shot asked for a
  deliberately huge change so the dissolve would be visible in a PNG, and the process died.
- **Reproduce:** any front end, no UI needed — `set exposure=250` on a loaded image, then let a
  preview render:
  ```
  cosmo_shots --outdir /tmp/s --images <a>.RAF --only editor-dissolve   # with exposure=250
  Thread 242 received signal SIGSEGV
  #0  arstro::ToneCurve::sampleLut (lut=0x7ffffffe77a8, d=-nan(0x400000))
      at core/ImageProcessing/src/tone/ToneCurve.cpp:78
  #1  arstro::ToneCurve::processPixel (…) at ToneCurve.cpp:88
  #2  arstro::PointProcessor::process …  (a parallelFor worker)
  ```
- **Expected:** an out-of-range value is clamped, or renders as white — a number a script can send
  must not be able to kill the process.
- **Actual:** `sampleLut` indexes its LUT out of bounds and the render worker segfaults, taking the
  app with it.
- **Cause:** [ToneCurve.cpp:70-79](../../../core/ImageProcessing/src/tone/ToneCurve.cpp#L70) clamps
  with `if (d < 0) d = 0; if (d > 1) d = 1;` — **both comparisons are false for NaN**, so a NaN
  input survives the clamp, `(int)(NaN * (kLut-1))` is undefined (INT_MIN in practice) and
  `lut[i]` reads wild memory. The NaN itself comes from the exposure gain: `2^250` overflows to
  `inf`, and a later `inf - inf` (or `inf * 0`) in the tone chain produces NaN.
- **Why it matters beyond the silly number:** the UI's own slider is limited to ±5 EV, so a mouse
  cannot reach this — but `set` is a documented command (R-SVC), which means a script, the control
  socket, `cosmo-cc`, or a preset file with a bad value all can. It is also the class of bug that
  turns any future NaN anywhere in the pipeline into a crash rather than a wrong pixel.
- **RECOMMENDED FIX (not applied — `core/ImageProcessing` is not this skill's to change):** guard
  the LUT index rather than trusting the clamp, in `ToneCurve::sampleLut`:
  ```cpp
  if (!(d > 0)) d = 0;      // false for NaN as well as for negatives
  else if (d > 1) d = 1;
  ```
  and, so a bad value cannot silently poison a whole frame, clamp the *parameter* where it enters
  the session (`deserializeParams` / `applySetFields`) to the range the UI exposes — an out-of-range
  `set` should be rejected with a message, which is also what makes it debuggable. A test belongs
  with each: a `sampleLut(lut, NaN)` unit assertion, and a service test that `set exposure=250`
  either fails or renders finite pixels.
- **Guard for the shot in the meantime:** `editor-dissolve` now uses `exposure=1.5`, inside the
  UI's range, so the harness does not depend on the fix.

### D-24 — One RAF takes 8.5 s, and 90% of it is one call that reports nothing
- **Area:** core / load · **Status:** Confirmed (measured) · **Severity:** S2
- **Found:** 2026-08-18, reported by the user: "loading 1 image takes too long … hard to track the
  progress".
- **Reproduce:** `apps/cosmo/core/tests/fixtures/raw_phase_timing.cpp` — the same call sequence
  `NativeImageDecoder::decodeRaw` uses, timed per phase:
  ```
  full      open  0 ms | unpack 713 ms | process 7688 ms | make_mem 107 ms | total 8508 ms  (4170x6246)
  half      open  0 ms | unpack 719 ms | process   97 ms | make_mem  38 ms | total  854 ms  (2085x3123)
  fast      open  0 ms | unpack 709 ms | process  337 ms | make_mem 109 ms | total 1155 ms  (4170x6246)
  ```
  (`half` = `params.half_size=1`; `fast` = `params.user_qual=0`, bilinear at FULL resolution.)
- **Expected:** opening an 18-photo project is trackable and does not take 36 s.
- **Actual:** two findings, and the second one bounds what progress reporting can ever achieve.
  **(a) 90% of a decode is a single `dcraw_process()` call** — 7688 of 8508 ms — and cosmo spends
  it producing 4170×6246 pixels to render a **1600 px** preview. **(b) The X-Trans demosaic
  reports no progress at all**: `grep -c RUN_CALLBACK src/demosaic/xtrans_demosaic.cpp` → **0**,
  while the Bayer paths report every 256 rows (`misc_demosaic.cpp:271`). Every file in the reported
  project is Fujifilm X-Trans, so LibRaw's own progress callback yields 10% → 92% with a
  7.5-second gap and nothing legitimate to put in it.
- **Judgement:** defect against R-LOADUX-4's intent. The requirement says report at the
  granularity of the work and explicitly forbids inventing a percentage — and for X-Trans there
  is no finer *honest* signal available, so **the reporting is already at its ceiling and the real
  defect is the cost**. Fixing progress harder cannot help; fixing the decode can.
- **Cause:** `decodeRaw` always runs LibRaw's highest-quality demosaic
  (`NativeImageDecoder.cpp`, `dcraw_process()` with default params), for pixels that are
  downsampled to `previewEdge` before anything is shown.
- **RECOMMENDED FIX — not applied, needs a decision on export quality:**
  Decode the LOAD with `imgdata.params.user_qual = 0` (bilinear) and keep the good demosaic for
  output. **`user_qual` rather than `half_size`** because it leaves the dimensions **identical**
  (4170×6246), so crop rectangles, normalised mask geometry and every slot's coordinates stay
  valid and a full-quality re-decode can drop straight into the same slot later. 8.5 s → 1.2 s per
  image, ~7×; the 18-image project goes from ~36 s to ~6 s, and no progress-reporting change is
  needed to make that trackable.
  Then one of two ways to get quality back, and **this is the user's call, not a technical one**:
  1. **Re-decode at export.** Export already re-renders every image; decode it properly there.
     Load is 7× faster, output unchanged, export pays ~7 s per image once.
  2. **Re-decode in the background after the load.** The editor is usable in 6 s and each slot's
     pixels are quietly replaced at full quality as they arrive. Best experience, most work — it
     needs `RenderService` to accept replacement pixels for a live slot, and a rule for what
     happens if the user exports mid-upgrade.
  A third option — ship bilinear everywhere — is **not** recommended: it silently lowers output
  quality, which is the one thing a photo editor may not trade for speed.
  Preferred: (1) first, because it is contained and reversible, then (2) if the pause at export
  proves annoying. Either way the test is a decode-timing assertion plus an export-path check that
  the exported pixels came from the quality decode.
- **Partially addressed already** by commit `49c5b40`, which is worth having on its own: the decode
  seam now carries LibRaw's phase callbacks, so a **Bayer** file (Canon, Nikon, Sony) reports every
  256 rows of demosaic and the bar moves inside one image. For X-Trans it yields the honest
  10%/92% steps plus a named stage. That is the ceiling of reporting; the cost is the remaining
  work.

### D-17 — The control socket does nothing on Windows
- **Area:** core / service · **Status:** **Deferred** (stubbed, and it says so) · **Severity:** S3
- **Found:** 2026-08-17, while building S5 — a known limitation, filed so it is tracked rather
  than remembered.
- **Reproduce:** `cosmo.exe --control \\.\pipe\cosmo` on Windows → the log carries
  `control channel is not implemented on Windows yet` and the app runs normally, unattended.
- **Expected:** R-SVC-8 on every platform cosmo ships to.
- **Actual:** POSIX only. `ControlChannel::open()` returns false with a reason on Windows and
  every other method is an inert no-op, so `--control` fails loudly at startup instead of hanging
  later. The branch is `-fsyntax-only` clean but has never been built or run.
- **Judgement:** requirement gap against R-SVC-8, which does not name platforms. Deferred rather
  than open because it is a deliberate stop, not an oversight: an overlapped named pipe needs a
  per-instance state machine (`CreateNamedPipe` + `FILE_FLAG_OVERLAPPED`, a pending
  `ConnectNamedPipe` per client with its own event, a pending `ReadFile` per client,
  `GetOverlappedResult(..., FALSE)` per frame) whose failure mode is a **hung UI thread** — and
  it cannot be tested from this Linux host at all. `PIPE_NOWAIT` is not the shortcut: it is
  legacy and its writes silently drop data when the buffer fills, so the event stream would lose
  lines with no error anywhere.
- **Fix:** pending, and the cheaper route is recorded in `ControlChannel.cpp`: Winsock `AF_UNIX`
  (Win10 1803+, `<afunix.h>`) makes the POSIX branch nearly portable — `ioctlsocket(FIONBIO)`
  for `O_NONBLOCK`, `DeleteFileA` for `unlink`, and no SIGPIPE to suppress. Do that on a Windows
  box, where it can be run. Until then the CLI (`cosmo-cc run`) is the whole harness on Windows,
  and it is unaffected.

### D-5 — The UI logs nothing, so a visual bug report cannot be traced
- **Area:** design / observability · **Status:** Confirmed (by source inspection) · **Severity:** S2
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `grep -rn "LOG[DIWE]" apps/cosmo/App.cpp apps/cosmo/widgets/ | wc -l` → 0. The only
  `LOGI` calls in the whole desktop app are startup and the export batch start/finish.
- **Expected:** the user says "this button does nothing" and the log says which widget consumed the
  click, or that nothing did.
- **Actual:** clicks, screen transitions, selections, panel and tab changes, scroll clamping, value
  commits and renders are all invisible. This is the single biggest observability gap in the app and it
  blocks the intended workflow (user reports a symptom → agent reads the log).
- **Judgement:** defect — no requirement guarantees it, but the reporting workflow is unusable without
  it. `R-AGENT-*` will state the contract.
- **Fix:** pending. P0.4 + P0.5.

## Closed

### D-44 — Switching to a photo re-decodes it, because the preview cache is filled only by rendering
- **Area:** core / engine · **Status:** **Fixed** (same session it was filed) · **Severity:** S2
- **Found:** 2026-08-23, reported by the user: *"sometime changing photo take too long"*, with a log
  showing one hop at 250 ms and the next at 2537 ms.
- **Front end:** the GTK app (the user's log); reproduced headlessly with the fixture below.
- **REGRESSION, and mine.** `1897d27` (R-MEM, yesterday) capped the engine's resident pixels. Before
  it, all 120 sources stayed resident, so every hop was warm — at the cost of the 54 GB that commit
  existed to remove. The memory fix was right; it traded 465 MB/photo for ~1 s/hop and **nothing
  measured the second half of that trade**, which is precisely what R-MEM-5 was written to prevent.
- **Reproduce:** `apps/cosmo/core/tests/fixtures/preview_hop_latency.cpp` (compile line in its
  header), then:
  ```bash
  /tmp/hoplat <one.ARW> <project-with-120-photos.cmp>
  ```
- **Expected:** R-MEM-5 — "the caps must be generous enough that **ordinary work — stepping along the
  filmstrip**, adjusting the photo in front of you, undo/redo — **hits resident pixels**."
- **Actual:** it almost never hits resident pixels. Walking a 120-photo project, **8 of 10 hops
  re-decoded the RAW**:
  ```
  node=7   1622.9 ms  rehydrated=1        node=6   (revisit)   683.0 ms  <- warm
  node=8   1866.6 ms  rehydrated=1        node=61  (revisit)   660.9 ms  <- warm
  node=61  1630.4 ms  rehydrated=1
  node=62  1626.6 ms  rehydrated=1
  ```
  A cold hop is ~1.8 s, a warm one ~0.65 s. That difference is the user's "sometimes".
- **Evidence:** the same fixture's stage breakdown says where the extra second goes — 16 engine
  threads, Sony 24 MP ARW, previewEdge 1600:
  ```
  1. LibRaw decode -> RGBA8 6024x4024   1020.9 ms  (92 MB)   <- the whole variance
  2. -> linear float source              105.3 ms  (369 MB)
  3. add + proxy + first render          841.5 ms  (proxy 26 MB)
  4. render again, proxy warm            601.4 ms             <- D-45, the floor
  ```
- **Judgement:** defect — contradicts R-MEM-5 as written. **R-MEM-5 amended in the same commit**,
  because its own wording is half the problem: "generous enough" is a claim with nothing reading it
  back, which is what §3d forbids and how D-11 and D-12 shipped. It now states a measurable target
  and names the fixture that measures it.
- **Cause:** two independent reasons a photo is cold, and both need fixing:
  1. **The proxy is built lazily, in `ensurePreviewProxy` (`EditEngine.cpp`), so it exists only for a
     photo that has already been rendered.** After a load the *sources* were evicted as the load
     progressed (correctly) but no proxies were ever built, so the whole rack is cold and the first
     visit to every photo pays a full decode. `engineRehydrations=0` right after the load and 1 per
     hop afterwards is exactly this.
  2. **Even with proxies built, the pool cannot hold the rack.** A 1600 px proxy is 26 MB in linear
     float and the cap is 1 GB, so ~37 of 120 fit. `resident` climbing 765 → 974 MB over ten hops is
     the pool filling one proxy at a time.
- **Requirement:** R-MEM-5 (violated; amended while filing), R-MEM-1/2 (unchanged and still right).
- **RECOMMENDED FIX — not applied.** Three parts, in value order; the first alone removes most of it.
  1. **Build the proxy at ingest, on the render worker that already holds the bytes, and drop the
     full-resolution source right after.** The worker converts to linear anyway (105 ms) and the
     downscale is ~130 ms — both already paid during a load — so the added cost is bounded and off
     the UI thread. This makes the load *fill* the browse cache instead of leaving it empty. Do it in
     `RenderService`'s add path rather than in `EditEngine::addImage`, so the non-threaded build
     keeps its current inline behaviour.
  2. **Size the pools from the machine, not from a constant.** 800 MB / 1 GB are literals chosen
     blind; on this 27.7 GB box a 3 GB proxy pool holds all 120 and is still ~11% of RAM. The host
     can query physical memory (`GlobalMemoryStatusEx` / `sysconf`) and call the existing
     `setMemoryCaps` — no new seam, and the platform call stays in the host layer where it belongs.
  3. **Make the decode a cold slot needs cheaper** — this is **D-24**, still open, whose own fixture
     already measured `user_qual=0` at ~7x faster with identical dimensions. A re-decode for a
     *preview* does not need the highest-quality demosaic. Landing D-24 shrinks what remains of this
     defect and every other decode in the app.
- **Also fix, and justified by the silence (§6):** `frame.ready` **never reports its ms**.
  `Event::ms` exists and `formatEvent` prints it when non-zero (`Event.cpp:67-70`), but
  `CosmoService.cpp:256` emits the event without it — so neither the user's log nor a script can tell
  a 250 ms hop from a 2537 ms one without timestamps and arithmetic. The user had to notice this by
  feel. One argument at the emit site, and the reproduction above becomes a one-line `cosmo-cc`
  script for everyone.
  for a rack that fits the cap — which fails on today's code at the first hop.

- **Fix:** commit `70e6abb`. Two changes, and both were needed — the first alone still left the
  early photos cold, which the re-measurement caught:
  1. `EditEngine::addImagePreviewOnly` builds the proxy **at ingest** and keeps no source, through
     `downscaleEncodedToLinear` — the same box filter with the sRGB LUT folded in, so the 387 MB
     linear-float intermediate is never allocated. `RenderService`'s worker uses it for any slot
     that has a source path behind it.
  2. `apps/cosmo/PixelBudget.h` sizes both caps from physical RAM (~1/12 sources, ~1/5 proxies),
     applied by both hosts. The 1 GB literal held 37 of 120 proxies on a machine with 27 GB free.
  Plus the observability half: `frame.ready` now carries `ms=` and `rehydrated`.
- **Verified:**
  ```
                       BEFORE                        AFTER
  walking the rack   8 of 10 re-decoded            0 of 10
                     cold ~1.8 s / warm ~0.65 s    uniformly ~0.62 s
  resident           765 MB                        3079 MB (120 proxies, cap 5.6 GB)
  load, 120 photos   50 s                          65 s
  ```
  **The cost is stated rather than buried:** ~2.3 GB more resident and ~15 s more load, for removing
  a ~1 s re-decode from nearly every photo switch for the rest of the session. The load regression is
  the fused downscale running on the render worker, which holds ONE engine thread while a load is in
  flight (R-CPU-2); moving it onto the decode pool, where the thumbnail is already built
  (R-LOADPERF-2), is recorded in `PROGRESS.md` as the follow-up.
- **Guarded by:** `walking_a_rack_that_fits_the_cap_never_re_decodes` in `sessionTests.cpp` — twelve
  photos, two full walks, and the decode count may not move past the load's. **Checked to fail with
  the preview-only ingest removed**, at exactly that assertion.

### D-43 — Every `assert()` in both cosmo suites is compiled out in a Release build
- **Area:** core / test infrastructure · **Status:** **Fixed** (same session it was found) · **Severity:** S1
- **Found:** 2026-08-22, while checking that D-41's new guard failed on the pre-fix code. It did
  not: the guard passed with the thing it guards deleted. Found the only way this class of failure
  ever is — by deleting a line the tests were supposed to be catching and watching them stay green.
- **Front end:** none — `cosmo_core_tests` and `cosmo_widget_tests`.
- **Reproduce:** on any tree configured `Release` (which the MSYS2 tree is, and which is where all
  the Windows work happens):
  ```bash
  grep CMAKE_BUILD_TYPE build-mingw64/CMakeCache.txt        # -> Release
  grep CMAKE_CXX_FLAGS_RELEASE build-mingw64/CMakeCache.txt # -> -O3 -DNDEBUG
  # delete the pin from PinnedDecoder::decodeFile, rebuild, run the suite:
  ./build-mingw64/apps/cosmo/core/cosmo_core_tests.exe      # -> 37x [PASS], exit 0
  ```
- **Expected:** R-TEST-1 — "a failing assertion exits, promptly and non-zero, on every supported
  host". A suite of 37 assertion-based tests reports red when an assertion is false.
- **Actual:** `-DNDEBUG` defines `assert` to `((void)0)`, so **every test in both suites checked
  nothing**. They printed `[PASS]` line by line and exited 0 with the code under test broken. The
  print statements were the only thing still running.
- **Evidence:** the same binary, same test, with only `#undef NDEBUG` added to `TestMain.h`:
  ```
  before:  [PASS] every_decoding_thread_is_pinned (1 threads pinned to 1)     exit=0
  after:   Assertion failed: ompPinnedThreadCount() == before + 1 &&
           "decodeFile must pin the thread it decodes on (D-41)",
           file .../sessionTests.cpp, line 620
           [abort] assertion failed — exiting 3                               exit=3
  ```
  Note the `1 threads` in the "before" line: the count was visibly wrong on stdout and nothing
  failed. D-42's guard did report red before this was fixed only because it exits via `_Exit(3)`
  from a watchdog rather than through `assert`.
- **Judgement:** defect — violates R-TEST-1 outright, and voids its premise. Every "the suite is
  green" claim made from a Release tree is worth nothing, which includes every verification done on
  Windows. **R-TEST-3 written** to state the rule rather than leave it as a fixed accident.
- **Cause:** the suites are written in plain `assert()` (by design — see the skill and R-TEST-1),
  and nothing prevented `NDEBUG` from reaching them. Not a Windows defect at all: any Release
  configure on any host has the same effect. It surfaced here because Windows is where a Release
  tree was being used for verification.
- **Regression?** No — the suites have always been assert-based and `Release` has always defined
  `NDEBUG`. Latent since the first Release configure; invisible because a suite in this state is
  indistinguishable from a passing one.
- **Requirement:** R-TEST-1 (violated), R-TEST-3 (written while filing this).
- **Fix:** commit `b0a4fbb`. `TestMain.h` undefines `NDEBUG` before including `<cassert>`,
  unconditionally — `<cassert>` is specified to be re-includable and to re-read `NDEBUG` each time,
  which is what makes this work rather than a trick. It is in that header rather than in each CMake
  target because both suites already include it and any future one will (R-TEST-2).
- **Guarded by:** the whole of both suites, which now assert. Directly checked by breaking
  `PinnedDecoder::decodeFile` and confirming `exit=3` with the assertion text on stderr; the same
  break exits 0 without the fix. `ctest` is 17/17 with assertions live.

### D-41 — The OpenMP pin reaches only the load pool, so every other decode ignores the CPU limit
- **Area:** core / load · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-22, reported by the user as "the CPU limit feature doesn't work on Windows".
  D-12's fix finished only halfway: it was landed on a Linux host where it could not be observed at
  all, and D-12's own entry asks for exactly this confirmation on MSYS2.
- **Front end:** reproduced with `cosmo-cc`; the same call was in the GTK app at three sites.
- **Reproduce:** the committed fixture measures cores-busy for three decode paths at two budgets:
  ```powershell
  pwsh apps/cosmo/core/tests/fixtures/cpu_budget_win.ps1 `
       -Cc  build-mingw64/apps/cosmo/cli/cosmo-cc.exe `
       -Raw C:/path/to/one.ARW
  ```
- **Expected:** R-CPU-1 — "cosmo's background CPU work runs on a **budget**: a percentage of the
  machine's logical cores"; R-CPU-2(c) — "nested parallelism inside a decoder … a second layer
  inside one image is pure oversubscription"; R-CPU-4 — the budget bounds "the threads cosmo starts
  **plus the one nested pool it can reach**". A decode is a decode: which internal path reached it
  is not something the user chose.
- **Actual:** the pooled paths obeyed the budget and the bare decode was **completely insensitive**
  to it — same wall time, same 4.5 cores, same 20 OS threads at 25% and at 100%:

  | path | budget 25% | budget 100% | |
  |---|---:|---:|---|
  | `project` (ProjectLoader pool, pinned) | 3.32 cores · 20.8% | 7.71 cores · 48.2% | budget honoured |
  | `render` (engine, `par::setThreads`)   | 3.13 cores · 19.6% | 7.34 cores · 45.9% | budget honoured |
  | `info` (bare `NativeImageDecoder`)     | **4.64 cores · 29.0%** | **4.50 cores · 28.1%** | **outside the budget** |

- **Evidence:** the control isolated the cause to LibRaw's OpenMP team rather than anything of
  cosmo's — the identical decode with the nested team pinned before `exec` (the only way libgomp
  reads it, D-12), on the same 16-core box:
  ```
  info  budget= 25%                      meanCores=4.64  peakOSThreads=20
  info  budget= 25%  OMP_NUM_THREADS=1   meanCores=1.54  peakOSThreads=9
  ```
- **Judgement:** defect — contradicted R-CPU-2(c) and R-CPU-4 as written. R-CPU-2 amended twice
  while fixing: the pin belongs to every thread that decodes, and the team size comes from the
  budget rather than being 1 everywhere (see **Fix**).
- **Cause:** `pinNestedOpenMPForThisThread()` was wired in exactly one place —
  `CosmoService::setWorkerInit`, which `OrderedParallelLoad` calls on each pool worker. Every decode
  that did not come from that pool constructed a bare `NativeImageDecoder` on an unpinned thread:
  `linux_main.cpp:237` (`openImageFile` — the user opens one photo, or launches `cosmo <image>`),
  `:291` (a `.cosmo` session), `:361` (the synchronous workspace load, once **per entry**) — all
  three on the **GTK main thread** — plus `cli/main.cpp` `info` and `bench`, and `renderShots.cpp`.
  Same shape as D-11: the budget had an owner, and a consumer that never asked it anything.
- **Platform note:** effect was Windows-only, which is why the user saw it and CI did not. MSYS2's
  LibRaw is built `-fopenmp` so the nested team exists; the vendored Linux `libraw.a` has no `GOMP_*`
  symbols, so on Linux these same call sites decode single-threaded and the hole is invisible.
  `ompPinStatus()` reported the pin resolving correctly on Windows the whole time — it was simply
  never called.
- **Regression?** No. `git log -S "pinNestedOpenMPForThisThread" -- apps/cosmo` shows it arrived in
  `95f3b78` (D-12's fix) already wired only to the worker hook; later commits copied that wiring.
- **Requirement:** R-CPU-2(c), R-CPU-4 (both violated); R-CPU-2(c) amended twice while fixing.
- **Fix:** commit `b0a4fbb`. `cosmo_v2::PinnedDecoder` (`apps/cosmo/PinnedDecoder.h`) wraps
  `NativeImageDecoder`, pins the calling thread and delegates — and is now the **only** decoder the
  host constructs, so the pin is a property of decoding rather than a hook a call site can forget.
  D-11's lesson applied a second time: a single owner, not a better clamp. The worker hook stays as
  well, because the failure is silent.

  **The recommendation filed with this entry said "pin once per thread", and that turned out to be
  half right.** Implemented literally — team size 1 everywhere — it removed the violation and made
  opening a 24 MP ARW take **1.85 s against 0.79 s, at 100% as much as at 25%**: insensitivity to
  the setting in the other direction, and a 2.3x regression on the commonest action in the app.
  1 is the *pool's* answer, where `decodeWorkers()` workers times a team of one is exactly the
  budget. A decode running alone has no outer parallelism to oversubscribe against, so it is sized
  from `ThreadBudget::total()`. R-CPU-1 grants the user's share; it does not ask cosmo to leave it
  unused.

  Also fixed here, same function: `pinNestedOpenMPForThisThread` called `omp_set_num_threads(1)`
  unconditionally, overriding a user who had set `OMP_NUM_THREADS` — against R-CPU-2(c)'s own "an
  explicit user value still wins". `ompPinUserOverride()` now reports it and it outranks both.
- **Verified:** after the fix, `info` measures **3.26 cores / 8 OS threads at 25% and 4.71 / 20 at
  100%** — it responds to the setting, stays inside it, and opens in 0.88 s / 0.76 s, so nothing got
  slower. `project` and `render` are unchanged.
- **Guarded by:** `every_decoding_thread_is_pinned` in `sessionTests.cpp` — **checked to fail on the
  pre-fix code**, at `"decodeFile must pin the thread it decodes on (D-41)"`, exit 3. It asserts the
  **count of distinct threads bound**, not an OpenMP team size, because a team assertion is inert on
  any host whose LibRaw has no OpenMP — which is exactly the host this defect hid on. That count is
  reported by `cosmo-cc info`, `cosmo-cc backends` and the host's `LoadFinished` log, so R-CPU-4's
  honesty clause is a number rather than a claim.
- **Note:** this guard is also what uncovered **D-43** — it passed with the fix deleted, because
  `-DNDEBUG` had been compiling every assertion in both suites away.

### D-42 — `OrderedParallelLoad::stop()` signals its condition variable without the lock, and hangs
- **Area:** core / load · **Status:** **Fixed** (same session it was filed) · **Severity:** S1 (hang)
- **Found:** 2026-08-22, on Windows, while reproducing "the CPU limit does not work on Windows"
  (D-41). Found because `cosmo_core_tests` never finishes on this host — which is *why* D-41 shipped.
- **Front end:** none — `cosmo_core_tests`, and by inspection `CosmoService` in every front end.
- **Reproduce:** MSYS2 MINGW64, 16 logical cores. Ran forever; three runs, identical:
  ```bash
  export PATH=/c/msys64/mingw64/bin:$PATH
  export XDG_CONFIG_HOME=/tmp/cosmo-dbg          # a sandbox: the suite writes the REAL recent.tsv
  ./build-mingw64/apps/cosmo/core/cosmo_core_tests.exe > /tmp/tests.out 2>&1 &
  sleep 60 && tail -1 /tmp/tests.out             # -> [PASS] ordered_parallel_load_is_in_order_and_never_stalls
  gdb -q -p $(pidof cosmo_core_tests) -batch -ex "thread apply all bt 8"
  ```
- **Expected:** `stop()` returns and the suite continues. The test that hung asserts exactly this:
  `assert(dt < std::chrono::seconds(3) && "stop() must not wait for the whole batch")`. Beyond any
  document, a hang is a defect (`arstro.cosmo.core.debug` §3).
- **Actual:** the suite stopped dead in `test_ordered_parallel_load_stops_cleanly_midway()` and never
  reached the three CPU-budget tests nine lines later. Killed after 75 s with 1.6 s of CPU consumed —
  blocked, not slow, so the 3 s assertion never got the chance to fail.
- **Evidence:** the two stacks that matter — one thread waiting to be joined, the other waiting for a
  notify that had already happened:
  ```
  Thread 1 (main):
    #0 ntdll!ZwWaitForSingleObject
    #2 libwinpthread-1.dll        <- pthread_join
    #4 (anonymous namespace)::test_ordered_parallel_load_stops_cleanly_midway()

  Thread 6 (a pool worker):
    #0 ntdll!ZwWaitForMultipleObjects
    #3 libwinpthread-1.dll        <- condition_variable::wait
    #6 std::thread::_State_impl<...OrderedParallelLoad<unsigned long long>::start(...)::{lambda()#1}>::_M_run()
  ```
- **Judgement:** defect — a hang. Also violated **R-TEST-1** ("a failing assertion exits, promptly
  and non-zero, on every supported host … a suite that cannot report red is not evidence"): the suite
  could report nothing at all on Windows past test 15 of 36.
- **Cause:** `stop()` mutated the predicate and signalled **without holding `mMu`**. A worker inside
  `mCv.wait(lk, pred)` holds `mMu` while it evaluates `pred`; if it evaluated `false` and `stop()`
  then ran to completion before the worker actually blocked, the `notify_all` reached no waiter and
  none was ever coming. `tryConsume` was always safe signalling outside the lock because it changes
  the predicate *inside* it — a worker in that gap holds the very lock `tryConsume` needs — which is
  what left `stop()`, the one signaller that took no lock at all, as the only racy one. Not
  Windows-specific in principle; winpthreads loses the race every time where glibc almost never does.
- **Was reachable in the product:** `ProjectLoader::stop()` is `mPipe.stop()`, and `CosmoService`
  calls it from `ProjectClose` (the user goes Home) and `ProjectLoader::start()` calls it first thing
  (the user opens a second project). Both on the UI thread while a load is in flight.
- **Regression?** No. `git log -S "mStop.store(true);" -- apps/cosmo/core/OrderedParallelLoad.h`
  returned only `af95c65` (the repo reorganisation): original code, latent since the class was written.
- **Requirement:** R-TEST-1 (existing). `DR-LOADPERF-1` amended to state the signalling rule — its
  deadlock-freedom argument was about the predicate's *logic*, was correct, and never covered the
  signalling, which is why it read as a complete proof for a class that could hang.
- **Fix:** commit `4807110`. `stop()` takes `mMu` for the flag change and notifies after
  releasing it; `mStop` stays atomic because `run()` also reads it outside the lock.
- **Guarded by:** `ordered_parallel_load_stops_cleanly_midway` in `sessionTests.cpp`, rewritten to
  report rather than hang — **checked to fail on the pre-fix code, exit 3, three runs out of three.**
  Two things in its shape are load-bearing and were established by measurement: the watchdog is armed
  **before** the scenario (a deadline checked afterwards never runs when `stop()` never returns), and
  **nothing sits between `tryConsume()` and `stop()`**, with the scenario repeated 20 times. The race
  needs the flag set in the narrow gap between a worker evaluating the predicate and blocking on it;
  a `std::async` version and a version that merely constructed the watchdog thread in between both
  added enough latency to park every worker first, and both **passed on the broken code**.
- **Verified:** all 36 `cosmo_core_tests` pass on MSYS2 for the first time, three runs out of three,
  and `ctest` is 17/17. The three guards that had never run on this host now do:
  `cpu_budget_scales_with_percent`, `one_budget_is_divided_not_duplicated`, and
  `project_load_peak_never_exceeds_its_pool (pool 5, peak 5 of budget 6)` — so D-11's arithmetic is
  now measured on Windows, not only on Linux.

### D-39 — Switching to touch mode showed no project, and the touch shell could have destroyed one
- **Area:** design / touch shell · **Status:** **Fixed** (same session it was reported) · **Severity:** S1 (potential data loss)
- **Found:** 2026-08-20, reported by the user immediately after touch mode shipped: "when change to
  touch mode, all project file must keep the same, make it like a view only … right now when change
  to touch mode i see no project".
- **Reproduce:** open a project in the desktop shell, then Settings → Input → Touch. Headlessly:
  `cosmo_touch_shots --assert` — a project is opened through the service with no touch shell in
  existence, then one is built, and its screen is checked. Fails on the old code.
- **Expected:** R-TOUCH-1 and the dialog's own promise ("touch keeps your project open") — the
  touch shell is a view of the same service, so it shows the open project.
- **Actual:** it came up on its own empty Home screen. Worse than cosmetic: tapping New / Open /
  Import there called `buildSession`, whose first line is `resetWorkspace()` — so the touch view
  could have thrown away the project the desktop view had open, without asking.
- **Cause:** the shell predated the service binding and kept its own project state — `mScreen`
  starting at Home, its own `mRecents` list, its own `mImgs` source images, and
  `buildSession`/`enterProject`/`pushRecent`/`refreshHome` reimplementing the project lifecycle
  against the session. T1 moved the WRITES onto commands but left that lifecycle in place, so the
  view still believed it owned the project.
- **Fix:** `PhoneApp::syncFromModel()` adopts the service's screen, project name, image count,
  edit target and recents, called at construction and whenever `AppModel::revision` moves (the same
  bind-on-revision rule the desktop shell uses). The local lifecycle is deleted: New / Open /
  Import are host seams that end in a `Command`, a recent card dispatches `project open <path>`,
  `finishProject` asks for `screen editor` rather than deciding, and `addProjectImage` — the one
  raw-pixel seam, since no Command carries pixels — no longer resets the workspace. Entering the
  editor over an already-loaded project re-selects the current image so the engine produces a
  preview, otherwise the stage would stay empty until the next edit.
- **Guarded by:** the `--assert` case above and the `adopted` shot (a touch shell built over an open
  project, showing its photo, histogram and controls).

### D-37 — The photo dissolve blinked: one dark frame per render, and a pixel swap under a visible layer
- **Area:** design / photo stage · **Status:** **Fixed** (same session it was reported) · **Severity:** S2
- **Found:** 2026-08-20, reported by the user against the U2.2 dissolve itself: "fix the fade from to
  target effect photo, it doesn't smooth make the photo blink when transition".
- **Reproduce:** open a project, drag any Basic/Detail slider. Headlessly:
  `theStageNeverBlinksDuringADrag` in `cosmo_ui_tests` — 100 adjustments, a new exposure every third
  frame, judging the recorded op stream frame by frame. Both of its assertions fail on the shipped
  code and pass on the fix (verified by re-breaking each cause in turn).
- **Expected:** the composite moves continuously from one render to the next.
- **Actual:** two discontinuities per dissolve cycle, neither of which a still frame or the previous
  tests could see.
- **Cause (a) — a stale alpha, one frame wide.** `PhotoCanvas::advance` set
  `mPhotoBase->visible = mPhotoTop->opacity.value() < 0.999` **before** `Segment::advance` updated
  that opacity, so it decided from the PREVIOUS frame's value. On the first frame of every `1 → 0`
  dissolve the base was therefore skipped while the top had already eased to ~0.9 — the canvas
  (`#0A0A0A`) showed through 10% of the frame. Every other render, i.e. ~8 Hz through a drag: a
  visible flicker.
- **Cause (b) — pixels written under a visible layer.** R-VIEW-1a specified that a render arriving
  mid-dissolve REVERSES it, writing the newest pixels into the layer the dissolve was leaving. That
  layer still contributed `1 − a`, so the composite stepped by `(1−a)` times the difference between
  two renders — and since renders arrive faster than the dissolve, that happened on nearly every one.
  The requirement was wrong, not just the code: it is amended, and the newest frame now waits.
- **Fix:** hold the incoming frame (`mHeld`/`mHeldPending`) while the dissolve runs and apply it from
  `advance()` once it settles (R-VIEW-1a as amended); set `mPhotoBase->visible` at the END of
  `advance`, from this frame's alpha (R-VIEW-1e); and change the curve to `Easing::Linear` over
  160 ms, since an ease-out puts 35% of the change in the first frame.
- **Guarded by:** `theStageNeverBlinksDuringADrag` (both facts) and the re-rendered
  `editor-dissolve-early/late` shots at 1600x1000 and 1280x800.
- **Lesson worth keeping:** both causes are properties of a *frame*, not of the code's structure, and
  the code read correctly in both cases — the second one had a comment explaining why it was safe.
  This is the third time in this project that a snap or a step passed review and was caught only by
  walking frames (R-G-1's clause (d) exists for exactly this).

### D-40 — The D-10 fix did not compile on the Windows host it was written for
- **Area:** core / test harness · **Status:** **Fixed** · **Severity:** S1 (build break: two of the
  suites could not be produced at all, so nothing could be verified on this host)
- **Found:** 2026-08-19, by the user, on the first MSYS2/MINGW64 build after `TestMain.h` landed.
- **Reproduce:** `cmake --build build-mingw64 --target cosmo_core_tests cosmo_widget_tests` on
  MSYS2 MINGW64 (msvcrt-based; `_UCRT` undefined).
- **Expected:** both suites build, as they do on Linux.
- **Actual:** two failures, both from `core/tests/TestMain.h`, both of them the first compile of
  that header on Windows:
  1. **Link.** `undefined reference to __imp__set_abort_behavior`, from `TestMain.h:43`.
  2. **Compile,** in a file that had nothing to do with the change:
     `widgets/tests/widgetTests.cpp:82: error: expected unqualified-id before 'double'` on
     `bool near(double a, double b, double eps = 1e-3)`.
- **Evidence:**
  ```
  $ nm libmsvcrt.a | grep -c set_abort_behavior      ->  0
  $ nm libucrt.a   | grep -c set_abort_behavior      ->  4
  $ g++ -fsyntax-only  #ifdef _UCRT ... #endif       ->  error: UCRT_NOT_DEFINED
  $ grep -n _set_abort_behavior /mingw64/include/stdlib.h
    296:  _CRTIMP unsigned int __cdecl _set_abort_behavior(unsigned int,unsigned int);
  ```
  So msvcrt **declares** the function unconditionally and **exports** nothing: `_CRTIMP` makes it
  `__declspec(dllimport)`, the call compiles, and it resolves only against the UCRT import library.
  The second failure is `<windows.h>`, included by `TestMain.h` for `SetErrorMode`, still defining
  the 16-bit memory-model keywords: `near` and `far` expand to nothing. `widgetTests.cpp:33`
  includes `TestMain.h`, so its float comparison at line 82 became `bool (double a, ...)`.
- **Judgement:** defect, and a **method** defect more than a code one. D-10's entry says in as many
  words that its Windows half was *"code-verified only"* and asks the next MSYS2 session to confirm
  it. Nobody did, and the thing that failed was not the subtle runtime behaviour it warned about —
  it was the build. A header written for a platform and never compiled on it is not verified in any
  sense, and this one then broke a suite that never asked for it.
- **Fix:** commit *"cosmo: the test harness header builds on the Windows host it was written for"*.
  `core/tests/TestMain.h` — `_set_abort_behavior` is gone; a `SIGABRT` handler that prints one line
  and calls `std::_Exit(3)` replaces it, which works on both CRTs and needs no `#if` on the CRT,
  because `abort()` raises `SIGABRT` on both and a handler that does not return never reaches the
  reporting path that stalls. The assert text is already on stderr by then — `_assert` prints before
  it aborts, so nothing is lost by leaving early. The header now also enters with
  `WIN32_LEAN_AND_MEAN`/`NOMINMAX` and leaves with `#undef near` / `far` / `small`, so it cannot
  redefine its includer's vocabulary.
- **Verified:** both targets build and pass; and, for the first time on Windows, the D-10 property
  itself — a probe including the header with one deliberately failing `assert`:
  ```
  run exit=3  elapsed=0.18s
  stdout: [PASS] a test that passes
  stderr: Assertion failed: 1 == 2 && "the deliberate failure", file d36_probe.cpp, line 7
          [abort] assertion failed — exiting 3
  ```
  0.18 s and exit 3, where D-10's symptom was a five-minute stall. `D-10`'s "Windows half code-
  verified only" caveat is now discharged.
- **Guarded by:** the build. `cosmo_widget_tests` declares `near` and includes `TestMain.h`, so the
  macro leak cannot return without breaking a target that is always built; and both suites call
  `testMainInit()`, so a CRT entry point that does not link cannot reach `main`. Requirement:
  **R-TEST-1**, **R-TEST-2** (new — the test harness had no requirement at all, which D-10 noted and
  left).

### D-35 — Changing the edit target left every panel showing the previous target's values
- **Area:** core + design (view-model binding) · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-19, reported by the user: *"it don't change the curve when i change target, each
  target need to get it info when move to, even the group … value on GUI must refresh when select an
  image."*
- **Reproduce:** give two images different curves, then select between them. Before the fix the
  curve panel kept the first one's curve. Same for a group and its child.
- **Expected:** every displayed value is the current edit target's.
- **Actual:** the host *did* call `syncFromSession()` on `SelectionChanged`, and the UI's own
  handlers called `syncControlsToSlot()` — so the push was happening. What it pushed was stale.
  `App.cpp` holds **95 direct `mSession.` calls**, a dozen of them mutations (`selectNode`,
  `navigateToGroup`, `jumpToHistory`, `applyPreset`, `createGroupFromSelection`). Those change the
  session **without going through the service**, so `refreshModel()` never runs, `AppModel` still
  describes the *previous* target, and the sync then faithfully fills every panel from it.
- **Judgement:** defect, and an architectural one rather than a widget bug — the third symptom this
  week whose cause was the view and the view-model disagreeing (D-33 the socket path, D-34 the emit
  ordering, D-35 the direct mutation). All three are the same shape: something changed the model
  without the snapshot the view reads being re-derived first. `App.cpp` was never covered by the S
  milestone's "the widget layer reaches nothing" check, because that check was
  `grep "mSession\.\|\.session()" widgets/*.cpp` — and `App.cpp` is not in `widgets/`.
- **Fix, in two halves.** *(core)* `CosmoService::refreshFromSession()` — a public re-derive, named
  for what it is: a bridge for the sites that still mutate directly. *(design)* R-SVC-12: the view
  binds in ONE place, `App::bindIfStale()` at the top of `render()`, guarded by `AppModel::revision`
  — whose own header had promised since S2 that "a view that has already drawn revision N can skip
  work", which nothing had ever used. `syncControlsToSlot()` becomes the dirty-marker its callers
  already spell: refresh the service, set a flag. A model change now reaches the screen whatever
  caused it, and there is no per-event push to forget.
- **Verified:** driven over the socket against the real window — two images given different curves
  and selected in turn logged
  `setCurves REPLACES [3] …0.750,0.200… -> [3] …0.250,0.700…` on every move, including moving *back*;
  then a group and its child, which is the "even the group" half.
- **Guarded by:** `panelsFollowTheEditTarget` (`cosmo_ui_tests`), which asserts the binding contract
  in **both** directions — the bound revision advances when the model moves, and stands still when
  it has not. The second half matters as much as the first: a bind that ran unconditionally every
  frame would satisfy the report and fight the user's hands on every drag.

### D-34 — An edit was answered by the view being handed back the value it had just replaced
- **Area:** core (service event ordering) · **Status:** **Fixed** · **Severity:** S1 (silent data loss)
- **Found:** 2026-08-19. The user had reported the curve symptom three times — D-28, D-31, D-32 —
  and after the third fix said *"add more debug log i will click for you it still happen."* The UI
  logging that request produced (§7, `WidgetLog`) named the cause on the very first click:
  ```
  [ui] curve: dblclick ADDS node at 0.500,0.500
  [ui] curve: EMIT ch=0 [3] 0.000,0.000 0.500,0.500 1.000,1.000
  [ui] curve: setCurves REPLACES master [3] 0.000,0.000 0.500,0.500 1.000,1.000 -> [2] 0.000,0.000 1.000,1.000
  [ui] curve: setReference master [3] 0.000,0.000 0.500,0.500 1.000,1.000
  ```
  The edit is added, emitted, and then **thrown off the edited curve and onto the readout**. That is
  the whole of "it will select the green curve and it switch to that curve", in four lines.
- **Expected:** an edit stays where it was made.
- **Actual:** `applySetFields` emitted `ParamsChanged` and left `refreshModel()` to its caller, so
  `mModel` still held the **pre-edit** value while the event was being delivered. The GTK host
  re-seeds the right column from the model on that event, so the panel was handed back the curve it
  had just replaced — and the green "final" readout, which is fed from a value read *after* dispatch
  returns, showed the new one. The user's edit visibly jumped from the blue curve to the green line.
- **Judgement:** defect, and the most serious of the four: silent loss of an edit. Every other
  handler in `CosmoService.cpp` already refreshed before emitting — `bypass`, `group new`,
  `ungroup`, `rename`, `delete`, `mask set`, `mask delete`, `select`, `save`, `close`. `set` was one
  of three that did not (`settings set` and the export finish were the others, latent because both
  write straight into `mModel`). Same family as **D-13**, one step further on: announce before
  mutating, and refresh before announcing — an event and the state it describes have to be
  consistent at the moment it is delivered, because a listener reads the model *during* the callback.
- **Fix:** `refreshModel()` moved before `emit()` at all three sites, and the caller's duplicate
  refresh for `Set` removed so one place owns the order.
- **Verified:** the same edit driven again shows `setCurves REPLACES [2] identity -> [3] …` — the
  panel now receives the new curve — and the panel and the model agree at `pts=3`.
- **Guarded by:** `an_event_sees_the_model_it_describes`, which asserts from **inside** the event
  handler that `ownParams` and `params` already hold the new value. That is the only place the bug
  exists: every after-the-fact check of the model passes on the broken code, which is why three
  rounds of widget-level fixes never touched it. Confirmed to fail on the old ordering.
- **Also closes D-33** (the right column not resyncing after a socket-driven `set`), which was the
  same root cause seen from the scripted side and was filed as a separate open item.

### D-32 — A node grabbed near the pointer teleported to it, so the curve snapped onto the readout
- **Area:** design (widget interaction) · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-19, by the user, with a screenshot: *"i still can select and it make this weird
  interaction / when i click slightly off the main curve it will select the green curve and it switch
  to that curve."* The screenshot shows an edited curve zig-zagging through the green readout's
  inflection points — which is what repeated accidental teleports look like.
- **Reproduce:** press a fixed distance above an existing node and move the pointer ONE pixel.
  ```
  node at local (125.8, 73.8); pick radius 13 px
  press offset   grabbed?   node after a 1px drag   moved by
  0              yes        y=72.8                  1.0 px
  4              yes        y=68.8                  5.0 px
  8              yes        y=64.8                  9.0 px
  12             yes        y=60.8                 13.0 px     <-- 1 px of input, 13 px of movement
  14             no         y=73.8                  0.0 px
  ```
- **Expected:** a grab moves the thing with the pointer. A forgiving pick radius exists so you can
  hit a 4 px node without being precise; it is worthless if the node then jumps to wherever you
  were imprecise.
- **Actual:** `mDragKind == 0` wrote `nx(pl.x)` / `ny(pl.y)` — the pointer's own position — straight
  into the node, with no grab offset. So a press anywhere inside the 13 px radius grabbed the node
  and the first pixel of movement teleported it up to 13 px, **to the click point**. Both halves of
  the user's sentence come from that one line: "click slightly off the main curve" is a press inside
  the radius, and "it switch to that curve" is the node landing where they clicked — which, if they
  were aiming near the readout, is *on* the readout, so their curve visibly snapped onto the green
  line. The tangent handles had the same defect.
- **Judgement:** defect. Not the reference's fault at all: D-28 and D-31 both chased the green line
  because that is where the symptom appeared, and the cause was in the drag arithmetic the whole
  time. The green curve was only ever the thing the node happened to land on.
- **Cause:** `widgets/CurvePanel.cpp` drag branch (and `HueCurveEditor.cpp`, identically): absolute
  pointer position assigned to the dragged element instead of pointer-plus-grab-offset.
- **Fix:** `beginGrab()` on the press records where inside the grabbed thing the pointer sat, in
  curve space; `grabbedX/Y()` add it back on every drag. A node grabbed 12 px off-centre simply
  stays 12 px from the cursor for the rest of the drag. Handles record their own offset (node +
  ix/iy or ox/oy), so they do not jump either. Hue is cyclic, so its x offset is carried in degrees
  and re-wrapped after adding; y is clamped to the axis after adding, not before.
- **Verified:** the same sweep reports exactly 1 px of movement for 1 px of input at every offset
  from 0 to 12. Through the whole app — `App::pointer` → router capture → `toLocal` → the UI-scale
  transform — a 24 px drag moves the node **24.0 px** (a teleport would be 34). Both guards were
  confirmed to FAIL against the old arithmetic before being kept.
- **Guarded by:** `curveGrabMovesByTheDragNotToThePointer` (widget level, swept over the whole pick
  radius, because the SIZE of the jump is the bug and a spot check at offset 0 passes on the broken
  code) and `curveNodeGrabThroughTheAppDoesNotTeleport` (`cosmo_ui_tests`, through the real gesture
  router, because the report was about the live window).
  **Two existing tests had to be rewritten:** `hueEnlargedTargetIsClickable` and
  `curveEnlargedTargetIsClickable` both asserted that the grabbed node landed *at the pointer* —
  they had encoded the teleport as the expected result while proving the pick radius worked. A test
  that pins a bug in place is worse than no test, and these two are why the defect survived every
  suite.

### D-31 — The green "final" curve was still a target, and D-28's test was a spot check
- **Area:** design · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-19, by the user, after D-28 shipped: *"i still can select and adjust the green
  curve, it should act as shown only no interact."*
- **Reproduce:** with a group curve set so the readout is drawn, double-click anywhere on the green
  line. Measured across 21 points along it:
  ```
  x       refY       Down+Drag              DoubleClick
  0.00    0.00       CHANGED the curve      -
  0.10    0.04       -                      ADDED a node
  ...
  0.95    0.92       -                      ADDED a node
  aimed-at-green gestures that changed the edited curve: drag 3/21, double-click 18/21
  ```
- **Expected:** the readout is shown and nothing else — no gesture aimed at it does anything.
- **Actual:** two ways in, neither of them a property of the reference itself:
  1. **Double-click adds a corner exactly where you clicked — 18 of 21 points along the line.** The
     new node lands *on* the green curve, so the edited curve snaps to touch it and the user has,
     to all appearances, grabbed the reference and dragged it. `pointAt` returning -1 means "empty
     space", and the reference is drawn in space the plot considers empty.
  2. **A press at either end grabs the user's own endpoint node**, because the two curves share
     their endpoints and the pick radius is 13 px.
- **Judgement:** defect against the user's stated requirement and against **R-G-3**'s premise, same
  as D-28 — but D-28 fixed the *appearance* and left the *reachability*. Nothing about the
  reference's geometry was ever wrong: it owns no nodes and is not hit-tested. It was simply **on
  screen to be aimed at** while the plot accepted gestures anywhere.
- **The test is the more important finding.** D-28's guard asserted this at ONE point and passed.
  That point (x = 0.5, with an identity curve) is one of the three of 21 where nothing happens. It
  is exactly the mistake `homeHeaderTitleNeverRunsUnderTheSearchField` was written to avoid three
  commits earlier — *a threshold proves nothing about a spot check* — and I made it anyway, in the
  same session, in a test whose whole purpose was to prove non-reachability.
- **Fix:** the readout **leaves while the plot is being worked in**. `mPressed` on Down inside the
  plot, cleared on Up, plus a 220 ms `mRevealAtMs` hold so a double-click's two presses read as one
  gesture instead of flashing the line between them. Fade out 110 ms, in 180 ms — getting out of the
  way should feel immediate, coming back should not startle (R-G-1). Same in `HueCurveEditor`. A
  line that is not on screen during the gesture cannot be aimed at, and the readout is still there
  whenever the user is not editing, which is when they are reading it.
  Deliberately NOT changed: double-click still adds a corner where you click (the documented editing
  model), and a press within 13 px of your own node still grabs it (that is your node).
- **Verified:** the same 21-point probe reports the readout on screen at **0** of 21 points during a
  press, down from 21; rendered at rest and during a press — dashed line plus caption, then neither.
- **Guarded by:** `curveReferenceIsNotEditable`, rewritten as a **sweep**: the readout is drawn at
  rest, is absent at every one of 21 points along itself after a press, stays absent through the
  hold, returns afterwards, and no press on it adds a node. It also records that at most 4 samples
  move anything at all — the endpoint ones — instead of pretending the shared endpoints do not
  exist. One verdict per property rather than one per sample.

### D-29 — The screen-scale setting snapped, and R-G-1 had said not to
- **Area:** design · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-19, by the user, immediately after the setting shipped: *"fix scale change
  that will apply animation"*. Filed alongside the rule amendment it caused, because the useful
  artefact is not this bug — it is the third instance of the same one.
- **Reproduce:** pick any other Screen scale. Before the fix the whole shell cut to the new size
  in one frame. `cosmo_ui_tests`' `scaleChangeIsAnimatedNotSnapped` fails against that code on
  "the drawn scale passes through several values strictly between old and new".
- **Expected:** R-G-1, which the same session had just quoted while writing R-SCALE: no visible
  property changes in a single frame.
- **Actual:** `setUiScale` assigned `mUiScale` and re-derived the logical size in the same call —
  the largest visible change the app can make (every rect, every type size, the transform the whole
  tree draws through), snapped.
- **Judgement:** defect against **R-G-1**, no interpretation needed. Worth recording *why* it got
  written: a scale is a *setting*, and a setting feels like configuration rather than motion. The
  grid-reflow snap (R-G-1a) was "a layout consequence of a resize"; this one was "a preference".
  Both were the same mistake, which is why R-G-1 is now amended to say that no such category
  exists, and why its compliance clause now says reading the code does not count as checking.
- **Fix:** `mUiScale` stays the **target** (a preference is a number); a new `mScaleAnim`
  `AnimatedProperty` carries the **drawn** scale and eases over 260 ms. `rootTransform()` and
  `toLogical()` read the eased value, and `App::render` re-derives the logical box and re-runs
  `layout()` **every frame while it moves** — deriving it once would have animated the transform
  and left the panels at the old size, which is worse than a snap. The window minimum stays on the
  *target* scale so it does not wobble mid-tween. `applySettings` passes `animate = false`: the
  startup apply has no previous scale to travel from.
- **Verified:** `cosmo_ui_tests` (new target) asserts the drawn scale passes through several
  intermediate values, that the logical size changes on those same frames, and that it arrives
  exactly on target; shots `scale-zoom-{000-before,060-mid,140-mid,999-after}` are the two
  mid-tween frames a human can compare, and they are visibly different sizes.
- **Guarded by:** `scaleChangeIsAnimatedNotSnapped` and `theStartupScaleDoesNotAnimate` in the new
  **`cosmo_ui_tests`** target — which exists because of this defect. `cosmo_widget_tests` could
  never have caught it: it builds widgets in isolation, with no `App`, no service and no clock.

### D-30 — The settings card and its chips asked each other how big they were, and the stack ran out
- **Area:** design · **Status:** **Fixed** · **Severity:** S1 (crash)
- **Found:** 2026-08-19, by `cosmo_shots` segfaulting on the very next run after chip wrapping was
  added — a 74,000-frame stack, so the cause was legible immediately from the backtrace.
- **Reproduce:** open the Settings dialog on the intermediate commit; `cosmo_shots --check` dies
  with SIGSEGV in `SettingsDialog::cardRect`.
- **Actual:** mutual recursion. `cardRect()` summed `rowBlockH(r)` (a wrapped row is taller than an
  unwrapped one, so the card's height depends on the wrap) → `rowLines()` → `chipRects()` → and
  `chipRects` asked `cardRect()` for the edges to wrap against. "How big is the card" and "where do
  the chips go" each needed the other's answer first.
- **Judgement:** defect, mine, introduced and found within one edit — recorded rather than quietly
  fixed because the shape is worth having written down: it is what happens when a *measure* query
  and a *place* query are the same function.
- **Cause:** `chipRects` used `cardRect()` for both its wrap bound and its origin, when only the
  origin needs it — the wrap bound is `kCardW`, a constant.
- **Fix:** one private `layoutChips(row, left, top, out)` does the single walk and returns the line
  count; `rowLines` calls it with no origin and no output vector, `chipRects` calls it with both.
  The measure path no longer touches `cardRect` at all, so the cycle cannot re-form.
- **Verified:** `cosmo_shots --check` renders all 41 fixture-free shots and exits 0; the settings
  shot shows the scale row wrapped 5 + 2 with the card grown to hold it and Done inside.
- **Guarded by:** `cosmo_shots_headless` in ctest, which now covers the dialog — it renders
  `home-settings` and every `scale-*-settings-min`, so any return of the recursion is a crashing
  test rather than a crashing app.

### D-28 — Two curves in one plot, and only one of them is yours
- **Area:** design · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-19, reported by the user: *"the shadow curve in curve[d] can be select and
  adjust lead to undefine behavior. I can now adjust 2 independent curve in the same curve
  section."* The "shadow curve" is the **effective ("final") curve** DR-EDIT-5 draws behind the
  editable one — confirmed with the user against a rendered frame before anything was changed.
- **Reproduce:**
  ```bash
  cosmo --control /tmp/c.sock --project <a>.cmp &
  printf 'select 1\ngroup new "G"\nselect 9\nset curve=0,0;0.5,0.25;1,1\nselect 1\nwait 400\nui dump --root editor\n' > s.txt
  cosmo-cc attach /tmp/c.sock --script s.txt | grep channel=
  #  · channel=0 pts=2 ref=33 refDrawn=1 plot=10,51 304x164 dragIdx=-1 dragKind=0
  ```
  Then open Mixer/Curve and press-drag anywhere along the green line: `dragIdx` stays `-1`,
  `curve=` does not change. The line is drawn; it is not grabbable.
- **Expected:** one plot, one curve you can edit, and anything else in it legible as a readout.
- **Actual:** the reference was a **solid 1.5 px line — the same weight as the editable curve** —
  with no caption. Two equal-weight lines in one interactive plot, one of them completely inert:
  a press on it lands in `pointAt`, finds no node (the reference has none), sets `mDragIdx = -1`
  and silently consumes the gesture. A double-click on it *does* fire — it adds a corner to the
  **other** curve at that point, so the editable curve jumps to touch the line the user was
  aiming at, which reads as having grabbed it. From there the two lines move together in a way
  with no explanation on screen, which is the "undefined behavior" reported.
- **Judgement:** defect. Not against DR-EDIT-5's intent — the reference should be drawn — but
  against **R-G-3**'s premise: hover and affordance are how cosmo says what is interactive, and a
  line drawn exactly like the editable curve claims to be interactive while having no hover, no
  nodes and no response. The sliders' equivalent (DR-EDIT-4's green stacked reach) never had this
  problem because a reach-and-tick behind a thumb cannot be mistaken *for* the thumb; the curve
  version was given the editable curve's own visual weight.
- **Cause:** `CurvePanel.cpp` `strokeCurve(ref, kRefColor, 1.5)` — same width as
  `strokeCurve(active(), accent, 1.5)` two lines below; `HueCurveEditor.cpp` the same at 1.5
  against an editable 2.0. Present since the reference feature was written (`git log -S
  "setReferenceCurves"` → `af95c65`, the repo reorganisation), so not a regression — it has simply
  never been looked at with a group curve set, which is the only state that draws it.
- **Fix:** the reference is drawn as a readout, in both editors — **dashed** (new
  `widgets/DashedLine.h`, arc-length stepped so the dash rhythm survives tight bends; hand-rolled
  because `IRenderTarget` has no dash state and `core/Artboard` is a submodule), **1.0 px** against
  the editable curve's 1.5, **α 0.34** instead of 0.5, and **captioned** `— final, with group`. It
  now also fades in and out over 180 ms instead of blinking (R-G-1, which it had been violating).
  Input is unchanged: it never was in the input path, and that is now asserted rather than assumed.
- **Verified:** headless render of the real `StackPanel(MixerPanel, CurvePanel)` at the shipping
  324x710 geometry, before and after — both editors now show a dashed, dimmer line with its
  caption, unmistakable against the solid node-carrying curve.
- **Guarded by:** `curveReferenceIsNotEditable` (press + drag on a point exactly on the reference
  emits nothing, adds nothing, moves nothing — and the identical gesture with no reference set
  behaves identically, so the reference is provably outside the input path; the double-click that
  adds a corner to the *own* curve is asserted too, so it stays a decision) and
  `curveReferenceLooksLikeAReadout` (the reference is stroked thinner than the edited curve, and in
  many short segments rather than one path — which is what dashed looks like from outside).

### D-26 — The splash's progress bar is drawn only while the splash is fading out
- **Area:** design · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-19, by the `ui dump` feature filed as D-25 — the first thing it was pointed
  at. Two prior attempts to answer the same question with screenshots produced a crop of the
  user's own desktop and a flat dark rectangle.
- **Reproduce:**
  ```bash
  cosmo --control /tmp/c.sock &
  printf 'ui dump --root splash\nwait 80\n%.0s' {1..30} > s.txt
  cosmo-cc attach /tmp/c.sock --script s.txt | grep '·'
  #  960ms  progress=0.615 barAlpha=0.229 ... alpha=0.416 exiting=1
  # 1040ms  progress=0.883 barAlpha=0.067 ... alpha=0.076 exiting=1
  # 1120ms  progress=0.964 barAlpha=0.001 ... alpha=0.001 exiting=1
  ```
- **Expected:** a progress bar the user can see, reaching 100% before the splash leaves.
- **Actual:** the bar's peak opacity over its whole life was **0.229**, held for one 80 ms sample,
  and every frame of it fell inside the exit fade. The fill peaked at **0.964** — it never once
  drew full. The splash was up for ~1.2 s and the bar was effectively invisible for all of it.
- **Judgement:** defect against the request that added it ("Loading page when open app need to have
  progress bar and text to indicate loading progress too"): the feature shipped, built, and did
  nothing. The bar had been *reviewed* by reading its drawing code, which is exactly the check that
  cannot catch a timing failure.
- **Cause:** two animations racing an exit that no longer waits for anything. `mBarFade` began its
  260 ms fade-in on the first `setProgress` call, and `mProgress` eased over 220 ms — but since
  DR-SPLASH-5a a cover is the embedded preview at ~7 ms instead of an 8072 ms decode, so with a
  handful of recents the first and last `setProgress` land in **the same tick**, which is also the
  tick that calls `beginExit()`. The bar's entrance, its fill and the splash's 260 ms departure all
  started together. The speedup that fixed the freeze is what made the bar unviewable; nothing was
  wrong with either change alone.
- **Fix:** the track fades in with the dots during the intro (`begin()`), so the bar is present
  before there is anything to report and merely fills; and `beginExit()` now only *requests* the
  exit — `advance()` starts the fade once the eased fill reaches ≥ 0.995. Launch grows ~200 ms
  (1.2 s → 1.55 s), the same trade R-SPLASH already makes for the intro.
- **Verified:** re-sampled at 80 ms through the same `ui dump`: `barAlpha` reaches 0.986 at 960 ms
  and holds **1.000** through 1280 ms while the fill goes 4 → 332 px and the count reads `1 of 1`;
  only then does `exiting` flip and the fade run.
- **Guarded by:** nothing automated — the failure is a timing relationship between two eased
  properties and a host tick, and the harness that would assert it is the `ui dump` sampler itself.
  Recorded as such rather than claimed: this one is guarded by the reproduce block above.

### D-25 — A UI question can only be answered by photographing the screen
- **Area:** tooling · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-19, reported by the user — "do it with out capturing screen, add feature to
  debug UI (like get the visual component info through ipc)" — after watching several turns go into
  screenshots that answered nothing.
- **Expected:** the state of the view is readable the way the state of the model already is
  (`state print`).
- **Actual:** it was not readable at all. Every question about layout, visibility or a widget's
  drawn state had to go through a screen grab: slow, racy against animation, impossible headless,
  and dependent on guessing crop coordinates on a multi-monitor desktop. One 4 px progress bar cost
  an afternoon and was still not seen — and when it finally was measured, it turned out not to be
  drawn at all (D-26).
- **Judgement:** defect against R-SVC-1's premise. The whole service architecture exists so an
  agent can drive the app without looking at it; leaving the *view* unreadable meant half the app
  stayed behind glass.
- **Fix:** `ui dump` (R-SVC-11 / DR-SVC-11) — the Segment tree as text over the control socket,
  plus `UiInspectable` for widgets that paint themselves and so have no children to walk.
- **Verified:** dumps the live editor tree (10 top-level children, world rects, `SHOWN=no` on the
  hidden compare pane), the home screen, and the splash — the last of which immediately produced
  D-26. The acceptance test still passes, so the new command did not disturb R-SVC-9 parity.
- **Guarded by:** `every_command_kind_has_a_grammar` now covers 27 kinds including `ui dump`.

### D-27 — `wait 60` sleeps in `cosmo-cc run` and pattern-matches in `cosmo-cc attach`
- **Area:** tooling · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-19, while sampling the splash for D-26 — 40 dumps 60 ms apart came back
  byte-identical, and the run took 1.2 s instead of 2.4 s.
- **Reproduce:** any `attach` script with `wait <ms>` between two commands: the whole script sends
  in one burst and every dump reports the same frame.
- **Expected:** one word, one meaning, in both front ends — which is literally what the comment on
  the line above the bug said (`canonicalWait(...)  // D-16: one vocabulary, both front ends`).
- **Actual:** `cmdRun`'s `waitFor()` treats a leading digit as a **duration** and spins `pumpOnce`
  for it. `cmdAttach` never had that branch: it put the string into `waitFor` and waited for an
  arriving event line to *contain* it. `wait 60` therefore matched the `260` in a 420x260 splash
  dump and cleared instantly — a sampler with no delay, silently.
- **Judgement:** defect, and the same D-16 shape it was supposed to have closed: `canonicalWait`
  unified the *spelling* of the conditions and nobody checked that both call sites still agreed on
  what a number meant.
- **Fix:** `cmdAttach` grows the duration branch — a `sleeping`/`sleepUntil` pair kept separate
  from `waitFor` so a duration is never matched as text, the socket kept drained throughout (so a
  sleep between two dumps genuinely samples two frames), the quiet-timeout suppressed while it
  runs, and the `select` timeout capped by the remaining time so `wait 60` is not silently 200 ms.
- **Verified:** the same 30-sample script now spans 2.4 s and shows the splash advancing frame by
  frame — the timeline in D-26 is its output.
- **Guarded by:** nothing automated; it is a CLI timing path. The D-26 reproduce block exercises it.

### D-23 — Images have no name, and the export dialog calls them "(missing image)"
- **Area:** core / persistence (symptom: design) · **Status:** **Fixed** · **Severity:** S2
- **Found:** 2026-08-18, reported by the user. **It was visible in this project's own acceptance
  dumps for a day before anyone read it** — `node=1 … kind=image slot=0 … name=` — which is worth
  more than the bug: a dump nobody reads is not evidence.
- **Reproduce:**
  ```bash
  printf 'project open two.cmp\nwait load.finished\nstate print --stable\n' \
    | cosmo-cc run --script - | grep '^  node='
  #  before:  node=1 parent=-1 depth=0 kind=image slot=0 bypass=0 selected=1 name=
  ```
- **Expected:** an image is named by its file, wherever a name is shown.
- **Actual:** every image node's name was the empty string. The export dialog substitutes
  `"(missing image)"` for an empty leaf name (`App.cpp:407`), which is what the user saw; the
  filmstrip cell and the breadcrumb simply drew nothing. The top bar looked *correct* the whole
  time because it derives from `currentSourcePath()` rather than the node — which is precisely why
  this survived so long: the one place a name was obviously wanted was the one place it worked.
- **Judgement:** defect. Not a regression from the service work, and checked rather than assumed:
  `git log -S "addPendingImage(parent, e.name)"` traces the line to the repo reorganisation, long
  before any of it. S2 moved the call; it did not break it.
- **Cause:** `readWorkspaceFile` parses `path=` into `imagePath` and **never sets `name`** — the
  format stores a name only for a `#group`, and `WorkspaceEntry::name` was documented "groups
  only". `addPendingImage(parent, e.name)` therefore created a nameless leaf, and `attachImage`
  copied that empty string into `mSlotNames`, from where every consumer read it.
- **Fix:** commit `11a8a72`, in two places on purpose. **The reader** names an image entry from
  its path, which fixes every consumer at once — the service's tree, the filmstrip, the breadcrumb,
  the export dialog and `cosmo-cc` — rather than each of them learning to derive it, which is how
  they came to disagree in the first place. **And `attachImage`** falls back to the path when it is
  handed a nameless leaf: it is the sink every producer funnels through, it has the path in its
  hand, and the original failure was **silent** — an empty name draws as nothing — which earns a
  second line of defence.
- **Verified:** `name=DSCF5186.RAF` in the state dump, and
  `[evt] export.progress done=1 total=2 name=DSCF5186.RAF` from the export path, which is the same
  `nameForSlot()` the dialog reads before deciding whether to print "(missing image)".
- **Guarded by:** `an_image_entry_gets_its_filename` — the reader names images (including one in a
  directory with a space) and keeps a group's stored name, and `attachImage` derives a name for a
  nameless leaf so the tree and the filmstrip agree.
### D-22 — A load shows "Preparing…" for 9 seconds, then a bar that jumps in fives
- **Area:** core + design / load UX · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-18, reported by the user: "it only shows Preparing… a long time then jumps
  into the edit page".
- **Reproduce:**
  ```bash
  XDG_CONFIG_HOME=/tmp/lux ./build/apps/cosmo/cosmo japan18.cmp   # 18 RAF files
  grep -E "project.opening|load.progress|load.finished" /tmp/lux/cosmo_v2/cosmo_v2.log
  ```
- **Expected:** R-LOADUX-3, "Progress that means something" — a photographer can always tell how
  much is left, in both phases.
- **Actual**, measured on the reported 18-RAF project:
  ```
  11:51:32.994  project.opening entries=18      <- "Preparing…", bar at 0
  11:51:42.104  load.progress done=1            <- 9.1 SECONDS with no signal at all
  11:51:42.169  load.progress done=2            <-   ...then 65 ms later
  11:51:51.440  load.progress done=9
  11:52:08.816  load.progress done=18           <- 35.8 s total, then the editor appears
  ```
  Two distinct faults. **(a) A quarter of the load has no signal**: the only progress event is
  "an entry finished", and with 5 workers each taking ~9 s on a 26 MB RAF, nothing at all happens
  for the first 9 s even though five decodes are in flight. **(b) The bar advances in bursts of
  five** — the pool size — because workers that start together finish together, so it leaps 5/18
  at a time rather than moving.
- **Judgement:** defect against R-LOADUX-3. The requirement was written when progress was assumed
  to arrive smoothly; it says the count must be against the project's real total, and it is, but a
  count that is 0 for nine seconds does not tell a photographer how much is left. The requirement
  needs amending to say what granularity means, which is the substance of the fix.
- **Cause:** the load's only progress signal is completion. `OrderedParallelLoad` knows when a
  worker *claims* an entry and nobody asks; `ProjectLoader` reports `consumed()` only; and the
  view has no way to distinguish "waiting" from "nothing happening", so it eases a bar toward a
  target that does not move.
- **Fix:** commits `e05183e` (core) + `b9dd26e` (design), core first as the skills require.
  **Core:** `ProjectLoader::drainStarted()` buffers each worker's claim under a mutex — a callback
  would fire on a worker and the service is single-threaded by contract (R-SVC-6) — and `pump()`
  turns them into `EntryStarted` events; `LoadModel` gains `started`, `stage` and `inFlight()`;
  new `LoadStage` events name reading / decoding / saving. **Design:** the bar draws three bands
  (finished, in-flight pulsing on a 380 ms sine, track) and the status line names a file when a
  worker CLAIMS it. Same load, after: `entry.started … name=DSCF5186.RAF` at **+17 ms** instead of
  the first signal at +9.1 s.
- **Guarded by:** `a_load_reports_work_before_any_result` — a decoder slow enough that claims must
  precede completions, asserting the first claim arrives at `done == 0`, which is the precise
  ordering the defect was about. Verified visually too: a window capture **four seconds into a
  load**, the moment that used to show "Preparing…" over an empty bar, reads
  `Loading DSCF5807.RAF` with the in-flight band about five-eighteenths across — the five workers.
- **Not fixed, and deliberately:** the load still takes 35.8 s and still reveals only at the end.
  That is R-LOADPERF-3's existing choice and a separate question; D-22 was about the nine seconds
  of silence, not the duration.

### D-21 — `AppModel::frameSeq` and `Event::FrameReady` are declared and never produced
- **Area:** core / service · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-18, by the shot renderer's author, who read `AppModel.h`'s "a test can wait
  for one" and built against it.
- **Reproduce:** `grep -rn "mFrameSeq\|FrameReady" apps/cosmo/core/service/` — `mFrameSeq` is a
  member nothing increments and `Event::Kind::FrameReady` has no emitter.
- **Expected:** a front end can tell when a preview has landed, which is what the field's own
  comment promised.
- **Actual:** nothing ever moves. `cosmo_shots` has to settle on a real-time floor instead, which
  is most of why the project shots take ~10 s of padding.
- **Judgement:** defect against R-SVC-3 — a declared field that is never written is a lie, the
  same species as D-15, except this one had documentation asserting the capability.
- **Cause, and why it is not a one-liner:** the **view** polls `RenderService::tryAcquire`, and
  that call **moves** the frame out. The service cannot also poll without stealing frames from the
  view, so this cannot be fixed by having `pump()` look — it needs the frame path to belong to the
  service, which is **S4c**. The comment in `AppModel.h` now says so instead of promising.
- **Fix:** commit `c6e7c39`, in S4c as predicted, and by the first of the two routes: the
  **service polls** and the view takes the frame from it. `pump()` calls `tryAcquire` — moved out
  from behind the load's early-return, which was the other half of why nothing ever moved — stores
  the frame, fills `frameSlot/Width/Height`, increments `frameSeq` and emits `FrameReady`;
  `takeFrame()` hands it to whoever draws. Exactly one owner calls `tryAcquire`, which is the
  invariant that made two pollers impossible in the first place.
- **Guarded by:** `a_preview_frame_reaches_the_model`, which opens a project headlessly, dispatches
  `set exposure=1.5`, pumps until `frameSeq` moves, and asserts the event fired, the metadata is
  filled, the pixels come out **once** (a second `takeFrame` finds nothing), and the model carries
  no pixels. `[PASS] a_preview_frame_reaches_the_model (seq 0 -> 1, 1 events)` — a test that could
  not have been written before the ownership moved.

### D-7 — No headless UI render is possible; the CMake glob structurally prevents one
- **Area:** design / build · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `grep -n "file(GLOB COSMO_SOURCES" -A4 apps/cosmo/CMakeLists.txt` — the glob includes
  `linux_main.cpp`, so no second `main()` can link the cosmo UI.
- **Expected:** any screen or state renderable to a PNG with no display, as `genesis_shots` does.
- **Actual:** impossible today. `App` is already platform-free and drivable (`App(w,h)`, `setSize`,
  `render(target,nowMs)`, `pointer`, `wheel`, `key`, and public `showHome`/`showEditor`/
  `beginOpenTransition`/`setLoadProgress`/`finishOpenTransition`) — only the missing
  `list(REMOVE_ITEM … linux_main.cpp)` stands in the way. `cosmo_widget_tests` covers isolated widget
  hit-testing only, never the assembled app.
- **Judgement:** defect against the family design rule that a UI change must be *seen* before it ships
  (`arstro.design.desktop` §5) — currently unsatisfiable for cosmo.
- **Fix:** commit `c755a76`. **One line was the whole of it** — `list(REMOVE_ITEM COSMO_APP_NOMAIN
  … linux_main.cpp)`, named and shaped after Genesis's `GENESIS_APP_NOMAIN`, which has had it all
  along. On top of that, `apps/cosmo/tests/shots/renderShots.cpp` renders **13 PNGs across 2 window
  sizes** with `DISPLAY` unset: home (empty / recents / the Settings modal opened by a real
  synthesised click), the empty editor, both mid-transition samples, the dissolve and reveal halves
  of the reveal, and the editor with a real 2-RAF project loaded through `CosmoService`. No
  `gtk_init` anywhere; GTK3 is linked for GdkPixbuf alone, and the vendored fonts go straight into
  fontconfig, so a shot is measured in the app's own typeface rather than the host's default.
  Two full runs are **byte-identical for all 13 files**.
- **Guarded by:** `ctest -R cosmo_shots_headless` — the fixture-free subset (0.5 s), with `--check`
  failing any PNG that came out uniform-colour. The project shots stay manual: they need ~26 MB RAW
  files that are not in the repo and should not be.
- **Three real bugs the harness caught in itself**, each worth carrying into P0.3: a `CairoTarget`
  built per frame invalidates every `registerImage` id, so every photo and thumbnail blanked after a
  resize (the app owns ONE target for the run — copy `onDraw`); snapping the shot clock *after* a
  settle made `beginOpenTransition` start its phases in the past, so a "reveal" shot was silently a
  second copy of the editor — **caught by comparing file sizes, not by `--check`, because a colour
  count catches blank frames and not wrong ones**; and sampling a transition at its linear midpoint
  is not its visual midpoint, since every one of them is EaseOutCubic.

### D-9 — Two divergent build paths, two undocumented build trees
- **Area:** core / build · **Status:** **Fixed** · **Severity:** S4
- **Found:** 2026-08-16, source inspection
- **Reproduce:** compare `./build.sh --project cosmo --target linux-native-app` (a hand-rolled one-shot
  `g++` producing `apps/cosmo/build/cosmo_linux`) with `cmake -S . -B build && cmake --build build`
  (producing `build/apps/cosmo/cosmo`). Also `ls build/apps/cosmo/cosmo.exe build-mingw64/apps/cosmo/cosmo.exe`.
- **Expected:** one documented way to build cosmo per platform.
- **Actual:** two Linux paths with different output names, different flags and no shared tests;
  `build.sh` cannot build cosmo on Windows at all (hardcoded `-lEGL -lGL`, `nproc`). Two Ninja trees
  exist on this host — `build/` (Debug, wired to `.vscode`) and `build-mingw64/` (Release) — with no
  documented distinction. `build.sh --target native-test` covers neither cosmo nor genesis.
- **Judgement:** requirement gap — nothing states which build path is canonical.
- **Fix:** commit `df993dc`. `docs/DEVELOPING.md` (new) declares **CMake into `build/` canonical on
  every platform** (MSYS2 MINGW64 shell on Windows), and the reason is stronger than "pick one":
  **`./build.sh --project cosmo --target linux-native-app` does not link at all today** —
  `multiple definition of 'main'`, because its glob is every `.cpp` under `apps/cosmo` and since S3
  that set has two (`linux_main.cpp` and `cli/main.cpp`). Even repaired it builds no tests and no
  `cosmo-cc`, so nothing it produces can be verified; it hardcodes `-lEGL -lGL` + `nproc` so it
  cannot build on Windows; and it maintains defines by hand beside CMake's, the ODR trap the
  decisions log records. CMake is what `.vscode` is pinned to, what `ctest` aggregates, and the
  acceptance script's default build dir.
  **The two trees:** `build/` is the real one and everything in the repo references it;
  `build-mingw64/` is one developer's private Release tree on the Windows host, referenced by
  nothing and absent on Linux — the guide says never write a command assuming it, and gives
  `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release` instead. It also flags that a fresh
  `cmake -S . -B build` yields **Release** (the root CMakeLists forces it) while the VSCode
  extension makes the same directory **Debug** — verified both ways.
- **Follow-on filed as D-19:** `build.sh`'s cosmo path is **broken, not merely divergent**.

### D-8 — `configDir()` resolves to the wrong place outside an MSYS2 shell
- **Area:** core / persistence · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** launch `build\apps\cosmo\cosmo.exe` from cmd.exe or Explorer (where `HOME` is unset)
  and look for `.config\cosmo_v2\` **relative to the current directory**.
- **Expected:** settings, recents and the log land in one predictable per-user location on Windows.
- **Actual:** `ProjectStore::configDir()` reads `XDG_CONFIG_HOME`, else `HOME` (never `USERPROFILE`),
  else `"."` — so a non-MSYS2 launch scatters config next to wherever it was started from. Supporting
  evidence: `C:\Users\Nam Doan\.config\cosmo_v2\` exists on this host but is **empty** — no log, no
  settings, no recents — so a real session's state has never been observed there.
- **Judgement:** requirement gap — no requirement states the Windows config location.
- **Fix:** commit `df993dc`. `resolveConfigDir()` in `ProjectStore.cpp`, documented in the header:
  1. `$XDG_CONFIG_HOME/cosmo_v2` — wins everywhere, decided before any platform reasoning, because
     it is how every scripted run in this project sandboxes itself and that must keep working.
  2. *(Windows)* a pre-D-8 directory that already holds state, **adopted**.
  3. *(Windows)* `%APPDATA%` + `cosmo_v2`, else `%USERPROFILE%` + `.config/cosmo_v2`.
  4. `$HOME/.config/cosmo_v2` — POSIX and MSYS2.
  5. `./.config/cosmo_v2` — last resort, byte-identical to the old behaviour.
  **Why APPDATA above HOME:** `HOME` exists only inside MSYS2, so ranking it first is what *produced*
  D-8 — one install kept two config directories depending on how it was launched.
  **Why adoption rather than migration:** `cosmo_core` has no logging and no error channel, so a copy
  that half-succeeded would leave two divergent directories and nothing able to say so; moving
  someone's recents is destructive and this layer cannot ask. Adoption is idempotent and re-decided
  identically every launch. The test is "not empty" rather than "exists", because `configDir()` calls
  `create_directories` every launch — D-8's own evidence is an *empty* scattered directory.
- **Verified:** POSIX resolution unchanged; the `_WIN32` branch was extracted verbatim into a probe
  with only the guard macro swapped and exercised across all five cases plus legacy-present and
  legacy-empty variants. `cosmo_core_tests` 31/31, with
  `settings_roundtrip_and_survive_a_bad_file` still backing up and restoring the real settings file.

### D-1 — Three design docs still describe the removed `onLoadingReady` hook
- **Area:** core / docs · **Status:** **Fixed** · **Severity:** S4
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `grep -rn "onLoadingReady" apps/cosmo/docs/ apps/cosmo/REQUIREMENTS.md`
- **Expected:** `design.md`, `architecture.md` and `detailed_design.md` agree with `REQUIREMENTS.md` and
  `docs/requirements.md`.
- **Actual:** they still say the decode is deferred to an `onLoadingReady` hook firing at the intro
  boundary. `R-LOADPERF` amended that — the decode now starts *with* the transition — and
  `DR-LOADUX` records that the hook is gone.
- **Judgement:** defect — a doc-sync failure, which is exactly what the V-model's sync check exists to
  prevent. A future session reading `design.md` first would build against the old model.
- **Fix:** commit `df993dc`, and it turned out to be **six** places, not three. `design.md`,
  `architecture.md` §4/§6.2 and `detailed_design.md` §1.3/§1.4/§1.6 were rewritten to say what
  happens now rather than deleting the stale sentence — the host dispatches `Command::ProjectOpen`,
  whose `ProjectOpening` event is what calls `beginOpenTransition`, so the pool is already running
  as the first intro frame draws. Beyond the original three: `docs/requirements.md`'s DR-SCREEN-2
  still said "the decode has **not** started" and contradicted DR-LOADUX-4 **in the same file**;
  `REQUIREMENTS.md`'s R-LOADING-0 contradicted its own R-LOADING-1 amendment two bullets below;
  `architecture.puml` still modelled the deleted `LoadJob` class; and `App.cpp`'s own comment at the
  Intro-to-Loading boundary still claimed "only now do we ask the host to start decoding".
  Also corrected `kLoadingBg` (the docs said `{0x14,...}`, the code says `{0x0A,...}`) and the
  `file:line` anchors on every line rewritten.
- **Note:** `detailed_design.md`'s anchors are broadly stale beyond the rewritten lines
  (`renderEditor` cited at 482 is really 686, `renderReturn` at 937 is 1251). A full sweep is P1.

---


### D-2 — The log level is never checked; `LOGD` and `setStderrEcho()` are dead
- **Area:** core / observability · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** read `log::write()` in `apps/cosmo/Log.cpp` — no level comparison anywhere; then
  `grep -rn "LOGD\|setStderrEcho" apps/cosmo/` — `LOGD` is never called and `setStderrEcho()` is never
  called.
- **Expected:** a debug mode that can be turned up for an investigation and down for normal use, per
  `DR-NFR-5`'s "timestamped levelled log".
- **Actual:** the levels are decorative. Every line is always written to both the file and stderr; there
  is no verbosity flag, no category filter, no way to silence the stderr echo, and no way to relocate the
  file except via `XDG_CONFIG_HOME`/`HOME`.
- **Judgement:** defect — "levelled" is stated in DR-NFR-5 and is not implemented.
- **Fix:** commit `b4847a3`. The level is compared in `writef()` **before** the `vsnprintf`, so a
  suppressed `LOGD` costs a load and a branch. Added `Category{Ui,Input,Render,Load,Session,Export,
  Gpu}`, the flags `--log-level` / `--debug` / `--log-categories` / `--log-file` / `--log-stderr`,
  and `COSMO_LOG_LEVEL` / `COSMO_LOG_CATEGORIES` / `COSMO_LOG_FILE` / `COSMO_LOG_STDERR` for when
  flags cannot be passed. `setStderrEcho()` finally has callers.
  **A service Event's category is derived, not tabulated twice**: `categoryForEventName()` maps the
  first segment of `eventName()`'s dotted name, and `write()` recovers it from the `[evt] ` prefix —
  which is what makes the host's single `LOGI("%s", formatEvent(e))` filterable without editing it
  (R-SVC-5). Two filter rules are deliberate: **Warn/Error ignore the category filter**, and
  **uncategorised lines are never filtered**, or a category typo would silence exactly the GTK
  warnings D-3 exists to surface.
- **Verified** in the real app on a 2-image project: default prints `[ui]`/`[session]`/`[load]` with
  today's shape otherwise unchanged; `COSMO_LOG_CATEGORIES=load` → 0 ui+session lines, 5 load lines;
  `--log-level=warn` → 2 lines against a 16-line baseline. The config rides on the **session header**
  rather than an INFO line, so `--log-level=warn` cannot filter away the explanation of why the log
  looks empty.


### D-3 — GTK/GLib warnings never reach the log file
- **Area:** core / observability · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `linux_main.cpp` installs `g_set_print_handler` and `g_set_printerr_handler`, but
  `g_warning`/`g_critical`/`g_message` go through `g_log`, which neither handler sees.
- **Expected:** DR-NFR-5 says every `g_print`/`g_printerr` is routed through the log; the same intent
  covers GTK's own diagnostics, which are exactly what you need when rendering or a dialog misbehaves.
- **Actual:** they hit stderr only and vanish from the file. `G_DEBUG=fatal-warnings` under gdb is the
  only current way to catch them.
- **Judgement:** defect — DR-NFR-5's intent is not met for the diagnostics that matter most.
- **Fix:** commit `b4847a3`. `log::installGlibHandler()` installs `g_log_set_default_handler`
  alongside the print/printerr pair, mapping ERROR/CRITICAL→Error, WARNING→Warn, MESSAGE→Info,
  INFO/DEBUG→Debug, all under `Category::Ui`, keeping GLib's domain in the text rather than
  translating it into our category vocabulary. Two traps are commented in place: a
  `G_LOG_USE_STRUCTURED` caller bypasses handlers entirely (GTK3 does not use it, and
  `g_log_set_writer_func` may be called only once and aborts on a second call), and taking over the
  default handler inherits `G_MESSAGES_DEBUG`'s filtering job — which is why `--log-level=debug` is
  now noticeably chattier.
- **Verified** against real GTK: `gtk_widget_show(nullptr)` lands in the file as
  `[ERROR] [ui] Gtk: gtk_widget_show: assertion 'GTK_IS_WIDGET (widget)' failed`.


### D-4 — No stack trace on a Windows crash
- **Area:** core / observability · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** inspect `apps/cosmo/Log.cpp` — the fatal-signal handler writes a banner for
  SIGSEGV/SIGABRT/SIGFPE/SIGILL (+SIGBUS on POSIX) but the `backtrace_symbols_fd` frame dump is POSIX-only
  and explicitly skipped on Windows.
- **Expected:** a crash on the development host produces frames.
- **Actual:** a signal name and nothing else — on the very host this project is developed on.
- **Judgement:** defect against R-NFR / DR-NFR-5's intent (a crash leaves a diagnosable record).
- **Fix:** commit `b4847a3`. `CaptureStackBackTrace` + dbghelp (`SymFromAddr`,
  `SymGetLineFromAddr64`), with dbghelp resolved by `LoadLibraryA`/`GetProcAddress` — the OmpPin
  pattern — so an optional diagnostic never becomes a link dependency. Symbols are initialised in
  `installCrashHandler()`, **never in the handler**: `LoadLibrary`/`SymInitialize` take the loader
  lock and the heap, which is exactly what an access violation has just walked over. The dump uses
  hand-rolled hex into stack buffers, no malloc or printf, matching the POSIX path's discipline.
  Also `SetUnhandledExceptionFilter`, because `signal(SIGSEGV)` on MinGW only fires for what the
  CRT chooses to translate. Degrades to bare frame addresses where dbghelp is absent.
- **NOT VERIFIED — code-reviewed only.** There is no mingw cross-compiler on the Linux dev host, so
  this branch has never been compiled or run. Confirm on MSYS2 by faulting the app and checking the
  log tail carries `#0 0x… name+0x… (file:line)`. The POSIX path was re-verified after the
  refactor: SIGSEGV at `--log-level=error` still writes the banner and six frames.


### D-18 — `state print --params` inside a script was parsed and then ignored
- **Area:** core / CLI · **Status:** **Fixed** (same session) · **Severity:** S3
- **Found:** 2026-08-17, while checking whether `set` already reaches curves and masks — the dump
  came back with no `params:` block and no complaint.
- **Reproduce:** before the fix, a script containing `state print --params`, run with
  `cosmo-cc run --script s.txt` → a dump with no params block. Passing `--params` globally on the
  command line worked.
- **Expected:** the option the caller wrote takes effect.
- **Actual:** `printModel()` read `--params`/`--stable` from the GLOBAL argv only, so the
  per-command form parsed into `Command::fields` and was dropped.
- **Judgement:** defect, and the **same failure as D-14 one layer up** — an option a caller wrote
  and the tool silently dropped is worse than one that does not exist, because the caller believes
  it worked. Two instances of one mistake in a day says the shape is worth naming: whenever a
  Command carries options AND the front end has global flags for the same thing, the per-command
  value must win and the global one must be the default.
- **Fix:** commit `7839715`. `printModel(h, json, const Command *cmd)` — per-command
  `--stable`/`--params` OR the global flag.
- **Guarded by:** exercised by the mask script in the same commit, whose `state print --params`
  now returns the `mask=` lines it was written to show.

### D-10 — A failing assert in `cosmo_core_tests` hangs instead of exiting
- **Area:** core / test harness · **Status:** **Fixed on POSIX, code-verified on Windows** · **Severity:** S3
- **Found:** 2026-08-16, while checking that the R-CPU-3 test fails without its fix
- **Reproduce:** break any assertion in `sessionTests.cpp` (e.g. stop `AppSettings::save()` writing
  `cpuPercent=`), rebuild, and run
  `XDG_CONFIG_HOME=/tmp/s ./build-mingw64/apps/cosmo/core/cosmo_core_tests.exe`.
- **Expected:** the assert prints to stderr and the process exits non-zero, so `ctest` reports a
  failure and an agent gets an answer in seconds.
- **Actual:** the process hangs indefinitely (killed at 5 min twice). MinGW/msvcrt `abort()` raises
  the Windows "terminated in an unusual way" path rather than exiting, and the assert text never
  reaches a redirected stderr because it is not flushed. A failing suite is indistinguishable from
  a slow one — and this suite legitimately takes 1–2 minutes (Release; longer in Debug), so the
  usual "it must be stuck" heuristic does not apply.
- **Evidence:** the same defect, isolated into a 10-line program linking `cosmo_core`, answered in
  under a second: `wrote cpuPercent=25, read back 50 -> FAIL`.
- **Judgement:** defect — no requirement covers the test harness, but "verify with a shell command"
  (the whole `R-AGENT` premise) is unusable when a red suite looks like a hung one.
- **Fix:** commit `d382b02`. `core/tests/TestMain.h` — `testMainInit()`, called first in both
  suites' `main()`. Unbuffered stdout+stderr on every platform, so an assert's message reaches a
  redirected pipe before `abort()` takes the process down and a mid-suite crash does not swallow
  the lines saying how far it got. On Windows additionally
  `SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX)` and
  `_set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG|_CALL_REPORTFAULT)` — print the message,
  never hand off to the fault reporter, because a CI or agent run has nobody to click OK and
  waiting for that click IS the hang.
- **Verified on POSIX** with a deliberate failure and both streams redirected:
  ```
  exit=134
  stdout: [PASS] a test that passes
  stderr: d10_probe.cpp:9: Assertion `1 == 2 && "the deliberate failure"' failed.
  ```
  so the passing output, the failure message and a non-zero exit all survive redirection.
  **The Windows half is code-verified only** — the dialog-suppression path cannot be exercised
  from this Linux host. Confirm on MSYS2 by breaking an assert and checking that
  `cosmo_core_tests.exe` exits promptly and non-zero instead of stalling.
- **Why it mattered beyond convenience:** every verification claim in this project's history rests
  on "the suite is green", and a suite that cannot report red is not evidence. Two sessions lost
  five minutes each to a hang before it was diagnosed at all.

### D-6 — A project cannot be opened from the command line
- **Area:** core / CLI · **Status:** **Fixed** · **Severity:** S3
- **Found:** 2026-08-16, source inspection
- **Reproduce:** `./build/apps/cosmo/cosmo.exe some.cmp` — `openPath()` special-cases only `.cosmo`, so
  a `.cmp`/`.cosmoproj` falls through to `openImageFile()` and fails to decode.
- **Expected:** the primary document format is openable from a shell.
- **Actual:** only bare images and `.cosmo` sessions are. Seeding `recent.tsv` is the sole way to make a
  project reachable, and that only puts it on the home screen.
- **Judgement:** requirement gap — no requirement covers command-line invocation at all.
- **Fix:** commit `127d213`. `openPath()` gained one branch — a `.cmp`/`.cosmoproj` dispatches
  `Command::ProjectOpen` — plus `--project <path>` for scripts where a bare path is ambiguous. It
  is one branch because the load is a command now (R-SVC-2); before the service there was nothing
  to dispatch it to, which is a fair summary of what S1–S3 bought.
  A project argument deliberately does **not** jump to the editor: it runs the normal animated
  load (R-LOADING) and the transition owns the screen. Only a bare image list lands straight in
  the editor.
- **Verified:** `cosmo japan18.cmp` on the 18-RAF project → `[evt] load.finished decoded=18
  total=18`, 18 `entry.decoded` lines, and `[evt] info load.peak decode=5 engine=6 budget=6` in
  the log. **This also clears U1.1a**, the `[!]` that had stood since the CPU budget shipped: the
  load's own numbers had never once been observed in the app, and six other defect entries cited
  D-6 as their reason for being unverifiable.

### D-16 — `wait` meant two different things to two front ends
- **Area:** core / service · **Status:** **Fixed** (same session, S5) · **Severity:** S3
- **Found:** 2026-08-17, the first time the committed acceptance script was run through BOTH
  front ends — which is the entire reason that test exists.
- **Reproduce:** before the fix, one script containing `wait load.finished`:
  ```bash
  cosmo-cc run --script s.txt          # -> "wait: unknown condition load.finished", then a
                                       #    120 s timeout and exit 1
  cosmo-cc attach /tmp/s.sock --script s.txt   # -> worked
  ```
- **Expected:** one documented command, one meaning, whichever front end reads it (R-SVC-5).
- **Actual:** `cosmo-cc run` matched model predicates (`load-finished`, hyphen) while the socket
  client matched event names (`load.finished`, dot). A script that worked against a live window
  failed headlessly, and the failure looked like a hang before it looked like a vocabulary error.
- **Judgement:** defect against R-SVC-5. Two front ends interpreting one command differently is
  the precise divergence the single-codec rule exists to prevent — and it appeared anyway,
  because `wait` is handled by the front end rather than the service, so it never went through
  the shared codec at all. Worth remembering: the rule protects what it routes.
- **Cause:** `wait` is deliberately front-end-owned (only the caller owns its loop), so both
  implementations grew independently.
- **Fix:** commit `e2d471a`. `canonicalWait()` normalises `-` to `.` and both front ends call it;
  the event name is canonical because events are the observable contract; hyphens stay accepted.
  `Command.h`'s documented example, the parser's error hint, the `--help` text, the coverage test
  and the proposal all now use the canonical spelling.
- **Guarded by:** `apps/cosmo/tests/acceptance/run.sh`, which runs the identical script through
  both front ends and diffs the results — the only check that could have caught this.

### D-15 — `AppModel::settings` reported defaults while `AppModel::budget` reported the truth
- **Area:** core / service · **Status:** **Fixed** (same session, S3) · **Severity:** S3
- **Found:** 2026-08-17, by diffing a `cosmo-cc` dump against a GUI dump of the same project —
  the R-SVC-9 check, doing precisely the job it was written for.
- **Reproduce:** before the fix, with `cpuPercent=25 useGpu=1` in `settings.txt`:
  ```bash
  cosmo --control /tmp/s.sock &   # then: state print
  ```
  → `settingsCpuPercent=50 settingsUseGpu=0` **and** `budgetPercent=25` in the same dump.
- **Expected:** one answer. What the user chose, everywhere it is reported.
- **Actual:** two halves of the same answer disagreeing, which is worse than either being wrong:
  anyone debugging from the dump would have believed the budget was 50%.
- **Judgement:** defect against R-SVC-3 — the model is the observable state, so a field that is
  never written is a lie, not an omission.
- **Cause:** the host applied the preferences piecemeal — percentage into `ThreadBudget`, edge and
  GPU into `EditSession`, model never told. `mModel.settings` was only ever written by the
  `settings set` command path, so a GUI that loaded settings from disk never populated it.
- **Fix:** commit `846bcef`. `CosmoService::applySettings(const AppSettings&)` does all of it in one
  place and refreshes the model; the host calls that instead of the three separate calls.
- **Guarded by:** `settings_and_dump_options_reach_the_model`, plus the CLI-vs-GUI dump diff, which
  is now byte-identical on the 18-RAF project.

### D-14 — `state print --stable` silently ignored `--stable`
- **Area:** core / service · **Status:** **Fixed** (same session, S3) · **Severity:** S3
- **Found:** 2026-08-17, same diff as D-15.
- **Reproduce:** `state print --stable` over the control socket, before the fix — the dump still
  contained `revision=`, `frameSeq=` and `budgetPeakDecode=`.
- **Expected:** the volatile fields dropped, so the dump can be compared with another front end's.
- **Actual:** they were all present, so **the comparison the option exists for was impossible**.
- **Judgement:** defect against R-SVC-9. An option that parses and is then dropped is worse than one
  that does not exist, because the caller believes it worked.
- **Cause:** `Command::StatePrint` had one `flag`, used for `--json`; `--stable` was parsed into a
  throwaway `Command` and discarded.
- **Fix:** commit `846bcef`. `--stable` and `--params` live in `fields` (so a third option needs no
  signature change), `--json` keeps `flag`, and `formatCommand` round-trips all three.
- **Guarded by:** `settings_and_dump_options_reach_the_model`.

### D-13 — A project opened from the GUI decoded all 18 images and attached none
- **Area:** core / service · **Status:** **Fixed** (same session, S5) · **Severity:** S2
- **Found:** 2026-08-17, by the FIRST live run over the control socket — minutes after S2 was
  committed with the bug in it, and by the very mechanism S2 existed to build.
- **Reproduce:** before the fix, with a GUI attached:
  ```bash
  cosmo --control /tmp/cosmo-ctl.sock &
  printf 'project open /tmp/japan18.cmp\nwait load.finished\nstate print\n' \
      | python3 attach.py /tmp/cosmo-ctl.sock
  ```
- **Expected:** `load.finished decoded=18 total=18`, `imageCount=18`.
- **Actual:** `load.finished decoded=0 total=18`, `imageCount=0`, `currentSlot=-1`, and every
  later command rejected (`select: no decoded images`). All 18 `load.progress` events fired —
  the files really did decode — but not one `entry.decoded`.
- **Evidence:** the event stream showed 18 progress lines and zero decoded lines, which localises
  it exactly: `pump()` only emits `entry.decoded` when `mNodeOf[r.index] >= 0` resolves to a live
  pending leaf.
- **Judgement:** defect against R-SVC-1/3. A view is *entitled* to clear its own state when it
  hears a project is opening — the GTK host calls `App::resetWorkspace()` there so the open
  transition captures the outgoing editor instead of fading in from nothing.
- **Cause:** `CosmoService::startProjectLoad` built the pending tree and *then* emitted
  `ProjectOpening`. Subscribers run synchronously inside that call, so the host's handler deleted
  all 18 nodes the load was about to attach to. The adapter's own comment claimed the event was
  emitted "BEFORE the session is reset" — the comment was right and the code was not.
- **Requirement:** R-SVC-3, and it produced a general rule now written into the service: **emit an
  event describing what is about to happen before the state it describes exists**, so a view that
  reacts by resetting cannot destroy work the service has already done.
- **Fix:** commit `4f6d82b`. Announce (`ScreenChanged` + `ProjectOpening`) first, then
  `resetWorkspace()`, then build the tree.
- **Guarded by:** `a_view_may_reset_on_project_opening`, which subscribes a handler that resets the
  session exactly as the host does and asserts all six images still attach. Checked to fail on the
  old ordering. The pre-existing headless tests could not catch this: with no subscriber doing real
  work, both orderings look identical — which is the lesson worth keeping.

### D-11 — The CPU budget is enforced per-subsystem, so a load peaks at ~2× what the user chose
- **Area:** core / load · **Status:** **Fixed** (S1) · **Severity:** S2
- **Found:** 2026-08-17, same investigation as D-12
- **Reproduce:** (no shell reproduction exists — that is D-6, and is the point) read the three call
  sites and evaluate them for one budget:
  ```bash
  grep -n "workersFor" apps/cosmo/linux_main.cpp apps/cosmo/App.cpp
  #  linux_main.cpp:724  workersFor(cpuPercent, kMaxDecodeWorkers)   -> decode pool
  #  App.cpp:869         workersFor(cpuPercent)                      -> engine Auto threads
  nproc
  ```
- **Expected:** R-CPU-1: "cosmo's background CPU work runs on a **budget**: a percentage of the
  machine's logical cores". A user who picks 25% expects cosmo to schedule at most 25% of the cores.
- **Actual:** the percentage is converted independently by each consumer and the two run
  **concurrently** — by design, since R-LOADPERF-3 streams decoded images into a live editor, so the
  decode pool and the engine's preview renders overlap for most of a load. Peak scheduled threads is
  `decodeWorkers + engineThreads + 1 (UI)`, i.e. roughly **twice the budget plus one**:

  | cores | budget | decode pool | engine Auto | peak threads | share of machine |
  |------:|-------:|------------:|------------:|-------------:|-----------------:|
  | 24 | 25% | 6 | 6 | 13 | **54%** |
  | 24 | 50% | 8 (cap) | 12 | 21 | **88%** |
  | 16 | 50% | 8 (cap) | 8 | 17 | **106% — oversubscribed** |

  So the shipped default (50%) can still schedule the whole machine, which is the complaint the
  feature was written to answer.
- **Judgement:** defect — contradicts R-CPU-1 as written. Not a requirement gap: R-CPU-1 already says
  "a share of the machine, not all of it", and R-CPU-2 lists the three consumers without ever saying
  they divide one budget rather than each taking the whole of it.
- **Cause:** `AppSettings::workersFor()` is a pure function each consumer calls for itself
  (`AppSettings.cpp:14`). Nothing owns the total, and nothing can — the two consumers live in
  different layers (host and app) and neither can see the other.
- **Requirement:** R-CPU-1, R-CPU-2 (existing). If the sum is judged acceptable, R-CPU-1 must be
  amended to say the budget is *per pool* — but that would make the number meaningless to a user.
- **Fix:** commit `95f3b78`. `cosmo::ThreadBudget` converts the percentage **once** into `total()`
  and divides it: the decode pool *reserves* `total - engineFloor` (capped at 8) for the load's
  duration and the engine gets the remainder, so it holds the whole budget when idle and shrinks only
  while a load runs. `AppSettings::workersFor` has no callers left in the app. R-CPU-2 and R-CPU-4
  amended; the load now logs the allotment and the **measured** peak.
- **Guarded by:** `one_budget_is_divided_not_duplicated` (fails on the old arithmetic at exactly the
  `decode + engine <= total` assertion — checked, not assumed) and
  `project_load_peak_never_exceeds_its_pool`. Measured end to end on 24 cores with the reported
  18-RAF project: 25% → pool 5, peak 5, **19% of the machine** mean over a 35 s load.

### D-12 — `OMP_NUM_THREADS` is set too late to bind, so R-CPU-2(c) does nothing
- **Area:** core / load · **Status:** **Fixed** (S1) · **Severity:** S2
- **Found:** 2026-08-17, debugging "CPU limit 25% still uses ~75% when opening a project"
- **Reproduce:**
  ```bash
  gcc -fopenmp -O1 -o /tmp/omp_env_order apps/cosmo/core/tests/fixtures/omp_env_order.c
  /tmp/omp_env_order                      # setenv() inside main(): team size = every core
  OMP_NUM_THREADS=1 /tmp/omp_env_order    # set before exec: team size = 1
  ```
- **Expected:** `linux_main.cpp:1282`'s `g_setenv("OMP_NUM_THREADS", "1", FALSE)` pins every nested
  OpenMP team to one thread, so "a decode worker means exactly one core" (R-CPU-2c).
- **Actual:** it has no effect. libgomp parses the environment in an **ELF constructor**, which runs
  when the library is loaded — before `main()` — so a `setenv`/`g_setenv` from `main()` is read by
  nobody. On this 24-core host:
  ```
  before setenv: omp_get_max_threads() = 24
  after  setenv: omp_get_max_threads() = 24
  actual parallel-region team size = 24  (machine has 24 cores)
  ```
  With the same variable set before `exec`, all three read 1. The commit that introduced the budget
  (5cb2688) calls this "the third layer, and the one that mattered" — on MSYS2, whose LibRaw *is*
  built `-fopenmp`, it is therefore still true that each decode worker opens a team sized to the
  whole machine, which is the reported 25%-budget-uses-75% symptom.
- **Judgement:** defect — contradicts R-CPU-2(c) ("nested parallelism inside a decoder … pin it to 1")
  and R-CPU-4's claim that the budget bounds "the one nested pool it can reach".
- **Cause:** environment variables consumed by a library constructor cannot be set from `main()`.
- **Platform note:** inert on this Linux host — the vendored `lib/LibRaw/lib/libraw.a` has zero
  `GOMP_*` symbols (`nm … | grep -c GOMP_` → 0), so there is no nested team here to pin. Live on
  MSYS2/Windows, where the CPU limit was developed and where the symptom was reported.
- **Requirement:** R-CPU-2, R-CPU-4 (both existing; neither needs amending — the code does not do
  what they say)
- **Fix:** commit `95f3b78` — option (a). `cosmo_v2::pinNestedOpenMPForThisThread()`
  (`apps/cosmo/OmpPin.cpp`) resolves `omp_set_num_threads` via `dlsym(RTLD_DEFAULT, …)` /
  `GetProcAddress`, so nothing links OpenMP, and `ProjectLoader`'s new per-worker start hook calls it
  **on each decode worker** — the ICV is per-thread, so the main thread could never have done it.
  `ompPinStatus()` is logged at startup so the mechanism is checked rather than assumed.
  **Mechanism proven** by a second fixture (`omp_pin.c`, scratch): three pinned pthreads report team
  size 1 while an unpinned control on the same machine reports 24. **Not observable in cosmo on this
  Linux host** — the vendored libraw.a has no OpenMP, so the startup line correctly reads
  `no OpenMP runtime loaded`. Confirm on MSYS2, where the symptom was reported, by checking for
  `nested OpenMP teams pinned to 1 per decode worker` in the log.
- **Guarded by:** `project_load_peak_never_exceeds_its_pool` asserts the hook ran once per worker.
