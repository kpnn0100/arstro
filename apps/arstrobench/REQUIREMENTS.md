# Arstrobench — Requirements

The single shared source of truth for Arstrobench behaviour. **Every agent must read this
before implementing, and check any new/changed requirement here for conflict before writing
code** (mirrors the `implement_artboard` V-model, step 1).

Arstrobench is an *application* built on the Artboard library, the `arstro_image`
ImageProcessing engine and the `arstro_dsp` DigitalSignalProcessing library. Artboard keeps
its own `core/Artboard/docs/`; this file plus [`docs/`](docs/) is Arstrobench's doc surface.
Requirements are numbered `R-<area>-<n>`.

## 1. Purpose

Arstrobench answers one question: **can this computer run cosmo comfortably?** It does that by
timing the two engines cosmo and its sibling apps actually depend on — the image pipeline
(`arstro_image`) and the signal pipeline (`arstro_dsp`) — and turning each measured time into a
score. It reports the two scores alongside the machine they were measured on (chip, RAM,
operating system) so two results can be compared meaningfully.

## Global rules (apply to every requirement)

- **R-G-1 Everything animates, nothing snaps.** No component may suddenly change size, appear,
  disappear, move, recolour or reflow in a single frame. Every visible property change goes
  through an animation primitive (`artboard::Property` / `AnimatedProperty` / `Spring`), never by
  direct assignment of the visible value. Collapses to the final state only under
  `artboard::reducedMotion()`. (Same rule as cosmo's R-G-1 — one motion language across Arstro
  desktop apps.)
- **R-G-2 cosmo is the design system.** Arstrobench uses cosmo's design tokens *verbatim* — the
  same palette, radius scale, type ramp and fonts — by including `apps/cosmo/Theme.h` and
  compiling `apps/cosmo/Theme.cpp` into this app rather than copying hex literals. A token
  re-declared here would be a second source of truth and is a bug. Bench-specific semantics
  (the per-workload accent) are *derived* from those tokens, never invented.
- **R-G-3 Everything interactive hovers.** Every clickable region shows an animated hover
  treatment driven by the framework's eased `hoverAmount()` — never a hard flip.
- **R-G-4 The measurement is never blocked by the UI, and the UI is never blocked by the
  measurement.** The workloads run on a worker thread; the render loop keeps animating at frame
  rate while a run is in flight.

## R-SCORE — Scoring model

- **R-SCORE-1 Score is the reciprocal of the measured time.** For each workload,
  `score = 1 / seconds`, where `seconds` is the wall-clock duration of the *processing* phase
  measured with `std::chrono::steady_clock`. A faster machine scores higher; the unit is
  "workloads per second".
- **R-SCORE-2 Generation is never timed.** Building the workload's input (the dummy image, the
  synthesised audio) happens before the clock starts. Only the filter/effect processing is
  measured. This is stated explicitly for the DSP workload in R-DSP-3 and holds identically for
  the image workload (R-IMG-3).
- **R-SCORE-3 The reported time is the fastest of N passes.** Each workload runs its timed phase
  `N` times and reports the *shortest* duration (and therefore the highest score). Standard
  benchmark practice: the minimum is the pass least polluted by unrelated system activity, so
  repeat runs on an idle machine are stable. `N` is **3** for both workloads. What is measured is
  still exactly one pass of the specified workload.
- **R-SCORE-4 The workloads are fixed.** Sizes, filter sets, voice counts, note sets and sample
  counts are compile-time constants (R-IMG-1/2, R-DSP-1/2). Two machines are only comparable if
  they ran identical work, so nothing about the workload is user-adjustable or auto-scaled to
  the host.
- **R-SCORE-5 Total score.** The headline `ARSTROBENCH SCORE` is the **sum** of the two workload
  scores. It is labelled as such in the UI (`image score + signal score`); no hidden weighting.
- **R-SCORE-5a The two workloads are sized to weigh the same.** The signal workload's sample
  count and chain depth (R-DSP-1/2) are chosen so that on a reference desktop its pass duration
  lands within ~20% of the image workload's CPU pass — so the sum in R-SCORE-5 is a genuine
  blend rather than one score wearing the other as noise. **The balance is calibrated, not
  guaranteed:** the image pipeline parallelises across every core while an audio chain is a
  single thread by nature (R-DSP-5), so on a machine with far more or far fewer cores than the
  reference the two will drift apart. That drift is itself information — it says which of the two
  this computer is better at — and the per-workload scores beside the total are what to compare
  between machines.
- **R-SCORE-6 Every timed phase produces a checksum.** Each workload folds its output into a
  checksum that the caller reads. This keeps an optimiser from eliding the measured work and
  gives the unit tests a determinism handle.

## R-IMG — ImageProcessing workload

- **R-IMG-1 Dummy image.** A deterministic, synthetic 1920 × 1080 RGBA8 image generated in code
  (no file I/O, no test fixture on disk): a two-axis colour ramp, a radial luminance falloff, a
  high-frequency checker and a hash-based per-pixel noise term. The content is deliberately
  broadband — a flat or purely smooth image would let the spatial filters (noise reduction,
  clarity, texture, sharpen) do unrepresentatively little work.
- **R-IMG-2 The filter set is cosmo's own pipeline.** The image is rendered through
  `arstro::EditEngine::renderImage()` with an `EditParams` in which **every** adjustment stage is
  engaged at a non-neutral value: lens correction (distortion/CA/vignette), noise reduction,
  exposure, contrast, tone regions (highlights/shadows/whites/blacks), white balance, tone curve,
  texture, clarity, vibrance/saturation, colour mixer, colour grading, dehaze, sharpen and grain.
  This is the exact pipeline cosmo runs on every edit, which is what makes the score predictive of
  cosmo's responsiveness rather than of a synthetic filter chain.
- **R-IMG-2a Geometry stays 1:1.** Crop is full-frame and rotation is zero, so the output is the
  same size as the input and the measured cost does not shift with a resampling factor.
  `maxEdge` is passed larger than the image so `renderImage` performs no downscale.
- **R-IMG-3 Generation and decode are outside the clock.** Generating the RGBA8 bytes and
  converting them to the engine's linear working space (`EditEngine::fromEncodedBytes`) happen
  before timing starts. The timed region is exactly the `renderImage()` call.
- **R-IMG-4 CPU by default; the GPU is an explicit opt-in.** The workload runs `setPreferGpu(false)`
  unless the user turns the GPU toggle on (R-UI-9). The CPU pipeline is `arstro_image`'s reference
  path and the one path guaranteed to exist on every machine, so timing whichever backend happened
  to be available would make scores incomparable. **The toggle changes only the backend — never
  the work** (same image, same 15 stages, same passes), which is what makes the GPU-on and GPU-off
  scores directly comparable to each other.
- **R-IMG-4a The card reports the backend that actually ran, not the one requested.** An available
  accelerator may still **decline** a job it does not implement (`IComputeBackend::process`
  returning false), in which case the CPU reference path produced the pixels. Reporting the
  requested backend would be a lie the user cannot detect, so the result reads one of: `CPU`,
  `GPU (<name>)`, `CPU (GPU declined this edit)`, or `CPU (no GPU backend)`. This needs
  `EditEngine::lastRenderAccelerated()` — a read-only accessor added to `arstro_image` for exactly
  this, since `activeBackendName()` states intent and cannot state outcome.
  **Known state of the world, recorded rather than discovered later:** the current OpenGL backend
  (`glcompute::computeSupports`) accepts only exposure / contrast / temperature / tint and requires
  every other stage to be at identity, so against this workload's full 15-stage pipeline it
  declines on every machine and the honest label is `CPU (GPU declined this edit)`. The toggle
  will start reporting a real GPU score the day that ported subset grows; it is wired to the
  engine's real setting, not to a mock.

## R-DSP — DigitalSignalProcessing workload

- **R-DSP-1 Nine voices, multiple notes, rendered per voice.** The synth is a bank of **9**
  `arstro::Voice` objects (the library's own `VoiceManager` caps at 8, so the bench owns its
  polyphony) sounding **9 distinct MIDI notes simultaneously** — an extended chord spread over
  three octaves, with per-voice velocity, waveform and detune variation so no two voices produce
  the same signal. Generation writes **one buffer per voice per channel**, not one summed buffer:
  the mix processes each voice through its own channel strip before the bus, and summing first
  would throw away the thing being measured.
- **R-DSP-2 192000 samples through a three-tier mix.** The timed chain is a real mix, not a token
  comp/EQ/reverb triple, applied to **192000 samples per channel** at 48 kHz (four seconds of
  audio) across the configured channel count:
  - **Per voice (x9):** 2nd-order rumble cut → `Compressor` → 3-band EQ (low shelf, mid peak,
    high shelf, each a filtered copy summed back at a gain) → `Overdrive` saturation. Each strip
    is tilted differently across the nine voices, so it is nine different jobs, not one nine times.
  - **Master bus:** a **4-band multiband compressor** (2nd-order crossovers at 200/1200/5000 Hz,
    per-band threshold/ratio/attack/release and saturation, summed in fixed band order) → a
    **6-section parametric EQ** (high-pass, low shelf, two peaking bands, high shelf, low-pass) →
    a **reverb send** (a short dense early-reflection `Reverb` feeding a long tail `Reverb`, the
    wet path damped and widened by a `Chorus`, then mixed back) → a bus glue `Compressor`.
  Every stage is **composed from `arstro_dsp`'s own primitives**; the library is not modified to
  suit a benchmark. `MixChain::stageCount()` is the number the UI reports, read from the chain
  itself so the card cannot drift from what runs.
- **R-DSP-3 Generation is not timed.** The 9 voices are rendered into their buffers *before* the
  clock starts; only the mix is measured. (This is the user-stated contract and the reason
  R-SCORE-2 exists.)
- **R-DSP-4 Fresh state per pass, and no allocation inside the clock.** Each of the N passes
  builds a fresh `MixChain` and processes a fresh copy of the generated signal, so a reverb tail
  from a previous pass cannot alter the next one's work or checksum. The `MixChain` constructor
  pre-allocates every scratch and band buffer, so the timed region measures signal processing
  rather than the allocator.
- **R-DSP-5 Single-threaded, deliberately.** The chain runs on one thread because a real audio
  thread is one core — so this score measures single-core throughput while the image score
  measures the whole machine. The two are complementary facts about the computer, not a
  redundant pair; see R-SCORE-5a.

## R-SYS — System report

- **R-SYS-1 The result names the machine.** Alongside the scores the UI reports **chip** (CPU
  model string plus the hardware thread count), **memory** (total physical RAM) and **operating
  system** (a human-readable name plus kernel/build version).
- **R-SYS-2 Both hosts.** The query is implemented for Linux (`/proc/cpuinfo`, `/proc/meminfo`,
  `/etc/os-release`, `uname`) and Windows (the `CentralProcessor` and `Windows NT\CurrentVersion`
  registry keys, `GlobalMemoryStatusEx`). A field that cannot be determined reads `Unknown`
  rather than being blank or omitted — a missing row would look like a rendering bug.
- **R-SYS-3 No shelling out.** System facts are read through the platform's own API/filesystem,
  never by spawning `lscpu`, `wmic` or similar. A benchmark must not depend on tools that may be
  absent or slow.

## R-UI — The window

- **R-UI-1 Fixed 1000 × 700 window, non-resizable.** The layout is a fixed composition sized to
  its content, so no panel can ever overflow its box and nothing needs to scroll (the
  `implement_artboard` overflow rule is satisfied structurally rather than by adding scrolling
  that could never engage). Deliberate and documented: if the window ever becomes resizable, every
  panel in it needs the clip-and-scroll treatment first.
- **R-UI-2 Screen composition.** Top to bottom: a header (wordmark + Run button), two
  side-by-side workload cards (Image Processing, Signal Processing), the total-score card, the
  system panel, and a one-line methodology footer.
- **R-UI-3 Workload card.** Each card shows its title, a status chip (Idle / Running / Done), the
  score as a large mono numeral, the measured time, a meter, and a one-line description of the
  work performed. Idle shows an em-dash rather than a zero — a zero score is a claim, absence is
  not.
- **R-UI-4 Entrance animation.** On launch the cards fade and rise into place, staggered by
  index, via `opacity` and `y` Properties (R-G-1).
- **R-UI-5 Scores count up, and the numeral cross-fades in.** When a result lands the em-dash
  does not become a digit in one frame: the numeral fades out (~90 ms), the value swaps at the
  trough, and the count-up runs from there to the new value over ~700 ms with
  `Easing::EaseOutCubic` while the numeral fades back in. The count-up starting at the trough is
  what stops the number being seen resting at zero. Nothing here is ever assigned directly.
- **R-UI-6 The meter shows the state.** While a workload is running its meter is an
  `artboard::ProgressIndicator` in indeterminate mode (a sweeping bar — the workload cannot report
  fractional progress, so a determinate bar would be a lie); when the result lands it becomes
  determinate and springs to full.
- **R-UI-7 The Run button.** One primary accented button. It shows the framework's eased hover
  treatment (R-G-3) and an eased press wash, is disabled (and eases to the disabled look) while a
  run is in flight, and reads `Run benchmark` / `Running…` / `Run again`.
- **R-UI-8 Status is coloured by meaning.** Idle uses the muted foreground, Running the single
  accent, Done the success green — all cross-faded through an animated Property, never switched.
- **R-UI-9 The GPU toggle.** A captioned `artboard::ToggleSwitch` in the header, snapped to the
  left of the Run button, opts the image workload into the accelerator (R-IMG-4). It is:
  **off by default**; **disabled and captioned `GPU unavailable`** when the machine has no GPU
  backend at all, rather than offering a switch that cannot do anything; and **locked for the
  duration of a run**, so the backend cannot change underneath the score about to appear. Flipping
  it restates the image card's detail line immediately, so what the next run will measure is
  visible before it starts. Both the caption's brightness and the disabled state fade (R-G-1).

## R-TEST — Verification

- **R-TEST-1 Device-free tests.** `arstrobench_tests` verifies the score maths, both workloads
  (at reduced sizes so the suite stays fast), the system query, and the UI through Artboard's
  `RecordingTarget` — no window, no GTK, no display.
- **R-TEST-2 The UI assertions are the framework's own.** Card rects must not intersect (layout
  snaps, does not stack), the op stream at t=0 must differ from t=end (motion is real, not
  claimed), and `reducedMotion()` must collapse it.
- **R-TEST-3 Headless shot.** `arstrobench_shots` renders the real app through the real Cairo
  adapter onto an image surface and writes a PNG, so the UI can be *seen* without a display.
