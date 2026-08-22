# cosmo — Requirements

The single shared source of truth for cosmo behavior. **Every agent must read this
before implementing, and check any new/changed requirement here for conflict before writing
code** (mirrors the `implement_artboard` V-model, step 1). cosmo is an *application* built
on the Artboard library + `cosmo::EditSession`; the Artboard library keeps its own
`core/Artboard/docs/`. Requirements below are numbered `R-<area>-<n>`.

This file is the numbered-requirement / decision ledger (intent + status). For the **detailed,
source-derived as-built specification and design** — a full functional/non-functional spec,
architecture, per-class detailed design, design rationale, and a PlantUML model — see
[`docs/`](docs/): [`requirements.md`](docs/requirements.md), [`architecture.md`](docs/architecture.md),
[`detailed_design.md`](docs/detailed_design.md), [`design.md`](docs/design.md),
[`architecture.puml`](docs/architecture.puml).

## Global rules (apply to every requirement)

- **R-G-1 Everything animates, nothing snaps. NON-NEGOTIABLE.** No component may suddenly change
  size, appear, disappear, move, recolor, or reflow in a single frame. Every visible property change
  (position, size, show/hide via fade, color, radius, scroll/zoom offset, panel open/close,
  list insert/remove) changes through an animation primitive (`AnimatedProperty` / `Property`
  / `Spring`), never by direct assignment of the visible value. Show/hide is a fade or a
  size-to-zero tween, not a `visible` flip. Collapses to the final state only under
  `artboard::reducedMotion()`.
  (**AMENDED (R-SCALE-2a), 2026-08-19**, to close the loophole every violation of this requirement
  has so far walked through — three of them now, each written by someone who had just read it:
  **(a)** the list above is examples, not a boundary. It also covers **the coordinate system
  itself** — the UI scale, the logical size, the transform the whole tree is drawn through — and
  anything derived from any of them. **(b)** a property is not exempt because a *setting* changed it
  rather than a click, or because the change arrived over the control socket; the user sees a frame
  either way. **(c)** a value computed from something that eases must be recomputed **every frame
  from the eased value**, never once from the target. **(d)** compliance is established by comparing
  two frames half a tween apart, or by a test asserting the live value differs from the target
  mid-tween — never by reading the code, because every one of these three shipped code that read
  correctly. R-G-1a was the grid reflow, R-SCALE-2a is the screen scale.)
- **R-G-1a Reflow animates too, including a reflow the USER caused by resizing.** (**Added
  2026-08-18.**) R-G-1 already forbids a component changing size or position in a single frame,
  and a grid whose column count is derived from the width breaks it in the one case nobody
  scripts: dragging the window edge past the threshold where 4 cards across becomes 3 changes
  every card's size *and* position at once. So a card carries its LIVE geometry eased toward the
  layout's target, and the chrome and its thumbnail are both drawn from the same eased values —
  drawing one from the target and one from the eased value would separate them mid-reflow. The
  first placement of a card sets rather than animates, because a card appearing for the first time
  has nowhere to travel from.
- **R-G-2 Figma is the spec.** Screens that reference a Figma frame must match it (spacing,
  type ramp, colors, radii pulled from `Theme`), not approximate it.
- **R-G-2a One wordmark.** The `cosmo.` wordmark is drawn identically everywhere it appears
  (home sidebar, editor top bar, the open/return transition, the splash, the phone shell):
  letter-spacing `-0.03 * size` and the accent dot at the **measured** end of the word,
  `x + measureText("cosmo", size, family, spacing)`. No per-site spacing tweaks.
  (**AMENDED (R-FONT-2), 2026-08-20:** it said `estimateTextWidth`, which is `len * px * 0.6` —
  font-independent by construction, so it is wrong for any particular font, and the dot detached
  from the `o` the moment the typeface changed. Two of the sites already measured; the requirement
  now says what the accurate ones do.)

## R-EDITSTACK — Right-column edit stack

- **R-EDITSTACK-1 Tabs.** Five merged tabs: **Basic/Detail** (all tone/colour/presence/effects +
  sharpening/noise/lens sections in one scrollable `ParamPanel`), **Mask**, **Mixer/Curve** (the HSL
  mixer above the tone curve in one scrollable `StackPanel`), **Grade**, **Xform**. Tab widths size
  to their label (+ even padding to fill the strip) via `EditStackTabs::tabW/tabX`, so wider merged
  labels are never clipped. The Mask overlay bridge keys off `RightColumn::maskTabActive()`, not a
  hard-coded index.
- **R-G-3 Everything interactive hovers.** Every button and every clickable region shows an
  animated hover treatment under the pointer — never a hard flip. Child-`Segment` controls
  (`Button`/`PillButton`/`IconButton`/`ComboBox`/`ToggleSwitch`/`Slider`) key off the framework's
  animated `hoverAmount()` via `artboard::hoverBox()`; self-drawn multi-region widgets (menu items,
  breadcrumb crumbs, filmstrip cells, tabs, tree rows, dialog rows/buttons, history nodes, home
  cards/actions, action-bar buttons) track a hovered-region id from `Gesture::Type::Move` and drive
  it through the shared `HoverFade` helper (`widgets/HoverFade.h`), which gives EACH sub-region its
  own eased 0..1 amount: the hovered one eases toward 1 and every other toward 0, so moving between
  items CROSS-FADES (the old item fades out while the new fades in) instead of the highlight jumping.
  The treatment is a `palette::hoverWash()` (or an accent-tinted border/label lift), eased (R-G-1) and
  collapsing under `reducedMotion()`. `Theme::hoverWash()`/`primaryAlpha()` are the shared tokens so
  hover reads identically app-wide (consistency lock). Known framework limit: true cross-fades
  between sibling children (e.g. Mixer channel editor, per-mask control blocks) need per-subtree
  opacity, which `IRenderTarget`/`Segment` do not expose — those switches are left instant (or given
  an overlay-scrim reveal where practical) pending an Artboard change; scroll offsets, panel/dialog
  open-close, and the edit-stack page swap all animate.

## R-LOADING — Animated open-project transition & loading screen — ✅ IMPLEMENTED

Opening a project decodes its images (~1 s for a multi-photo project). That decode runs on a
**background thread** (`linux_main.cpp` `decodeWorker`) so it never stalls the UI; the GTK main
thread polls finished results (`pollLoad`) and applies them to the session in entry order, so the
animated loading screen renders at full frame rate and the progress bar reflects real progress.
Applies to the two "open a project" paths — a recent-project card and Open Project… —
(`onOpenRecentRequested`, `openProjectDialog`). Orchestrated by `App` as a third screen state
(`Screen::Loading`) drawn in `App::renderTransition`; the star-sky backdrop is `widgets/Starfield.h`.
Honors `reducedMotion()` (the eases collapse; the load still streams in).

The transition is split into **three linked parts** so every movement flows into the next with no
sudden jump, and the heavy work is isolated to the middle part:

- **R-LOADING-0 Part 1 — transition (animation, decode running behind it).** The intro plays while
  the background decode streams in behind it (see R-LOADING-1's amendment): the home `cosmo.` wordmark flies to the editor top-bar wordmark slot (46 px → 13 px, home
  position → top-left, `App::drawWordmark`), and the clicked project's **WHOLE card — cropped
  thumbnail + name + photo count + total size + last-edit date — lifts off the grid and translates
  to screen centre AT ITS CARD SIZE. It does NOT expand/zoom into a large hero image**; the entire
  recent item simply glides to the middle and fades in over a cleared star-sky backdrop, and the
  small stars fade in. The decode does NOT start yet. The fly uses the clicked card's **full** screen
  rect + its info (`HomeScreen::lastOpenCardRect` / `lastOpenCardInfo`); the centred rest rect is the
  **same size** (a pure move, no scale). The card itself is drawn by the shared
  `widgets/ProjectCard.h` `drawProjectCardChrome` (+ the cover blitted over its thumbnail band), the
  SAME renderer the home grid uses, so the flying item is pixel-identical to the grid item.
  Open-dialog opens (no source card) fade in at centre at a default card size (name only).
  (**AMENDED (R-LOADPERF), recorded 2026-08-18:** the sentence "The decode does NOT start yet"
  above is struck — it contradicted this requirement's own R-LOADING-1 amendment two bullets
  below, which has been the truth since R-LOADPERF. The decode runs behind the intro. D-1.)
- **R-LOADING-1 Part 2 — loading (status text + progress bar under the card).**
  **AMENDED (R-LOADPERF):** the decode now starts with the transition, not after the intro.
  The `onLoadingReady` hook that used to carry this is **gone** (D-1): under R-SVC-1 the host
  dispatches `Command::ProjectOpen`, and the `ProjectOpening` event it emits is what calls
  `beginOpenTransition` — so the pool is already running as the first intro frame draws, and the
  ordering is enforced by the service rather than by a hook the view had to remember to fire. The deferral existed so a full-resolution decode on the
  UI thread could not hitch the intro — but decoding moved to a worker pool and the per-image apply
  moved off the UI thread with it (R-LOADPERF-1/2), so there is nothing left to hitch on, and
  deferring only bought 460 ms of a dead progress bar reading "Preparing…". Overlapping them means
  the bar is already filling and the status line already naming real photos by the time the intro
  lands. Everything is already placed; a **small accent progress bar the
  SAME WIDTH as the card, positioned directly UNDER it** (not a wide bar at the bottom of the screen)
  fills 0→1 with the real decode fraction (`setLoadProgress`), eased. **Directly above that bar a
  status line — left-aligned to the card's (and bar's) left edge — shows what is currently being
  loaded** (`App::setLoadStatus`, e.g. `Loading  <photo name>` — fed per item by the host as each
  entry is applied; `Preparing…` before the first). The
  status line + bar fade in together at the part-1→part-2 hand-off (they never pop). A short minimum
  keeps the bar from merely flashing on a fast load.
- **R-LOADING-2 Loading-screen look.** A **deep near-black** star-sky backdrop (`kLoadingBg`
  `#0A0A0A`, darker than the home/editor background so the loading screen reads as a dim, focused
  moment; the editor gently brightens in on reveal rather than matching exactly) of **small**
  twinkling white particles
  (`widgets/Starfield.h`). The project's whole card sits centred **at card size** with its **cover
  thumbnail** (already decoded for the home card and cached by the host, so it needs no I/O in part
  1) **cropped (Cover fit)** into the thumbnail band and **faded in** when available (never pops);
  below the card, top-to-bottom: the load-status line and the card-width progress bar (one centred
  stack — the name/meta live inside the card).
- **R-LOADING-3 Part 3 — reveal (loading → editor).** When the load completes (and the intro has
  played), the editor components **materialise on top of the matching dark backdrop** (fade in),
  while the loading elements — the centred card, stars, status line and progress bar
  — **fade out in place** (no move/expand). The wordmark **cross-fades**: the loading copy fades out AS the editor's
  top-bar wordmark fades in, in the same slot at the same time, so it never doubles and never
  re-fades from nothing (`drawWordmark` with an alpha; the reveal drives the editor fade itself so
  `renderEditor` adds no scrim).
- **R-LOADING-4 Non-interactive.** Input (pointer/keys/wheel) is swallowed during the transition
  (open AND return) so nothing behind the loading screen is touched.
- **R-LOADING-5/6 Reverse (project → home), also in 3 parts.** Returning to the launcher (`showHome`
  from the editor → `App::renderReturn`) mirrors the open, through the same dark star-sky:
  - **ReturnEnter** — the editor fades OUT to the star-sky (a dark, matching-bg + stars overlay
    fades IN over the still-rendered editor). The loading screen appears IMMEDIATELY on click — the
    launcher prep is deferred (below), so there is no freeze before the fade.
  - **ReturnLoad** — a brief full star-sky beat, during which `refreshHome()` runs (rebuild recents +
    thumbnails) BEHIND the shown loading screen instead of as a click-time freeze. Thumbnails are
    reused from the host cache (no re-decode on return), so this is cheap.
  - **ReturnExit** — the home screen fades IN from the star-sky (home rendered, the dark+stars
    overlay fades OUT on top).
  Across all three the wordmark flies from the top-bar slot back to its big home position
  (`mReturn`, `App::drawWordmark`), landing as the exit begins; the sidebar's own wordmark is hidden
  (`HomeScreen::setWordmarkHidden`) until it lands, so it reads as one element. Collapses instantly
  under `reducedMotion()`.

## R-LOADPERF — Opening a project is parallel, off-thread and progressive

Opening a catalog of large frames was bounded by three serial costs, all avoidable
(measured on a 24 MP JPEG: ~109 ms to decode, ~72 ms to apply):

- **R-LOADPERF-1 Decode in parallel.** The loader decodes on a **pool** of worker threads
  (**AMENDED (R-CPU-2):** the pool was one worker per core, clamped to 2..8, which saturated the
  machine for the whole load; it is now sized by the CPU budget, whose floor is 1 — an explicit
  user budget outranks a hardcoded parallelism floor) instead of one, since decoding is
  CPU-bound and independent per image. Workers claim entries with an atomic counter, so they
  finish out of order, but each result is stored **at its entry index** and the UI thread still
  applies them strictly in order — the `.cosmoproj` format identifies a node's parent by entry
  index, so out-of-order application would reparent the tree.
- **R-LOADPERF-1a Bounded in flight.** A decoded 24 MP frame is ~100 MB and N workers outrun the
  single applier, so an unbounded pool would decode a whole catalog into RAM. A worker waits before
  claiming work until the batch is within a small window of the apply cursor **and** total decoded-
  but-unapplied bytes are under a cap. The worker holding the entry the applier needs next is
  always exempt from both limits, so the pipeline can never deadlock against its own cap.
- **R-LOADPERF-2 Apply off the UI thread.** The two costs inside the old apply — the ~100 MB pixel
  copy into the engine and the filmstrip thumbnail's full-image downsample — both move to the
  decode worker: the thumbnail is built there and handed over ready-made, and the decoded buffer is
  **moved** into the engine (`RenderService::addImage(std::vector&&)`) instead of copied. What is
  left on the UI thread per image is bookkeeping, so the loading screen keeps animating instead of
  hitching once per photo.
- **R-LOADPERF-3 Reveal — complete, or capped.** **AMENDED (R-LOADUX):** revealing on the *first*
  image made the loading screen meaningless — it flashed "Preparing…", showed a bar at zero and
  jumped to the editor, so nothing ever reported how long the wait would be. The editor is now
  revealed when the load **completes**, so the progress bar actually fills and means something —
  **or**, for a catalog too big to wait for, once `kMaxLoadingMs` has elapsed AND at least one image
  is usable, after which the rest stream in behind the editor with the rack's spinner cells and its
  own progress bar (R-LOADUX-2/3). The minimum-visible time is measured from when the DECODE
  started, not from the end of the intro: with the two overlapped the intro already gave the bar
  time on screen, so a small project reveals the moment the intro lands instead of sitting through
  a further hold.
  `finishWorkspaceLoad` consequently must **not** steal the selection — it auto-selects the first
  image only when nothing is selected yet, so a photographer who started working during the stream
  is not yanked back to image 1 when the last one lands.

## R-TOUCH — The touch shell: one core, two UIs — 🚧 IN PROGRESS

cosmo has two front ends that must stay one application: the desktop shell (`App`, mouse and
keyboard, three zones at once) and the **touch shell** (`touch/PhoneApp`, `arstro::cosmo_touch`) for a
small touch screen — Android first. The desktop went through R-SVC and now binds to `CosmoService`
through `Command` in and `AppModel`/`Event` out; the touch shell predates that and does not, so it is
a second application that happens to link the same library. This section is what makes it one.

The visual language is **identical** to the desktop's (R-G-2, `Theme.h` tokens) — the touch shell may
diverge in *metrics* (hit bands, type steps, its own brighter muted grey for arm's-length reading) and
never in palette, radii, accent, motion vocabulary or wordmark. `docs/touch-ui-brief.md` is the
component-level spec; where it disagrees with this section, this section wins and the brief is amended.

- **R-TOUCH-1 One core, one view-model, two views.** The touch shell owns **no session and no
  engine**: it is constructed over a `CosmoService`, every change it makes is a `Command` it
  dispatches, and everything it draws comes from `AppModel`, the service's frame channel
  (`takeFrame`) or its own presentation state (animation, detents, scroll — R-SVC-4). It must
  therefore be drivable by the same scripts, the same control socket and the same `cosmo-cc` as the
  desktop, and a project opened on one shell must dump byte-identical state on the other (R-SVC-9's
  check, extended to the second view).

  **In particular — and this needed saying, because its absence shipped (D-39):** the touch shell
  derives its **screen**, the **open project** (name, image count, edit target) and its **recents
  list** from `AppModel`, and keeps no copy of any of them. A shell built while a project is already
  open — which is exactly what the desktop's touch-mode switch does — shows *that* project, and a
  shell must never reset or rebuild the workspace on its own initiative, because the other view is
  looking at it. Asserted, not assumed: `cosmo_touch_shots --assert` opens a project with no touch
  shell in existence, then builds one and requires it to land on the editor. The UI→`Command` mapping is **shared code**, not two copies:
  one place turns "this control moved" into a command, and both shells call it. Two mappings drift
  the first time a parameter is added, and the drift is silent.
- **R-TOUCH-2 No component overlaps another. NEW RULE.** Every element of the touch shell has its
  own space: opening the control tray **shrinks the photo's box** rather than covering it, the tool
  bar and top bar are never drawn over content, and no panel is partly hidden behind another. The
  only things allowed above content are **deliberate modal overlays** — action sheets, dialogs, the
  preset drawer, the fullscreen curve editor — each with a scrim, drawn in the overlay pass, and
  dismissible. This is stricter than the desktop's R-G-3 (siblings snap, overlays are the exception)
  and stricter than the brief, which had the tray *rise over* the photo: on a phone the photo is the
  work, and a sheet across it hides the thing being edited. Asserted, not eyeballed: the harness
  walks the tree and fails on any two non-overlay siblings whose world rects intersect.
- **R-TOUCH-3 Both orientations, and the change animates. NEW RULE.** The shell works in portrait
  **and** landscape, and rotation is a reflow like any other (R-G-1: it eases, it does not snap).
  - **Portrait** (tall): one column — top bar, photo, breadcrumb + filmstrip, tool bar; a tab raises
    the tray *below* the photo, which shrinks to fit (R-TOUCH-2).
  - **Landscape** (wide): **two panes** — photo on the left, the active tray as a **fixed panel on the
    right** (~40% of the width, its own column), so nothing is ever over the photo. This is the
    desktop's shape at phone size, which is also why it needs no new interaction model.
  - Either orientation, every control stays reachable: a panel taller than its pane scrolls (R6) and
    nothing is clipped away.
- **R-TOUCH-4 Touch-sized, and curves especially. NEW RULE.** Every interactive element has a
  ≥ **44 dp** hit band (the drawn control may be smaller), slider rows ≥ **48 dp**, adjacent targets
  ≥ **8 dp** apart. Curve and mixer nodes are the hardest case on a small screen and get their own
  treatment: a **fullscreen curve editor** (the plot takes the whole screen, so nodes are far apart),
  a ≥ **24 dp** grab radius, and while dragging a **loupe** offset above the finger showing the node
  under it — because the fingertip covers exactly the thing being positioned. Tapping selects the
  nearest node rather than requiring a hit, so a miss adjusts something instead of nothing.
- **R-TOUCH-6 The desktop build can RUN the touch shell, and it is a setting.** A touch screen is
  not a different product: a convertible folded into a tablet, a touchscreen panel, or a desktop
  being driven by hand wants the touch layout from the same binary. So `AppSettings::touchUi`
  (persisted like every other preference, R-SETTINGS-4; settable as `settings set touchUi=1`) picks
  which shell the host draws, and:
  - **The service stores and forwards it, and never acts on it** — which shell exists is the view's
    business (R-SVC-3). It is the second such key, alongside `uiScale`, and it is in the grammar and
    the model for the same two reasons: a script can flip it, and whoever owns a view can read the
    value it must honour.
  - **Both shells bind to the same service**, so switching mode keeps the open project, the
    selection, the parameters and the undo history — nothing reloads and nothing is lost. That is
    the practical payoff of R-TOUCH-1, and the check for it is that a `state print --stable` taken
    either side of the switch is identical.
  - **The switch is a visible change, so it animates** (R-G-1): the outgoing shell fades out as the
    incoming one fades in, both drawn through the render target's layer alpha. It does not require
    a restart, and it does not resize the window.
  - **Interim, until R-TOUCH-3 lands (T2):** the touch shell has no landscape layout yet, so in a
    window wider than it is tall the host draws it in a **centred portrait column** at the phone's
    design width rather than in the stacked-controls layout D-38 describes. That is a deliberate
    letterbox with a date on it, not the intended end state — when the two-pane landscape layout
    exists, the shell fills the window.
- **R-TOUCH-5 The touch shell is renderable and assertable with no device.** `PhoneApp` is Artboard
  `Segment`s over `CairoTarget`, so it builds on the desktop host: `cosmo_touch_shots` renders every
  screen and state to PNG at phone sizes in both orientations, and `cosmo_touch_tests` asserts the
  R-TOUCH-2/3/4 rules over the assembled shell. Without this the touch UI can only be checked by
  building an APK and looking at a phone, which is why M4–M7 of `docs/android.md` had no evidence
  behind them.

## R-FONT — The typeface travels inside the binary — ✅ IMPLEMENTED

An app's own type is not something that may differ between machines, and cosmo's did: the faces were
registered with Fontconfig from a path baked in at build time, so the text depended on a directory
existing next to the source tree, on Fontconfig resolving the family the same way on every host, and
— when either failed — silently on whatever the system's default sans happened to be. A shot then
measures the wrong widths, a panel overflows on one machine and not another, and the app does not
look like itself.

- **R-FONT-1 The faces are compiled into the binary.** The vendored TTFs are generated into a C++
  array at build time and handed straight to the render adapter (`CairoTarget::registerFontMemory`,
  Artboard FR-22a). No font file beside the executable, no Fontconfig in the text path, no
  system-installed family: the binary draws its own glyphs on any machine, which is the only way the
  UI is identical across platforms. The generator is a `cmake -P` script (no `xxd`, no `objcopy`, no
  host codegen target to build first), so Linux and MSYS2 run the same line, and it re-runs only
  when a TTF changes.
- **R-FONT-2 Roboto is the UI face; JetBrains Mono stays the numeric one.** `font::sans` /
  `sansMedium` / `sansSemiBold` are Roboto Regular / Medium / SemiBold — one family, three real
  static weights, because a weight is selected by its own family name at the text-stack level
  (Artboard FR-22) rather than by a number. `font::mono` / `monoMedium` stay JetBrains Mono for
  numerics and filenames, embedded on the same terms — a mono face resolved from the system would
  reintroduce exactly the inconsistency this requirement removes.
- **R-FONT-3 One list, in one place.** The names the faces are registered under and the names
  `Theme.h`'s `font::` accessors ask for are the same list, and the registration is the only code
  that pairs a name with bytes. A name in `Theme.h` with no registered face falls through to the
  host's default sans — silently, since that is the adapter's documented fallback — so the build's
  font list is treated as part of the theme, not as build plumbing.
- **R-FONT-4 Every shell registers them, including the harnesses.** The app, `cosmo_shots` and
  `cosmo_ui_tests` share one registration call, so a shot is in the app's own type. A shot in the
  wrong typeface measures the wrong widths and reports overflow bugs that do not exist, which makes
  it worse than no shot. The Android shell asks for the same families from its APK assets (it ships
  them anyway); embedding there is a follow-up, not a difference in the design.

## R-THUMB — A thumbnail is the same photo, only smaller — ✅ IMPLEMENTED

Every reduced-size copy of a photo — the home screen's project-card cover (R-HOME-6), the
open-project and splash covers (R-LOADING, R-SPLASH-3), the filmstrip cell (R-LOADPERF-2) — must
read as *that photo*. A cover that is a cheaper copy of the pixels is a performance decision
(DR-SPLASH-5a) and must stay invisible to the photographer; the moment it differs in orientation
it stops being a thumbnail of the photo and becomes a different picture beside it.

- **R-THUMB-1 A thumbnail is oriented like its photo.** A thumbnail is shown in the same
  orientation the full decode produces, so a portrait shot's cover is portrait. This is not
  automatic: LibRaw applies the RAW's orientation (`sizes.flip`) inside `dcraw_process`, but the
  camera's **embedded preview** — which is what a cover reads, because it costs 6.6 ms against
  8072 ms — is handed back exactly as the camera stored it, in sensor orientation. So the same
  flip is applied to the preview. A maker that already stores an upright preview must not be
  rotated twice: a quarter turn swaps the aspect, so the preview's own aspect against the sensor
  frame's says which of the two frames it is already in, and only a preview still in the sensor
  frame is turned. The full-decode fallback (a file with no preview) is already oriented and is
  left alone.
- **R-THUMB-2 A cell crops, it never distorts and never rotates to fit.** A thumbnail keeps its
  aspect and is cropped by the cell that holds it (`ImageView::Fit::Cover` — filmstrip cells, home
  covers), so a portrait photo in a 16:9 band shows its middle rather than being squeezed or laid
  on its side. Rotating the image to fit its box is never the answer: the box crops.
- **R-THUMB-3 Same photo, same pixels, whatever produced them.** The cheap path and the full path
  must agree. A change to one is verified against the other on a real RAW — the preview's
  orientation and aspect compared with the full decode's, not assumed from the flag.

## R-CPU — A CPU budget, so the machine stays usable while cosmo works — ⚠️ REOPENED (D-41; was D-11, D-12)

Opening a catalog saturated the machine. The decode pool took one worker per core (R-LOADPERF-1) and
the engine's Auto thread count is also every core, so importing photos made the rest of the computer
unusable for as long as the load ran. Decoding fast is worth nothing if the photographer cannot do
anything else meanwhile — a photo app is something you run *alongside* your work, not instead of it.

- **R-CPU-1 A share of the machine, not all of it.** cosmo's background CPU work runs on a
  **budget**: a percentage of the machine's logical cores, **50% by default**. Percent is the unit
  the user is offered because it is the honest answer to "how much of my computer may this take";
  it is **enforced as a worker count**, because no portable per-process CPU-time cap exists across
  Linux / Windows / Android, and throttling by sleeping would occupy the very cores it is trying to
  spare. The count is `clamp(round(cores × percent / 100), 1, cap)` — **never zero**, so even the
  smallest budget on the smallest machine still makes progress.
- **R-CPU-2 What the budget governs.** Three things:
  (a) the **decode pool** that opens a project, capped as before at 8 workers — this **amends
  R-LOADPERF-1**, whose floor of 2 becomes 1; (b) the **engine's worker count while CPU threads
  is Auto** — an explicit CPU-threads choice (2 / 4 / 8) still wins for the engine, because that
  setting is a deliberate override and a budget that silently contradicted it would make both
  controls untrustworthy; and (c) **nested parallelism inside a decoder**. LibRaw is built with
  OpenMP, so each decode worker opened a team sized to the whole machine — eight workers on a
  16-core box meant up to 128 threads, which is why a RAW import took the entire computer *no
  matter how the pool was sized*. cosmo already parallelises across images, so a second layer
  inside one image is pure oversubscription: `OMP_NUM_THREADS` is pinned to 1 at startup (an
  explicit user value still wins) and one decode worker then means one core.
  (**AMENDED (R-SVC-10), 2026-08-17:** (a) and (b) are **one budget divided**, not two budgets each
  taking the whole percentage. As written, both consumers converted the same percentage
  independently and they run concurrently — R-LOADPERF-3 streams decoded images into a live editor
  — so a load peaked at roughly twice what the user chose: 13 of 24 cores at 25%, and 17 of 16 on a
  16-core box at the default 50%. D-11. A single `ThreadBudget` now hands out both counts from one
  total. — And (c) **cannot be done with an environment variable set from `main()`**: libgomp parses
  the environment in a load-time constructor, so the pin was never read by anyone and the nested
  team stayed machine-sized wherever LibRaw is built `-fopenmp`. D-12, proven by
  `core/tests/fixtures/omp_env_order.c`. The intent of (c) stands; the mechanism is replaced by one
  that works — see R-SVC-10.)
  (**AMENDED (c), 2026-08-22:** the pin belongs to **every thread that decodes**, not to the decode
  pool. Naming ProjectLoader's per-worker hook as *the* mechanism read as though the pool were the
  only place a decode happens, and it is not: opening one photo, opening a `.cosmo` session and the
  synchronous workspace load all decode on the GTK **main thread**, and `cosmo-cc info` decodes on
  its own — six call sites that constructed a bare decoder on an unpinned thread and were therefore
  outside the budget entirely, measurably insensitive to the setting (D-41). A decode is a decode:
  which internal path reached it is not something the user chose. So the pin is applied by whatever
  the host uses to decode, once per thread, and **the number of threads it has bound is counted and
  reported** next to `ompPinStatus()` — R-CPU-4 asks for honesty that is measured, and "the pin ran
  on every decoding thread" is a count that can be asserted on every platform, including the ones
  whose LibRaw has no OpenMP to pin.)
- **R-CPU-3 Changeable, and persisted.** The budget is a chip row in the Settings surface
  (R-SETTINGS-1) offering 25% / 50% / 75% / 100%, and it round-trips with the other preferences
  (R-SETTINGS-4). A change takes effect on the **next** load and on the next render: a pool is sized
  when it starts, and re-sizing one mid-load would mean tearing down workers that are holding
  decoded frames.
- **R-CPU-4 Honest about what it cannot cap.** The budget bounds the threads cosmo starts plus the
  one nested pool it can reach (R-CPU-2c). Threads a driver or library starts out of cosmo's sight —
  a GPU driver's helpers, a codec with its own pool and no env knob — remain outside it, so the
  setting means "of the work cosmo schedules". The log records the worker count actually used at
  each load, so the nominal budget and the real one can be told apart:
  `load: 12 entries on 8 decode workers (cpu budget 50% of 16 cores)`.
  (**AMENDED (R-SVC-10), 2026-08-17:** honesty has to be *measured*, not asserted. This requirement
  was satisfied by a log line that no one had ever seen — reaching it needed a project opened by
  clicking (D-6), so both of the budget's real defects shipped unnoticed. The load now also logs the
  **observed peak** of concurrently scheduled cosmo threads next to the allotment, and a test
  asserts the peak never exceeds the total. A number cosmo cannot measure does not belong in this
  requirement.)
- **R-CPU-5 The cap can bind before the budget does.** On a machine with more than 16 logical cores,
  50% exceeds the decode pool's cap of 8 and the cap is what applies — the pool does not grow past
  8 whatever the budget says, because beyond that point workers contend for memory bandwidth rather
  than adding throughput (R-LOADPERF-1a). Lowering the budget below the cap still shrinks the pool.
  This is why the log states both numbers.

## R-LOG — File logging & crash diagnostics — ✅ IMPLEMENTED

- **R-LOG-1** The app writes a timestamped, levelled log to `~/.config/cosmo/cosmo.log`
  (`ProjectStore::configDir()`), appended across sessions with a per-session header. Every
  `g_print`/`g_printerr` diagnostic is routed through it (GLib print handlers), so the console
  output and the file stay in sync. Facility: `apps/cosmo/Log.{h,cpp}` (`log::init`, `LOGI/LOGW/LOGE`).
  Platform-note: logging does OS I/O, so it lives in the app layer, never in the platform-free
  Artboard core.
- **R-LOG-2** A fatal-signal handler (SIGSEGV/SIGABRT/SIGBUS/SIGFPE/SIGILL) appends a backtrace to
  the log (async-signal-safe `write`/`backtrace_symbols_fd`) and re-raises the default handler, so a
  field crash leaves a diagnosable trail. Installed at startup (`log::installCrashHandler`).

## R-BUGFIX — Reset-then-open segfault — ✅ FIXED

`EditSession::resetWorkspace()` cleared the per-slot vectors but `RenderService` kept incrementing
its monotonic slot-id counter, so the first image opened after a reset (New Project / Open Project /
Import Catalog / Reset Workspace, all of which reset then open) got a slot id past the end of the
freshly-emptied vectors → out-of-bounds `mSlotPaths[mCurrentSlot]` in `currentSourcePath()`
(and siblings) → SIGSEGV. Fix: `RenderService::reset()` (+ `EditEngine::clearImages()`) drops all
engine slots and restarts id assignment from 0 on reset, restoring the "slot id == index, grow-only"
invariant; the slot accessors also got upper-bound guards as defence in depth. Regression-guarded by
the reset→open reproduction.

## R-BUGFIX-2 — Stale editor during the open reveal — ✅ FIXED

Opening a project after another was already open showed the *previous* project's editor (photo,
filmstrip thumbnails, breadcrumb) underneath/through the reveal instead of the new one fading in
from scratch. Cause: `App::resetWorkspace()` reset the `EditSession` but not the editor's *visible*
state — the photo `ImageView`, the split/before `ImageView`, the cached `mLastAfterFrame`, the
`Filmstrip` thumbnails, and the `Breadcrumb` path all retained the old project's content until the
new render landed. Fix: `App::resetWorkspace()` now also `clearImage()`s both photo views, resets
`mLastAfterFrame`, `Filmstrip::clearThumbs()`, and clears the breadcrumb path, so the editor is blank
the instant the workspace is reset (which happens right after `beginOpenTransition`, before the new
decode). `Filmstrip` reuses its `ImageView` child pool (`mThumbCount`) across projects — `Segment`
has no `removeChild`, so `clearThumbs()` restarts the active count at 0 and `addThumb()` refills the
pool in lockstep with the engine's reset slot ids (a new project's `thumbSlot` must not index an old
thumbnail). Headless-verified: red project → reset → editor blank → green project opens fresh, no
stale photo, no crash.

## R-MIXER — A per-hue tool only touches pixels that have a hue — ✅ IMPLEMENTED

Reported by the user: using the Mixer's **Lum** curve made flat grey areas break into speckle —
"random noise particle got lit up". Not a matter of taste, and not fixable by moving a control: hue
is *derived* by dividing channel differences by chroma, so on a near-neutral pixel it is decided by
the last bit of sensor noise. Neighbouring pixels in a grey read as red, green and blue at random,
and the Lum channel — the one that adds rather than multiplies — then gave each of them a different
full-strength lift. Measured at **624×** the luminance spread of the flattest patch of a real
X-Trans frame.

- **R-MIXER-1 Every mixer channel is scaled by a chroma weight.** `smoothstep(0.010, 0.040, chroma)`
  in linear-light units: exactly **zero** where a pixel is indistinguishable from neutral, full where
  there is real colour, a ramp between. All three channels — Lum is where it showed, but boosting Sat
  on grey amplifies colour noise and bending the hue of a noise pixel is equally meaningless. The
  floor is set by measurement, not by taste: ±1/255 of per-channel noise reaches ~0.008 of chroma, and
  a weight that merely *attenuates* still fans out under a steep curve.
- **R-MIXER-2 The weight is on chroma, never on saturation.** HSL saturation is normalised by
  lightness, so it reports a large value for a tiny chroma in the shadows — exactly where noise
  lives, so gating on it would let dark speckle through. A genuinely saturated shadow keeps its full
  weight, because chroma is recovered exactly as `s · (1 − |2l − 1|)`.
- **R-MIXER-3 The trade is stated, not hidden.** A genuinely desaturated region now moves less under
  the lum curve. That is intended — darkening a grey sky is exposure, tone regions or a mask, not a
  per-hue curve — but a project that leaned on the old behaviour renders differently, which is why
  this is a requirement rather than a tweak.
- **R-MIXER-4 A GPU port must carry the weight.** The compute backends currently **decline** any
  non-identity mixer, so CPU is the only implementation and cannot diverge. When the mixer moves to
  GLES (ledger T6) the weight goes with it in the same change, and the conformance test covers a
  neutral patch — otherwise GPU and CPU would differ on precisely the pixels this exists for.

## R-BUGFIX-3 — Mixer curve saved as samples, not bezier points — ✅ FIXED

The colour-mixer (Mixer/Curve tab) is edited as a bezier curve with smooth, Alt-dragged tangent
handles, but the handles were discarded on save: `HueCurveEditor` sampled the curve to a dense
piecewise-linear polyline (~14 points/segment) and stored *that* in `EditParams.mixer[]`. Reopening a
project rebuilt those samples as bare CORNER points — the smooth curve came back as a coarse linear
approximation with no editable handles (the "opens as smaller/linear points" symptom).

Fix: the persisted representation is now the bezier CONTROL points. `EditParams.mixer[]` is
`std::array<std::vector<CurvePoint>,3>` where `CurvePoint{x,y,ix,iy,ox,oy,smooth}` (new
`core/ImageProcessing/src/base/CurvePoint.h`, which also owns the single shared `curve::sample()` so the
editor's on-screen curve and the engine's LUT can never diverge). `HueCurveEditor` now emits/restores
control points (handles + smooth flag intact); `ColorMixer::setCurve` flattens the control points with
`curve::sample()` when building its LUT, so render is unchanged. Serialization writes `x,y` for a
corner point (identical to the old format → pre-bezier files still load, as corners) or
`x,y,ix,iy,ox,oy` for a smooth one (field count flags smooth); the `.apf` preset path matches. The
legacy `apps/cosmo/` v1 editor keeps its sampled-point model via a thin convert-at-the-seam shim in
`CosmoApp`. Regression-tested (`EditParamsIO_mixer_bezier_roundtrip`) + headless-verified: a smooth
mixer point round-trips as 3 control points with handles (vs 43 handle-less corners on the old path)
and the engine renders byte-identically before/after the round-trip.

## R-PERSIST — Save the edit history + save shortcuts — ✅ IMPLEMENTED

- **R-PERSIST-1 History in the project.** A saved project (`.cosmoproj` workspace) persists each
  image's full branching edit history, not just its live params. `saveWorkspaceAs` writes, after an
  image's current-params block, a history header (`hcurrent`/`hmax`/`hcoalesce`) then one `#hnode`
  block per node — `hparent`/`hseq`/`hlabel` + a full params snapshot, in vector-index order so the
  parent indices stay valid. `readWorkspaceFile` parses these back into `WorkspaceEntry::history`, and
  the load path restores them via `EditSession::applyParamsToSlot(slot, params, history)` →
  `History::restore()`, which rebuilds each node's `kids` from its parent, restores `current`, and
  resumes `seq` numbering past the highest loaded seq (so later edits get fresh, non-colliding ids and
  redo still prefers the newest branch). Old projects (no `#hnode`) load with a single-node root, as
  before. Regression-tested (`workspace_history_roundtrip`): a branched tree round-trips with its
  nodes, branch, current node, and undo/redo intact.
- **R-PERSIST-2 Save shortcuts.** `Ctrl+S` saves the project to its current path (prompting only if it
  has none); `Ctrl+Shift+S` is Save As (always prompts). File ▸ Save / Save As… map to the same
  workspace actions (labels show the shortcuts), so "save" means "save the project" everywhere —
  group tree, per-image params, and history. (Bare `s` remains the quick PNG export.)

## R-MASK — Mask adjustable inside the photo (item 1) — ✅ IMPLEMENTED

Status: implemented. `apps/cosmo/widgets/MaskOverlay.{h,cpp}` (ported from cosmo), owned by
`PhotoCanvas` (above the image, below the Before/Split/After pill), bridged through
`RightColumn::{activeTab,selectedMaskParams,writeSelectedMask}` and synced each frame in
`App::render`. Reference: original cosmo's `apps/cosmo/widgets/MaskOverlay.{h,cpp}`.

- **R-MASK-1** When a local-adjustment mask is selected (Mask tab), an interactive overlay is
  drawn over the photo on the center stage that lets the user position/resize the mask *inside
  the photo*, in the photo's normalised framed-image coordinates (the space the engine's
  `maskCoverage` uses). The UI edits `MaskParams` geometry only — it never touches pixels.
  - Radial: drag centre to move, edge handles to resize.
  - Linear: drag the two endpoints (0% and 100% gradient lines).
  - Brush: drag anywhere to paint coverage dabs.
  (**AMENDED (R-MASK-5), 2026-08-18:** "inside the photo" describes where the *interaction*
  happens — on the photo rather than in a dialog — and was implemented as a hard clamp of the
  geometry to the framed image, which is not the same thing and is wrong. See R-MASK-5.)
- **R-MASK-2** With no mask selected the overlay is fully click-through (does not intercept
  zoom/pan or the Before/Split/After pill).
- **R-MASK-3** The overlay maps to the ImageView's current fitted rect **including zoom/pan**
  (R-ZOOM), so handles track the photo as it is magnified/panned.
- **R-MASK-4** Handle/geometry changes animate their on-screen position (R-G-1); the committed
  `MaskParams` value itself is not eased (it is data), only its rendered handles.
- **R-MASK-5 A mask's geometry may extend BEYOND the framed image.** (**Added 2026-08-18, amends
  R-MASK-1.**) A radial mask larger than the frame, or centred off-frame, and a linear gradient
  whose endpoints sit outside the image, are all ordinary tools — a vignette that darkens every
  corner equally cannot be built from an ellipse trapped inside the frame, and a gradient that
  enters from off-canvas is how a sky is graded. Normalised framed-image coordinates are a
  *coordinate space*, not a boundary: 0..1 spans the image and values outside it are meaningful.
  The overlay therefore does not clamp a dragged handle to the image rect; it is bounded only by
  what the pointer can reach on the canvas, plus a generous finite sanity limit so a degenerate
  fitted rect cannot produce an absurd or non-finite value.
  The engine already agreed — `maskCoverage` (`engine/MaskStack.cpp`) never clamped mask geometry,
  only the coverage it computes and `feather`, so out-of-frame geometry has always rendered
  correctly. The clamp existed solely in the overlay's `localToNorm`, which is why the symptom was
  "the slider stops at the border" rather than "the mask renders wrong".
  Radii stay strictly positive: a zero or negative radius is not a small mask, it is a
  division-by-zero the engine guards with an epsilon.

## R-VIEW — The photo dissolves; it never pops — ✅ IMPLEMENTED

Dragging a slider is the one place in cosmo where the *photo itself* changes, and it changed the
only way R-G-1 forbids: each preview render replaced the pixels in a single frame, so a drag read as
a stutter of discrete pictures rather than one continuous edit. The photo is a visible property like
any other.

- **R-VIEW-1 A render cross-dissolves onto the one on screen.** The stage holds **two** stacked
  photo views; the top one's opacity **is** the dissolve, so the composite is
  `a·top + (1−a)·bottom` — a true cross-dissolve with no dip through the canvas, because the layer
  being covered stays opaque instead of fading out underneath. A new render lands in whichever view
  is currently hidden and the opacity eases toward it (**120 ms**, `EaseOutCubic`, collapsing under
  `reducedMotion()`), so consecutive renders dissolve in alternating directions and no pixels are
  ever copied between views.
- **R-VIEW-1a A render arriving mid-dissolve WAITS; nothing is ever written into a layer that is
  on screen.** (**AMENDED 2026-08-20**, and the amendment is the whole point: the first version
  reversed the dissolve instead — it wrote the newest render into the layer that still had weight
  `1−a` — and treated the resulting step as an acceptable residual. It is not acceptable and it is
  what the user reported as *the photo blinks*. Opacity being continuous is not sufficient: the
  composite is `a·top + (1−a)·bottom`, so replacing the **pixels** of either layer while its weight
  is non-zero is a discontinuity of exactly that weight times the difference between two renders,
  and during a drag that happens on nearly every render.) So while a dissolve is in flight the
  newest frame is **held**, and it is applied the moment the dissolve settles — when the hidden
  view's weight is exactly zero and writing to it changes nothing on screen. Newest wins: a held
  frame is overwritten by a newer one, the same coalescing `RenderService` already does upstream.
  The cost is stated plainly: a render can wait up to one dissolve (120 ms) before it is shown, and
  intermediate renders during a fast drag are dropped rather than flashed. Latency is the right
  thing to trade for continuity here, because a preview that is 120 ms behind still tracks the
  slider while a photo that jumps does not read as an edit at all.
- **R-VIEW-1b The first photo is set, not dissolved.** A photo appearing on an empty stage has
  nothing to travel from (R-G-1a's rule for a card's first placement, applied to pixels).
- **R-VIEW-1c A differently-SHAPED photo is set, not dissolved.** (**AMENDED 2026-08-20:** the test
  was the pixel size, which is wrong for the same photo at a different preview resolution — a zoom
  step changes the pixel count and not the shape, and cutting there was a visible pop while
  zooming. The test is the **fitted shape**: two frames that fit the same rect dissolve, whatever
  their resolution.) A frame whose aspect differs cannot cover the one on screen, so dissolving it would leave the old photo visible around its edges and
  then snap it away at the end. Both views take it, so no stale pixels can peek. A proper
  cross-photo transition (dissolve through the canvas, since nothing covers anything) is its own
  task — tracked in `docs/PROGRESS.md`, not silently absent.
- **R-VIEW-1d The desktop stage first; the phone stage is tracked, not forgotten.** The touch
  shell (`touch/PhoneApp`, §1.8's separate UI) drives the same session through a single
  `ImageView` and still replaces its pixels in one frame. It is the same fix and the same twenty
  lines, but that shell has no headless test or shot to prove it with, so it is a ledger task
  (U2.3) rather than an unverified edit — and it is written here so the requirement is not read as
  claiming something that is only true on the desktop.
- **R-VIEW-1e A layer that is covering another may not be dropped a frame early.** The stage skips
  drawing the fully covered layer, which is legitimate only while the covering layer is **exactly**
  opaque. That decision therefore reads **this** frame's alpha — computed after the animation
  update and before anything is drawn — never the previous frame's. Reading the stale value hid the
  base on the first frame of every `1 → 0` dissolve, so the canvas showed through the
  partly-transparent top: a one-frame darkening on every other render, at roughly 8 Hz through a
  drag. This is R-G-1's own clause (d) restated for a value derived from an eased one: derive it
  every frame, from the eased value, or do not derive it.
- **R-VIEW-2 Before / Split / After changes dissolve too.** The mode pill routes through the same
  path, so toggling Before↔After dissolves rather than cutting, and Split's clipped half and its
  seam **fade** in and out instead of flipping `visible` (R-G-1). The pill's own highlight keeps
  sliding as it already did.

## R-ZOOM — Zoom & pan inside the photo (item 2) — ✅ IMPLEMENTED (except R-ZOOM-5)

Status: implemented in `PhotoCanvas` (owns zoom/pan, mirrors to before+after views) +
`App::wheel`. Uses `artboard::ImageView::zoomAbout/panBy/resetView` and
`EditSession::setPreviewZoom/resetPreviewResolution`. **R-ZOOM-5 (eased zoom) NOT yet done** —
zoom applies in 1.15× notches; smooth easing belongs in Artboard's `ImageView` (a library
change under the Artboard skill) and is tracked there, not faked at the app layer.

- **R-ZOOM-1** Ctrl + mouse-wheel over the photo zooms about the cursor: scroll up = zoom in,
  down = zoom out. Zoom is clamped 1×–8× (ImageView clamp). Plain wheel over the photo does
  nothing (it is reserved for panel/rail scrolling elsewhere).
- **R-ZOOM-2** While zoomed (>1×), press-drag on the photo pans the view; pan is clamped so the
  image always covers the view (ImageView `clampPan`).
- **R-ZOOM-3** Before and After (split) image views share one zoom/pan state so the split seam
  stays pixel-aligned — zoom/pan is routed through `PhotoCanvas`, applied to both views.
  (**AMENDED (R-VIEW-1), 2026-08-20:** "both views" is now **every** image view on the stage. The
  dissolve of R-VIEW-1 adds a second after-view, and a view left out of a zoom or a pan would slide
  into place under the next dissolve — the seam alignment this requirement exists for is a property
  of the whole stage, not of a pair.)
- **R-ZOOM-4** On each zoom step the preview render resolution scales with the zoom
  (`EditSession::setPreviewZoom`) so a high-res original stays sharp when magnified; resets to
  base when the view returns to 1× or the selected image changes.
- **R-ZOOM-5** Zoom and pan **animate** toward their target (R-G-1) rather than snapping per
  wheel notch / per drag frame — magnification eases, it does not jump.

## R-PARITY — Backlog carried over from the original cosmo (item 3)

This app began as `cosmo_v2` alongside an original `apps/cosmo/` GUI; it has since fully replaced
that app and been renamed to `cosmo` (the original was removed). A few features from the old
app were not yet ported — that backlog (verified 2026-07-06 against the original) lives in
[PARITY.md](PARITY.md); each row is a requirement to either implement or explicitly descope
with a reason. PARITY #1 (R-MASK) and #2 (R-ZOOM) are done. Remaining rows:

- **R-PARITY-CROP** (PARITY #3) — interactive crop box over the photo (corner/edge handles,
  aspect lock) while the Xform tab is active; writes normalised crop x/y/w/h. *Open.*
- **R-PARITY-PRESETPICK** (PARITY #4) — see R-PRESETPICK below. *Implementing now.*
- **R-PARITY-SETTINGS** (PARITY #5) — see R-SETTINGS below. *Implementing now.*
- **R-PARITY-SPLIT** (PARITY #6) — draggable split divider in Before/Split/After. *Open.*

### R-PRESETPICK — Preset category-picker modal (PARITY #4)

Reference: the original cosmo's `widgets/PresetDialog.{h,cpp}` (since removed). Today cosmo
applies every present category of a preset immediately (documented stub in `App.h`).

- **R-PRESETPICK-1** Applying a preset (from the preset tree / Preset menu) opens a modal that
  lists the categories the preset actually contains (e.g. Tone, Colour, Detail, Effects, Curve,
  Grade, Mixer, Xform, Masks — whichever are present) as toggleable checkboxes, all on by
  default, with Apply / Cancel.
- **R-PRESETPICK-2** Apply applies only the checked categories to the current image; Cancel
  applies nothing. The same picker backs Save/Export (choose which categories to write).
- **R-PRESETPICK-3** The modal is a real overlay drawn in the overlay pass (dim scrim behind,
  centered card), it opens/closes with a fade+scale (R-G-1), and it is click-outside/Esc
  dismissable. Only one modal open at a time (coordinate with the History modal).
- **R-PRESETPICK-4** Backed by the engine's category masking: a `PresetCategory` bitset chooses
  which `EditParams` fields copy from the preset onto the current params.

### R-SETTINGS — Engine settings panel (PARITY #5)

Reference: `apps/cosmo/panels/SettingsPanel.{h,cpp}`. Exposes engine/app settings that map onto the
`RenderService`.

- **R-SETTINGS-1** A settings surface exposes: **Preview quality** — the base preview render
  resolution (speed vs. detail), **CPU threads** — the worker count for the multicore engine,
  **CPU limit** — the share of the machine cosmo may schedule (**AMENDED (R-CPU-3)**: 25 / 50 / 75 /
  100%, defaulting to 50, sitting next to CPU threads because Auto resolves to it), **GPU
  acceleration** — off / on-if-available (see R-GPU), and **Screen scale** (**AMENDED (R-SCALE-1),
  2026-08-19**: 75 / 90 / 100 / 125%, defaulting to 100, first in the list because it changes the
  window the other four rows are read in). The first four map directly onto `RenderService` /
  `EditSession` (`mPreviewEdge` / thread count / prefer-GPU) or onto `AppSettings::cpuPercent`;
  Screen scale is the one row that maps onto the **view** and not the engine (R-SCALE-2).
- **R-SETTINGS-5 Reachable from both screens.** The surface opens from the editor's Settings menu
  **and** from the home screen's sidebar "Settings" link (**this amends R-HOME-8**). One modal, one
  set of callbacks, one persisted record — not a second settings page for the launcher. Because the
  dialog lives in the editor's Segment tree while Home is drawn standalone, the Home screen advances,
  renders and routes gestures to that same dialog while it is open, rather than owning a copy of it.
  Everything else about it is unchanged: same chrome, same fade, same Esc / click-outside dismiss
  (R-SETTINGS-3), and a change made from the launcher is in force and persisted immediately, so the
  project opened next already loads under it.
- **R-SCALE-1 The shell has one scale, and it persists.** A **Screen scale** setting draws the
  whole shell at `75 / 90 / 100 / 125 / 150 / 175 / 200` percent of the Figma sizes
  (**AMENDED, 2026-08-19**: the range was 75-125; a really small device needs the shell drawn
  *bigger*, not smaller, and 200% is what makes a 7-inch high-density panel usable), default 100,
  persisted with the other preferences (R-SETTINGS-4) and settable from a script as
  `settings set uiScale=N`, so a scaled shell can be rendered and asserted headlessly. It runs in
  **both** directions: below 100 the shell is drawn smaller so a 1366x768 or 1280x800 panel can show
  the editor's rail + canvas + right column at all; above 100 it is drawn larger so a small dense
  screen is legible and touchable. Either way the fix is to draw the one design at a different size,
  never to invent a second, narrower layout. Only the listed scales are legal and a stored value is
  **snapped** to the nearest one — every offered scale has a rendered shot and a layout assertion
  behind it, an arbitrary 83% has neither.
- **R-SCALE-2a A scale change animates, like everything else (R-G-1).** Changing the scale eases the
  whole shell between the two sizes over ~260 ms: the root transform, the logical size derived from
  it, and therefore every widget's layout, recomputed each frame from the **eased** scale. It does
  not jump and then reflow. This is an **amendment to the first implementation**, which set the scale
  and the logical size instantly — the single largest visible change in the app, snapped, by the
  same change that documented R-G-1 two requirements above. `AppSettings::uiScale` and
  `App::uiScale()` remain the *target* (the setting is a number, not a motion); only the drawn scale
  eases.
- **R-SCALE-2 It scales the view, never the model.** The scale is a single transform on the view
  root plus the inverse on incoming pointer coordinates: `mW`/`mH` become **logical** units
  (`physical / scale`) and every widget keeps laying out in the one coordinate system it was written
  for. No widget reads the scale, no constant is multiplied at its use site, and no second set of
  metrics exists — a per-widget scale factor is how a layout acquires two truths and starts
  disagreeing with itself. The service stores the value (it is a machine preference) and never acts
  on it, because the layout it scales is presentation (R-SVC-3).
- **R-SCALE-3 A scale the screen cannot honour is not SELECTABLE, and the window enforces the rest.**
  The window's minimum size is `logical minimum x scale`, and the logical minimum is the **larger**
  of what the launcher needs and what the editor needs — the editor's is bigger and had never been
  computed, so before this the editor could be resized until the photo canvas was 64 px wide with
  the rail still open. Below the width where the canvas would fall under its minimum the **left
  rail collapses itself**, eased like the manual toggle (R-G-1), so the canvas keeps its floor
  instead of the three columns squeezing each other.
  (**AMENDED, 2026-08-19**, because the range now reaches 200%: it is no longer true that every
  offered scale fits every screen — 200% needs 1168x932 of window, which a 1366x768 panel cannot
  give. So a scale whose minimum exceeds the **display** is shown **disabled**, with the reason,
  exactly as the GPU row already shows "unavailable" — the same affordance for the same fact. Hiding
  it would leave a user on a large screen wondering what happened to it; letting it be chosen would
  hand a small screen a window it cannot open. The host reports the display size; with none reported
  nothing is disabled, so a headless harness sees the full range.)
- **R-SETTINGS-2** Changing preview quality updates the base preview edge and re-renders;
  changing thread count reconfigures the engine's parallelism. Values persist for the session.
- **R-SETTINGS-3** Presented as a modal overlay consistent with R-PRESETPICK-3 (scrim, centered
  card, fade+scale open/close, Esc/click-outside dismiss).
- **R-SETTINGS-4 Settings persist across launches.** Preview quality, CPU threads, GPU
  acceleration and **screen scale** (**AMENDED (R-SCALE-1)**) are statements about the **machine**,
  not about the session — reverting them on every
  launch makes the panel feel broken. They round-trip through `cosmo::AppSettings`, a plain
  `key=value` file in the same user config dir as the recent-projects index (the same file
  convention the workspace format uses). They are **loaded before the first render** and applied to
  the session/engine at startup, and **saved whenever one changes**, so the Settings dialog always
  opens seeded with what is actually in force. A missing, partial or corrupt file falls back to the
  defaults per field rather than failing to start.

## R-GPU — GPU-accelerated image processing (abstract, opt-in) — ✅ IMPLEMENTED (abstraction + OpenGL/Linux backend; more stages + platforms deferred)

The image engine can process on the GPU when a platform GPU backend is available and the user
opts in, behind a **cross-platform abstraction** so concrete per-platform GPU implementations
(Metal / Vulkan / Direct3D / WebGPU / OpenGL) are added **without touching the engine or the UI**.
A first concrete backend — **OpenGL 4.3 compute over surfaceless EGL** — is implemented (Linux;
runs on AMD/Intel/NVIDIA via Mesa/desktop GL, with an llvmpipe software fallback). This mirrors
Artboard's platform-independence rule: the CPU/software path is the reference and the floor, and an
accelerator is used only when its result matches it.

- **R-GPU-1 Abstract seam.** `core/ImageProcessing/src/compute/ComputeBackend.h` defines
  `IComputeBackend` — an *optional* accelerator that renders the per-image edit pipeline (global
  chain + masks + gamma encode) for a `(linear source Image, EditParams)`, returning the encoded
  result and the histogram taps; it exposes `name()`, `kind()` (`Cpu`/`Gpu`), `available()`, and
  `process(...)` which returns **false to decline** a job. A single factory
  `createComputeAccelerator()` returns the platform backend — the **OpenGL 4.3 compute backend**
  where built (`ARSTRO_GL_COMPUTE`), `nullptr` (CPU-only) otherwise, e.g. the web build. It is the
  one per-platform extension point.
- **R-GPU-2 CPU is the reference & the fallback.** The existing CPU pipeline in `EditEngine` is the
  guaranteed fallback **and** the correctness reference: a GPU backend that accepts a job must match
  the CPU path **within a small tolerance** (a hardware backend is not bit-exact in float — the
  OpenGL backend is verified ≤ 2/255 vs CPU). If no backend is available, the toggle is off, or the
  backend declines, the engine renders on the CPU exactly as before — **byte-identical, no behavior
  change** (headless-verified). The accelerator branch lives in the single render choke point
  `EditEngine::renderInto`.
- **R-GPU-3 Setting (opt-in).** Engine Settings… gains a **GPU acceleration** row (Off / On),
  shown **disabled + "unavailable"** when no GPU backend is present. When available and On, the
  engine *prefers* the GPU backend; otherwise CPU. It is a session/engine setting (not persisted to
  the project), plumbed `SettingsDialog → App → EditSession::setUseGpu → RenderService::setPreferGpu
  → EditEngine::setPreferGpu`, mirroring Preview quality / CPU threads.
- **R-GPU-4 Cross-platform & platform-free.** The seam is platform-free (`ImageProcessing` has no OS
  deps and compiles under Emscripten); concrete backends live behind the factory per platform.
  Selection/availability/fallback are regression-tested with a mock backend (the compute-side
  `RecordingTarget`).
- **R-GPU-5 OpenGL compute backend (first concrete, Linux/AMD).**
  `core/ImageProcessing/src/compute/GlComputeBackend.cpp` implements `IComputeBackend` with an **OpenGL
  4.3 compute shader** over a **surfaceless EGL** context (headless — no window; the shader is
  compiled at runtime, so no offline SPIR-V/GLSL toolchain is needed). Built when `ARSTRO_GL_COMPUTE`
  is defined and EGL/GL link (CMake auto-detects; `build.sh` passes it for the cosmo native build).
  - **Ported subset (this increment):** the per-pixel colour/tone point ops — **Exposure**,
    **Contrast**, **White Balance** (gains reused from `color::kelvinToRgbGain`) — plus the exact
    sRGB **encode**. `process()` runs on the GPU only when the edit is entirely within that subset
    (every other stage at its default); anything else (tone curve, mixer, grade, tone regions,
    vibrance/saturation, dehaze, grain, sharpen, noise reduction, lens, geometry, masks) **declines
    → CPU**, so all edits stay correct. The histogram taps are computed on the CPU from the GPU
    result (which equals the final image because the un-ported stages are identity in the fast path).
  - **Threading & availability:** availability is a one-time cached probe; the working GL context is
    created lazily on the RenderService **worker thread** on first use. If context creation or the
    edit is unsupported, it falls back to CPU. Verified on the AMD (Mesa `radeonsi`) GPU — GPU output
    matches CPU ≤ 2/255, on both the direct engine path and the RenderService worker path
    (`EditEngine_gl_backend_matches_cpu`, `RenderService_gpu_worker_matches_cpu`).
- **R-GPU-6 OpenGL ES 3.1 compute backend (Android).**
  `core/ImageProcessing/src/compute/GlesComputeBackend.cpp` is the Android sibling of the desktop GL
  backend, selected by the same factory (`#elif defined(ARSTRO_GLES_COMPUTE)`). It shares the
  accepted-edit predicate and the compute-shader body with the desktop backend
  (`compute/GlComputeShared.h`) so the accepted subset and per-pixel math cannot drift; only the EGL
  context (an **ES 3.1** context over a 1×1 pbuffer, `EGL_OPENGL_ES_API`) and the shader header
  (`#version 310 es` + `precision highp`) differ. ES 3.1 exposes compute / SSBOs / `glMapBufferRange`
  as core, so no proc-address loader is needed. Built when `ARSTRO_GLES_COMPUTE` is defined and
  EGL/GLESv3 link (the Android app CMake sets it; the umbrella `android-app` target links GPU); the
  CPU pipeline stays the reference + guaranteed fallback exactly as R-GPU-2/5. Verified GPU==CPU
  ≤ 2/255 on desktop Mesa GLES (`EditEngine_gles_backend_matches_cpu`, skips cleanly with no ES 3.1
  device) and on-device on an Android GLES 3.2 GPU (Mali). The GLES backend cross-compiles for
  arm64-v8a with the NDK.
  - **Deferred:** the remaining pipeline stages (moving encode + histograms fully onto the GPU too),
    and other APIs (Vulkan/Metal/D3D/WebGPU) — each an incremental add behind the same seam.

## R-HOME — Home screen & projects (item 4) — ✅ IMPLEMENTED

Status: implemented. `widgets/HomeScreen.{h,cpp}` (Figma-faithful launcher) + a screen
state machine in `App` (`Home`/`Loading`/`Editor`, cross-fade scrim on switch) + `apps/cosmo/core/
ProjectStore.{h,cpp}` (persisted recent index) + host dialogs in `linux_main.cpp`. The app
starts on Home. Notes vs. the spec below: the recent index is a small **line-based TSV** in
the config dir (not literally JSON); the Figma **LoadingScreen** is implemented as the animated
open-project transition (see **R-LOADING**); a `.cmp` is exactly the existing workspace catalog
with a `.cmp` extension. Search is a focusable `artboard::TextBox` fed by host key/text events.


Reference Figma: `ref/2/extracted/src/app/App.tsx` → `HomeScreenDesktop` (lines ~214–386) and
`LoadingScreen`. Must match 100% (R-G-2): left 300px sidebar (`#161616`) with wordmark
`cosmo.` (accent dot), tagline, New Project / Open Project… / Import Catalog… actions, bottom
Settings / What's New / Help & Documentation links + version; right pane with a "Recent
Projects" header + count + search box, and an `auto-fill minmax(220px,1fr)` grid of project
cards (16:9 cover thumbnail, "Edited" badge, name, `N photos · size · date`) plus a dashed
"New Project" card. Empty-search state shows the "No projects match" placeholder.

- **R-HOME-1 Screen state machine.** App has three screens: `loading → home → editor`. The
  editor is the current cosmo UI. Clicking the top-left **`cosmo.` wordmark** in the editor
  returns to home; if the project has **unsaved changes** (`EditSession::isDirty()`), a modal
  first asks to **Save** (accent) or **Discard** (red/destructive), with Cancel/click-outside to
  stay (`ConfirmDialog`). Screen transitions cross-fade (R-G-1).
- **R-HOME-1b Project name in the top bar.** The open project's name (the `.cmp` stem) is shown
  centred in the editor top bar; set on New/Open/Import/Recent-open.
- **R-HOME-2 Project file format `.cmp` = catalog/manifest.** A `.cmp` is a JSON catalog that
  **references the original image files on disk** (absolute/relative paths) plus each image's
  edit settings and the group tree — i.e. it reuses cosmo's existing workspace serialization
  (`writeWorkspaceFile`/`readWorkspaceFile`) with a `.cmp` extension and a `name` field.
  Rationale: matches the reference-by-catalogue import model, small/fast, no gigabyte copies.
  Consequence: if an original is moved/renamed the reference is stale (acceptable for now).
  Project "size" shown on cards = sum of the referenced files' sizes. All create/open/save go
  through one `ProjectStore` seam so the format can evolve without touching the UI.
- **R-HOME-3 New Project.** "New Project" (sidebar button, dashed grid card, or the empty-state
  card) opens a popup to enter a project name (and location); confirming creates a new `.cmp`
  and opens the editor on it, where the user adds photos (the current editor screen / import).
- **R-HOME-4 Open Project.** "Open Project…" shows a native file dialog filtered to `.cmp` and
  opens the chosen project in the editor.
- **R-HOME-5 Import Catalog.** "Import Catalog…" shows a native multi-select image dialog, asks
  where to save the new project, and opens the selected images inside it **through the same
  animated open-project transition a recent project uses (R-LOADING)** — the picked images become
  root-level workspace entries and stream in on the background decode thread behind the loading
  screen, with the real progress bar and per-image status line. It must NOT decode inline on the UI
  thread: a catalog of large frames would otherwise freeze the app for the whole import with no
  feedback. The one difference from opening an existing project is that the `.cmp` does not exist
  yet, so it is written once the last image has landed (`LoadJob::saveOnFinish`); the loading-screen
  cover falls back to the first decoded image, since no thumbnail is cached for a never-opened
  catalog.
- **R-HOME-6 Recent Projects (real, persisted).** The grid lists recent projects from a
  **persisted recent-projects index** (a JSON file in the app config dir), each entry storing
  the project name, `.cmp` path, photo count, last-opened time, and a cached thumbnail of the
  project's first image. Each card shows: thumbnail = first image of that project, name, photo
  count, and last-opened time (relative, e.g. "2h ago"). Creating/opening a project updates the
  index (path → front, refresh last-opened).
- **R-HOME-7 Search.** The search box filters recent projects by name (case-insensitive,
  substring); no matches shows the empty-state placeholder.
- **R-HOME-11 The window's minimum size is summed from the layout, not chosen.** (**Added
  2026-08-18.**) The sidebar has two blocks anchored to opposite edges — the three action buttons
  to the top, the Settings / What's New / Help & Documentation links and the version to the bottom
  — so a window shorter than their sum plus air makes them **overlap**, which a hardcoded 400 px
  minimum did. `HomeScreen::minContentHeight()` / `minContentWidth()` sum the parts and the host
  asks for them, so the minimum follows the layout when the layout changes instead of being a
  number that happened to work at the size someone tested. On the current layout that is
  **584 × 466**. A magic number here is not a shortcut; it is a second copy of the layout that
  nobody updates.
- **R-HOME-8 Reserved.** Settings / What's New / Help & Documentation are present but inert
  (reserved), matching the Figma affordances without behavior. **AMENDED (R-SETTINGS-5): Settings is
  now live** — it opens the same modal the editor's Settings menu opens. A link that draws a hover
  wash and then does nothing reads as broken, and the settings a photographer most wants to set
  (preview quality, CPU limit) are the ones they want set *before* opening a project, when the only
  screen available is this one. What's New and Help stay reserved.

## R-BYPASS — Per-node filter bypass (disable/enable a group's or photo's edits)

A photographer needs to see what an item looks like *without* its own develop settings without
throwing those settings away. Any node of the group tree — an image leaf **or** a group — can
therefore be **bypassed**: its own `EditParams` stop contributing to what is rendered, while the
values themselves stay intact and stay editable, so re-enabling restores the look exactly.

- **R-BYPASS-1 Model (one flag per node).** `EditSession::GNode` carries a `bypass` flag
  (`isBypassed(node)` / `setBypassed(node,on)` / `toggleBypass(node)`). It is a property of the
  **node**, not of the slot, so an image leaf and a group are bypassed the same way.
- **R-BYPASS-2 Semantics — a node's OWN params only.** Bypass removes exactly the bypassed node's
  own contribution from the composition, nothing else:
  - a bypassed **image leaf** contributes `EditParams{}` instead of its own params; its ancestor
    groups still stack onto it;
  - a bypassed **group** contributes `EditParams{}` instead of its own offsets; its members' own
    params and its *other* ancestors are unaffected.
  Bypass therefore composes: each node in a chain can be bypassed independently, and bypassing a
  group does **not** implicitly bypass the images inside it. `effectiveParams(slot)` is the single
  place this is applied for everything that renders — preview, histogram, before/after and export
  all agree by construction.
  `effectiveEditParams()` (which exists only to derive the panel's green "stacked reach" =
  effective − own) honours bypass on the **ancestors** but never zeroes the edit target's *own*
  values, because that reach measures what the ancestors add on top: a bypassed ancestor correctly
  contributes nothing, while the target's own bypass is communicated by the dim scrim (R-BYPASS-4)
  rather than by faking a negative reach on every slider.
- **R-BYPASS-3 Right-click toggle.** The filmstrip/photo context menu (`App::openEditContext`) gains
  a **Disable Filter** / **Enable Filter** item whenever the click landed on a cell (group or image).
  The label reads the target's current state, so the item always names what the click will do.
  Toggling re-renders immediately (R2) and marks the session dirty.
- **R-BYPASS-4 The edit section dims.** While the item currently being edited is bypassed, the
  right column's **edit stack** (the tab strip + the panel body between the histogram and the action
  bar) is covered by a dark scrim and its content is muted, so it reads at a glance as "these values
  will not be applied". A small non-interactive **FILTER DISABLED** pill sits at the top of the
  dimmed area naming the state. The controls stay live — a bypassed item is still editable.
  The scrim's opacity is an `AnimatedProperty` eased in/out (R-G-1); it never pops, and it collapses
  instantly under `reducedMotion()`. The histogram and the pinned action bar are NOT dimmed (they
  are not part of the edit stack).
- **R-BYPASS-5 Filmstrip indicator.** A bypassed cell draws a muted "no entry" badge in its
  top-left corner and its thumbnail ring reads muted rather than accent, so the state is visible
  from the strip without opening the menu. `Filmstrip::Cell` carries a `bypassed` flag fed by
  `App::syncControlsToSlot`.
- **R-BYPASS-6 Persistence.** The flag round-trips through the `.cosmoproj` workspace as a
  `bypass=1` line on the node's `#group` / `#image` section (absent = not bypassed, so older
  projects load unchanged). `WorkspaceEntry::bypass` carries it to the host loader, which passes it
  to `addWorkspaceGroup` / `setSlotBypass`.

## R-EXPORT — In-app Export modal (batch export with a group tree)

File ▸ Export… opens an **in-app modal**, styled exactly like the rest of the editor, that exports
one or many images in one action. It replaces the old "immediately show a native save dialog for the
current image" behaviour of that menu item. The **bare `s` shortcut keeps its quick single-image
PNG/JPEG export** through the native save dialog (R-PERSIST-1's note stands) — the modal is the
deliberate, batch path, `s` is the fast path.

Reference design: `ref/cosmo/File Reader Design(1).zip` → `src/app/App.tsx` `ExportModal`, adapted
to this app's tree model per the deltas called out below.

- **R-EXPORT-1 Chrome.** A modal `ExportDialog` Segment drawn in the overlay pass on the same
  chrome as R-PRESETPICK-3 / `PresetDialog`: dim scrim over the whole window, one centred card on
  `palette::popover()` with a `radius::control()` border, a header band carrying the **download
  icon + the title "Export"** and a close **✕**, a scrollable body, and a pinned footer. It fades +
  rises in and out (R-G-1), and click-outside / **Esc** cancels. Every region hovers (R-G-3).
- **R-EXPORT-2 Images to export — a checkbox TREE (delta from the reference).** The reference's
  group chips + flat thumbnail grid are replaced by the **real group tree**: one row per node,
  indented by depth, groups carrying an expand/collapse chevron + child count, images carrying their
  filename. **Every row has a checkbox**, and the check state propagates:
  - **select a parent → select all its children** (recursively);
  - **deselect a parent → deselect all its children** (recursively);
  - **deselect any child → the parent is unticked**;
  - **when every child is selected → the parent is ticked**.
  This is realised by keeping the **image leaves** authoritative and *deriving* every group's state
  from its descendants — ticked when all descendant images are ticked, **indeterminate** (a dash,
  not a tick) when only some are, unticked when none — which satisfies all four rules by
  construction and cannot drift. A **Select all / Select none** master button sits above the tree,
  and a "`n` of `m` photos selected" line below it. Rows only count toward `m` if they are real
  image leaves (a group is a container, never an export target). The tree scrolls (eased, R-G-1)
  when it is taller than its box.
- **R-EXPORT-3 Destination (delta from the reference).** The destination row carries a **checkbox
  on its right, "Same as source"**, ticked by default:
  - **ticked** → each image is written next to its own original file, so a mixed selection lands
    beside each source. The path field shows `<source folder>/` greyed as a non-editable hint;
  - **unticked** → one explicit output folder is used for everything. It **defaults to the folder of
    the first selected image**, and a **Change…** button asks the host for a native folder chooser
    (`onChooseDestination` → `setDestination(path)`).
  Below it, two optional modifiers, each a checkbox + label + text field (the field is disabled and
  muted while its checkbox is off): **Filename prefix** (prepended to each output name) and
  **Export to subfolder** (a folder created under the resolved destination). The resolved path +
  example filename is previewed live in mono under the destination row, ellipsized to fit (R5).
- **R-EXPORT-4 Format / size / quality.** Format chips: **JPEG · PNG · TIFF** — the three the host
  writer can actually produce (GdkPixbuf); WebP from the reference is dropped rather than shipped
  broken. Size chips: **Original · 2048 px · 1080 px · 720 px** (long edge; downscale only, never
  upscale). **Quality** applies to JPEG only and is laid out as **two rows** (delta from the
  reference, which put the value inline with the slider): a **label row** — `Quality` left, the
  current `NN%` right — and the **slider on its own row** below it, full width. The quality block
  fades out entirely for PNG/TIFF, which have no quality knob.
- **R-EXPORT-5 Metadata.** Three toggles, matching the reference: **Embed EXIF data**,
  **Strip GPS coordinates**, **Embed colour profile (sRGB)**. They are honoured by the host writer
  for the formats that can carry them: EXIF/GPS operate on the JPEG `APP1` segment copied from a
  JPEG source, and the sRGB profile is written as a JPEG `APP2` `ICC_PROFILE` segment / a PNG
  `iCCP` chunk. A toggle that a chosen format cannot carry is simply not applied (no silent
  failure, and the footer summary never claims otherwise).
- **R-EXPORT-6 Footer + progress (R2).** The footer shows a live summary — `n photos · FMT · SIZE`
  (+ `· prefix: …` when one is set) — and **Cancel** / **Export n photos**. Export is disabled while
  nothing is selected. Because a batch full-res render is far slower than a frame, pressing Export
  **does not freeze the UI**: the host feeds `setExportProgress(done, total, name)` one image per
  main-loop idle step. The dialog stays modal (and non-cancellable mid-write) for the duration.
  Pressing Export plays a **three-beat animation**, every beat eased and reduced-motion-safe.
  **The animation plays first and the export starts after it** — pressing Export snapshots the
  request but hands the host nothing until the collapse has finished (`onExport` fires from
  `advance()` when the tween settles), exactly the split R-LOADING-0/1 uses so its intro never
  hitches on I/O. And the batch itself runs on a **worker thread**: `exportFullResSlot()` goes
  through `RenderService::renderFull()`, which *blocks its caller* until the engine finishes, so
  running it on the UI thread would freeze every frame for the length of each image. The UI thread
  only drains finished results (~1 poll per frame), so the dialog animates at full framerate from
  the first frame to the last. While a batch is running `App::render` skips its own full-render
  path (`renderBefore`), because the engine's full-render channel holds one request at a time and
  two callers would steal each other's frame.
  1. **Collapse.** The card *shrinks in place* to a progress card: the header and the
     **Images to Export** section survive; the master button, the selection count, Destination,
     the modifiers, Format/Size/Quality and Metadata all **fade out** and the card height tweens
     down to what is left. The tree's **checkboxes fade away** and each row's chevron/icon/label
     slides left into the space they occupied, so the list reads as a manifest rather than a
     picker. The tree itself is rebuilt to the **participating rows only** — the selected images
     plus the groups that contain them — so what is on screen is exactly what is being written.
  2. **Writing.** A determinate accent **progress bar sits directly under the tree section**, above
     a mono status line naming the file in flight. As each file lands, **that row's background
     highlights** (a success-tinted wash + a small tick, eased in per row, never popping) and a
     group's row highlights once every one of its members is written. The tree auto-scrolls to keep
     the in-flight row visible.
  3. **Done.** On the last file the tree, bar and footer content **fade out together**, the card
     shrinks again to a compact panel showing a **green tick and `Exported n photos`**, and after a
     short hold the dialog closes itself.
- **R-EXPORT-8 The modal is draggable.** The card can be **dragged by its header band** (anywhere
  but the ✕) and stays where it is put, so it can be moved off whatever the photographer wants to
  look at — including mid-export, since the header is the drag handle in every state. The offset is
  clamped so the header always stays reachable on screen, survives a window resize, and resets when
  the dialog is next opened. A drag never fires the click under it.
- **R-EXPORT-7 Bypass is honoured.** Batch export renders each slot through
  `EditSession::exportFullResSlot(slot,…)`, which composes exactly the same `effectiveParams(slot)`
  the preview uses — so a bypassed image or group exports without those edits (R-BYPASS-2), and
  what you see is what is written.

## R-BROWSE — Navigating the photo rack

- **R-BROWSE-1 The filmstrip scrolls on the wheel.** The photo rack is a scroll view like every
  other overflowing surface: a wheel over it scrolls it horizontally to reach photos beyond the
  viewport (it already eased its offset for the sliding selection ring — it simply was never given
  the wheel). `App::wheel` routes to it by hit-testing the strip's world rect, before the
  right-column branch. One notch moves one cell + gap, so a notch is one photo.
- **R-BROWSE-2 Arrow keys walk the rack.** **Left/Right** select the previous/next cell of the
  current group — image or group chip alike, in display order — and the selection ring slides
  (R-G-1) rather than jumping. The strip **auto-scrolls to keep the newly selected cell in view**,
  so holding an arrow walks the whole rack. Navigation is clamped at both ends (no wrap: wrapping
  from the last photo back to the first is disorienting in a cull). Arrow keys are ignored while a
  modal is open or a text field has focus (`App::isTextEditing`), and while the loading transition
  is playing (R-LOADING-4).
- **R-BROWSE-3 Edit-section scroll intensity.** The develop panels scrolled **one pixel per wheel
  notch** (`scrollBy` took the raw wheel delta as pixels), which reads as broken. One notch now
  moves `kEditScrollStep` = **10 px** — ten times the old step — via one named constant, so the
  feel is tunable in one place rather than per panel.

## R-SPLASH — Application open animation (windowless)

Starting the app decoded every recent project's cover thumbnail on the UI thread **before the first
window was even shown** (`refreshHome` → `onDecodeThumbnail`), so cosmo appeared to hang on launch.
It now opens the way a project does: **animation first, work after**.

- **R-SPLASH-1 A small, windowless splash.** On launch cosmo shows a **compact undecorated window**
  (no titlebar, no border, centred, ~`kSplashW`×`kSplashH`) — not a fullscreen screen — rendering an
  Artboard `SplashScreen` through the same `CairoTarget` as the editor. The main window is not
  created until the splash is done, so the first thing on screen is the animation.
- **R-SPLASH-2 The animation** follows the reference design's `LoadingScreen`
  (`ref/cosmo/File Reader Design(1).zip` → `src/app/App.tsx`), scaled to the small window: the
  **`cosmo.` wordmark** (accent dot, R-G-2a spacing) rises 8 px and scales 0.96→1 while fading in;
  the **`PROFESSIONAL PHOTO EDITOR`** tagline (letter-spaced, muted) fades in behind it; **three
  dots** pulse in sequence; and a **2 px accent progress bar is pinned to the bottom edge** of the
  window, filling as startup proceeds. Every part is an eased `AnimatedProperty`; the whole thing
  collapses under `reducedMotion()`.
- **R-SPLASH-2a It says what it is loading.** A launch that only shows a bar tells you *how far*
  but never *what*. The dots therefore **hand off to a status line**: while there is nothing
  specific to report (during the intro) the reference's pulsing dots hold the slot; the moment the
  host reports its first item they **cross-fade out** and a **spinner + status text** cross-fades in
  in the same slot — `Scanning projects…`, then `Loading  <project name>` per cover thumbnail, then
  `Ready`. One activity affordance at a time (never dots *and* a spinner: that is the same
  affordance twice), and the slot never jumps. The spinner is the same rotating arc the filmstrip's
  loading cells use (R-LOADUX-2), so "work is happening" reads identically across the app.
  The **text itself is data, not motion** (R-G-1): the line fades in once and out at the end, while
  the string inside it simply changes — cross-fading every string swap would flicker, since items
  are reported as fast as they complete.
- **R-SPLASH-3 Animate first, load after.** The intro plays against an idle main loop. Only when it
  finishes does the host do the deferred startup work (scanning recents and decoding their cover
  thumbnails), reporting real progress **and the current item's name** into the splash; the splash
  then fades out, is destroyed, and the main window is shown. A launch with images on the command
  line skips straight through.
- **R-SPLASH-4 Non-interactive.** The splash takes no input and cannot be dismissed; it is chrome
  for a fixed, short moment.

## R-LOADUX — A project load says what it is doing

Revealing the editor on the first decoded image (R-LOADPERF-3) made opening fast but **opaque**: the
loading screen flashed "Preparing…", the editor appeared with one photo, and the rest arrived with no
indication that anything was still coming or how much. Fast is not the same as understandable. The
load is therefore made **visible** rather than slower:

- **R-LOADUX-1 The whole rack exists immediately.** The group tree is built from the project's
  entries **up front, on the UI thread, before any decoding** — groups and one **placeholder leaf per
  image** (`EditSession::addPendingImage`, a node with no engine slot, the same shape
  `addWorkspaceMissingImage` already produced). So the filmstrip shows the project's real size from
  the first frame and photos fill in where they belong. As each image decodes it is **attached** to
  its waiting node (`EditSession::attachImage`), which also removes the need to apply results in
  entry order — the parent indices were resolved before any of it started.
- **R-LOADUX-2 Placeholder cells show they are loading.** A `Filmstrip::Cell` that has no image yet
  draws as a dimmed cell with a **spinning indicator** (a rotating arc — the placeholder for the
  supplied loading animation) instead of a thumbnail, so an un-arrived photo reads as *pending*, not
  as *missing*. The spinner is driven from the frame clock and stops as soon as the cell has pixels.
- **R-LOADUX-4 Progress at the granularity of the work, not of the results.** (**Added
  2026-08-18, D-22.**) A count of finished entries is not progress when one entry takes nine
  seconds: on the reported 18-RAF project the bar sat at 0 for 9.1 s and then advanced in bursts
  of five, because five workers start together and finish together. So the load reports **three
  things, not one**: entries **finished**, entries **started** (a worker has claimed them and is
  decoding), and a named **stage** — reading the project, decoding, writing the project for an
  import, done. The view therefore always has something true to say and something to animate:
  the determinate part of the bar is `finished/total`, the **in-flight** part
  `(started-finished)/total` is drawn as work-in-progress rather than as emptiness, and the
  status line names an entry the moment a worker picks it up instead of when it lands.
  This is deliberately *not* a fake percentage: a RAW decode reports no internal progress, so
  cosmo states what it knows — how many are done, how many are being worked on — and animates the
  uncertainty instead of inventing a number for it.
- **R-LOADUX-3 Progress that means something.** The loading screen's bar and status line report
  **`n of N`** against the project's real total, not an unlabelled fraction; and because the editor
  is revealed early, a **slim determinate progress bar remains along the top edge of the filmstrip**
  while images are still arriving, with the count beside it. It fades out when the last one lands.
  A photographer can therefore always tell how much is left, in both phases.

## R-SVC — The core is a service; every front end is a view — 🚧 APPROVED, IN PROGRESS

Design: [`docs/service-architecture-proposal.md`](docs/service-architecture-proposal.md).

The architecture doc has always claimed cosmo's logic is UI-free ("this is what lets the same
session/engine be driven headlessly"). It is not: opening a project — the app's most important
behaviour — is implemented in the GTK host (`startEntriesLoad`/`decodeEntry`/`pollLoad` in
`linux_main.cpp`), so it cannot be run without a mouse (D-6). The cost came due in 2026-08-17's
investigation of "the CPU limit does not limit": both defects behind it (D-11, D-12) were found by
*reading*, because the load path could not be made to run from a shell at all, and the log line that
was supposed to prove the feature had never once been observed. A behaviour an agent cannot reach is
a behaviour nobody can verify — and this app is developed across machines and sessions by agents.

So the fix is not another side door. It is to put the behaviour where the docs already say it lives,
and make every front end — the GTK window, the CLI, the shot renderer — a *view* of it.

- **R-SVC-1 One service owns the behaviour.** `CosmoService`, in `cosmo_core`, owns the application
  state and every operation on it: the edit session and group tree, selection, history, the project
  load pipeline, the export queue, presets, settings and recents. It links no Artboard type, no GTK,
  no codec, and calls no `getenv` — the existing layering rule, now enforced by there being nothing
  above it that behaviour can leak into.
- **R-SVC-2 One way in — `Command`.** A front end never calls `EditSession`, mutates an `EditParams`,
  or opens a file. It builds a `Command` and calls `dispatch`. A behaviour reachable from a front end
  but not expressible as a `Command` is a defect in the command set, not a shortcut.
- **R-SVC-3 One way out — `AppModel` + `Event`.** `AppModel` is the entire observable state as plain
  data (frames referenced by id, never pixels; no Artboard types), carrying a `revision` that
  increments on every change. `Event` says what just changed. A view renders the model and reacts to
  events; it computes nothing it could be told.
- **R-SVC-4 The view holds presentation only, and presentation is a real category.** A widget may
  read the model, animate itself, hit-test itself, and emit commands. It may not hold non-visual
  state or decide what a click *does* beyond naming a command. **Animation, easing, transitions,
  hover, and scroll offsets stay in the view** — R-G-1 is a view requirement and is untouched by
  this: the service knows a load is 6-of-18 done, the view knows the bar eases toward it.
- **R-SVC-5 The text form is generated, never written twice.** Every `Command` and `Event` has one
  codec converting it to and from a line of text. The typed struct is the truth; the grammar is
  derived. This is what keeps the CLI, `--script` files, the control socket, the journal and the
  debug log from drifting apart — they are all the same two codecs.
- **R-SVC-6 The host drives the clock.** The service never blocks and never starts a thread on its
  own initiative: `pump(nowMs)` drains completed work and emits events, called from a GTK timeout at
  frame rate, from a CLI loop, or from a test at a fixed 16 ms tick. One code path, deterministic
  when driven deterministically.
- **R-SVC-7 The platform is injected, not imported.** `IImageDecoder` (existing), `IFileStore`,
  `IClock`, `ITaskPool` and `ILogSink` are supplied by the host. `ITaskPool` is what lets the GUI run
  a real pool while a test runs a synchronous one — the same load code, parallel in the app and
  reproducible in `cosmo_core_tests`.
- **R-SVC-8 The running app is controllable, and stays one process.** `cosmo --control <path>` opens
  a socket (unix domain; named pipe on Windows) speaking exactly the R-SVC-5 grammar: commands in,
  events out. The service stays **in-process with whichever front end hosts it** — a preview frame is
  5-20 MB and a full-res frame ~100 MB, so a separate service process would buy a frame transport
  problem and nothing else. An agent can therefore drive the window the user is watching, which is
  the point.
- **R-SVC-9 Coverage is asserted, not hoped for.** A test walks the command enum and fails if a
  behaviour named in `PARITY.md` or in an `R-*` requirement has no command; another asserts that
  `cosmo-cc` and the GUI, given the same commands, produce the same `AppModel` dump (animation
  fields excluded by construction, since the dump is of state, not of presentation).
- **R-SVC-12 The view BINDS to the view-model; it does not remember.** `AppModel` is the
  view-model: a value snapshot with a `revision` that increments on every change. The view reads it
  in **one** place — `App::bindIfStale()`, at the top of `render()`, skipped when the revision has
  not moved — and every displayed value is written from that snapshot. So a change reaches the
  screen whatever caused it: a click, a script, an agent on the control socket, a load finishing,
  an undo. There is no per-event push to wire up and therefore none to forget, which is what the
  previous arrangement required: fifteen UI handlers each remembering to call a sync function, and
  no handler at all for "the edit target moved", so **selecting another image left every panel
  showing the previous one's values** (D-35).
  Two obligations follow. **(a)** The bind is a pure push — it fires no callbacks and reads nothing
  back — because it runs whenever the model moved, including under the user's hands mid-drag.
  **(b)** Anything that changes state must make the view-model reflect it *before* the next bind:
  a `Command` does this by construction, and the call sites that still mutate `EditSession`
  directly must call `CosmoService::refreshFromSession()`. That bridge exists because `App.cpp`
  still holds ~95 direct session calls, a dozen of them mutations; each one that becomes a Command
  deletes a use of it, and the last one deletes the function.
  This completes the MVVM split the rest of R-SVC set up: **model** = `EditSession` + `EditParams`,
  owned by the service · **view-model** = `AppModel` + `Command` in + `Event` out · **view** =
  widgets that render the snapshot and send commands, and now re-read it when it changes.
- **R-SVC-11 The view is inspectable without a screen.** `ui dump [--root <name>] [--json]
  [--visible] [--depth N]` returns the Segment tree as text over the same socket: per node its
  type, its **world** rect, size, visibility, opacity, hover and enabled state. A widget that
  paints itself rather than composing children — the splash, the filmstrip — additionally
  reports one line of its own presentation state (`UiInspectable`), because a leaf's rect says
  nothing about what the leaf drew. Answered by the **front end**, never by the service: the
  Segment tree is presentation and R-SVC-3 forbids the service to know it exists; `cosmo-cc`
  headless answers "no view attached" so one script runs against both. This does not replace a
  rendered frame — a shot says it looks wrong, a dump says which node is wrong — and the two
  together are what make a visual defect diagnosable without a human at a monitor.
- **R-SVC-10 One owner of the CPU budget.** `ThreadBudget`, owned by the service, converts the
  user's percentage **once** and divides that single total between the decode pool and the engine —
  this **amends R-CPU-2**, under which each consumer converted the whole percentage for itself and a
  load peaked at about twice the chosen budget (D-11). Nested library parallelism (R-CPU-2c) is
  pinned by a mechanism that is *checked at runtime* rather than assumed, because the environment
  variable used before was read by nobody (D-12). The load logs the allotment and the **measured
  peak**, and a test asserts the peak never exceeds the total — R-CPU-4's honesty clause becomes an
  assertion instead of a claim.

## R-TEST — The test suites build and can report red on every host they run on — ✅ IMPLEMENTED

Every verification claim in this project rests on "the suite is green", so the suites themselves are
part of the contract, not scaffolding. D-10 established the behaviour clause; D-40 established the
build clause, after the D-10 fix shipped code-verified-only and turned out not to compile on the
Windows host it was written for.

- **R-TEST-1 A failing assertion exits, promptly and non-zero, on every supported host.** The
  assert text reaches a redirected stderr before the process dies, and no path waits for a human to
  dismiss a dialog — an agent or CI run has nobody to click OK, and waiting for that click is
  indistinguishable from a slow suite. A suite that cannot report red is not evidence.
- **R-TEST-2 Shared test infrastructure compiles on every host, and never breaks its includer.**
  A header included by more than one suite is verified by *compiling it there*, on each host, not by
  reading it. In particular it must not leak platform macros into the suite that includes it: a
  system header pulled in for one Win32 call defines `near`, `far`, `small`, `min` and `max`, and a
  test that legitimately names one of those must keep compiling. Platform CRT entry points are used
  only where they are known to link, not merely where they are declared.
