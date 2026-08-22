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

**► Two defects filed on Windows on 2026-08-22, and they are next, in this order.** Both were found
running the suites and the CLI on MSYS2 for the first time — the platform where the CPU budget was
originally reported broken and the one place its two known defects could ever have been observed.

1. **D-42 (S1, hang)** — `OrderedParallelLoad::stop()` signals `mCv` without holding `mMu`, so a
   worker that has evaluated the predicate but not yet blocked misses the wake and `join()` never
   returns. `cosmo_core_tests` hangs at test 15 of 36 on Windows and therefore **has never run
   `cpu_budget_scales_with_percent`, `one_budget_is_divided_not_duplicated` or
   `project_load_peak_never_exceeds_its_pool`** — the three guards written for D-11/D-12, on the
   only host where those defects were live. In the product the same call is `ProjectClose` and
   `ProjectLoader::start()`, both on the UI thread, so going Home mid-load can hang the app. Fix
   this first: nothing about the budget is verifiable on Windows until the suite finishes.
2. **D-41 (S3)** — D-12's pin was wired to `CosmoService::setWorkerInit` and nowhere else, so it
   covers ProjectLoader's pool and none of the six other decode paths (opening one photo, a
   `.cosmo`, the synchronous workspace load — all on the GTK main thread — plus `cosmo-cc info` and
   `params`). Measured: a bare decode takes 4.6 cores and 20 OS threads at `cpuPercent=25` and 4.5
   cores at 100% — identical, i.e. the setting does nothing to it, while `project` and `render` on
   the same box track the budget correctly (3.3 / 7.7 cores). Fixture:
   `apps/cosmo/core/tests/fixtures/cpu_budget_win.ps1`. R-CPU-2(c) amended: the pin belongs to every
   thread that decodes, and the threads it has bound are **counted**, so R-CPU-4's honesty clause is
   measured rather than asserted. Recommended fix is a host-side `PinnedDecoder` wrapper that is the
   only way a decoder gets constructed — filed in full in `DEFECTS.md`.

Both are recommendations, not code: `arstro.cosmo.core.debug` filed them and applied nothing. The
user runs `arstro.cosmo.core.implement` to land them.

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

Last updated: 2026-08-21 · U3.1 landed (the mixer no longer lights up noise) · T1 + T1a + T1b landed (the touch shell is on the service and renders with no device) · U2.1 + U2.2 + U2.2a + U2.4 landed (a cover is oriented like its photo; the photo
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
| U1 | CPU budget + Settings reachable from home | reopened again by **D-41** — D-12's pin covers the load pool only, and no other decode is inside the budget. D-11's half stands: `project` and `render` measure correct on Windows |
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
