# cosmo — Progress Ledger

**This file is the single source of truth for what is done and what to do next.** It is committed and
travels between computers. The four cosmo skills read the **NEXT** line below, do one task, update this
file, and commit.

- Implement: `.claude/skills/arstro.cosmo.core.implement/` · `.claude/skills/arstro.cosmo.design.implement/`
- Debug: `.claude/skills/arstro.cosmo.core.debug/` · `.claude/skills/arstro.cosmo.design.debug/`
- Defects: [`DEFECTS.md`](DEFECTS.md) · Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) ·
  As-built: [`requirements.md`](requirements.md) · Backlog: [`../PARITY.md`](../PARITY.md)
- Checkbox legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but
  UNVERIFIED (say why in Verification notes).

---

## NEXT

**► 2026-08-24 — "port it to a small SBC (Allwinner A733, RK3588); the render may be slow but the
UI must never lag". Analysed, planned, and T0.1 landed. NEXT is T0.2.**

**The user set T2's numbers** (2026-08-24, so no other machine re-litigates them): a live gesture
gets a frame every **33 ms (~30 fps)**, the full 1600 px level is reached within **500 ms of the
gesture ending, stepping up** through the pyramid, and the **A733 is the floor that must work** —
tune for it and RK3588 plus the desktop come free. Written up as **R-PREVIEW-1..6**.

**► NEXT: T4.2 — a disk-backed proxy cache**, then T5 if any of it still proves necessary. T0, T1,
T2, T3.1 and T4.1 are all done and measured.

**► (done) T4.1 — the load path.** T0, T1, T2 and T3.1 are done. T3.2/T3.3 (damage rectangles, a
30 fps shell) are now optional rather than necessary: with the window idle at rest, the remaining
per-frame cost only matters while something is moving, and a frame measured 1.50 ms. T3.4 is
**Artboard's**, not this skill's — see below.

**► (done) T2.5 (design) and T3.1.** T2's core half is done and demonstrated end to end. What is
left is the view: crossfading level to level (R-PREVIEW-4), sending `gesture on|off` from the
widgets that drag, and wiring `setWantIntermediateHistograms` to whether the Curve/Mixer panel is
open (T0.4's unwired half).

**► (done) T2 core — progressive resolution.** T0 and T1 are complete. T2 is the item that changes the
shape of the experience rather than a constant factor, and the user's numbers for it are recorded
above: 33 ms live, 500 ms to settle, stepping up.

**► (done) T1 — a real thread pool.** T0 is complete: **193 -> 23.8 ms at 24 threads (8.1x) and
980 -> 113.4 ms single-threaded (8.6x)** for a 1600 px default-params preview. The single-thread
column is the one that predicts an SBC, because the pipeline stops scaling near four threads anyway
— which is also exactly why T1 matters there and barely shows here.

**► (done) T0.2 — LUT `srgbEncode`/`srgbDecode`.** With T0.1 in, the whole of what a default-params
render now costs is the floor: three histogram passes, one `pow`-based sRGB encode, and the
per-render allocation of three working Images. T0.2 attacks the first two at once, because every
one of them calls `srgbEncode`/`srgbDecode` per channel per pixel
(`Histogram.cpp:43-52`, `ColorSpace.cpp:18,29`). Mirror `kSrgbToLinear`: a 4096-entry table plus
linear interpolation, well under 1/255 of error, with a max-abs-error assertion against the `pow`
reference as the guard.

The plan is committed here rather than left in a chat, because it spans core and design and will take
several sessions. **D-45 now carries the per-stage measurement** it asked for — read that entry first.

**What the measurement says.** At 1600x1066 with every parameter neutral, on a 24-thread Ryzen:

```
threads=24  193 ms      threads=8  221 ms      threads=4  326 ms      threads=1  980 ms
of which, with ALL STAGES BYPASSED:  61 ms
```

Two conclusions, and they set the whole strategy:

1. **~132 of the 193 ms is spent by stages that are at their default value**, and **the remaining
   61 ms is not spent by stages at all** — it is per-render allocation, three histogram passes and a
   `pow`-based sRGB encode. Almost none of the current cost is the user's edit.
2. **Cores do not help.** 24 threads beat 8 by 13% and beat 1 by only 5x, because nine spatial stages
   early-out through a *serial* 27 MB `std::copy` and `renderInto` page-faults 82 MB per render. So an
   SBC's problem is NOT its core count — RK3588's 4xA76 is within ~2x of this box's *useful*
   parallelism. It is per-core throughput and memory bandwidth, which is exactly what the wasted work
   above consumes. Extrapolating from the 4-thread number and ~2.5-3x lower per-core throughput:
   **~0.8-1.0 s per preview on RK3588, ~1.5-2.5 s on an A733-class part** — matching the report.

**Therefore the port is not a port.** Nothing here is ARM-specific work. Fixing what the measurement
found makes cosmo fast on the Ryzen too, and it is what makes it *possible* on the SBC.

**T0 — stop doing work nobody asked for (core; four commits; no output pixel changes).**
Exactly D-45's recommended fix, in its order. `ImageProcessor::isIdentity()` + chain skip · LUT
`srgbEncode`/`srgbDecode` · hoist `renderInto`'s three working Images to members · make the pre-curve
and pre-mixer histogram taps opt-in. Expected 1600 px: ~200 ms -> ~20 ms. Each step is independently
measured by `apps/cosmo/core/tests/fixtures/preview_stage_cost.cpp`, and each needs a guard test that
fails without it.

- [x] T0.1 `isIdentity()` on every processor + `ImageBlock` skip — **DONE, 3.0x measured.**
      1600 px at default params: 193 -> 69 ms (24 thr), 221 -> 75 (8 thr), 326 -> 109 (4 thr),
      980 -> 331 (1 thr); `cosmo-cc bench` `renderFull` on a 3000x2000 image 714.57 -> 240.23 ms.
      The after number **equals the all-stages-bypassed floor**, which is the check that the skip
      is complete. Guarded by two tests in `image_tests`, the second of which was verified to fail
      on the unfixed code (`CHECK failed: differing == 0`). DR-PREVIEW-6.
- [x] T0.2 LUT the sRGB transfer function both ways — **DONE.** 69 -> 53 ms (24 thr),
      331 -> 208 ms (1 thr). Worst error 1.6e-5 encode / 1.2e-7 decode, i.e. 1/240th of one 8-bit
      step; guarded by `Srgb_tables_match_the_closed_form_far_below_one_8bit_step`. DR-PREVIEW-6 step 2.
- [x] T0.3 hoist `preCurve` / `preMixer` / `processed` to members — **DONE.** 53 -> 42 ms (24 thr).
      `renderFull` releases them (plus the chains' scratch, via the new `ImageBlock::releaseScratch()`)
      so a 24 MP export does not park ~600 MB against R-MEM-1's caps; `renderImage` keeps them,
      because that is the fixed-size video seam.
- [x] T0.4 opt-in pre-curve + pre-mixer histogram taps — **DONE.** 42 -> 23.8 ms (24 thr),
      195 -> 113 ms (1 thr). `EditEngine::setWantIntermediateHistograms` + `RenderService`
      passthrough; both default ON. **The front-end half is not wired yet** — nothing calls it, so
      the app still pays for both taps. Wiring it to "is the Curve or Mixer panel open" belongs to
      `arstro.cosmo.design.implement` and is listed under T3.

**T1 — a real thread pool (core; one commit). SBC-critical, cheap here.**
`par::parallelFor` spawns fresh `std::thread`s per call, ~13 times per render, in **equal** chunks
(`Parallel.h:44-58`). On big.LITTLE, equal chunks mean every pass finishes at little-core speed. A
persistent pool with many more chunks than threads (work-stealing) removes a standing 2-3x SBC loss
and changes nothing about results — `parallelFor`'s contract already guarantees row independence, so
serial and parallel output stay byte-identical. Must keep working with `ARSTRO_ENABLE_THREADS` off,
and needs the randomized/TSan argument the skill requires for anything thread-shaped.

- [x] T1.1 persistent pool + work-stealing chunks behind the existing `par::parallelFor` signature —
      **DONE.** Measured against the OLD equal-split scheme run side by side on a deliberately
      uneven workload (the same scheduling problem as asymmetric cores): **1.5-2.4x on a 3x spread**,
      landing within 4-56% of perfect balance. `parallelFor(empty)` 0.37 -> 0.08 ms per call; the
      1600 px render 42 -> 32 ms at 24 threads. A wash at 4-8 symmetric threads, which is the
      honest result — there is no imbalance there to absorb. DR-PREVIEW-2a.
      **The first two versions of the batch handshake were broken (D-47)** — a segfault, then a hang
      under concurrent callers. Fixed in `9c7020d`; guarded by a 6-thread concurrent-caller test and
      six consecutive clean `ctest` runs.

**T2 — THE BREAKTHROUGH: progressive resolution, so interaction never waits for a render (core +
design). This is the one item that changes the shape of the experience rather than a constant factor.**
Even at 20 ms on this box, a 1600 px render cannot be real-time on an A733. The answer is to stop
trying: **a live gesture renders at whatever resolution meets a latency budget, and refines upward
when the gesture settles.** Cost is near-linear in pixels (`800px 55 ms`, `400px 15 ms` at 8 threads),
which is what makes this work.

- [x] T2.1 **Requirement written** — R-PREVIEW-1..6, with the user's numbers (33 ms live, 500 ms
      settle stepping up, A733 the floor). R-PREVIEW-3 was then **AMENDED by measurement before the
      code shipped**: a real edit costs ~10x a neutral one (21 ms vs 200 ms at 1600 px/24 thr), so
      500 ms cannot cover a heavy edit's level-0 render on a weak board. The walk always completes;
      500 ms is the target for the machine and edit in front of it; `previewEdge` is the lever.
      ~~Write the missing requirement first~~ — D-45 filed a requirement gap and said the number
      is a product decision to take with the user, not to guess here. It must be stated as a *latency*
      budget, not a resolution, so it is hardware-independent and can fail: "while an adjustment
      gesture is live a new preview frame lands within N ms; the full preview level is reached within
      M ms of the gesture ending." **Ask the user for N and M before coding.**
- [x] T2.2 **Proxy pyramid per slot** — 4 levels, built at ingest from one read of the source,
      **1.328x** one proxy in bytes (measured exactly). ~~Proxy pyramid per slot~~ (1600 / 800 / 400 / 200). `downscaleEncodedToLinear` already
      does a fused convert-and-downscale in one read of the source (D-44); emit every level from that
      same read, so the pyramid is nearly free and no full-resolution float image is ever allocated.
      Also moves proxy building to the DECODE worker — the ledger's existing open item below, which
      is what turns D-44's serial 15 s back into a parallel ~2 s.
- [x] T2.3 **Level selection from measured cost** — `RenderService::levelForBudget()` from ONE
      number (ms per megapixel, exponential average, re-decodes excluded). No hardware detection,
      no setting. ~~Level selection from measured cost.~~ `Frame::ms` already carries what the last render
      cost (added for D-44). Pick the level whose last measured cost fits the budget; no hardware
      detection, no configuration — the Ryzen settles on 1600 and the A733 on 400 by themselves.
- [x] T2.4 **Settle-and-refine** — `gesture on|off` command + `maybeRefine` in `pump`, one level
      per frame, no history. Never refines while the finger is down, and that guard is measured:
      a paced drag oscillated 1,0,1,0 without it. ~~Settle-and-refine.~~ Gesture end (or ~120 ms of no input) walks the level up one step at
      a time. Every level is a complete, correct frame, so there is never a blank or torn state — and
      because each level is a real render, `frame.ready` stays honest.
- [x] T2.5 **Design half** — DONE, and R-PREVIEW-4 turned out to need **no new code**: R-VIEW-1
      already cross-dissolves every render, and a coarse frame is a frame, so refinement reads as the
      photo resolving through the mechanism that existed. What the view DID need was to report
      gestures: `App::pointer` dispatches `gesture on` on any Down and `gesture off` on any Up — at
      the root and on any press, so every draggable surface is covered by construction and a press
      that edits nothing costs nothing. Verified on rendered frames. ~~Design half~~ A coarse frame must
      arrive as a *visible* refinement, not a pop: Cairo already upscales the preview with
      `CAIRO_FILTER_GOOD` for 1.5 ms, so a 400 px frame is soft but live. Crossfade level-to-level per
      R-G-1 — a sharpness change is a visible property change. Shots at 2+ sizes, mid-refine and at
      rest.

**T3 — stop burning a core drawing identical frames (design).**
`onTick` calls `gtk_widget_queue_draw` **unconditionally every 16 ms** (`linux_main.cpp:1306`), so the
whole window is re-rendered in software Cairo 60 times a second forever, at rest, with nothing moving.
Measured on this box a frame's photo paint is cheap (1.50 ms scaled paint, 0.07 ms full-window fill) —
the sin is not that it is expensive, it is that it is **always on**, and on an SBC that is the core the
engine needs. R-G-1 says nothing may change in one frame; it does **not** say repaint at rest.

- [x] T3.1 `App::needsRedraw()` — **DONE.** An idle window stops asking to be repainted; every
      frame of a UI-scale tween is still requested, so R-G-1 is untouched. It is a PURE query cleared
      by `render` (the first version cleared it on the ask, which reported "nothing to do" while the
      screen still showed the old frame — caught by the test, not by reading). Conservative by
      design: Artboard has no tree-wide "is anything animating" query and adding one is
      `implement_artboard`'s territory, so a 1000 ms activity window can waste a frame and can never
      truncate a tween. Also fixed: `cosmo_ui` now sandboxes `XDG_CONFIG_HOME`, so the assembled-app
      tests stop being driven by the developer's own recents file. ~~`App::needsRedraw()`~~ — true while any property is animating, a frame arrived, or input
      landed since the last paint; `onTick` honours it. A tween in flight keeps requesting frames, so
      R-G-1 is untouched. Guard: a test that advances an idle app N ticks and asserts zero repaints,
      and one that asserts a live tween requests every frame.
- [ ] T3.2 Damage rectangles (`gtk_widget_queue_draw_area`) — a slider drag dirties the slider row and
      the canvas, not the whole window.
- [ ] T3.3 A 30 fps shell option for slow devices. cosmo's durations are 120-520 ms, so 30 fps still
      gives 4-16 frames per tween.
- [ ] T3.4 **NOT OURS — `implement_artboard`.** `CairoTarget::buildEntry` premultiplies every new preview frame at 1.75 ms
      (`CairoTarget.cpp:287-315`) although the engine emits alpha=255 for every photo. Fast path for
      known-opaque, and better: have the engine pack BGRA directly on little-endian so `buildEntry`
      becomes a per-row `memcpy`. (Artboard is a submodule and goes through `implement_artboard`.)

**T4 — the load path, which on an SBC is the difference between usable and not.**
- [x] T4.1 **D-24 — FIXED.** `cosmo::Fidelity{Preview, Full}` on the decode seam; the load AND the
      cold-slot rehydration path ask for `Preview`, export asks for `Full`. By now it was barely a
      choice: R-MEM-5/D-44 had already made export re-decode from the file, so option (1) WAS the
      architecture and only the cheap ask was missing. Guarded by a test that counts the asks, since
      the requirement is about which code path runs and is invisible in the output. ~~decode the LOAD
      with `user_qual = 0`~~ and keep the quality demosaic for export.
      7x on decode, already measured, and still needs the user's call between "re-decode at export"
      and "re-decode in the background". On a 4 GB board this is the whole opening experience.
- [ ] T4.2 A **disk-backed proxy cache** beside the project, so a board with 4 GB does not re-decode
      the rack every session. R-MEM's caps are sized from physical RAM now, which on an SBC means the
      caps bind almost immediately.

**T5 — deferred, deliberately: only if T0-T4 prove insufficient. Measure first.**
- **Tile-fused execution.** A real edit still costs 5-8 whole-buffer passes = ~400 MB of DRAM traffic
  per render; on a ~12 GB/s part that is a ~35 ms floor no arithmetic fix can go under. Running the
  *point* stages fused over L2-resident tiles turns N passes over DRAM into one. Big change, big win,
  but it is a rewrite of the chain's execution model — not before T0 has removed the waste.
- **Half-float or 16-bit fixed preview buffers.** Halves bandwidth, doubles NEON lanes. Preview only;
  export stays float, because the CPU float path is the correctness reference.
- **Make the GPU actually reachable.** `glcompute::computeSupports()` accepts only exposure / contrast
  / temp / tint with *everything else* at identity (`GlComputeShared.h:66-86`), so in practice it
  declines and the CPU runs. Both target SoCs have a GPU that is better at this than their CPUs and,
  crucially, does not compete with the UI thread for cores. Porting the remaining **point** stages
  (ToneRegions, ToneCurve as a 1D LUT texture, Vibrance, ColorMixer, ColorGrading) is shader-shaped
  work; keep decline-and-fallback for the spatial ones and extend the conformance tests in the same
  commit. Highest-variance item on the list (Mali driver quality on these boards), which is why it is
  last and not first.

**Order: T0 -> T1 -> T2 -> T3 -> T4.** T0 and T1 are constant factors and pay off everywhere. T2 is
the item that answers the actual request — "the render can be slow but usage must not lag" is a
statement about *decoupling*, and T2 is that decoupling. T5 is only for after the cheap wins are in.


**► 2026-08-23 (later) — "sometime changing photo take too long". D-44 FIXED; D-45 open and is
the ledger's NEXT.**

Measured on a real 120-photo ARW project with the new fixture
`apps/cosmo/core/tests/fixtures/preview_hop_latency.cpp`:

```
walking the rack:   8 of 10 hops re-decoded    cold ~1.8 s   warm ~0.65 s
the cold path:      LibRaw decode 1020.9 ms | to linear 105.3 | +proxy+render 841.5
                    render again, proxy warm  601.4 ms   <- the floor, fully cached
```

1. ~~**D-44 (S2)**~~ — **CLOSED.** It was a regression I introduced.** Yesterday's R-MEM commit (`1897d27`) capped
   resident pixels and traded 465 MB/photo for ~1 s/hop; **nothing measured the second half of that
   trade.** Two causes: the proxy is built only when a photo is *rendered*, so after a load the whole
   rack is cold; and a 26 MB proxy against a 1 GB cap holds 37 of 120 anyway. Recommended: build the
   proxy at ingest on the render worker and drop the source; size the caps from physical RAM in the
   host; and land **D-24** (still open — its fixture already measured `user_qual=0` at ~7x faster,
   same dimensions) so the decode a cold slot needs is cheaper. Plus the observability half:
   `frame.ready` never sets `ms`, so neither the user's log nor a script can tell a 250 ms hop from a
   2537 ms one.
   **R-MEM-5 amended** — "generous enough" was unmeasurable and wrong within a day; it now states a
   target that can fail and names what reads it back.
2. **D-45 (S2) — the floor underneath D-44.** A 1600 px preview render costs **~600 ms at 16 threads
   with every parameter at its neutral value**. Paid on every hop and every slider move. Filed as a
   **requirement gap**: nothing says what an interactive preview may cost, and that number is a
   product decision to take with the user rather than guess. First step is a measurement — extend
   `cosmo-cc bench` to time the preview pipeline per stage — not an optimisation.

**D-44 is fixed.** The proxy is now built at ingest (fused convert-and-downscale, no 387 MB
intermediate) and the caps are sized from physical RAM by the host:

```
                       BEFORE                        AFTER
walking the rack   8 of 10 re-decoded            0 of 10
                   cold ~1.8 s / warm ~0.65 s    uniformly ~0.62 s
resident           765 MB                        3079 MB (120 proxies, cap 5.6 GB)
load, 120 photos   50 s                          65 s
```

**Two things left open, both stated rather than buried:**

- **[ ] The load is ~30% slower (50 s → 65 s).** The fused downscale runs on the render worker, which
  holds exactly ONE engine thread while a load is in flight (R-CPU-2), so 120 proxies are built
  serially. **Fix: build the proxy on the DECODE worker instead**, where the thumbnail is already
  built and handed over ready-made — R-LOADPERF-2 established that pattern for exactly this reason.
  It means `ProjectLoader::Result` carrying the proxy `Image` and `RenderService::addImage` taking a
  pre-built one. Worth doing; it turns a serial 15 s into a parallel ~2 s.
- **[ ] D-45 — the ~600 ms floor.** Every hop and every slider move costs this even fully cached, at
  default parameters. Needs the preview pipeline timed per stage (`cosmo-cc bench`) before anything
  is optimised, and needs a target number decided with the user — see the entry.

Run `arstro.cosmo.core.implement` for either.

**► 2026-08-23 — four problems reported against a 120-photo RAW project. (2) and (4) are DONE;
(1) and (3) are next.**

Reported: *(1) selecting another photo while one is loading does nothing and never reaches the core;
(2) memory — a 120-RAW project takes the whole machine; (3) the UI lags badly during a load, reserve
something for it; (4) changing a parameter seems to recompute every photo.*

- **[x] (2) Memory — R-MEM, the big one.** `EditEngine::Slot::source` held the decoded image in
  linear float (24 MP = 387 MB) for **every** slot, for the life of the project: **465 MB per photo,
  measured**, ~54 GB for 120 on a 27.7 GB machine. Not a leak; no smart pointer applies. Fixed with
  two **byte-capped LRU pools** plus re-decode-on-demand for a cold slot. Now: **120 RAW photos open
  in 50 s at `engineResidentMB=765`**, and engine residency is FLAT — 739 MB at 8, 739 at 16, 765 at
  24, 765 at 120.
- **[x] (4) "a parameter change recomputes every photo".** Measured against the event stream: it
  does not, and never did — one `set exposure=…` emits exactly one `frame.ready`. What the user felt
  was (2): with the machine swapping, a slider move had to fault a 387 MB source back in to rebuild
  an evicted proxy. Closed by the R-MEM work; no separate change. Worth keeping as the example of
  why a symptom gets measured before it gets fixed.
- **[x] (3) UI lag — the core half: a UI reservation in `ThreadBudget` (R-CPU-2d).**
  `decodeWorkers()`/`engineThreads()` divided `total()`, so the GTK main thread was in nobody's
  share: at `cpuPercent=100` on 16 cores that was 8 decode + 8 engine = the whole machine, with the
  UI as the seventeenth thread. `schedulable() = max(1, total - kUiReserve)` is now what the two
  consumers divide, and `backends` prints it. **Honest scope:** at the default 50% on this 16-core
  box only 8 of 16 cores were ever scheduled, so core starvation was NOT the whole story — most of
  the reported lag was (2)'s paging. The remaining UI-side cost is design's and is filed below.
- **[x] (3b) UI lag — the design half: MEASURED, and there is nothing to fix.** This entry
  originally claimed `App::refreshLibrary()` per decoded entry was "the cost that actually scales" —
  O(N) per photo, O(N²) over a load. **That claim was written from reading, and it is wrong.**
  Measured through the assembled-app rig in `cosmo_ui_tests`:

  ```
  refreshLibrary, once per arriving photo:   n=10 0.01 ms | n=40 0.13 | n=80 0.31 | n=120 0.55 ms
                                             (total, for the WHOLE load — 0.0046 ms per call)
  UI frame times across a 120-entry load:    14 860 frames, mean 0.022 ms, worst 14.59 ms,
                                             exactly ONE frame over 8 ms
  ```

  The O(N²) shape is real (0.0011 → 0.0046 ms per call as the rack grows) and the absolute cost is
  half a millisecond across an entire 120-photo load. `Filmstrip::onPaint` already culls off-screen
  cells (`Filmstrip.cpp:226`), so the per-frame cost does not scale either. Optimising this would
  have been a change with a good story and no effect — the D-41 lesson applied to my own diagnosis.

  **So the reported lag was (2) and (3), not the view.** Re-measure in the real app now that
  resident memory is flat and the window has its own thread; if any lag remains, it needs a fresh
  measurement rather than this guess. (Caveat, stated because it bounds the claim: the rig draws
  through a `RecordingTarget`, so real Cairo rasterisation is excluded — but that cost is per-frame
  and per-visible-cell, not per-photo, which is the thing that was in question.)
- **[x] (1) Selection now travels as a Command, mid-load included (R-SVC-2, DR-SVC-2d).**
  `App.cpp:100` called `mSession.selectNode(cell, …)` directly, so a click emitted no `Event`, wrote
  no log line and no other front end could see or script it — which is what "it doesn't even reach
  the core" meant. It is now `Command::Select` by node id, with multi-select promoted into the
  grammar as `select <node> [add|range]` because the filmstrip could always ctrl/shift-click and no
  command said so. Verified mid-load from the shell: `select 7` while the photos were still decoding
  gives `[evt] selection.changed node=7 slot=-1`. One of S4b's 96 direct session calls retired.

  **Still open, and worth a decision:** selecting a photo that has not decoded yet moves the ring
  and keeps the stage (R-LOADUX-2) but does **not** move that photo up the decode queue. Prioritising
  the photo the user just asked for is the obvious next behaviour and needs a requirement first.

**► 2026-08-22, on Windows: the CPU limit was reported broken, and three defects came out of it.
All three are now CLOSED (D-41, D-42, D-43).** Every one was found by running the suites and the CLI
on MSYS2 for the first time — the platform where the CPU budget was originally reported broken, and
the one place its defects were ever live.

1. **D-43 (S1)** — `CMAKE_BUILD_TYPE=Release` puts `-DNDEBUG` in the flags, so **every `assert()` in
   both cosmo suites was compiled out**. 37 tests printed `[PASS]` while checking nothing, and every
   "the suite is green" claim ever made from a Release tree was worth nothing. `TestMain.h` now
   undefines `NDEBUG` before `<cassert>`. **R-TEST-3 written.** Found only because D-41's new guard
   passed with the thing it guards deleted — *a test you have not seen fail is not a test.*
2. **D-42 (S1, hang)** — `OrderedParallelLoad::stop()` signalled `mCv` without holding `mMu`, so a
   worker that had evaluated the predicate but not yet blocked missed the wake and `join()` never
   returned. Deterministic on winpthreads. In the product it is `ProjectClose` and
   `ProjectLoader::start()`, both on the UI thread — going Home mid-load could hang the app.
3. **D-41 (S3)** — D-12's pin was wired to `CosmoService::setWorkerInit` and nowhere else, so it
   covered ProjectLoader's pool and none of the six other decode paths. A bare decode took 4.6 cores
   and 20 OS threads at `cpuPercent=25` and 4.5 at 100% — the setting did nothing to it. Now
   `cosmo_v2::PinnedDecoder` is the only decoder the host constructs and it pins the thread it
   decodes on. Sized from `ThreadBudget::total()` when it runs alone, **not** hard-pinned to 1:
   pinning everything to 1 also removed the violation and made opening a 24 MP ARW take 1.85 s
   instead of 0.79 s at every budget. After: 3.26 cores at 25%, 4.71 at 100%, 0.88 s.

**All 37 `cosmo_core_tests` pass on MSYS2 with assertions live — the first meaningful green this
host has ever produced — and `ctest` is 17/17.** The three guards written for D-11/D-12 have now run
here for the first time: `cpu_budget_scales_with_percent`, `one_budget_is_divided_not_duplicated`,
`project_load_peak_never_exceeds_its_pool (pool 5, peak 5 of budget 6)`. Fixture for the whole
investigation: `apps/cosmo/core/tests/fixtures/cpu_budget_win.ps1`.

**Worth carrying forward:** two of these three were invisible on Linux and one was invisible
everywhere. The Windows tree is not a port to be checked occasionally — it is where the only
`-fopenmp` LibRaw and the only winpthreads scheduler live, and it found what four months of green
suites did not. Run `ctest` there before believing a threading or budget claim.

**[`service-architecture-proposal.md`](service-architecture-proposal.md) is APPROVED** (2026-08-17,
in-process service + control socket, full S1-S5) and written up as **R-SVC-1…10**. P0 is superseded:
the harness stops being side doors bolted onto a GUI-shaped app and becomes a consequence of the
architecture. **S1 is done (S1a + S1b) and D-11 + D-12 are closed.** ► Next is **S2** — `AppModel` +
`Command`/`Event` + the `CosmoService`, **done** — a project opens headlessly — and **S5 is done**:
the control socket, verified by driving the live window over it. ► Next are **S3** (`cosmo-cc`) and
**S4b/S4c** (App's remaining 96 + 60 direct session calls, then session ownership).

The proposal exists because U1's CPU budget could not be debugged. Chasing "25% still uses 75%" on
2026-08-17 produced two confirmed defects and no runtime measurement: **D-12** — the
`OMP_NUM_THREADS=1` pin that U1.1 called "the layer that mattered" is a no-op, because libgomp reads
the environment in a constructor that runs before `main()` (proven with a 30-line fixture) — and
**D-11** — the budget is converted independently by the decode pool and by the engine, which run
concurrently, so a load peaks at about twice what the user asked for. Neither could be *measured*,
only read, because opening a project still requires a mouse (D-6). Two hours went into trying to
drive the real load path from a shell (seeded config + `.cmp`, XTEST and XSendEvent clicks into the
live window) and it never ran once. That is the case for the proposal, stated as evidence rather than
as preference.

U1 made P0's case concretely, twice over. **First**, the decode-pool log line added in U1.1 still
cannot be observed from a shell, because a project cannot be opened without clicking (D-6) — it is
`[!]`, not `[x]`. **Second**, U1.2's gesture-routing change was silently a no-op in the working
tree (an edit whose match string used `--` where the source has an em dash), and *every*
lower-level check passed anyway: it compiled, both suites were green, and the widget test proved
the link fires its callback. Only an end-to-end render — real `App`, real pointer event, look at
the PNG — caught it, and only on the second shot, when click-outside failed to dismiss. That is
precisely the gap P0.1–P0.3 close permanently; the throwaway harness used here is described in
the decisions log so the next session can rebuild it in one command if P0.2 is still pending.

Last updated: 2026-08-24 · D-45 attributed per stage and the SBC plan (T0-T5) written; nothing implemented yet · 2026-08-21 · U3.1 landed (the mixer no longer lights up noise) · T1 + T1a + T1b landed (the touch shell is on the service and renders with no device) · U2.1 + U2.2 + U2.2a + U2.4 landed (a cover is oriented like its photo; the photo
dissolves instead of popping). Open from U2: **U2.3** (the phone stage dissolves too). New defect
**D-36** — an out-of-range `set` crashes the render worker on a NaN that walks through ToneCurve's
clamp; core-owned, filed with the fix. · Also merged in from the other machine: **D-40** (filed there as D-36 and renumbered on
merge — see the top of the decisions log), the test harness header builds on the Windows host it
was written for — `_set_abort_behavior` is UCRT-only and `<windows.h>` was leaking `near` into a
suite that declares it. R-TEST-1/2 added; D-10's "Windows half code-verified only" is discharged. · Previous commit: D-22, a load now reports the WORK (entries claimed, named
stage) rather than only finished results — the nine seconds of "Preparing…" are gone.

**Milestone S is COMPLETE** (S1…S5). The §5 checks all pass: a CLI dump and a GUI dump of the same
project are byte-identical, no widget reaches past the service, every behaviour is a command,
a load's measured peak stays inside the budget, and the acceptance test runs unattended.

---

## Milestone status at a glance

| M | Milestone | State |
|---|---|---|
| U1 | CPU budget + Settings reachable from home | **done, and measured on Windows at last** — D-41 closed (every decoding thread is inside the budget, and sized from it); D-11's half re-confirmed here: `project` 3.3 → 7.7 cores, `render` 3.1 → 7.1, `info` 3.3 → 4.7 across 25% → 100% |
| S  | Core-as-a-service: `CosmoService`, `Command`/`Event`, CLI + GUI as views | **COMPLETE** — S1…S5. The §5 checks pass: identical dumps, the gate at 0, every behaviour a command, budget measured, acceptance unattended |
| P0 | Agent harness — CLI, headless render, debug logging, scripted input | superseded by S, except P0.11 / P0.12 |
| P1 | Doc-drift cleanup (D-1) and requirement coverage for what already shipped | not started |
| P2 | PARITY backlog: crop overlay (#3), preset picker (#4), settings (#5), split-drag (#6) | see `PARITY.md` |
| P3 | Known gaps: R-ZOOM-5 eased zoom (belongs in Artboard), unwired seams | not started |

---

## U1 — a CPU budget, and Settings reachable from the launcher

Reported symptom: "loading an image takes almost all the CPU"; wanted a 50% limit, changeable in a
Settings page that can also be opened from the home screen. Requirements: **R-CPU-1…5** (new),
amending **R-LOADPERF-1**; **R-SETTINGS-1** and **R-HOME-8** are amended by U1.2.

- [x] **U1.1** Core: `AppSettings::cpuPercent` (default 50) + `workersFor()`, enforced at the decode
      pool, at the engine's Auto thread count, and on LibRaw's OpenMP team. Persisted and tested.
- [x] **U1.1a** **Cleared.** The load's log line — unobserved since the CPU budget shipped,
      because reaching it needed a mouse (D-6) — is now observed from a bare
      `cosmo japan18.cmp`: `[evt] load.finished decoded=18 total=18` and
      `[evt] info load.peak decode=5 engine=6 budget=6`. It reads as events rather than one
      `LOGI` because an Event *is* the log line (R-SVC-5).
- [x] **U1.2** Design: the home sidebar's Settings link is live and opens the same modal over the
      launcher (Home advances/renders/routes to the dialog that lives in the editor tree), plus the
      CPU-limit chip row (25/50/75/100). Three `cosmo_widget_tests` assertions; verified end-to-end
      by rendering the real `App` driven by real pointer events at two window sizes.

## T — the touch shell: one core, two UIs (R-TOUCH)

Asked for on 2026-08-20: "use the same theme, make a touch version of cosmo suitable for a small
touch screen", with three added rules — **no component overlap**, **works in both orientations**,
**touch-friendly, especially curves** — plus "the same view/view-model mechanism so the same core can
run a different UI", Android first, and Android GPU adapters for the three core libraries.

Decisions taken with the user before any code (so no other machine re-litigates them):
1. **Service binding first**, before any new screen — everything else sits on it.
2. **Landscape is two-pane**: photo left, the active tray a fixed right-hand panel (~40% width), so
   nothing is ever over the photo. Portrait keeps one column.
3. **Curves get a fullscreen editor with a loupe** and direct drag (≥24 dp grab, tap selects the
   nearest node), not a curve inside the tray.
4. **GPU work: ImageProcessing stage coverage + an Artboard GLES render adapter.** DSP is **not** in
   scope (cosmo does not use it).

- [x] **T1** The touch shell binds to the service, and can be seen without a device (**R-TOUCH-1**,
      **R-TOUCH-5**). `PhoneApp` takes a `CosmoService&` and owns no session; every write is a
      `Command` (`editcmd::diff` for whole-`EditParams` edits, the mask commands for masks, `settings
      set` for preview/threads/GPU — which also removed a direct `par::setThreads` from the UI);
      frames come from `takeFrame`; decode moved behind the service's factory, which is what made the
      file buildable off Android at all. The UI→Command mapping is **shared** with the desktop
      (`EditCommands.h`; `RightColumn`'s local formatters are now `using` declarations of it).
      New `cosmo_touch_shots` renders every state at 393×852, 360×780 and 852×393 and asserts the
      shell builds, renders, takes a photo into the model and survives a rotation (`ctest -R
      cosmo_touch_layout`). Its first frames immediately found **D-38**
- [x] **T1a** **Touch mode is a setting on the desktop** (**R-TOUCH-6**, asked for directly:
      "make me a setting in desktop version that change UI to touch mode"). Core half:
      `AppSettings::touchUi`, persisted, `settings set touchUi=1`, stored-and-forwarded by the
      service like `uiScale` and printed in the model dump. Host half: a sixth Settings row
      (**Input** — `Mouse` / `Touch`), and `linux_main.cpp` holding a `PhoneApp` beside `App`,
      cross-faded (260 ms) with both drawn through `pushLayer` while the switch is in flight.
      Both shells share the service, so the project, selection, parameters and undo history
      survive the switch — nothing reloads. In a window wider than tall the phone shell is drawn
      in a centred 430 dp column (`TouchViewport.h`, shared with the shot renderer) because
      there is no landscape layout yet — a dated letterbox, not the end state. Found by looking:
      the first shot drew the column flush left, because the tree sets the transform absolutely
      and an outer translate is wiped — hence `PhoneApp::setOrigin`. Verified by the
      `desktop-touch` shot and by launching the real app with `touchUi=1` seeded
- [x] **T1b** **The touch shell is a view, not a second owner** (**R-TOUCH-1** tightened, closes
      **D-39**). Reported the moment touch mode shipped: switching showed no project. The shell
      kept its own screen, its own recents and its own `buildSession` — whose first line was
      `resetWorkspace()`, so its Home actions could have thrown away the project the desktop view
      had open. `syncFromModel()` now adopts the service's screen, project, edit target and
      recents on construction and on every revision change; the local lifecycle is deleted; New /
      Open / Import are host seams ending in commands (the desktop host wires them to its own
      dialogs); entering the editor re-selects so a preview actually arrives. Asserted by a
      harness case that opens a project with NO touch shell alive and then builds one, plus the
      `adopted` shot
- [ ] **T2** **No overlap, and both orientations** (**R-TOUCH-2**, **R-TOUCH-3**, closes **D-38**).
      Portrait: the tray gets its own box and the photo's box SHRINKS for it (no overlay); the row
      list is measured against the space left above the action bar and scrolls. Landscape: the
      two-pane layout. Then the assert mode gains the sibling-rect intersection check, and both
      orientations get shots at rest and mid-rotation
- [ ] **T3** **Touch sizing pass** (**R-TOUCH-4** minus curves): every hit band ≥44 dp, slider rows
      ≥48 dp, ≥8 dp apart, asserted from the shell's own hit zones rather than by eye
- [ ] **T4** **The fullscreen curve editor** (**R-TOUCH-4**): whole-screen plot, ≥24 dp grab radius,
      tap selects the nearest node, and a loupe offset above the finger so the fingertip never covers
      the node it is placing. Mixer/hue curves on the same screen
- [ ] **T5** **The rest of the editing surface on touch**: mask tray + on-canvas overlay, grade and
      xform trays, filmstrip + breadcrumb + group indicators, pinch-zoom and two-finger pan
      (M4–M7 of `docs/android.md`, now with a harness behind them)
- [ ] **T6** **ImageProcessing: more stages on the Android GPU.** The GLES compute backend covers
      exposure / contrast / white balance / sRGB encode; extend it to tone curve, tone regions,
      clarity/texture, sharpen and masks, each conformance-tested against the CPU reference
- [ ] **T7** **Artboard: a GLES render adapter.** Draw the UI on the GPU instead of software Cairo
      (a new `IRenderTarget` adapter in the Artboard repo, via `implement_artboard`) — for UI
      smoothness and battery on device, and reusable by every Arstro app on Android

## U3 — reported 2026-08-21

- [x] **U3.1** (engine) The Mixer's Lum curve no longer lights up noise in grey areas
      (**R-MIXER-1…4**, `arstro_image` commit). Asked as a question first — "is it make sense for
      lum curve that lower saturation receive less amount of lum" — and the answer was yes, for a
      numerical reason: hue is derived by dividing channel differences by chroma, so on a neutral
      pixel it is decided by noise, and the additive Lum channel then gave each pixel of a flat
      grey a different full-strength lift. Every mixer channel is now scaled by
      `smoothstep(0.010, 0.040, chroma)`, weighted on **chroma** rather than HSL saturation
      (saturation is normalised by lightness, so it lies in the shadows — exactly where noise
      lives). Measured: a noisy grey patch's luminance spread went 0.00240 → 0.26549 unweighted
      and 0.00240 → 0.00240 weighted; on a real X-Trans frame the flattest patch went 0.00025 →
      0.15329 (**×624**) unweighted and ×1.00 weighted, with the most colourful patch in the same
      frame moving identically either way. Test fails on the unweighted code, checked by
      re-breaking it

## U2 — what the photographer reported on 2026-08-20

Two items, in the user's words: **(1)** "thumbnail photo shouldn't be rotate, just use original image
and crop"; **(2)** "make the edit more smooth by duplicate the photo on UI, photo will be fade from
current to new photo with adjustment when doing adjustment on slider". Requirements: **R-THUMB**
(new) and **R-VIEW** (new). Item 1 is core-owned (`core/decode/`), item 2 design-owned
(`widgets/PhotoCanvas`) — two commits, core first.

- [x] **U2.1** (core) A cover is turned the way its photo is (**R-THUMB-1**). `dcraw_process`
      applies the RAW's `sizes.flip`; `dcraw_make_mem_thumb` does not, and `sizes.flip == 5` on 18
      of the 19 sample RAWs — so `DSCF5186.RAF` decoded to 4170x6246 portrait while its project card
      showed a 4416x2944 landscape cover. `NativeImageDecoder::applyFlip` (LibRaw's own `flip_index`
      math) is applied to the embedded preview, guarded by the preview's own aspect so a maker that
      already stores it upright is not turned twice. The crop half of the report was already right:
      covers and filmstrip cells are `Fit::Cover` (**R-THUMB-2**), which crops rather than distorts.
      Verified both ways (**R-THUMB-3**): a unit test pinning all four turns against a hand-computed
      3x2, and a probe over the real RW2/RAF/JPEG set where `decodeThumb` and `decodeFile` now agree
      on aspect and need **no extra rotation** to match (rms 50.0/59.4/10.2/0.43 against
      81.0/82.9/36.3/41.2 for the 180-degree alternative)
- [x] **U2.2** (design) The photo dissolves instead of popping when an adjustment lands
      (**R-VIEW-1**, **R-VIEW-2**). `PhotoCanvas` holds two stacked `ImageView`s and ONE animated
      property — the top one's opacity — so the composite is `a·top + (1−a)·bottom`: a true
      cross-dissolve with no dip through the canvas, no pixels copied between views, and
      successive renders dissolving in alternating directions. A render arriving mid-dissolve
      reverses it instead of restarting (during a drag that is the normal case). The public seam
      is `setPhoto(rgba, w, h, nowMs)`, so the Before/After toggle dissolves for free, and Split's
      clip + seam now fade instead of flipping `visible`. Also closed R-ZOOM-3's stale wording (it
      said "both views"; there are three) and stopped drawing the fully covered layer.
      Verified three ways: `cosmo_ui_tests` asserts off the recorded op stream that mid-dissolve
      there are TWO stage photos inside a fractional layer whose alpha MOVES, and one photo with
      no layer at rest; `cosmo_shots --only editor-dissolve` renders early/late frames of one
      dissolve at 1600x1000 and 1280x800 (looked at, the blend is visible in both); all 16 ctest
      suites green
- [x] **U2.2a** (design) The dissolve no longer blinks (**R-VIEW-1a** amended, **R-VIEW-1e** new,
      **D-37**). U2.2's own report back from the user: "it doesn't smooth, make the photo blink when
      transition" — and it was two per-frame bugs, neither visible in the code's shape. **(1)** The
      covered-layer optimisation read the PREVIOUS frame's alpha, so the base was out of the tree
      for the first frame of every 1→0 dissolve and the canvas showed through the partly
      transparent top: one dark frame per render, about 8 Hz through a drag. **(2)** A render
      arriving mid-dissolve was written into the layer that still carried weight `1−a` — R-VIEW-1a
      called that residual acceptable and it is not; the newest frame is now HELD and applied when
      the dissolve settles, at the only moment a write is invisible. Also swapped `EaseOutCubic`
      for **linear over 160 ms**: an ease-out is 35% across after one frame, so the first frame
      carried a third of the change and read as a partial cut. Guarded by
      `theStageNeverBlinksDuringADrag`, which drives 100 adjustments and asserts both per-frame
      facts off the op stream — **checked against the shipped code first: both assertions fail on
      it** — plus a re-rendered `editor-dissolve` pair at two sizes, looked at
- [x] **U2.4** (core + design) The typeface travels inside the binary, and it is Roboto
      (**R-FONT-1…4**, **R-G-2a** amended, Artboard **FR-22a**). Asked for as "copy this font to
      this repo … use this font for the app to make sure consistent UI between platform, embed the
      font directly into binary build". Roboto Regular/Medium/SemiBold vendored under
      `assets/fonts/Roboto/` with its OFL; `cmake/embed_fonts.cmake` generates a C++ array from the
      five faces (Roboto + JetBrains Mono, which stays the numeric face) and
      `registerEmbeddedFonts()` hands them to `CairoTarget::registerFontMemory` — so **Fontconfig
      is out of cosmo entirely**, `ARTBOARD_CAIRO_FT` is on for the desktop build, and the binary
      draws its own glyphs with nothing to find on disk. `cosmo_shots` and `cosmo_ui_tests` share
      the one registration, so a shot measures the app's real text. Fell out of it: the wordmark's
      accent dot was placed with `estimateTextWidth` (`len·px·0.6`, font-independent) and detached
      from the `o` under the new face — both stragglers now measure, like `SplashScreen` and the
      phone shell already did, and R-G-2a says so. Verified by `strings` finding every family in
      the binary, home + editor shots at two sizes in Roboto with the dot tight against the word,
      and all 16 ctest suites green
- [ ] **U2.3** (design) The **phone** stage dissolves too — `touch/PhoneApp.cpp:872/891`
      (`EditorScreen::mPhoto` / `setPhoto`) still replaces its pixels in one frame. Same fix as
      U2.2, ~20 lines: a second `ImageView`, one opacity, the same "newest pixels into the hidden
      view" rule, plus `mNowMs` threaded through `setPhoto`. Deliberately deferred, not missed
      (**R-VIEW-1d**): that shell has no headless test or shot target, so landing it needs the
      harness first or it ships unverified

## S — the core as a service (R-SVC-1…10)

Design + migration plan: [`service-architecture-proposal.md`](service-architecture-proposal.md).
Strangler: each step leaves both paths compiling, so the app works after every commit. S2 and S4
cross into `App`/`widgets/`, which belong to `arstro.cosmo.design.implement` — those are two commits,
core first.

- [x] **S1a** (core) `ProjectLoader` + `ThreadBudget` in `cosmo_core`; `linux_main.cpp` keeps only the
      UI-side consumer. Closes **D-11** and **D-12**. Two new tests, the first checked to fail against
      the old arithmetic; measured end to end on the reported 18-RAF project (25% → 19% of a 24-core
      machine, peak 5 of a budget of 6)
- [x] **S1b** (design) `App::applyThreadBudget` deleted; the Settings dialog's thread controls report
      the choice and let the host's `ThreadBudget` apply it, notified before the re-render so the new
      frame uses the new width. `App.cpp` no longer includes `base/Parallel.h` at all
- [x] **S2** `AppModel` + `Command`/`Event` + both codecs + `CosmoService`, and the GUI rewired onto
      it: the project load left `linux_main.cpp` entirely, `onServiceEvent` translates the event
      stream into App's animation, and `onTick` pumps the service once per frame. Four new tests,
      including **a project that opens, decodes and reaches the editor with no window at all**
- [x] **S3** `cosmo-cc` over the service — `info`, `backends`, `project --print`, `render`, `export`,
      `params --print/--diff`, `check`, `bench`, `run --script/-`, and the global `--json` /
      `--watch` / `--serial` / `--stable` / `--params`. Subsumes A1–A9 and old P0.8–P0.10.
      **R-SVC-9's headline check passes**: `cosmo-cc project japan18.cmp --print --stable` and the
      GUI's `state print --stable` over the socket are now **byte-identical** on the real 18-RAF
      project. Getting there found **D-14** and **D-15**, both fixed here — neither was visible any
      other way, which is the argument for the check existing at all. `cosmo-cc attach <socket>`
      completes the loop the proposal promised: it drives a live `--control` window from a script
      and exits non-zero if any command was rejected.
  - [x] **S5b** the acceptance test is a **committed, unattended artifact** —
        `apps/cosmo/tests/acceptance/run.sh` + `drive-a-live-window.txt`. It runs the SAME script
        through the headless service and through a live window over the socket, asserts the event
        stream, and diffs the two `--stable` dumps. It found **D-16** on its first run. The socket
        is also serviced during the splash now, so a client that attaches immediately is not
        ignored for the length of the intro.
- [~] **S4** the rest of `App`'s logic moves down — export batch, presets, copy/paste settings, group
      ops, save/load workspace. `App.cpp` ends as render + gestures + animation *(core, then design)*
  - [x] **S4a** `App::onCommand` — the view's outbound channel (R-SVC-2). Undo/redo now emit a
        `Command` and fall back to the direct call only when no service is wired (a bare `App` in
        a widget test). A menu item, a shortcut and a socket line take one path.
  - [~] **S4b** **The metric was wrong and is corrected here** — worth reading before continuing.
        Counting `mSession.` in `App.cpp` does not measure progress, because a converted method
        *keeps* its direct call as a deliberate fallback for a bare `App` with no service wired
        (`cosmo_widget_tests` builds one). The count stayed at 96 across four real conversions.
        The three numbers that do mean something:
        - **Behaviours expressible as a `Command`: 24** (was 22). Rising is progress, and
          `every_command_kind_has_a_grammar` fails if a kind is added without a grammar rule.
        - **`App` methods that route through a command when a service is wired: 4** — `undo`,
          `redo`, `deleteSelected`, `renameGroup`. This is the numerator; the denominator is
          every discrete user action App still performs itself.
        - **The actual gate, and the only one in the proposal's §5 checklist: `widgets/*.cpp`
          reaching past the service = 60**, all in `RightColumn.cpp`, which must reach **0**;
          plus **2** `svc->session()` uses in the host, also to 0.
  - [x] **S4b-2 — RightColumn, DONE.** All 23 sliders (one lambda change: it takes a field name
        and a unit converter instead of a setter closure, so the command carries engine units),
        the mask panel, the mixer, both curve sets, grading, the remap quad and transform — every
        one now leaves as a `Command`. **No new command was needed for any of it**, exactly as
        DR-SVC-2b said; the single genuine gap was `dabs`, added to `MaskSet` here, without which
        a Brush drag — whose whole product is dabs — was the one edit no command could express.
        The gate: `grep -c "mSession\.\|EditParams" widgets/*.cpp` → **RightColumn 29, every
        other widget 0**. Of those 29: 9 are prose in comments, 11 are *reads* through one named
        seam (`params()`/`effectiveParams()`, legitimate under R-SVC-4 and S4c's job), and 7 are
        the documented unwired fallback. **Converted-surface writes reaching past the service: 0.**

  - [x] **S4c** **DONE.** `CosmoService` owns the `EditSession`; a front end constructs the budget,
        then the service, then the view — declaration order in `Host` and in the shot `Rig` is now
        the architecture, bottom-up. `App` holds `CosmoService&` plus a *reference* member
        `mSession = mSvc.session()`, which is what let 95 not-yet-migrated call sites compile
        unchanged while the ownership actually moved. The frame path moved too, closing **D-21**.
        **The gate is 0**: `grep "mSession\.\|\.session()" widgets/*.cpp` is empty — RightColumn
        reads `AppModel` and, with no App above it, dispatches straight to the service it holds
        rather than writing to a session it no longer has.

  - [ ] **Watch item from S4b-2**, not yet a defect: now that every control emits a command, each
        `set` re-enters `App::syncFromSession()` synchronously, so a **crop drag** and a **mask
        feather drag** get `XformPanel::setState` / `MaskPanel::setMasks` pushed back at them
        mid-drag — the very thing the feather handler's comment warns against. The setters are
        programmatic and do not refire `onChange`, so it is believed harmless, and the acceptance
        run is clean; but it is a new interaction and it has not been driven by hand at 60 Hz.
        Check it before trusting a long crop drag.
- [x] **S5** `ControlChannel` (non-blocking `AF_UNIX`; Windows stubbed with a reason) + `--control`,
      wired into the frame tick. **The acceptance test passed on the real 18-RAF project**: the GUI
      driven entirely over the socket, `load.finished decoded=18 total=18`, and a window capture
      showing `DSCF5194.RAF (2/18)` with the Exposure slider moved. It also found **D-13** on its
      first run — a bug S2 had just committed, which no headless test could see.

**S is done when** the §5 checks in the proposal pass: a CLI-opened project and a GUI-opened project
dump the same `AppModel`; `grep "mSession\.\|EditParams" apps/cosmo/widgets/*.cpp` is empty; every
`R-*` behaviour has a command; a load's measured peak concurrency is within budget; and the
acceptance test runs unattended.

**Absorbed from P0 along the way:** P0.1/P0.2/P0.3 (headless UI render + assertions) land with S3's
harness; P0.4/P0.5/P0.6 (log levels, categories, UI logging, `--dump-ui`) become `Event` plumbing in
S2 — an event *is* a log line under R-SVC-5; P0.7's `--project` is S3; P0.11/P0.12 (`DEVELOPING.md`,
Windows `configDir()`) stay as written and are listed below.

## P0 — the agent harness (superseded by S; kept for the two tasks S does not absorb)

Each task is one session. Specs: `arstro.cosmo.core.implement` §5–§6 (A1–A12) and
`arstro.cosmo.design.implement` §6–§7.

- [ ] **P0.0** `R-AGENT-*` requirements written and conflict-checked *(do this first — V-model)*
- [x] **P0.1** `COSMO_APP_NOMAIN` in `apps/cosmo/CMakeLists.txt` (A11) — one `list(REMOVE_ITEM … linux_main.cpp)`; it structurally blocks every headless UI harness
- [x] **P0.2** `cosmo_shots` — headless PNG renders of every named app state, 2+ window sizes, mid-transition (A12)
- [ ] **P0.3** `cosmo_ui_tests` — and P0.2 left three lessons for it: one `CairoTarget` per RUN
      (a per-frame one invalidates every registerImage id); a wrong shot is not a blank shot, so
      assertions must compare content rather than count colours; and every transition is
      EaseOutCubic, so its linear midpoint is ~85% finished. `cosmo_ui_tests` — headless assertions over the assembled app: non-overlap, reachability, text fit, reflow (A12)
- [x] **D-10** (was folded into P0.3/P0.11) `core/tests/TestMain.h` — a failing assert now exits
      non-zero with its message intact through a redirect, instead of hanging where a red suite
      looked like a slow one. **The MSYS2 confirmation this line asked for happened on 2026-08-19
      and failed: the header did not build on Windows at all (D-40).** Now fixed and measured
      there — exit 3 in 0.18 s — so the caveat is discharged and the harness has a requirement,
      R-TEST-1/2
- [x] **P0.4** `Log`: level honoured (compared before formatting), categories **derived** from the
      event stream's dotted names so the two vocabularies cannot drift, all four env vars + flags,
      `setStderrEcho()` alive, GLib routed through `g_log_set_default_handler`, Windows backtrace via
      dbghelp resolved at runtime. **Closes D-2, D-3, D-4** — D-4's Windows branch code-verified only
- [~] **P0.5** **Smaller than it was.** Session/load/selection/params/history/export are all logged
      already, because `onServiceEvent` writes `formatEvent(e)` and an Event *is* the log line
      (R-SVC-5) — that was most of D-5. What is genuinely left is **input**: which widget consumed
      a click, and screen/scroll/hover decisions, none of which are service state
- [x] **R-SVC-12 / D-35** **The view binds to the view-model.** Selecting another image left every
      panel showing the previous target's values: `App.cpp` still holds ~95 direct `mSession.` calls
      (a dozen mutations), which changed the session without the snapshot being re-derived — so the
      sync faithfully filled the panels from a model describing the OLD target. `App.cpp` was never
      covered by S's "the widget layer reaches nothing" check, because that grepped `widgets/*.cpp`.
      Now: one bind, `bindIfStale()` at the top of `render()`, guarded by `AppModel::revision` —
      which had promised exactly this since S2 and was never used. Completes the MVVM split.
      **Owed:** turn those dozen mutations into Commands; each deletes a use of the
      `refreshFromSession()` bridge, and the last one deletes the bridge.
- [x] **D-34 / §7 UI logging** **The actual cause of the "green curve" reports.** `applySetFields`
      emitted `ParamsChanged` before `refreshModel()`, so the host re-seeded the right column from a
      PRE-EDIT model and handed the panel back the curve it had just replaced — while the green
      readout, fed after dispatch returns, showed the new one. The user's edit visibly jumped from
      the blue curve to the green line. Found in four log lines by the widget tracing the user asked
      for (`WidgetLog` + `App::pointer` input logging, closing §7's oldest gap). Invariant now stated
      as DR-SVC-5a: refresh before emit. Closes D-33 as the same root cause.
- [x] **D-32** **The curve-grab teleport** — the actual cause behind two earlier "green curve"
      defects. A node grabbed anywhere inside the 13 px pick radius was written the POINTER's
      position, so one pixel of input moved it up to 13 px, to the click point — which landed it on
      the green readout whenever the user aimed near it. Fixed with a grab offset in both curve
      editors. Two existing tests had encoded the teleport as their expected result and had to be
      rewritten; that is why it survived every suite. Filed **D-33** (the right column does not
      resync on a socket-driven param change) as a recommendation, not applied.
- [x] **D-31** **The green "final" curve is shown-only for real now.** D-28 fixed how it looked and
      left it reachable: a double-click on it adds a corner exactly on it (18 of 21 sampled points),
      so the edited curve snapped to touch the readout and the user had, to all appearances, dragged
      it. Fixed by having the readout leave while the plot is worked in. The bigger finding is that
      D-28's guard was a **spot check** on the one point where nothing happens — the same mistake
      the home-header test was written to avoid, made in the same session — so the guard is now a
      21-point sweep.
- [x] **R-SCALE-2a / D-29** **The scale change animates**, and R-G-1 became a stated
      non-negotiable. The setting shipped snapping the largest visible change the app can make —
      the third time a change violated R-G-1 while its author had just read it, so the rule is
      amended (the property list is examples not a boundary; it covers the coordinate system; a
      setting is not exempt; a derived value must be recomputed from the eased source; compliance
      is two frames, not a code read) and the design skill now leads with it. Range extended to
      **200%** for a small dense screen, with scales the display cannot honour offered disabled.
      New **`cosmo_ui_tests`** target — the assembled app, with a clock — because
      `cosmo_widget_tests` structurally cannot see a snap. Also D-30: the chip wrapping the 7-chip
      row needed made `cardRect` and `chipRects` call each other, which crashed the dialog.
- [x] **R-SCALE** **Screen scale** — a small-screen UI scale, 75 / 90 / 100 / 125%, persisted and
      scriptable (`settings set uiScale=N`). Two commits, core then design. **One transform on the
      view root plus its inverse on pointer input** (R-SCALE-2): `mW`/`mH` became LOGICAL units, no
      widget reads the scale and no constant is multiplied at its use site. It had to live in `App`
      rather than as a `cairo_scale` in the GTK layer because `setTransform` is absolute and the ten
      identity resets inside `App` would have wiped it.
      The layout-integrity half is the substance, and it found two real breaks that predate the
      setting: the **editor had no computed minimum** (the photo canvas could be dragged to 64 px
      wide with the rail still open), and the launcher's **"Recent Projects" header ran under the
      search field** at minimum width. Both fixed — `App::minLogical*` takes the larger of the two
      screens' floors and the window minimum is `that x scale`; the rail now folds before the canvas
      does, with the user's intent kept separate from the effective state so widening restores it;
      the header row shares its width, title first. Shots at every scale in the smallest window that
      scale permits, including the settings modal itself, all fixture-free and in ctest.
- [x] **P0.6** **`ui dump`, not `--dump-ui`** — a *command* over the control socket rather than a
      launch flag, because the question is almost always about a window that is already running
      (R-SVC-11 / DR-SVC-11). Segment tree with demangled type, world rect, size, visibility,
      opacity, hover, enabled and clip; `--root` picks the editor / home / splash tree, `--json`,
      `--visible`, `--depth`. Answered host-side, never by the service, since the tree is
      presentation (R-SVC-3); `cosmo-cc` headless answers "no view attached" so one script drives
      both front ends. Widgets that paint themselves add one line via `UiInspectable` — otherwise a
      self-drawn leaf dumps as a rectangle and says nothing about what it drew. Closes D-25, and
      found D-26 (the splash's progress bar was never visible) and D-27 (`wait <ms>` meant two
      different things in the two front ends) within minutes of existing.
      Scroll offsets are **not** in it: no widget exposes one, and inventing an accessor per
      widget to fill a column belongs with the widget that needs it
- [~] **P0.7** `cosmo` flags. **`--project` and a bare `.cmp` argument are done** (closes D-6), and
      `--control` landed with S5. Still open: `--headless`, `--script`, `--shot`, `--log-level`,
      `--debug`, `--exit-after`. Note `--script` is largely redundant now — `cosmo --control` plus
      `cosmo-cc attach --script` does the same job and is already tested by the acceptance run
- [x] **P0.8** `cosmo-cc` skeleton + `info` + `backends` + `check` — **done in S3**
- [x] **P0.9** `cosmo-cc render` + `params` + `project` — **done in S3**
- [x] **P0.10** `cosmo-cc export` + `bench` — **done in S3**
- [x] **P0.11** `docs/DEVELOPING.md` — **written**, and every command in it was run before it was
      written down. Declares **CMake into `build/` canonical on every platform**, because the
      alternative turned out not to link at all (D-19). Records the `build/` vs `build-mingw64/`
      distinction, the Release-vs-Debug trap in the same directory, the document map, the
      invariants, and the closed-defect lessons with ids. Closes **D-9**
- [x] **P0.12** `configDir()` outside MSYS2 — `XDG_CONFIG_HOME` first everywhere, then an adopted
      pre-D-8 directory, then `%APPDATA%`/`%USERPROFILE%`, then `$HOME`, then the old relative path.
      Adoption rather than migration, because this layer has no error channel to report a half-copy.
      Closes **D-8**
- [x] **P1 (part)** doc drift — **D-1 closed**, and it was six places rather than three: two of them
      were requirement files contradicting *themselves* (DR-SCREEN-2 vs DR-LOADUX-4 in one file;
      R-LOADING-0 vs its own R-LOADING-1 amendment), plus a puml class that no longer exists and a
      comment in `App.cpp`. A full `file:line` anchor sweep of `detailed_design.md` remains
- [ ] ~~**P0.11**~~ superseded above — the cold-start guide: where things are, which document decides
      what, the invariants, the worked commands, the mistakes already made (model it on
      `apps/genesis/docs/DEVELOPING.md`, which is the best example in this repo)


**P0 is done when** a session on a fresh machine can, from a shell alone: build, run every test, render
any screen to a PNG, dump the UI tree, replay a scripted interaction, drive the whole engine headlessly,
and read a debug log that explains what the UI did.

---

## Decisions & deviations log (newest first)

- **2026-08-24 — one green run of a threading change is not evidence, and this is the second time
  the project has paid for that.** The parallelFor pool passed its two tests, passed `ctest`, and
  segfaulted one run in three (D-47). Both tests checked *chunk coverage*; the hazards were *object
  lifetime* and *concurrent callers*, and neither is expressible in a test that runs one batch on one
  thread. Worse: the first fix was correct about a real bug and the crash stayed, because
  `parallelFor` is called from the render worker, the export path AND a load's convert-and-downscale
  at the same time — 19 call sites, and one shared batch descriptor cannot serve two of them. **When
  a fix does not make a flake go away, check whether it fixed a different bug rather than assuming it
  was wrong.** And "who else is on another thread right now" was answerable by
  `grep -c par::parallelFor` before a line of the pool was written.
  **So: after any change to the pool, the load pipeline or the render worker, run the suite at least
  five times in a row, and write the test that makes the batches OVERLAP.** The same family as D-42
  (a condition variable signalled without the lock) and D-11 (a concurrency claim "verified" by a log
  line nobody ran); §4's "randomized or TSan argument, not a looks-fine" was already the rule and a
  looks-fine wearing a test's clothes still got through.

- **2026-08-24 — T0.3 and T0.4 landed in ONE commit, deliberately.** The skill says never batch two
  features, and these are two ledger tasks. They are both edits to the same dozen lines of
  `renderInto`'s buffer-and-observation handling, and splitting them would have meant committing an
  intermediate state where the working buffers are members but both histogram taps still run — a
  state nobody would ever want to bisect to, and one whose measurement means nothing on its own.
  Both are separately measured and separately guarded by their own test, which is what the rule is
  actually protecting.

- **2026-08-24 — "porting to a small SBC" turned out not to be a porting problem, and the measurement
  is what showed it.** The request was to make cosmo survive on an Allwinner A733 / RK3588 class board
  where a slow render is acceptable but a laggy UI is not. The obvious readings were both wrong:
  * *"the SBC has too few cores"* — no. 24 threads beat 8 by 13% and beat **one** by only 5x, because
    nine spatial stages early-out through a serial 27 MB `std::copy` and `renderInto` page-faults 82 MB
    of freshly-allocated working buffers per render. The pipeline stopped scaling somewhere around four
    threads, so RK3588's four A76s are within ~2x of this machine's *useful* parallelism. Adding cores
    was never going to be the fix, and neither was taking them away the cause.
  * *"it needs ARM-specific optimisation"* — no. 132 of 193 ms goes to stages sitting at their default
    value, and the 61 ms that remains is allocation + three histogram passes + a `pow`-based sRGB
    encode. **Nothing on the T0/T1 list is ARM work**; it all makes the Ryzen faster too. The port is a
    defect list, not a platform.
  * The one thing that IS genuinely about the target hardware is the **shape** of the interaction, not
    its speed: no fixed resolution can be real-time on every board, so the preview resolution has to
    follow a measured latency budget instead of a constant (T2). That is also the requirement D-45
    filed as missing, and the number in it is the user's to set.
  * Decided **not** to reach for the GPU first, although both target SoCs have one and it would not
    compete with the UI thread. `computeSupports()` accepts only exposure/contrast/temp/tint with
    everything else at identity, so it declines in real use; widening it is real work, and Mali driver
    quality on these boards is the highest-variance thing on the list. Cheap, certain wins first.


- **2026-08-23 — a reported symptom was traced to the wrong layer twice, and measuring was what
  settled it both times.** Of the four problems reported against a 120-photo RAW project, only two
  were where they appeared to be.
  * *"changing a parameter computes every raw photo"* — it never did. One `set exposure=…` emits
    exactly one `frame.ready`; the event stream said so in one command. What the photographer felt
    was the memory defect: with the machine swapping, a slider move had to fault a 387 MB source
    back in to rebuild an evicted proxy.
  * *"the UI is really lag"* — I filed `refreshLibrary`'s per-entry rebuild as "the cost that
    actually scales", from reading. Measured through the `cosmo_ui_tests` rig it is **0.55 ms
    across an entire 120-photo load**, and UI frame times over that load are mean 0.022 ms with one
    frame above 8 ms. `Filmstrip::onPaint` already culls off-screen cells. Optimising it would have
    been a change with a good story and no effect.

  Both readings were plausible and both were wrong in the same way: a symptom was attributed to the
  layer it was *visible* in rather than the layer it came from. The rule this project already had
  for defects — prefer a measurement to an argument (R-CPU-4, R-MEM-4, D-41) — applies just as much
  to a fix in progress as to a diagnosis. **Measure before optimising, including your own last
  guess.** The probes were throwaway (a timing assertion would be flaky on any other machine); the
  numbers live here so nobody re-derives them.

- **2026-08-21 (merge) — Two machines allocated D-36 at the same time, and the published one
  keeps it.** This machine filed the `TestMain.h` Windows build break as D-36 on 2026-08-19 while
  origin filed the render-worker NaN crash as D-36; both branches read "next free id: D-36" from a
  common ancestor whose counter was itself stale (it said D-25 with D-35 already filed). Origin's
  D-36 had been pushed and is referenced from `tests/shots/renderShots.cpp`, so it keeps the id and
  the local one was renumbered **D-36 → D-40** everywhere it is named (DEFECTS.md, this file,
  REQUIREMENTS.md, docs/requirements.md, docs/DEVELOPING.md, `core/tests/TestMain.h`). The one
  deliberate exception is the `d36_probe.cpp` filename inside D-40's Evidence block: that is
  verbatim recorded stderr from the run, and rewriting a transcript to match a later renumbering
  would make the evidence a fiction. Next free id is now **D-41**. The lesson is the counter: an
  id claimed only in a working tree is not claimed at all, so a session that files a defect should
  fetch before it picks the number, and the ancestor's stale counter shows this had already been
  drifting.

- **2026-08-21 (U3.1) — Chroma, not saturation, and a floor rather than only a ramp.** Two things
  that looked like details and were not. (1) Gating a per-hue effect on HSL *saturation* fails in
  the shadows, because `s = d/(mx+mn)` inflates a tiny chroma into a large `s` — the noise is worst
  exactly where the gate would be loosest. (2) A weight that merely attenuates is not enough: with
  the floor at 0.004 the ±1/255 noise still carried ~6% of a steep curve, and 6% of a 0.5 lift is
  still visible speckle. The floor has to sit **above** the noise chroma (0.010) so a neutral pixel
  receives exactly zero.
- **2026-08-21 (U3.1) — A flat test curve cannot show this defect.** The first fixture lifted every
  hue by the same amount, so the random hues produced identical lifts and the spread did not move.
  The defect is hue-DEPENDENCE landing on pixels whose hue is noise, so the curve has to swing (+1
  at red, −1 at cyan) for the measurement to mean anything.

- **2026-08-20 (T1b) — Moving the writes onto commands was not enough; the LIFECYCLE had to go.**
  T1 converted every parameter write in the touch shell into a Command and called the binding done.
  The shell still owned its screen, its recents and a `buildSession()` that reset the workspace —
  so it looked bound and behaved like a second application, and the first thing the user saw after
  the mode switch was an empty Home. The lesson worth keeping: "does it write through the service"
  is a weaker question than "does it keep any state the service already has". The second question
  is the one R-TOUCH-1 now asks, and the harness asks it too.

- **2026-08-20 (T1a) — Touch mode is a live switch, not a restart, and the letterbox is dated.**
  Both shells bind to the same service, so swapping them keeps the project open — that is the
  payoff of T1 and the reason this could be a 260 ms cross-fade instead of "restart to apply".
  The centred 430 dp column in a wide window is a deliberate interim while T2 is unbuilt: filling
  a 1600x1000 window with the current touch layout stacks four bands of controls (D-38), and a
  letterbox that says "this is the phone layout" is more honest than a broken one that says
  nothing.
- **2026-08-20 (T1a) — A shell offsets itself; a caller's translate cannot.** `CairoTarget` maps
  `setTransform` onto `cairo_set_matrix` (absolute), and the segment tree sets the transform per
  node, so wrapping `PhoneApp::render` in a translate is wiped on the first node. Hence
  `setOrigin`. Worth remembering for any future "draw this shell over there" idea.

- **2026-08-20 (T1) — A whole-EditParams edit becomes a DIFF command, not a per-field table.** The
  touch tray hands over a mutated copy of `EditParams` rather than naming the field that moved, so
  the desktop's "one control, one key" pattern did not fit. `editcmd::diff` serialises both sides
  with the engine's own writer and sends the lines that differ: no second key table to keep in step,
  and it can never accept fewer keys than a project file does. The one thing it cannot express is a
  mask edit — `mask=` APPENDS on parse — so masks keep their own commands and the mask rows carry
  their index. That asymmetry is worth remembering rather than rediscovering.
- **2026-08-20 (T1) — The phone shell's reads stay on `session()` for now.** Converting the 47 read
  sites to `AppModel` in the same commit as the write path would have made one unreviewable change,
  and the desktop is itself mid-migration (S4). They are marked as S4's list for this shell; the
  write path is the part that had to move first, because that is what makes the two shells one
  application.
- **2026-08-20 (T1) — The harness taps the recent card instead of calling an entry point.**
  `finishProject` only registers the project on Home (the shell starts there by design), so a shot
  that wants the editor has to get there the way a user does. Driving it through a private entry
  point would have made every editor shot a picture of a path nobody ships.

- **2026-08-20 (U2.4) — The font is embedded and registered by NAME, not resolved.** Two things
  were true of cosmo's type before this and neither is acceptable: it needed a directory next to
  the source tree (`COSMO_SOURCE_DIR/assets/fonts`), and it went through Fontconfig, which resolves
  a family name however the host is configured — including silently to the system sans when the
  file is missing. The face now goes from a C++ array to `FT_New_Memory_Face` to Cairo, so the only
  agreement left is between the names in `CMakeLists`' font list and the names in `Theme.h` (that
  pairing is R-FONT-3, and a mismatch is silent by the adapter's design — worth remembering).
- **2026-08-20 (U2.4) — `cmake -P` as the code generator, not xxd/objcopy/a host tool.** A
  generator target needs building before the thing that needs it and is awkward when
  cross-compiling; `xxd -i` and `objcopy` are not portable to MSYS2 the way `file(READ … HEX)` is.
  0.08 s per face, and the custom command re-runs only when a TTF changes.
- **2026-08-20 (U2.4) — DM Sans is left in the tree, unused.** Vendored, licensed assets with their
  OFL; removing them buys nothing and loses the ability to compare. `Theme.h` and the Android font
  table are the only places that ever named them, and both now name Roboto.

- **2026-08-20 (U2.2a) — A cross-fade is linear; the house EaseOutCubic is for things that move.**
  Everything else in cosmo eases out, and for position/size that is right. For a dissolve the
  quality metric is the LARGEST single-frame step, and an ease-out front-loads: 16 ms into a 120 ms
  EaseOutCubic is 35% across. Linear over 160 ms gives ten even ~10% steps, which is what a video
  cross-dissolve does and for the same reason. Written down because "why isn't this EaseOutCubic
  like everything else" is the obvious future question.
- **2026-08-20 (U2.2a) — Continuity beats latency for the photo, and the trade is stated.** Holding
  a render until the dissolve settles costs up to 160 ms of lag and drops intermediate renders
  during a fast drag. That is the right trade: a preview 160 ms behind still tracks the slider,
  while a photo that steps does not read as an edit at all. The rejected alternative — reversing the
  dissolve in place, which is what shipped — has no lag and blinks.

- **2026-08-20 (U2.2) — One animated property, not two, and no pixel copies.** The obvious
  cross-dissolve is "fade the new one in, fade the old one out", and it is wrong: two stacked
  layers at alpha `p` and `1−p` let the canvas through in the middle
  (`p·new + (1−p)²·old + p(1−p)·bg` — a 25% dip at halfway), so the photo visibly darkens
  mid-drag. Keeping the covered layer OPAQUE and animating only the top one gives exactly
  `a·top + (1−a)·bottom`. The consequence worth writing down: the roles then have to alternate
  (the dissolve runs 0→1, then 1→0) rather than the new render always landing on top, which in
  exchange removes the swap-at-completion and the pixel copy that a fixed "top is always newest"
  design needs. `Segment::raise()` is not an alternative — it moves a child to the END of the
  parent's list, which on this canvas is above the mask overlay and the pill.
- **2026-08-20 (U2.2) — An interrupted dissolve reverses; it does not restart.** Renders land
  faster than 120 ms while a slider is moving, so interruption is the common case, not the edge.
  The newest pixels go into the view the dissolve is *leaving* and `animateTo` retargets from the
  current eased value: opacity stays continuous and always converges on the newest frame. The
  residual step is the outgoing layer's alpha times ONE preview-to-preview delta — smaller than
  the whole-frame swap it replaces, and stated in R-VIEW-1a rather than hidden.
- **2026-08-20 (U2.2) — `cosmo_ui_tests` now pumps the service every frame.** The rig drew frames
  without calling `CosmoService::pump`, which is what moves a finished render out of the engine —
  so no preview could ever reach the stage and *every* photo assertion would have passed on an
  empty canvas. The first version of the dissolve test did exactly that. Same class as the harness
  bugs in P0: a test rig that diverges from the host tests a path nobody ships.

- **2026-08-19 (D-40) — "Code-verified on Windows" is not verified, and the harness now has a
  requirement.** `TestMain.h` shipped with its Windows half read rather than compiled; the first
  MSYS2 build after it landed failed twice — a link error for `_set_abort_behavior`, which msvcrt
  declares in `<stdlib.h>` and does not export (it is UCRT-only), and a compile error in
  `widgetTests.cpp`, whose `bool near(...)` collided with the empty `near` macro that
  `<windows.h>` still defines. Two lessons, both already visible in D-12's: a **platform claim
  needs the platform**, and a header included by more than one suite owes them a clean namespace —
  the second failure was in a file that had nothing to do with the change and named neither the
  macro nor the header. Also: the CRT lever was replaced by a `SIGABRT` handler rather than
  guarded with `#if defined(_UCRT)`, because one path that works on both CRTs beats two paths of
  which only one is ever exercised here. **R-TEST-1/2 are new** — the test harness had no
  requirement at all, which D-10 noticed and left, and §2's "no code without a requirement" had
  therefore never applied to the one thing every other verification rests on.
- **2026-08-17 (S5) — Announce before you mutate.** D-13: a subscriber runs synchronously inside
  `dispatch`, and a view is entitled to clear its own state when it hears "a project is opening" —
  the GTK host does exactly that. So an event describing what is ABOUT to happen must be emitted
  BEFORE the state it describes exists, or the handler destroys work the service already did. The
  bug shipped in S2 with a comment in the host that described the correct order while the service
  did the opposite; the first live socket run found it in seconds, and no headless test could,
  because with no subscriber doing real work both orderings look identical.
- **2026-08-17 (S2) — The service borrows the session; it does not own it yet.** App has owned
  `mSession` since long before any of this, and reparenting ownership in the same step as
  introducing the service would have meant rewriting App and the service at once with nothing
  working in between. So `CosmoService(EditSession&, ThreadBudget&)`, `App::session()` is the
  transitional accessor, and S4 moves ownership in. Every use of `svc->session()` is a line S4
  deletes — that is the strangler, stated as a countable number.
- **2026-08-17 (S2) — `refreshModel()` rebuilds the whole snapshot instead of patching it.** The
  tree is hundreds of nodes at most, and a snapshot that is always derived cannot drift from the
  session the way incrementally-maintained mirror state does. If this ever shows up in a profile,
  gate it on a dirty flag — do not hand-maintain the model.
- **2026-08-17 (S2) — `set` reuses the params text codec rather than a new key table.** Every
  `EditParams` field already round-trips through `deserializeParams` for `.cosmo`/`.cmp`/`.apf`, so
  `set exposure=1.2` accepts exactly the fields a project file does and can never fall behind them.
  A new adjustment becomes shell-reachable for free, which is the R-SVC-2 coverage rule made cheap.
- **2026-08-17 (S2) — An Event IS the log line.** `onServiceEvent` logs `formatEvent(e)` and
  nothing else. This is why P0.4/P0.5/P0.6 (log levels, categories, UI logging) stopped being
  separate work: emitting an event is logging it, and the same line feeds `--watch`, the journal
  and the control socket.
- **2026-08-17 (S1a) — The budget is a reservation, not a fixed ratio.** The engine gets the *whole*
  budget when nothing is loading and only the remainder while a load runs, rather than a permanent
  share. A fixed split would slow every render on an idle app to protect a load that is not
  happening. The floor on each side is 1: previews must not stop entirely (the editor is visible
  during a load, R-LOADPERF-3) and a load must always progress (R-CPU-1). Those two floors are the
  only case where the sum can exceed the total, and only on a machine whose entire budget is one
  thread.
- **2026-08-17 (S1a) — `OrderedParallelLoad` had never been restarted, and could not be.** `stop()`
  latches `mStop` and `start()` did not clear it, so a reused pipeline started with every worker
  returning immediately. It was invisible until now because each load built a fresh `LoadJob`;
  `ProjectLoader` keeps one pipeline and calls `stop()` before each `start()`. `start()` now
  re-initialises all run state. Found by the new test, not by inspection — which is the argument for
  S1 in one sentence.
- **2026-08-17 (S1a) — The measured peak is part of the feature, not instrumentation.** R-CPU-4 used
  to be satisfied by a log line that had never been executed. `ThreadBudget` counts producers, the
  load logs the high-water mark, and a test asserts it. Anything that claims a thread count from now
  on has to be able to report one.

- **2026-08-17 — U1.1's decisive claim was wrong, and the reason is worth keeping.** The decisions
  entry below says "the nested OpenMP team was the real cause … pinning `OMP_NUM_THREADS=1` is what
  actually changes the load, not the pool arithmetic." The pin does not work (D-12): libgomp reads the
  environment in a load-time constructor, so `main()` is too late. The entry is left standing rather
  than edited because *why* it was believed matters — it was reasoned from the source and never run.
  Anything asserted about thread counts from now on needs a measurement, not an argument.
- **2026-08-17 — Proposal: the core becomes a service and every front end becomes a view.** Written up
  in [`service-architecture-proposal.md`](service-architecture-proposal.md) at the user's request. The
  driver is that `startEntriesLoad`/`decodeEntry`/`pollLoad` live in `linux_main.cpp`, so the app's
  most important behaviour is unreachable without a window — and the CPU budget is read in three
  places by three owners with nobody owning the total. Recorded here so that if the proposal is
  declined, the reason it was raised is still on file.
- **2026-08-16 (U1.2) — One dialog, driven by two screens.** The launcher does not get its own
  settings surface: `mSettingsDialog` stays in the editor tree and Home sizes, advances and renders
  it, and routes gestures to it while it is open. A second instance would mean two copies of the
  callbacks and a second place for the persisted record to diverge. It is advanced on *every* Home
  frame, not only while open, because `isOpen()` is false during the closing fade and `show()`
  needs a current `nowMs`.
- **2026-08-16 (U1.2) — How to render cosmo headlessly before P0.2 exists.** Link the app sources
  minus `linux_main.cpp` plus `CairoTarget.cpp` against `artboard_core` + `cosmo_core` +
  `arstro_image`, and **pass the same defines the libraries were built with**
  (`-DARSTRO_ENABLE_THREADS -DARSTRO_GL_COMPUTE -DNDEBUG -O3`). Omitting them is an ODR violation
  that segfaults in `~EditSession` at exit, which reads as a teardown bug and is not one. Also note
  `App::pointer`'s kind codes: **0 = Down, 2 = Up**, anything else = Move — a `1` produces a Move
  and no click, with only a hover wash to show for it.

- **2026-08-16 (U1.1) — A CPU limit is enforced as a core count, not as a CPU percentage.** No
  portable per-process CPU-time cap exists across Linux/Windows/Android, and a sleep-based throttle
  would occupy the cores it is sparing. The user picks a percentage; `AppSettings::workersFor`
  converts it to a thread count once, and both pools read that. Recorded because "why isn't this a
  real % cap?" will be asked again.
- **2026-08-16 (U1.1) — The budget governs the engine's Auto threads too, not just the load.** The
  reported symptom was loading, but Auto meant every core for rendering as well, so a 50% budget
  that left renders at 16 threads would not be believed. An explicit thread choice still overrides.
- **2026-08-16 (U1.1) — The nested OpenMP team was the real cause.** MSYS2's LibRaw is built
  `-fopenmp`; 8 decode workers each opening a 16-thread team is ~128 threads, which saturated the
  machine *regardless* of pool size. On this 16-core host, 50% of the pool was already 8 (the cap),
  so pinning `OMP_NUM_THREADS=1` is what actually changes the load, not the pool arithmetic.
- **2026-08-16 — The harness is a requirement, not tooling.** Anything an agent cannot reach from a
  shell is treated as a product defect in cosmo, filed in `DEFECTS.md`, and given an `R-AGENT-*`
  requirement — not left as an informal wish. Rationale: this project is developed across machines and
  sessions by agents; unreachable behaviour is untestable behaviour.
- **2026-08-16 — Copy Genesis, do not invent.** `genesis-cc`, `genesis_shots`, `genesis_ui_tests` and
  `GENESIS_APP_NOMAIN` already solve exactly these problems in this repo. cosmo's harness mirrors them
  so the two apps stay learnable from each other.
- **2026-08-16 — Two requirement tiers stay.** `../REQUIREMENTS.md` (`R-*`, intent + history) and
  `requirements.md` (`DR-*`, as-built + `file:line`) are not merged; the split is deliberate and both
  files state it. Every change touches both.

---

## Verification notes

- **U2.2 is verified on the desktop shell only.** The op-stream assertions and the two PNG pairs
  are all `App` (desktop); `touch/PhoneApp`'s stage is unchanged and still snaps — U2.3, and
  R-VIEW-1d says so in the requirement too. The phone shell has no test or shot target at all,
  which is why the task is filed rather than done.
- **U2.1's orientation fix is verified on the RW2/RAF/JPEG set on this machine** (`/home/namdln/photo`,
  19 files, 18 of them `flip == 5`). No Canon/Nikon/Sony file was available, so the branch that
  SKIPS the flip — a maker that already stores an upright preview — is covered by reasoning and by
  the aspect guard, not by a file. If one turns up, run the probe in DR-SPLASH-5b against it.

- **S1a is verified on Linux, not on Windows.** The suite is green (24/24), the new peak test fails on
  the old arithmetic, and a real 18-RAF load was measured on a 24-core Linux host. The D-12 pin is
  *inert here* — the vendored libraw.a has no OpenMP, and the startup line says so — so the fix's
  effect on the reported symptom still needs one confirmation on MSYS2: look for
  `cpu: nested OpenMP teams pinned to 1 per decode worker` in the log. The mechanism itself is proven
  by `core/tests/fixtures/omp_pin.c`.
- Nothing in P0 has been built or verified yet; every claim about it comes from reading the source on
  2026-08-16.
- The host used for that reading is Windows 10 + MSYS2 MinGW64. `build/` (Debug, Ninja) and
  `build-mingw64/` (Release, Ninja) both contain a working `cosmo.exe`; `build/` is the one wired to
  `.vscode`. The distinction between the two trees is undocumented — resolve it in P0.11.
- `${XDG_CONFIG_HOME:-$HOME/.config}/cosmo_v2/` exists on this host but is **empty** — no log, no
  settings, no recents — so the log has never been confirmed to be written here in practice. Confirm
  before trusting it as a debug channel (see D-8).
