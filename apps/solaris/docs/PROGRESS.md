# Solaris — Progress Ledger

**The only authority on what is done and what is next.** Committed, so work resumes on any machine.
A session reads **NEXT**, does one task, updates this file, and commits — in the same commit.

- Rules: `.claude/skills/arstro.rule` · `.claude/skills/arstro.design.rule`
- Skill: `.claude/skills/arstro.solaris.implement` (the DSP half: `arstro.dsp.implement`)
- Intent: [`../REQUIREMENTS.md`](../REQUIREMENTS.md) · As-built: [`requirements.md`](requirements.md)
  · Format: [`project-format.md`](project-format.md) · Audio: [`../../../docs/audio-format.md`](../../../docs/audio-format.md)
  · Architecture: [`architecture.md`](architecture.md) · Defects: [`DEFECTS.md`](DEFECTS.md)
  · Why it is shaped like this: [`discussion.md`](discussion.md)
- Legend: `[ ]` not started · `[~]` in progress · `[x]` done + verified · `[!]` done but UNVERIFIED

*Last updated: 2026-10-09 — B12: audition.*

---

## NEXT

**► B13 — the MIDI keyboard** (R-EDM-10), then R-EDM-11…20 one task each (B13+ below). B13+ are
Claude's own EDM suggestions from the first brief — check the user still wants them before a long run.
The second brief (C1–C6, 2026-10-09) is built; T2 and T3 are small and may go first.

---

## The build, in order

Each row is one commit (two where a submodule is involved: the submodule first, pushed, then the
umbrella's pointer — `arstro.rule` §7).

### S — specification
- [x] **S0** Second specification (`REQUIREMENTS.md`), `.slp` format, architecture, this ledger, the
      `arstro.solaris.implement` skill; the first specification archived in `docs/history/`.

### D — the sound, in the DSP library (R-DSP, R-INST, R-FX)
- [x] **D1** Primitives: `Biquad` (RBJ) + `ParametricEQ`; `StateVariableFilter` (TPT); `Noise`
      (seeded); `DecayEnvelope` (exponential). `## Math` in each README; unit + integration tests.
      — DSP `1256c64` (REQ-eq-1/2, REQ-svf-1/2, REQ-noise-1, REQ-decay-1); measured against
      formulas recomputed in Python: EQ 0.000 dB, SVF 0.042 dB, decay −298.5 dB/s vs −300.
- [x] **D2** Instruments: `BasicSynth` (2 OSC → SVF → amp ADSR, filter ADSR, unison, noise);
      `DrumMachine` (10 synthesized pads on GM notes, hat choke). — DSP `1fd677f` (REQ-inst-1,
      REQ-synth2-1…5, REQ-drum-1…5); equal temperament to 0.026 cents, kick settles on
      48·2^(tune/12) Hz, choke > 60 dB, every pad sounds and stops, renders byte-identical.
- [x] **D3** `Device` interface + `DeviceRegistry`: synth, drums, compressor, eq, reverb, delay,
      chorus, drive, filter — names, units, ranges, defaults, choices; one factory. — DSP `60a4090`
      (REQ-device-1…4); every parameter of every type at both ends renders finite; a name-written
      parameter matches the RBJ formula to 0.000 dB. DR-DSP-1, DR-INST-1/2, DR-FX-1 written.

### M/E — model and engine
- [x] **M1** `solaris_model`: the `.slp` (parse, serialize fixed point, validate, repair). — DR-FMT-1,
      DR-MIX-4; `solaris_model` 8 tests (fixed point over every node type + unknowns + comments;
      forward-only routing refused naming both ends; 24 structural refusals; 4 repairs counted).
      Device parameters are checked against the registry by the core (V1), not the model.
- [x] **E1** `solaris_engine`: `MixGraph` → devices, strips in mixer order, sends, ports, master;
      sample-accurate notes; pan/fade laws equal to Interstellar's; meters; deterministic.
      — DR-ENG-1, DR-MIX-11; `solaris_engine` 8 tests; its chunking test found a DSP bug (the
      synth's shared noise), fixed in DSP `a16e972`.

### V — the service (rung 1 → 4)
- [x] **V1** `SolarisService` + grammar table + events + `AppModel` + codec; `solaris-cc`; L2 tests.
- [x] **V2** `render` to WAV (master, stems, per port) through the host's writer; audio clips decoded
      through the host's decoder.
- [x] **V3** `api --json|--md` generated, `docs/api.json` + `docs/API.md` committed, drift test.
      — V1–V3 landed as one commit (render needs the host's codecs, the document needs the
      registry and the grammar): DR-SVC-1…3, DR-MIX-2/7/8/9/10, DR-CLIP-2/3, DR-RENDER-1, DR-API-1;
      `solaris_service` 11 L2 tests, `solaris_api_current`; rung 4. End to end with the real CLI:
      drums + synth bass + a sample + a reverb bus, rendered and measured.

### H/P — host and live sound
- [x] **H1** Host: FFmpeg decode, WAV writer (`host/AudioFiles.cpp`); settings + recents files,
      folder listing, device enumeration through PulseAudio (`host/Machine.cpp`). — DR-SET-1,
      DR-HOME-1, DR-BROWSE-1; `solaris_service` 12 tests; on this machine `devices list` finds 2
      outputs (analog, HDMI) and 2 inputs (Brio mic, analog).
- [x] **P1** Real-time playback on one device (the clock): transport, meters, param edits through
      lock-free queues, structural edits by graph swap. — DR-PLAY-1/2/3; live = offline sample for
      sample; edits heard; the audio path allocates nothing (counted); DSP `317bed2`. Plays on this
      machine through PulseAudio.
- [ ] **P2** Several devices: followers through an adaptive resampler (a DSP module), latency
      compensation, per-device drift/xrun/latency in the model.

### U — the UI (cosmo + interstellar family, teal accent)
- [x] **U1** Home + Settings (devices, rate, buffer, folders, port map). — DR-UI-1, DR-SET-2,
      DR-UI-6; `solaris_app_shots` (9 states × 2 sizes, looked at) and `solaris_app_ui` (5 tests,
      every transition caught mid-tween) over the REAL service; the window runs (`solaris`). The port
      map is a `settings set port.<name>=…` line today; its chips come with P2's several devices.
- [x] **U2** Project view: browser, lanes + clips, transport. — DR-UI-3, DR-BROWSE-2, DR-UI-7;
      `clip add --instrument` makes every drop one command (R-BROWSE-3); D-1 found and closed (a
      refused edit announced changes). `solaris_service` 17 tests, `solaris_app_ui` 9 (a tab's
      cross-fade, a clip arriving, moved and removed from a shell — each caught mid-tween; two
      mutants that snap fail it), `solaris_app_shots` 14 states × 2 sizes, looked at. The version
      chip waits for X1.
- [x] **U3** Mixer dock: a tab per mixer, strips (fader, pan, M/S, meter, rack chips, sends), the
      master, the Matrix tab; device panels generated from the registry. — DR-UI-8, DR-UI-5,
      DR-MIX-4 (`targetsOf`, `strips[].targets`); R-MIX-12 amended (groups start open). Cosmo's
      `ContextMenu` and `SliderRow` reused, `SliderRow` with an opt-in formatter. `solaris_service`
      17, `solaris_app_ui` 11 (a fader follows the pointer, a shell's gain travels — a 0 ms mutant
      fails it —, mute eases, the route menu is the service's list, tabs slide, a fold and the dock
      ease, + Effect, a generated panel, a matrix send), `solaris_app_shots` 21 × 2, looked at; the
      window runs. Waveforms and the meters' peak hold wait for U4/P2.
- [ ] **U4** Linked selection; Arrange by Channel; drag and drop onto the dock (an effect onto a
      rack, a sample onto a strip); the audit panel; waveforms (a peaks hook).
- [ ] **U5** Note editing: piano roll for the synth, step grid for the drum machine (views of a
      pattern).

### B — the 2026-10-09 brief (user: *"no need discussion, implement all needed features"*)
- [x] **B0** Requirements: R-AUTO, R-WIN, R-ROLL, R-VST, R-EDM written; R-INST-4, R-MIX-4 (sidechain),
      R-TIME-4, R-UI-3, R-UI-5, R-SET-3 amended; this phase.
- [x] **B1** Formulas + automation core. — DR-AUTO-1…4; `#aauto`/`#point`/`#abind`; the formula
      language; bindings ordered, loops refused, inert ones audited; evaluated every 64 samples at
      absolute positions; `auto create|add|delete|point …`, `bind clear`, `eval --explain`. Measured:
      a curve renders −5 / −15 dB at beats 2 / 6 within 0.1 dB; byte-identical at chunks 77/128/1000;
      no allocation. Mutants: no control split → chunking test red; no `checkBinding` → refusal test
      red. A silent refusal is now impossible (`dispatch` names it).
- [x] **B2** Undo / redo (R-EDM-1). — DR-EDM-1; byte-exact both ways; a dragged fader is one step
      (mutant: no merging → the depth assertion fails); Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y.
- [x] **B3** The song bar: Settings beside Home, cosmo's menu strip (File / Edit / Song / View), the
      settings sheet's full sections (R-UI-3, R-SET-3). — DR-UI-9, DR-SET-3; new machine keys
      metronome, metronomeLevel, newBpm, newSig, reducedMotion; View folds the dock and the browser,
      eased; `solaris_app_ui` 12; shot menu-file-open, looked at.
- [x] **B4** Windows: the device window, its parameter list with the last change lit, the parameter
      menu — automation, formula, clear, reset (R-WIN-1…4). — DR-WIN-1, DR-UI-5 rewritten; the dock's
      popover panel replaced; `solaris_app_ui` 13 (the light caught mid-ease — a mutant that snaps
      fails it; a typed formula through cosmo's field); shot device-window-bound, looked at.
- [x] **B5** Automation on the timeline (R-AUTO-6). — DR-AUTO-5; `solaris_app_ui` 14 (a dragged
      point drawn under the pointer, one move on release, nothing jumps; a shell's edit eases — a
      mutant that snaps fails it); shot automation-rows, looked at.
- [x] **B6** The piano roll window: `note move`, velocity, snap, step mode, quantize + swing
      (R-ROLL-1…5, R-EDM-6). — DR-ROLL-1; DSP REQ-device-6 (the registry names a kit's keys);
      `solaris_service` 21 (move, resize, re-velocity, replace on landing, quantize + swing, refusals);
      `solaris_app_ui` 16 (a note fading in and out, a drag drawn under the pointer and one move, the
      modes cross-fading — mutants that snap the fade or the mode fail them); shots piano-roll,
      piano-roll-note-in, step-mode, looked at.
- [x] **B7** The mixer: "+ Line", relink — `strip relink`, "Play through" (R-MIX-13/14). — DR-MIX-13;
      `solaris_service` 22 (relink moves every clip, one undo; refused onto a bus, across kinds, onto
      itself, with nothing to move); `solaris_app_ui` 17 (the new card growing in and "+ Line" caught
      sliding — a mutant placing it from target widths fails it); shots mixer-add-line,
      clip-play-through, looked at. Every DR anchor re-checked with a script; 40-odd that had drifted
      since U1 re-pointed.
- [x] **B8** VST3: the SDK, Basic Synth + Drum Machine as plugins, the shared normalisation, validator,
      plugin = device sample for sample (R-VST-1…5). — DR-VST-1; DSP REQ-device-7, REQ-vst-1…5
      (`4ab11ad`); SDK v3.8.1 (MIT) at ~/sdk/vst3sdk, built with our CMake; validator 47/47 on both;
      `vst3_equivalence` 0 of 96 000 differ (a wrapper ignoring note offsets fails it); installed to
      ~/.vst3. Solaris's parameter text is now the DSP library's.
- [x] **B9** EDM sound: sidechain (DSP compressor + `send add --sidechain`), the limiter, the
      metronome (R-MIX-15, R-EDM-2/3/4). — DR-MIX-15, DR-EDM-2, DR-EDM-4; DSP REQ-fx-sidechain-1,
      REQ-fx-limiter-1 (`c31bc9f`: never above the ceiling BY CONSTRUCTION, the clamp idle — 3 mutants
      caught); `solaris_service` 24 (the pump measured 6–22.5 dB and back; soloed still pumps — a mutant
      found that a key dragged its source INTO a solo, fixed; the metronome on each beat's sample, never
      in a render); `solaris_app_ui` 18; shot mixer-sidechain.
- [x] **B10** The loop region on the ruler (R-EDM-7). — DR-EDM-7; `solaris_app_ui` 19 (the brace fades in
      and moves eased after a shell's loop — a mutant that snaps it fails; Shift-drag is the pointer's
      and one line; a click inside clears it, fading); shots loop-region, loop-dragging.
- [x] **B11** A sampler (R-EDM-8). — DR-EDM-8; DSP REQ-inst-sampler-1 (`1d944b6`: at the root the recording
      itself; an octave up 880 Hz in half the time; a fifth down within 2e-3 — a nearest-frame mutant
      fails it; the 220 Hz recording at ±semitones, measured); `solaris_service` 25 (the file through the
      whole chain, sample for sample — a mutant skipping `setSample` fails it); `solaris_app_ui` 20 (a
      sample dropped on the sampler's window: lit, eased, one line); shot sampler-window.
- [x] **B12** Audition in the browser (R-EDM-9). — DR-EDM-9; `solaris_service` 26 (the live stream IS the file at
      the audition level, sample for sample; it ends itself, said); `solaris_app_ui` 21 (the row's fill
      eases in and out — a mutant that snaps it fails); shot browser-audition; the UI rig now has a
      silent output device that keeps time, and pumps the service every frame as the window does.
- [x] **C1** The grid follows the zoom; the ruler seeks on it (R-UI-10, R-TIME-5). — DR-UI-10 (`780bde9`, a
      sub-agent): seven levels, bar → 1/32 beat, each fading in with its room from the EASED zoom; one
      `snap()` for ruler, clip drag, loop, automation points and browser drops; zoom 4.7–637 px/beat,
      the deepest unsnapped; `solaris_app_ui` 22 (3 mutants caught); the lines' strength raised at
      merge so the divisions read.
- [x] **C2** Bezier automation; the automation's window (R-AUTO-10/11). — DR-AUTO-6/7 (`b5a102a`, a sub-agent):
      `#point shape=bezier speedIn inflIn speedOut inflOut` — Interstellar's keyframes, its `Anim.h` included
      in the engine (allocation-free); smooth = AE's ease, equal to the old smoothstep to 1e-9; a bezier
      gain renders within 0.019 dB of the formula; cosmo's handles (Alt-pull, mirror, Alt-break); the
      `auto:<au>` window with the value at the playhead (`automations[].now`); 4 mutants caught.
      Merged over C1/C3: docs merged section by section, anchors followed by source text.
- [x] **C3** The mixer's numbers bound from the dock; IDs shown and copied (R-MIX-16, R-UI-11). — DR-MIX-16,
      DR-UI-11 (`76fb935`, a sub-agent): one `ParamMenu` for device rows and the dock's fader/pan/send/master;
      `bindings[].live` from the audio thread through pre-sized atomics (no allocation, counted); a
      `showIds` setting, ids eased in everywhere; Copy Address/Value/as Formula through the host's
      clipboard; 5 mutants caught; merged over C1 with its anchors followed by source text.
- [x] **C4a** Agent-sized composition: shell, scripts, ids, bulk notes, arrangement, readback, honest values (R-SVC-8).
      — DR-SVC-8 (`3055cea`, a sub-agent): `solaris-cc shell`, `line N:`, exit 4 on dropped edits;
      `made:` lines + announced nodes; `notes add` notation, pitch names and kit pads, `--chord`,
      `pattern steps|duplicate|clear|delete|transpose`, `clip duplicate --count`, lane reuse; `ls`, `show`,
      `pattern print`, compact JSON; out-of-range values refused; `docs/AGENTS.md`. An 8-bar song: 32
      commands (the audit's 16 bars took 110). 7 mutants caught.
- [x] **C4b** The faces: control channel + attach, equivalence test, web (NTWB) (R-SVC-5…7).
      — DR-SVC-5/7 (`596c04a`, a sub-agent): `solaris --control <socket>` over cosmo's `ControlChannel`
      compiled in place (`[out]`/`[ok]`/`[refused]` per line, `wait` holds the queue, never the GTK
      thread); `solaris-cc attach` prints what `solaris-cc` prints; `solaris_equivalence` runs one
      script headless and through a live window — same events, output, dump, `.slp`, WAV (skipped
      without a display); `solaris-cc ntwb serve|install|uninstall|api` and `apps/solaris/web/` (song bar,
      Home, lanes, mixer, console; tokens generated from Theme.h). Rung 5. 3 mutants caught. Open: the web
      re-derives the fader/meter maths (`MixerDock.cpp:89-90`) — the service should publish it; the web
      not yet run inside a real Arstro Remote.
- [x] **C5** A whole song made by an agent with `solaris-cc`, measured (R-SVC-9).
      — DR-SVC-9: `demo/canon/` — Pachelbel's Canon as 128 bpm EDM, 86 commands, 7 instruments, 2 buses,
      formula delay, kick-keyed sidechains, bezier filter automation, limiter; `solaris_demo_canon`
      regenerates the script, runs it and measures the render (kick within 1 sample on 64 beats, line A
      within 8.4 cents, peak −0.80 dBFS). 2 mutants caught. Found D-2, D-3.
- [x] **C6** The audit's fixes (R-MIX-17 latency, R-MIX-4 amended, strip order, matrix, recents, lanes, docs).
      — DR-MIX-17, DR-MIX-4, DR-LANE-1 (`ee7eb4c`, a sub-agent; DSP `b611e4a` REQ-device-8
      `Device::latency()`): the engine delays every path to meet the latest at each strip, bus, key and
      port, sized at build; the render trims `outputLatency()`; the player's position subtracts it.
      `feedsForward` = later in processing order for outputs, sends and keys (a new bus on Buses goes
      before Main and feeds it); `strip move --order` renumbers; `matrix.columns` published and drawn;
      recents absolute; `lane move`; Song › Quantize Clip; `render --out` required in the grammar;
      `--stems all`; docs made honest. 8 mutants caught. Merged over C2–C5: two tests updated to the
      amended rules (a new bus feeds Main; recents name their own folder). The canon re-rendered: the
      mix was 96 samples behind its kick stem, now 0.
- [ ] **B13+** R-EDM-10…20, one task each, in that order unless the user reorders.

### T — tasks found on the way
- [ ] **T1** Promote Interstellar's `TextFit`, `EasedScroll`, `Glyphs`, `AnimatedRows`, `FadePage` into Artboard (via
      `implement_artboard`); both apps include them from there.
- [ ] **T2** The service publishes a fader's position law and a meter's scale in `AppModel`, so the web
      face stops re-deriving `MixerDock.cpp:89-90` (law 13; found merging C4b).
- [ ] **T3** D-2 (a renamed strip's lane and pattern keep the old name) and D-3 (a kit's pad names are
      not its parameter names) — found making the canon (C5).

### X — later (specified, not scheduled)
- [ ] **X1** Versions (R-VER) · [ ] **X2** Recording (R-REC) · [ ] **X3** Automation (R-AUTO)
- [ ] **X4** Equivalence test + control socket (rung 5)

---

## Decisions log (newest first)

- **2026-10-09 — the second brief, decided without a round (the user asked to build):**
  - bezier automation stores Interstellar's keyframe model (speed + influence per side) — one curve
    model in the suite — and is EDITED with cosmo's handles (Alt-drag pulls them, Alt breaks symmetry);
  - a double-click on a point still deletes it (R-AUTO-6); on the row's header or empty curve it opens
    the automation's window;
  - the zoom's grid IS the snap step on the lanes, and the deepest zoom does not snap;
  - "copy" goes through a host clipboard hook — the core has no clipboard;
  - "web" is NTWB through Arstro Remote, as cosmo's, not a web server of Solaris's own;
  - the song an agent makes is a public-domain tune, so it can be shared.

- **2026-10-09 — a solo-silenced strip still keys; a muted one does not; a key brings nothing into a
  solo.** A soloed bass must keep its pump (the EDM reason to sidechain at all) without the kick
  becoming audible; a mute is the user saying "this is off". The metronome lives in the Player, after
  the engine — the one place that is heard and never rendered.

- **2026-10-09 — the VST3 SDK is built from its sources by our CMake**, not through its own: v3.8.1 (the
  first MIT release) needs CMake 3.25 and this machine has 3.22; an older SDK would build but is not MIT.
  The source lists are the SDK's own, so an SDK update is a list check, not a rewrite. No warm-up block
  in the plugin: a mutant without it still matched, because the instruments have no parameter ramp —
  so it was dead code claiming a job; the reference keeps Solaris's warm-up and would catch a ramp.

- **2026-10-09 — the piano roll's Steps mode zooms to fit.** A sixteenth at the Notes zoom is 12 px —
  a cell nobody hits. Steps fits the pattern to the window (64–160 px a beat), eased, and Notes keeps
  its own zoom; Ctrl+wheel does nothing in Steps. A melodic pattern's Steps rows are the pitches it
  uses plus middle C (a kit's are its pads).

- **2026-10-09 — the brief is built without a discussion round**, on the user's word. Its open
  choices were taken as written in R-AUTO/R-WIN/R-ROLL/R-VST/R-EDM: a binding is a formula (a link is a
  formula naming an address); automations are ids `au_n` (the user's `=automation1` is `=au_1`); windows
  float INSIDE the song view as FL Studio's do; our plugins come before hosting; a sidechain key may
  come from an earlier strip on the same mixer.

- **2026-10-08 — the fader law is gain ∝ position², +6 dB at the top** (0 dB at 70 %, −24 dB at
  a quarter): the usual console taper, so the useful range takes most of the travel. A value set
  above +6 from a shell sits at the top.
- **2026-10-08 — a fold group starts open** (R-MIX-12 amended): a new song's few strips must not
  hide behind a click; the fold is for many one-shots, and it is the view's, like zoom.
- **2026-10-08 — cosmo's `SliderRow` grew an opt-in formatter**, rather than Solaris forking a
  slider: one more hook on the shared widget, cosmo's rows unchanged (its tests pass).

- **2026-10-08 — a strip's default colour comes from its id, not its place** (R-UI-7): by place,
  deleting one strip recoloured every later one's clips in one frame; and the mixer (U3) must agree
  with the lanes, so the service resolves it.
- **2026-10-08 — the browser's tabs are Samples · Instruments · Song** (R-BROWSE-1 amended): named
  for what is in them. Presets join the Instruments tab when a preset store exists.
- **2026-10-08 — an instrument drop is `clip add --instrument`**, not `strip add` then `clip add`:
  R-BROWSE-3 says every drop is one command, and two lines can half-land.

- **2026-10-08 — the accent is `#159387`, not `#5AEDDE`** (R-UI-2 amended): the hue-rotation recipe
  keeps HSL lightness, and teal at cosmo's lightness is three times as luminous — white on it is
  1.4:1. Matched to cosmo blue's luminance instead (white on it 3.8:1).

- **2026-10-08 — `render --out` is the MASTER BUS** (after its rack and gain), not "whatever reaches
  the Main port": a strip routed straight to a port (a headphone cue) is not part of the mixdown.
  `--ports` writes what each port receives.
- **2026-10-08 — a new clip with no `--lane` gets a new lane** (audio and notes alike), the CLI form
  of "dropped into empty space" (R-BROWSE-3). `--lane` puts it on an existing one.
- **2026-10-08 — a new source strip feeds the first bus on a LATER mixer** (that is "Main" in a new
  song); with none, the master. No header field names the default — renaming Main keeps it working.
- **2026-10-08 — Solaris has its own small `Json`**, the same shape as Interstellar's: that one lives
  in `interstellar_core`, which hosts all of Cosmo; a shared utility library is an Interstellar
  refactor, out of a Solaris task's scope.

- **2026-10-08 — the engine warms every device with a block of silence.** The DSP library smooths
  each parameter write over a block (`SignalProcessor`), so a freshly built device ramps from its
  default to the project's value over the first 128 samples of every render. Warming finishes the
  ramp before time zero. The alternative — a "snap" API in the library — would have touched every
  `SignalProcessor`; this touches none.
- **2026-10-08 — a muted strip sends nothing, pre-fader sends included.** Simplest rule a user can
  predict; revisit if a cue-mix workflow needs pre-fader sends to survive mute.

- **2026-10-08 — the open questions of the discussion, closed with the recommended defaults** when
  the user said "implement": one clip → one strip (Q-MIX-1); dragging in Arrange-by-Channel
  re-routes (Q-MIX-2); the mixer docks under the timeline (Q-MIX-3); a sample file gets its own
  strip (Q-MIX-4, the user's); routing is forward-only (Q-MIX-5); a new strip feeds "Main" on Mixer 2
  (Q-MIX-6); PulseAudio API first, Solaris's own drift correction (Q-DEV-1 — this machine runs real
  PulseAudio, not PipeWire, so PipeWire's adaptive followers cannot be relied on); versions are
  Interstellar's R-VER (§3); accent teal `#5AEDDE` (Q-LOOK-1); the touch brief is retired. Each is
  one line to change if the user disagrees.
- **2026-10-08 — parameters in engineering units, not 0..1** (R-DSP-3, changes SR-PARAM-2).
- **2026-10-08 — media by relative path, content hash deferred** (R-FMT-3, changes SR-FMT-3).
