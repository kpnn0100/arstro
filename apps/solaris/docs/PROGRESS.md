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

*Last updated: 2026-10-16 — N3: the DSP library's meters.*

---

## NEXT

**► N41 part 1 — the merge in the model** (R-COLLAB-1; Round 19 is the design), then N42… in order (the user: *"implement all"*). Owed live checks: D-5, D-9, the resize cursor, N40's video window on a real file, N4's meters in the GTK window, N8's PipeWire playback on a PipeWire server, N16's recording through a real input, N28 against a real surface, N32 against a second real card. B13+ wait (ask).

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
- [x] **E1** The third brief's requirements (R-TIME-6, R-LANE-3, R-CLIP-6…8, R-BROWSE-4, R-VST-7/8; discussion Round 5).
- [x] **E2** The ruler dragged (R-TIME-6).
      — DR-TIME-6: a press waits for click-or-drag; a drag scrubs (the playhead the pointer's, a seek at each
      grid line, playing or not) or, from the brace's lower half, moves or resizes the loop with one
      `transport loop` on release. 3 mutants caught; shots `ruler-scrubbing`, `loop-brace-moving`.
- [x] **E3** Instrument tracks (R-LANE-3; R-LANE-1 amended).
      — DR-LANE-3: `#alane … track=<ch>` + header `tracks = on`; validated; a lane made for an instrument is its
      track, showing its name (D-2's lane half); `clip add --lane <track>` implies the strip; `clip move` onto a
      track re-routes in one edit and says so; audio refused there; `lane add --strip`, `<lane>.strip`; relink
      and delete carry tracks; older songs adopted on opening. The view: stripe in the strip's colour, the
      instrument named under it (fading), the lane header's Track of ▸ / Plain Lane / Copy ID; a browser drop
      onto a track → `--lane new`. 5 mutants caught (3 service, 2 UI).
- [x] **E4** MIDI clips made, looped and moved on the lanes; the song's MIDI list (R-CLIP-6…8, R-BROWSE-4).
      — DR-CLIP-6, DR-BROWSE-4: a double-click (or New MIDI Clip) on a track makes a one-bar clip there; a
      clip's right edge sets its length (one `set .length` on release; lengths now ease); the Song tab lists
      the MIDI first — New MIDI, each pattern's length, notes and players — double-click → its roll, a menu,
      dragged onto a track / a plain lane / below. Model: `patterns[].strips`, `.lastStrip`. 3 mutants caught;
      shots `midi-list`, `clip-end-dragging`.
- [x] **E5** The plugin editor in the Arstro look; Basic Synth's (R-VST-7/8).
      — DR-VST-7 (DSP `e53edb6` REQ-vst-6, the seam): the plugins' editor is Solaris's DevicePanel over a
      one-device model built by the shared `deviceModelOf` (the service uses it too), song controls off,
      the instrument titled with the arstro mark; its lines → beginEdit once / performEdit per step (the
      shared mapping) / endEdit at release; a host change springs in and is lit; an XEmbed X11 view on the
      host's run loop, resizable. Validator passes; L5 in the SDK's editor host (a drag moved Kick Decay
      450 → 29.9 ms). 4 mutants caught.
- [x] **E6** Drum Machine's editor with its pads (R-VST-7/8).
      — DR-VST-7 (DSP `9634166`: REQ-device-9 `notePrefixes`, REQ-vst-7 `playNote` → `arstro.note` → a lock-free
      queue in the processor): ten pads named by the registry in the panel's band; a press plays the pad
      through the processor (heard end to end in process: peak 0.358) and picks it, the panel scrolling its
      group to the top; pick and flash ease. 5 mutants caught (4 umbrella, 1 DSP). L5 in the editor host.
- [x] **G1** The analog face's requirement (R-UI-12; R-WIN-2, R-VST-7 amended).
- [x] **G2** The registry's group labels in the model (DSP REQ-device-10).
      — DSP `c8a15eb` `DeviceType::groupLabels` (every instrument group titled); `devices[].groups` (`prefix`,
      `label`) filled by the shared `deviceModelOf`, documented, API regenerated; L2 test (mutant: an untitled
      group, red in the DSP unit test). The DR text lands with the face (DR-UI-12, G3).
- [x] **G3** The analog face in Solaris's instrument windows and the plugins' editors (R-UI-12).
      — DR-UI-12: `AnalogFace` as an instrument's DevicePanel body — titled modules, 270° knobs (detents, bipolar
      arcs, lit caps), sliding-light switches with shape keys, screens (wave with unison, the filter's DSP prototype,
      ADSR) — generated from the registry; everything eased; one `set` per drag step, Shift fine, double-click
      default, the parameter menu, bound knobs inert; windows 820 wide, the plugin editor at its own size (980).
      Found and fixed D-4 (the synth's Resonance did nothing; DSP `66b5ecb`). 4 mutants caught.
- [ ] **B13+** R-EDM-10…20, one task each, in that order unless the user reorders.

### H — a song made in the window (user request, 2026-10-15; discussion round 6)
- [x] **H1** The brief's requirements: R-ROLL-6/7, R-INST-5, R-SVC-10/11; discussion round 6; this section.
- [x] **H2** Play from the roll: its ▶, `transport play --clip`, the clip's loop published, Space with a roll
      in front, the playhead in the roll (R-ROLL-6).
      — DR-ROLL-6: `transport play --clip <ac>` loops the clip's span in the song, follows its edits, gives the
      song's loop back (stop, a plain play, the clip deleted; a hand-set loop stays); `transport.loopClip`. The
      roll knows the clip it was opened from (else the pattern's first); ▶/■ in its toolbar, dimmed on no track;
      Space stops whatever plays and, stopped, is the front roll's ▶; the playhead fades in where the clip is
      heard and the grid pages after it. L2 hears the kick each loop; UI test; shot `piano-roll-playing`. 3
      mutants caught (a snapping playhead, stop keeping the clip's loop, a loop not following its clip).
- [x] **H3** A note heard when it is made: `note preview`, a preview stream through the track's chain; the
      roll sounds an added note, a pitch a drag enters, a pressed key (R-ROLL-7).
      — DR-ROLL-7: `note preview <ch> <pitch>` compiles the strip, its rack and one note into an engine of their own
      (the same compiler) — heard equal to the strip's render in the song to 1e-6; `NotePreview`, a stream kept
      open across a run of previews, the old voice faded under the new, voices handed over and back through
      atomics. The roll sounds a note added, pressed, dragged into a row, a key pressed or swept (lit), a Steps
      cell; never in undo. 3 mutants caught (a cut instead of a fade, the fader left out, a snapping key light).
- [x] **H4** The piano: the DSP library's physical piano as a library instrument and a registry entry
      (DSP first; R-INST-5).
      — DR-INST-5 (DSP `9d6ef27`, REQ-inst-piano-1): `PianoEngine` into `src/physical`, `Piano` and registry
      `piano` (voicing bank, pedals, drive, volume; titled groups), 16 voices stolen longest-released first,
      `silence()` making a seek's replay bit-identical — which found two engine defects: never-struck voices
      were undamped phantom strings ringing through the bridge (now seated: one voice 4× cheaper), and a stale
      `displacementAtStrike` met every stolen voice's strike. Bench 10.7× RT (Release), budget met. The
      umbrella's examples/piano plays the library's engine. Shot `piano-window`. Stealing mutant seen red.
- [x] **H5** The Prelude, attempt 1: its score table (`demo/prelude/score.py`, 549 notes, 140 beats), the driver
      that makes it through the window's gestures (`makePrelude.cpp`, `solaris_demo_prelude`), the UX audit
      (`docs/ux-audit.md`): two bars cost 50 gestures; U-1…U-6 found — five became the tasks below.
- [x] **H6** The song's tempo set in the song bar (U-1; R-UI-13).
      — DR-UI-13: the bpm readout is a control — dragged, the pointer's, one `set project.bpm` per step (Shift: a
      tenth); double-clicked, typed; set elsewhere, eased. UI test; a snapping tempo seen red.
- [x] **H7** A selection in the roll and what is done to it: `notes move`, `notes delete`, duplicate, copy and paste
      (U-2; R-ROLL-8).
      — DR-ROLL-8: `notes move` / `notes delete` (one edit, refused whole); the roll selects by click, Shift+click,
      a Ctrl box, Ctrl+A; the selection dragged, the arrows, Delete, Ctrl+D, Ctrl+C/V (a shared clipboard) — each
      one line; lit, eased; the keys go to the roll last pressed. L2 + UI tests; 2 mutants caught.
- [x] **H8** The pattern grows to hold its notes; a whole-pattern clip grows with it (U-3; R-ROLL-9).
      — DR-ROLL-9: every note-writing edit grows the pattern to the bar holding its last note's end, a clip that
      played it whole with it, in the same edit; said in the output; R-SVC-8's warning amended. L2; mutant caught.
- [x] **H9** The length a click makes, shown and chosen; a window resized by its corner (U-4, U-5; R-ROLL-10,
      R-WIN-5).
      — DR-ROLL-10: "Length 1/16* ▾" and its menu (the last length, or a note value). DR-WIN-5: a corner grip on
      every window, the pointer's, floored and kept in the layer, the content laid out anew. UI test; 2 mutants.
- [x] **H10a** D-5 (found starting H10, the UX audit's U-7): Shift+wheel and a sideways swipe never reached the app
      from the window — `App::wheel` carries Shift, the GTK host passes it and turns a sideways scroll into it. Guard
      `test_the_wheel_scrolls_sideways_with_shift` (mutant seen red); the host's half owed a live check (DEFECTS).
- [x] **H10b** D-6 (found in H10, the UX audit's U-8): a note dragged under the 5 px threshold was a click — the
      resize lost, a note added instead. `PianoRoll::track` runs at the release too; the Click after it is
      swallowed; a click still moves nothing. Guard `test_a_short_drag_lands_where_it_was_let_go` (mutant seen red).
- [x] **H10c** D-7 (found in H10, U-9): a note under 5 px wide was all resize edge — it could not be moved. The edge
      is the right 5 px or the right third of the note. Guard in `test_a_short_drag_lands_where_it_was_let_go`.
- [x] **H10d** U-10 (found in H10): a Ctrl+drag starting on a note grabbed the note (and the selection with it). Ctrl
      is selecting: a box wherever it starts, never a move (R-ROLL-8 amended). Guard in the selection test (mutant
      seen red).
- [x] **H10** The Prelude, made in the window with the fixes: every note, the tempo, measured against the score
      (R-SVC-10), rendered and delivered; the audit's attempt 2 — what the fixes saved.
      — DR-SVC-10/11: 549 of 549 notes, 35 of 35 bars the same samples as the score's own render, 376 gestures
      (0.68 a note, from 1.6); four more findings fixed on the way (H10a–d). ctest `prelude_made_in_the_window`
      (label `long`). Delivered to ~/Music/Solaris/ (prelude-in-c.slp/.wav/.mp3).

### I — an EDM track, as a test (user request, 2026-10-15; discussion round 7)
- [x] **I1** The brief's requirements: R-INST-1/2 amended (stereo unison; crash and ride), R-SVC-12; round 7.
- [x] **I2** Stereo unison in the Basic Synth: a width per oscillator, spread starting phases (DSP first; R-INST-1).
      — DSP `94d259f` (REQ-synth2-6): width 20 → L/R correlation 0.44; attack 0.9× sustained (aligned: 7.8×, seen red).
      Solaris: pointer, API regenerated, the face's test scrolls its Volume knob into view (the face grew).
- [x] **I3** Crash and ride in the Drum Machine (DSP first; R-INST-2).
      — DSP `79648e7` (REQ-drum-6): crash 49 (1750 ms, 71 % above 3 kHz), ride 51 (1200 ms, a bell over its wash; the
      first bell missed its band — retuned). Solaris: pointer, API, two tests that pinned ten pads.
- [x] **I4a** D-8 (found making the track): a synth's volume / a sampler's level, automated or set while a note
      sounded, moved nothing until the next note — the riser could not swell. DSP `0707803`: applied per block,
      ramped. Guard `Volume_and_level_move_a_sounding_note` (a stepped volume seen red).
- [x] **I4** The track: `demo/edm` — its script written by a generator, rendered, measured as a production
      (`measure.py`), a ctest; anything it finds missing becomes I5…; delivered (R-SVC-12).
      — DR-SVC-12: *Perihelion*, 126 bpm, A minor, 2:17. −10.3 LUFS, −1.03 dBTP, drops 4.8 LU over the breakdown,
      builds +3.1 LU landing under their drops, mono lows / wide highs, a 10 dB pump, the kick within 0.19 ms, 98 % in
      key. Seven mixes to get there (the measure's first key test was fooled by drum noise — rewritten to peaks).
      ctest `edm_track_measured` (label `long`). Delivered to ~/Music/Solaris/perihelion.*.

### J — the time bar remembered, hotkeys as settings (user request, 2026-10-16)
- [x] **J1** The requirements: R-TIME-7 (stop back to the cue), R-TIME-8 (stop here), R-SET-4 (hotkeys).
- [x] **J2** The cue: `transport stop --return`, `transport.cue`, Space returns and Ctrl+Space stays, the Stop button
      returns, the cue marked on the ruler while playing (R-TIME-7/8).
      — DR-TIME-7: the cue set by a seek or where play begins; Space / Stop / a roll's ■ return, Ctrl+Space stays; an
      outlined mark on the ruler while playing. L2 + UI tests, shot `ruler-cue`; 2 mutants seen red.
- [x] **J3** The keymap: the actions and their defaults (the service's table), `settings set key.<action>`,
      `settings.keys` in the model, the App resolving every key through it, the host passing more keys (R-SET-4).
      — DR-SET-4: 21 actions in four areas (two keyboards); combos spelt one way (`KeyCombo.h`); refusals name the
      action holding a key; the API's Keyboard section generated. The host now names the key (unshifted keysym:
      F-keys, Tab, Home/End, PageUp/Down, punctuation) and sends what it types — D-9 found and closed here.
      L2 + UI tests; 2 mutants seen red.
- [x] **J4** The sheet's Keyboard section: the keys drawn, a click listening for the next, add, remove, Reset (R-SET-4).
      — DR-SET-4 (the sheet): per area, an action a line; listening eased (accent, "Press a key…" cross-faded);
      Backspace removes, Escape cancels; a refusal toasted; chips keyed by combo, growing in and shrinking out;
      Reset as one line. UI test, 2 shots; 2 mutants seen red. The 2026-10-16 request is done (J1–J4).
- [x] **J5** The user's follow-up: Ctrl+Space's place is temporary — a plain play no longer moves the cue, so
      Space's stop after a Ctrl+Space goes back to where the time bar was put (R-TIME-7/8 AMENDED; DR-TIME-7).
      L2 + UI tests extended; mutant (every play moves the cue) seen red in both.

### K — settings in pages, combo boxes, the history tree (user request, 2026-10-16)
*"setting need to have section, don't put all in 1 page / for option need to have combo box / need to have tree
history like cosmo".* Requirements: R-SET-5, R-SET-6 (R-SET-1, R-SET-3 amended); R-HIST-1…4 (R-EDM-1 amended).
- [x] **K1** The requirements, conflict-checked; R-HIST-4 (keep the tree in the song?) asked of the user.
- [x] **K2** The sheet in pages: a rail of sections, one page cross-faded in, the rail's highlight sliding; choices
      as Artboard's `ComboBox` in cosmo's tokens, on/off as a toggle (R-SET-5, R-SET-6).
      — DR-SET-5: `SettingCombo` / `SettingToggle` (Artboard's controls, fields drawn here — a value from the model
      cross-fades / slides); pages clipped, input-transparent, their own scroll. 2 UI tests (one new), 4 shots;
      3 mutants seen red. Found on the way: `clipToBounds` clips a segment's children, not its own paint.
- [x] **K3** The history tree in the service: nodes, branching, `history`, `history jump <n>`, `history[]` +
      `historyCurrent` in the model (R-HIST-1, R-HIST-2).
      — DR-HIST-1: `mHist` (ids never reused), undo → parent, redo → newest branch, a jump no node, 200 nodes pruned
      oldest-off-path first. L2 `test_history_is_a_tree`; 3 mutants seen red.
- [x] **K4** Edit ▸ Show History Tree…: cosmo's `HistoryView` over the model's tree, a click one `history jump`
      (R-HIST-3).
      — DR-HIST-2: cosmo's view reused; two OPT-IN additions to it (`easeCurrent`: the highlight travels; `hint`:
      a host's own keys) — cosmo's own tests green. Edit's menu hints follow the keymap. UI test, 2 shots; mutant
      seen red. The 2026-10-16 request is done (K1–K4).
- [x] **K5** R-HIST-4 answered (2026-10-16): the tree lives only while the song is open — never saved. Already how
      it is built; guarded by `test_history_is_a_tree` (a song saved and opened again is one root, "Opened").

### L — copying clips (user request, 2026-10-16)
*"when hold shift drag and drop, dupplicate the sample/midi clip, same with ctrl C and V".* R-CLIP-9, R-CLIP-10.
- [x] **L1** `clip duplicate --lane`; Shift-drag duplicates (the original stays, a "+" on the copy, eased); Ctrl+C /
      Ctrl+V as keymap actions `lanes.copy` / `lanes.paste`, pasting at the time bar, end to end; Edit menu items.
      — DR-CLIP-9. L2 + UI tests, shot; 4 mutants seen red. A roll with the keys keeps its own Ctrl+C / Ctrl+V.
- [x] **L2** The resize cursor (R-UI-14, user request 2026-10-16): `App::cursor()` from each widget's grip zones and
      the drag in hand; the GTK host sets the window's cursor when it changes.
      — DR-UI-14. UI test; 3 mutants seen red. The GTK half is compiled, not driven live (as D-5): owed.

### M — the wordmark, looping automation, a professional-DAW analysis (user request, 2026-10-16)
- [x] **M1** No Home button: the `solaris.` wordmark is Home, as cosmo's and Interstellar's (R-UI-3 amended).
- [x] **M2a** A looping automation in the service and the engine: `<au>.loop/.at/.length`, `#aauto` keys, the curve's
      cycle compiled; `auto create --pump` (R-AUTO-12, R-AUTO-13). Measured: a render's level at known beats, `eval`.
      — DR-AUTO-8. L2 test measured (render = eval within 0.15 dB, the dip > 15 dB); 2 mutants seen red.
- [x] **M2b** Seen and made in the window: the row's span (cycles, seams, ends dragged), the window's Loop combo,
      ParamMenu › Create Pump (R-AUTO-12, R-AUTO-13).
      — DR-AUTO-8 (in the window). UI test, shot; 2 mutants seen red.
- [x] **M3** A full analysis — what a professional DAW needs that Solaris lacks — in `docs/pro-daw-analysis.md`.
      — P0 blockers (autosave, a MIDI keyboard, recording, low-latency audio, other makers' plugins, warp and tempo,
      MIDI files, mastering meters + a true-peak limiter, export formats), P1, P2, and a five-phase order.

### N — a professional DAW: "implement all" of the analysis (user request, 2026-10-16; discussion round 8; R-PRO)
One task a commit, in this order. A DSP half is its own commit in the submodule first.
**Phase 1 — finish a track safely and deliver it**
- [x] **N1** Autosave and recovery (R-SAFE-1/2).
      — DR-SAFE-1. L2 + UI tests; 2 mutants seen red.
- [x] **N2** MIDI files: import, export, a `.mid` dropped (R-MIDI-1).
      — DR-MIDI-1. L2 + UI tests; 2 mutants seen red (the velocity-0 one survived a first test: the test grew a note).
      Found on the way: the live-playback test read the HEARD position the instant 96000 frames were taken — a latency
      short at times (as J5's); it waits a tenth of a second more.
- [x] **N3** DSP: loudness (BS.1770 gated), true peak, correlation, spectrum meters (R-METER-1's DSP half).
      — DSP `926a2b4` (REQ-meter-1…4, `src/meter/`): integrated loudness equal to BS.1770-4 computed independently
      (−8.053 vs −8.053 LUFS), true peak within 0.06 dB; 3 mutants seen red.
- [x] **N4** The master's loudness meters in the model and the window; strip meters RMS / held peak / clip latch
      (R-METER-1, R-METER-2).
      — DR-METER-1/2. The engine taps its master into the player's buffers; the player runs the DSP meters and the
      ballistics per block (in samples) and publishes atomics; the service keeps the clip latch, `meters clear` (a
      Live message, so it lands after the edit before it), `render --measure` (one line). Window: RMS core, held line,
      clip light on every meter; the master's LUFS chip opens the Loudness window. L2 (a −6 dBFS sine reads −6.02
      LUFS / dBTP, correlation 1, its band; holds fall; no allocation on a clear) + UI + 2 shots. 9 mutants red; the
      pending-clear guard's race not reproduced by its mutant (kept — the message ordering is the main fix, found by
      a mutant run that tripped on it).
- [x] **N5** DSP: the Limiter's true-peak mode (R-EDM-4a).
      — DSP `aa56ae2` (REQ-fx-limiter-2, README (L6)): the meters' interpolator made its own type and taken at 8×
      on the samples; each frame's gain meets ±8 neighbours' needs; latency L + 16. DR-EDM-4a: an L2 render reads
      −1.0000 dBTP (sample-peak mode −0.52). 2 mutants red. Found: a 4×-only detector held the meter's grid but
      read 0.19 dB over on the quarter grid — the requirement's "4×" amended to 8×, with why.
- [x] **N6** Delivery formats: FLAC / MP3 / OGG / Opus, rate, bits, dither, normalise to LUFS; File › Export…
      (R-EXPORT-1).
      — DSP `c23768c` + `d97a6cd` (REQ-out-dither-1: TPDF from two Noise uniforms; an exact zero kept silent). DR-EXPORT-1:
      the host's FFmpeg encoders behind `writeAudio`; `--rate` renders the song AT the rate (no resampler); normalise
      held under a true-peak ceiling; integer depths dithered, seeded per file. File › Export… is `ExportSheet`. L2 + L3
      (ffprobe, ebur128) + UI + a shot; 5 Solaris mutants and 2 DSP mutants red. Found: a default render's silence
      turned to dither hiss — fixed in the library ((D0)), not by the caller.
- [x] **N7** `strip duck --under`: a side-chain in one gesture (R-MIX-18).
      — DR-MIX-18: `device add` + `set` + `send add --pre --sidechain` in one edit; R = 30/(30 − amount) at −30 dB,
      1 ms, half a beat. L2 (the duck measured on the stem: 7.3 dB after a kick, back within 2) + UI (Duck Under ▸);
      3 mutants red. Found: measuring the duck on the master hides it (the kick is in the mix) — the stem.
**Phase 2 — play it in**
- [x] **N8** The PipeWire driver, xruns counted (R-DEV-9).
      — DR-DEV-9: `AudioOutPipewire` (pw_stream, `node.latency` from the buffer) over an SPSC `AudioRing` (xruns counted
      once primed); `openOutput` for every stream, PulseAudio the fallback, said once; `transport.driver` / `.xruns`;
      Settings › Audio › Driver. Ring tests + L2 + UI; live failure path checked with no device reached; 2 mutants red.
      Owed: playback on a PipeWire server (this machine's server is PulseAudio).
- [x] **N9** A MIDI keyboard: devices, arm, play live, record into a clip, count-in, input quantize (R-MIDI-2).
      — part 1 (DR-MIDI-2): ALSA input (live-tested through Midi Through), the service's lock-free queue drained by the
      audio thread that runs, the sustain pedal, `strip arm` (the session's), the input monitor while stopped, the arm
      ring on a track's header, Settings › MIDI input. Part 2: `transport record` (a count-in pre-roll, notes where
      heard, the take landed at the stop as one undo — new clip or overdub), input quantize, the Record button.
- [x] **N10** DSP: pitch bend and mod wheel in the instruments; glide / legato in the Basic Synth (R-MIDI-3's DSP
      half, R-INST-6).
      — DSP `9d99565` (REQ-inst-expr-1, REQ-synth2-7, -8: an absolute control grid, glide linear in semitones, legato,
      the wheels; 7 unit tests, `glide_and_bend`; 2 mutants red). DR-INST-6: Solaris has them with no code — the
      face grew to eight modules. Found: two face tests assumed every module was wide (the small Voice and Vibrato
      share a row when narrow) and one read a parameter by its face index (now by name).
- [x] **N11** Expression lanes: pitch bend and CC on a note clip, in the roll, recorded (R-MIDI-3).
      — part 1 (DR-MIDI-3): `#expr` lanes on a pattern, `expr add/delete/clear`, compiled to control events (ramps on the
      64-sample grid, at rest after a clip, restored on seek), the keyboard's wheels live and recorded, MIDI files both
      ways. Part 2: the roll's lower lane on Vel | Bend | Mod — a click adds a point, a drag sets it, a double-click
      removes it, each one `expr` line; eased; a shot.
- [x] **N12** Note tools: humanise, probability, grooves, scale lock, chord stamps, legato, arpeggiate (R-ROLL-11).
      Part 1 (2026-10-16): the grammar — `pattern scale` (placed notes to the nearest, a tie down; moved on the way they
      went), `Note::chance` rolled per clip and pass by a hash (the render and the song's MIDI file agree), `notes set`,
      `pattern humanise` / `legato` / `arpeggiate`, `pattern quantize --groove --strength --notes` (nine templates);
      chord stamps were already `note add --chord`. Part 2: the roll's toolbar in two rows — Scale ▾ (Change key…; the
      rows off it dim, eased), Chord ▾ (a click stamps it), Tools ▾ (on the selection), the grooves in Quantize…; a
      Chance lane; velocity and chance changed elsewhere ease; the vocabularies published (`chords`, `scales`,
      `grooves`); the roll's window 720 × 520; menu carets drawn (the font has no ▾).
- [x] **N13** Markers and sections (R-ARR-1).
      Part 1 (2026-10-16): Interstellar's marker in beats — `#amarker` (`length` a section), `marker add|rename|move|delete`,
      `markers[]` with each section's `end`, `transport seek|loop <mk>`. Part 2: the ruler's top band (`kRulerH` 35.75) —
      flags, sections, click seeks, double-click loops, drag moves, right-click renames/deletes/adds; eased, keyed.
- [x] **N14** Time insert / delete across the song (R-ARR-2).
      2026-10-16: `time insert|delete` — clips, automation points (a looping span), markers, the loop; what crosses an edge
      decided first (insert: stays, named; delete: a repeating clip shortened, any other refused); the ruler's menu.
- [x] **N15** Lane groups (R-ARR-3).
      Part 1 (2026-10-16): `#alanegroup` + a lane's `group=`, side by side (`gatherLaneGroups`, also on open), `lane
      group|ungroup`, `lane move <lg>`, a lane joining/leaving by where it lands, `set lg.folded`, `clips move --by`.
      D-10, D-11 (test defects) closed. Part 2: `set <ln>.group`; the timeline — an open group's bar and chevron, a
      folded group ONE row (each lane's clips in its band, eased), its clips dragged together, the menus.
**Phase 3 — work with audio**
- [x] **N16** Record audio: arm, monitor, record, punch, loop takes (R-REC-1).
      Part 1 (2026-10-16): the core — `IAudioIn`, a strip's `input`/`monitor`, `port add`, arming an audio line, monitoring
      (auto/on/off), the player reading input each cycle (heard through the strip; a take through a ring a recorder
      thread drains), the take landed sample-aligned (out + in latency), `transport punch`; a loopback fake proves it to
      the sample. Part 2 (same day): the host's capture — `AudioInPulse` (a read thread), `AudioInPipewire` (the realtime
      callback), `AudioRing::push` (never waits; overflow counted), the driver setting with PulseAudio the fallback.
      Part 3 (same day): the window — M | S | R on audio and instrument strips (R lit red, eased; no input: the input
      ports offered), Input… and Monitor… in an audio line's menu, the ruler's Punch the loop's span (a red line, eased).
      Loop takes: N17. Live check owed: a recording through this machine's real input.
- [x] **N17** Takes and comping (R-REC-2).
      Part 1 (2026-10-16): a looped recording → a take per pass (each heard as heard: the turn's tail the pass before's),
      `#take`/`#comp`, `clip comp`, the compile playing the comp with 5 ms crossfades; D-12 (a take's path) closed.
      Part 2 (same day): `clips[].plays` (the comp resolved, in the model); the window — "N takes", Show takes (a row per
      take under the lane, lit where it plays, cross-faded), a drag across a take is one `clip comp`, a double-click the
      whole clip.
- [x] **N18** Audio editing on the lanes: split, fades and gain drawn, crossfades, reverse, consolidate, slice
      (R-EDIT-1).
      Part 1 (2026-10-16): DSP `485785f` — the Sampler's `slices` mode (REQ-inst-sampler-2) and `detectOnsets`
      (REQ-onset-1); Solaris — `set <ac>.reverse`, overlapping clips on a lane crossfaded (`clips[].fadeInHeard/
      fadeOutHeard`), `clip split`, `clip consolidate` (a raw render to `Bounces/`), `clip slice --every|--transients`
      and `--every --kit` (a Sampler that plays the same, to the sample). Part 2 (same day): the window — a clip's fade
      handles and gain line (one `set` when let go), its crossfade drawn as heard, Split at the time bar · Reverse ·
      Consolidate N clips · Slice ▸ in its menu, Ctrl+E (`lanes.split`).
- [x] **N19** DSP: a time-stretcher and pitch shifter (phase vocoder, transients) (R-WARP-1's DSP half).
      2026-10-16, DSP `267a112`: `src/spectral/` — `FFT` (REQ-fft-1), `timeStretch` (REQ-stretch-1…3: identity phase
      locking; a time map rigid around each onset, so an attack lands at ratio·onset whole — nothing of it ahead, where a
      plain vocoder smears it 20 ms early) and `resample` (REQ-resample-1, Kaiser sinc). 440 Hz kept to the millihertz at
      ×1.5 and ×0.6; a fifth up 659.255 Hz; deterministic.
- [x] **N20** Warp and pitch on audio clips, BPM detected (R-WARP-1).
      Part 1 (2026-10-16): DSP `c526f8d` — `estimateTempo` (REQ-tempo-1); Solaris — `warp`/`bpm`/`pitch` on an audio
      clip (`set`, `clip add --warp` heard on import), its own beats when warped, its span stretched once per key by the
      DSP library (`stretchKey`), model fields. Part 2 (same day): the window — Warp / Unwarp, Tempo… typed, Pitch ▸ in a
      clip's menu; its tag "60 bpm · +7 st" cross-faded.
- [x] **N21** A tempo map (R-TEMPO-1).
      Part 1 (2026-10-16): DSP `6f2df21` — a stretch following a time map (REQ-stretch-4), for warped clips under a
      changing tempo; D-14 (the tempo estimate's (B4) left disabled by a cut-off mutant run) closed.
      Part 2 (same day): `#atempo` and `tempo add|move|delete`; `engine/Tempo.h` (closed-form constant and gliding
      segments) in the clock; the compile, transport, loop, count-in, metronome, recording, formulas, warped clips and
      the MIDI file through it; a song of one tempo renders the bytes it did. `project.bpm` as a formula target refused
      (the map is its automation — R-TEMPO-1 amended).
      Part 3 (same day): the window — flags on the ruler's band (dragged, retyped, glided, deleted), Tempo change here…,
      the song bar at the time bar's tempo setting the change in effect there.
**Phase 4 — open the ecosystem**
- [x] **N22** Device presets and the browser's Presets tab (R-PRESET-1).
      Part 1 (2026-10-16): `.slpreset` files in the user's folder and the factory's (five shipped); `preset
      save|load|list|delete`, `--preset` on `device add` and `clip add --instrument`; `presets[]` in the model.
      Part 2 (2026-10-17): the browser's Presets tab (tabs as wide as their names, the highlight easing its width), an
      instrument's preset dropped in one line, a device window's Presets (load, Save as…).
- [x] **N23** Song templates (R-PRESET-2).
      Part 1 (2026-10-17): the user's and the factory's templates folders (two shipped, made by `solaris-cc`); `template
      save|list|delete`; `project new --template` (its own id and name, the template's tempo unless told);
      `templates[]` in the model.
      Part 2 (same day): Home's From template…, File › New from Template… and Save as Template….
- [x] **N24** A sample library: tags, favourites, search (R-PRESET-3).
      Part 1 (2026-10-17): the machine's library file; `sample tag|untag|favourite`; `browse --search` (names through
      the folders, tags, the library's own) and `--favourites`; `library[]`, `browser.search` in the model.
      Part 2 (same day): the Samples tab — Search samples…, Favourites, a star and tags on every sound, Tag… / Remove tag.
- [x] **N25** VST3 hosting: scan, an out-of-process host, process, latency, parameters (R-VST-6).
      Design round 2026-10-17 (discussion.md Round 9). Part 1 (same day): `solaris-plughost --scan`, the host's scan (a
      bundle per process — a crashing module loses only itself), `plugins scan|list`, the machine's list, `plugins[]`.
      Part 2 (same day): `solaris-plughost --serve` (one instance; blocks through shared memory and two eventfds, state
      over its socket); the host's `loadPlugin` (a budget; dead, frozen or refusing → offline for good, the helper
      killed); the core's `IPluginInstance` seam and `PluginDevice`; the engine's made devices (never warmed); `vst3`
      devices in the `.slp` (class, name, kind, bundle, `p<id>`, state); `strip add|clip add|device add --plugin`,
      `device reload`, `plugin.offline`, `devices[].plugin/offline/why`; a render's own instances. Basic Synth as a
      plugin renders what the native synth renders, to the bit.
- [x] **N26** VST3 hosting: a plugin's own editor window; its offline state and Reload in the device panel (crash and
      overrun → offline is N25's) (R-VST-6).
      Part 1 (2026-10-17): the helper's two threads (blocks never wait on an editor); `editor-open|close` — the plugin's
      view in a window of its own, the plug frame and run loop served by the helper; its edits back as lines; `device
      editor <dv> [--close]`, `devices[].editor`, `plugin.editor`; an editor's values land as `set <dv>.p<id>` (one undo
      step, never echoed back). Live: Basic Synth's own editor through the helper, a knob dragged came back as edits.
      Part 2 (same day): the device window — a plugin's list (no face; values as percentages), Editor in the Presets
      place (lit while open), offline a band sliding open with why and Reload.
- [x] **N27** MIDI learn (R-CTRL-1).
      Part 1 (2026-10-17): `map add|remove|list|learn` (the machine's settings, never the song's); controllers moving
      their numbers (a gain by the fader law — moved to `FaderLaw.h`, one copy — a parameter by its taper), landing as
      `set` lines; MIDI Machine Control (play, stop, record, a bar on / back) parsed by the ALSA host; inert mappings
      said by `audit`; `settings.mappings[]`, `mapLearning`.
      Part 2 (same day): Learn MIDI CC / Forget CC <n> in every number's menu; the song bar's pill while a learn waits
      (a click cancels); Settings › MIDI — the mappings, × forgets one; each eased.
- [x] **N28** Control surfaces: Mackie Control / HUI (R-CTRL-2).
      Design round 2026-10-17 (discussion.md Round 10). Part 1 (same day): `core/Surface` — Mackie Control and HUI as
      pure codecs of one surface model; the MIDI output seam and ALSA's; `settings surface|surfacePort`, the keyboard
      deaf to that port; `ServiceSurface` — its moves as lines, what it shows as a diff, a touched fader left to the
      hand, banks; `surface.changed`; `surface` in the model. Not yet checked against a real surface.
      Part 2 (same day): Settings › MIDI — Control surface and Surface port combos, the note saying connected or why.
- [x] **N29** A CPU meter (R-PERF-1).
      2026-10-17: the player times each block (all but the device's wait) over its duration — smoothed, its worst
      held; the engine adds up each strip's time when asked (never in a render); `transport.cpu|cpuPeak`,
      `strips[].cpu`; the song bar's readout, catching up.
- [x] **N30** A multi-core engine, byte-identical (R-PERF-2).
      2026-10-17: the strips nothing feeds do their own work on a pool first, then everything else in processing
      order (each sum in one thread's order); `WorkerPool`; `settings threads`; live and render both. A TSan build
      found D-16 (closed).
- [x] **N31** Freeze and bounce (R-PERF-3).
      2026-10-17, part 1: `strip freeze|unfreeze|bounce` — a strip's own sound rendered by an engine of its own (to
      the song's end and its tail), frozen beside the song and played instead (its rack not built), or bounced onto a
      new strip; `strips[].frozen`; `frozen=` on `#atrack`; the audit. Part 2: the mixer strip's menu (Freeze /
      Unfreeze / Bounce to a new strip, offered by `fromStrips`) and a frozen rack receding under FROZEN, eased.
- [x] **N32** Several audio devices, drift-corrected (R-DEV-1/2/4).
      2026-10-17: the design round (discussion Round 11). Part 1, the DSP library (f3d6683): `AdaptiveResampler`
      (REQ-resample-2) and `FillController` (REQ-resample-3), REQ-resample-1's kernel shared as `SincKernel`. Next:
      part 2, the followers in the core (rings, threads, alignment, `transport.devices[]`); part 3, an input on another
      device; part 4, Settings › Audio.
      Part 2: `Follower` (core) — a ring per follower device, time-stamped pushes, a continuous fill steering the DSP
      pieces, primed to its target; the Player feeds them and delays the clock to the slowest path; the service maps
      ports to devices, aligns the paths, remembers each device's drift, says a device that will not open;
      `transport.devices[]`. Measured in real time: an impulse heard on two devices 0.02–0.12 ms apart. D-17 filed (the
      test fakes race under TSan; the product is clean).
      Part 3: `DriftIn` — an input on any device but the default or Settings' input, held to the clock (the input
      seam's `held` + `CaptureStamp`, marked by both host captures); its latency its device's and its target's; role
      `input` in `transport.devices[]`. Part 4: Settings › Audio's PORTS — each song port on its device (a field whose
      menu is one `settings set port.` line), its line saying where it plays, and while playing how its device is.
**Phase 5 — grow the sound and the craft**
- [x] **N33** DSP instruments: wavetable, FM, multi-sample (SFZ), granular, 808 (R-INST-7…11) — a commit each.
      2026-10-17: the wavetable (DSP ad3f2a3: `Wavetable`, `Oscillator::Table`, BasicSynth's table oscillators, the
      registry's "wavetable") — here with no code of its own. The FM synth (DSP 3cb818d: `FMSynth`, eight algorithms,
      the registry's "fm"); every instrument now held by one test (dropped, sounding). The multi-sample (DSP 2cf1457:
      `parseSfz`, `MultiSampler`, a device's definition and named sounds) — its .sfz read here, its sounds decoded beside
      it. The granular sampler (DSP eb1d014: `GranularSampler`, the registry's "granular"). The 808 (DSP 190ce5f:
      `Bass808` — a composition of Phasor, DecayEnvelope, Noise and Biquad's design).
- [x] **N34** DSP effects: multiband, gate, transient, de-esser, saturator, phaser, flanger, imager, convolution,
      pitch shifter, vocoder, tuner (R-FX-6…17) — a commit each.
      2026-10-17: the multiband (DSP 122d264); every effect held by one L2 test. The gate / expander (DSP f0cb867).
      The transient shaper (DSP bb091f1). The de-esser (DSP ed63bbd).
      The saturator and the bitcrusher (DSP cfe7808). The phaser (DSP 3569086); D-18 filed (the library's delay
      line extrapolates — the flanger needs it right); D-18 closed (DSP 2148541). The flanger (DSP 0b08063).
      The imager (DSP 04ddda6). The convolution reverb (DSP ac7aa74; `device add --sample`). The pitch shifter
      (DSP 8ceaa31). The vocoder (DSP 051007a; the audit hears a switchless key). The tuner, part 1 (Round 12;
      DSP 6c07198: YIN and a device's readings). Part 2: the readings carried up — engine, Player ring, model, `get`
      (DR-FX-17). Part 3: its window — the note, an eased needle, shots. N34 DONE (twelve effects; D-18, D-19, D-20 on
      the way).
- [x] **N35** Group / VCA faders (R-MIX-19). Round 13 (e580437).
      Part 1: a strip of kind vca; membership; the compile (its dB added, its mute and solo its members', a bound
      member's offset); the grammar and the refusals; a member's live fader now carries its VCA's dB (DR-MIX-19).
      Part 2: a VCA's fader, mute and solo live with no rebuild (a formula member: one); a VCA-muted member keys
      nothing; strips[].vca / vcaGain / members; transport.swaps. Part 3: the dock — a VCA's card, a member's eased
      tag, "+ Line" › VCA, a strip's VCA ▸, shot mixer-vca. N35 DONE.
- [x] **N36** Phase invert, width, mono check, solo-safe (R-MIX-20). Round 14 (139db7d).
      Part 1: a strip's phase and width in the engine (after its rack, before its fader; untouched at defaults);
      solo-safe buses; the mono check at the Player (DR-MIX-20). Part 2: width and phase live with no rebuild (ramped;
      a flip crosses through silence), the model's fields. Part 3: the dock — a strip's Invert Phase, Width ▸ and a
      bus's Solo-safe; the card's Ø and width tag, the safe ring, the master's Mono pill, all eased; shot mixer-tools.
      N36 DONE. (On the way: DR-MIX-4's and the dock's anchors had drifted unseen — re-pointed by symbol.)
- [x] **N37** Versions of a song (R-VER-1…4). Round 15 (the design): deltas nested in `#aversion`, worked out after
      every edit on an open version; one freeze lever; Interstellar's verbs. Part 1: the model — `#aversion` read and
      written after the song, a resolution (drops, sets, unsets, bodies, orders, its own nodes), the deltas worked out
      from the song an edit left, dangling kept until rebase, freeze/thaw/rebase/revert, ids never reused (DR-VER-1).
      Part 2: the service — Interstellar's verbs (`version new|list|open|diff|freeze|thaw|rebase|delete`, `revert`,
      `render --version`), one hook in `dispatch` recording an edit on an open version, a frozen one refusing edits,
      opening a history step but not unsaved, `versions[]`/`version`. D-22 filed (bare anchors on the wrong file).
      Part 3: the song bar — the version chip after the menus (the name follows it), its menu the tree, New Version…
      (ONE line, opened), Freeze/Thaw, Rebase, Rename…, Delete; label, ring and width eased; shots song-versions(-mid).
      N37 DONE. (On the way: DR-UI-1/9/13's App and SongBar anchors re-pointed by symbol.)
- [x] **N38** Collect and save; relink (R-COLLECT-1). Round 16 (the design): what a song uses — in every version, an
      `.sfz`'s sounds too; collect into `Samples/` as ONE edit; relink a file or a folder across the song. Part 1: the
      service — `project files|collect|relink`, `clip relink`, `missing[]`; D-23 found and fixed (a dry run was an edit).
      Part 2: File › Collect and Save (one line), File › Relink… (asks first, then the window: Locate…, Search a
      Folder…, rows keyed and eased), a toast on open; shots relink-window, relink-found-mid. N38 DONE.
- [x] **N39** A clip launcher (R-LAUNCH-1). Round 17 (the design): scenes × strips, slots as their own nodes; launching
      live (quantized, the same graph, messages); recording lands the spans and cuts the arrangement under them. Part 1:
      the grid — `#ascene`/`#aslot`, `launchQuantize`, `scene add|delete|move`, `slot add|delete`, the fields. Part 2:
      launching, heard — slots in the graph, `launch`/`launch stop`/`launch back` quantized ahead of the render, live
      messages, a swap carrying the session, `transport.launched[]`. Part 3: recorded — `transport record` takes the
      session (with nothing armed, only it); spans land as clips, the arrangement under them cut, a note clip's `offset=`;
      the song renders exactly what was heard. Part 4: View › Session — the grid in the lanes' place, every gesture
      one line, fills and pulses eased; shots session-view(-mid), launch-recorded. N39 DONE.
- [x] **N40** A video lane (R-VIDEO-1). Round 18 (the design): one picture a song with an offset; the host probes,
      decodes, muxes; `video.time` in the model; `render --video` copies the stream beside the mix. Part 1: the service
      and the host — `#avideo`, `video set|remove`, `set vd_1.offset`, the probe kept per file, `video` in the model
      (`time` through the tempo map, moved by the pump), `VideoReader` (a frame at a time, fitted, forward with no
      seek), `render --video` and `muxVideo` (the stream copied, a keyframe cut, the end in decode order, the mix
      interleaved); L2 with a fake host, L3 `solaris_video` against the real FFmpeg and ffprobe. Part 2 (the plan's
      part 2 cut in two — the window, then the lane and the export): View › Show Video — `VideoPanel` in a window, the
      frame at `video.time` from the host's decoder (`App::onVideoFrame`), asked only on a change; the picture fading
      over the words, a jump dissolving; `video.path`/`at`/`end` in the model; shots video-window(-mid). Part 3: the
      picture's row between the ruler and the lanes (`top()` the lanes' LIVE top) — thumbnails at the model's
      `video.thumbs` points from a second host decoder, a few a frame, kept, faded in; dragged → ONE `set <vd>.at=<beat>`;
      the Export sheet's "With the video" (MP4 / MKV, `--video`); shots video-lane(-dragging), export-with-video. Found
      on the way: stale anchors in DR-AUTO-5, DR-CLIP-6 and DR-UI-10's step/zoom paragraphs re-pointed by symbol (D-22).
      N40 DONE (a live look at the GTK window's decoder owed).
- [~] **N41** Sharing through a folder: merge by node (R-COLLAB-1). Round 19 (the design): the base is what this window
      last read or wrote; an atomic save; the file looked at once a second; nothing unsaved → reload, else a three-way
      merge node by node (the versions' node table), the same new id renumbered on mine, conflicts asked (`project sync
      --take`), a save refused while one is pending. Next: part 1 — the merge in the model.
- [ ] **N42** Accessibility: UI scale, high contrast, AT-SPI names, keyboard reach (R-ACCESS-1).
- [ ] **N43** Localisation (R-ACCESS-2).
- [ ] **N44** Analysis tools: separation, audio-to-MIDI, chords (R-ASSIST-1).

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
