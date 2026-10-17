# Solaris — Defects

`D-<n>`, sequential, never reused, never deleted; a resolved entry moves whole to **Closed** with its
commit hash and the test that now guards it. Entry format: `arstro.solaris.debug` §4 (filed by that
skill, which never fixes; fixed by `arstro.solaris.implement`). Reproductions live in `tests/repro/`.

## Open

### D-22 Bare `:N` anchors bound to the wrong file in eight DR sections (as-built docs drift)
- **Found:** 2026-10-17, N37 part 2's anchor pass: `follow2.py` could not follow six anchors (each past the end of the
  file the any-path convention binds it to), and a scan printing every anchor's target line (scratch `showanchors.py`)
  finds more of the same — a bare `` `:N` `` after a path, meant for ANOTHER file the paragraph names in words:
  DR-UI-8 (ten, `ProjectScreen.cpp` — the dock's lines), DR-UI-5 (three, `ParamMenu.cpp`), DR-UI-12 (thirteen,
  `ParamFormat.h`/`Theme.h` — the face's), DR-SVC-5 (three, `ControlServer.h`), DR-MIX-17 (nine, `Engine.h` —
  `Engine.cpp`'s), DR-VST-7 (four, `DevicePanel.h`/`PluginAccess.h`), DR-BROWSE-4 (one), DR-PERF-2 (one).
  DR-ROLL-11's four (`ServiceCompose.cpp` read as `Notation.cpp`'s) were fixed in N37 part 1.
- **Effect:** the as-built tier points at lines that are not the code it describes (D-1's class: a doc describing what
  the code no longer is). `anchorcheck_strict` does not see it — it checks an anchor against HEAD's line, which was
  already wrong.
- **More of the same kind** (found N39 part 1, by the same scan run on every section anchoring into a file the commit
  edited): about thirty anchors sit on a `{` or `}` line — most already did at HEAD (an anchor written one line after
  a two-line signature) — in DR-SVC-1, DR-AUTO-1/4/6/8, DR-EDM-1, DR-ROLL-11, DR-MIX-15/16, DR-SVC-8, DR-LANE-3,
  DR-BROWSE-4, DR-VST-6/7, DR-SAFE-1, DR-ARR-1/3, DR-REC-1, DR-PRESET-1, DR-PERF-3. A brace has no content of its
  own, so a follower that matches lines by content attaches it to whichever brace is nearest — they drift further with
  every edit of their file.
- **Recommended fix:** in each section, write the file before every anchor whose file differs from the last one named
  (`app/widgets/MixerDock.cpp:N`, not `` `:N` ``), at the line the paragraph means; then the scan prints no line past
  its file's end and no `{`, `}` or blank target.

### D-21 The keyboard-recording test's held note once fell outside 0.25 … 1.0 beats again (D-11's widened window)
- **Found:** 2026-10-17, N35 part 3: one run of `solaris_service` alone (load ~2.5–3) failed
  `test_record_the_keyboard_into_a_clip` at `pt->notes[0].length > 0.25 && pt->notes[0].length < 1.0`
  (core/tests/serviceTests.cpp:2399); three runs straight after, green. Nothing in N35 touches recording.
- **Suspected cause:** as D-11 — a key is stamped where the AUDIO thread drains it, and the test thread's sleeps between
  the press and the release are wall-clock; a descheduled test thread (or audio thread) stretches or squeezes the
  held length past the window D-11 widened it to. The value was not printed, so which side it fell is not known.
- **Recommended fix:** stamp the press and the release on the device's own clock (wait for frames between them —
  `waitFrames` — instead of sleeping), so the held length is counted in audio, not in scheduling; print the length
  on failure.

### D-17 The service tests' fakes race their own threads (ThreadSanitizer; the product is clean)
- **Found:** 2026-10-17, the TSan run of the service tests made to check N32's followers (the follower's own race —
  its target read off its thread — was fixed there). Five reports remain, all inside test fakes: (1, 2) the keyboard
  test reads `Capture::sink` (`peakOf`, serviceTests.cpp) while the player's thread still appends to it in `FakeOut::
  write`; (3) `FakePlugin`'s offline flag written by the engine's thread in `process`, read by `PluginConnection::
  offline()` from `pump`; (4) `FakePlugin`'s event list filled in `process`, swapped in `takeEvents` from `pump`.
- **Effect:** none in the product — the races are between a fake and the test or the service reading it; the asserts
  read values a few blocks old at worst. But a TSan run is only a guard while it is clean.
- **Recommended fix:** the capture read only after `transport stop` (or under a mutex the fake takes); the fake
  plugin's flag an atomic and its event list behind a mutex (it is a fake: a lock costs nothing there). Then the TSan
  run of the suite clean, and kept so.

### D-13 The looped-recording test once counted fewer takes, run straight after a parallel build
- **Found:** 2026-10-16, running N18's mutants: one run of `solaris_service_tests`, started the moment a `-j16` build
  finished, failed `test_a_looped_recording_makes_takes_and_a_comp_chooses` at `contains(said, "recorded 3 takes of
  1.0 s")`; the mutant under test (in `clip consolidate`) touches nothing it runs. Twelve runs since, idle or not, green.
- **Suspected cause:** as D-11 — the fake capture is paced by the audio thread, and `waitFrames` lets the test thread
  stop the transport after 3.4 passes as COUNTED; a descheduled audio thread can leave the passes HEARD short of three,
  the third then under half and dropped. Not reproduced, so not yet measured.
- **Recommended fix:** wait on the heard position (the song past the third turn) rather than on captured frames, or
  4.4 passes with the fourth dropped by rule — then four runs under a parallel build.
- **Seen again, 2026-10-17 (N32 part 3's mutant run):** `test_a_looped_recording_makes_takes_and_a_comp_chooses` once
  more ("recorded 3 takes"), a mutant in `DriftIn` (which that test never builds — its input is the default one)
  built with `-j16` the moment before; the same mutant run again went past it.
- **Seen again, 2026-10-17 (N28's mutant run):** `test_record_the_keyboard_into_a_clip` (a held note's length) once
  more, the restored suite run straight after a `-j16` build; twice green right after.
- **Seen again, 2026-10-17 (N26's mutant run):** the RESTORED suite, straight after a `-j16` build, failed once in
  `Run::no` (a line expected to be refused was accepted) — which line, the run's filter did not keep (it now keeps the
  `accepted` line). Six idle runs since, green. Logged here as the same family until the line is known.
- **Seen again, 2026-10-17 (N23's mutant run):** `test_record_the_keyboard_into_a_clip` failed once on a held note's
  length (D-11's widened range), run straight after a `-j16` build — the same cause: the fake devices are paced by an
  audio thread a busy machine deschedules. Both tests green in every idle run. And once more (N24's mutant run, the
  restored build's first run): the same keyboard test — then three idle runs green.

### D-2 A strip's new name does not reach its lane or its pattern — lane half FIXED in E3 (DR-LANE-3), pattern half open
- **Found:** 2026-10-09, making the canon (C5) with `solaris-cc` alone.
- **Symptom:** after the reproduction below, `ls` shows `ch_2 "Bass"` but still
  `lane ln_1 "Basic Synth"` and `pattern pt_1 "Basic Synth"`. A song of four synths shows four lanes
  called "Basic Synth" — the timeline cannot tell the bass from the pad.
- **Reproduction:** `project new d.slp` · `clip add --instrument synth --at 0 --length 4` ·
  `set ch_2.name=Bass` · `ls`.
- **Cause:** the lane and the pattern copy the strip's name when they are made (`laneFor`,
  `core/service/ServiceEdit.cpp:629`); nothing links them after.
- **Recommended fix:** a lane or pattern made for a strip keeps that strip's name while it was never
  renamed itself — renaming the strip renames the ones still carrying its old name, in the same edit.
  Needs an R-LANE line first (lanes are organisation only, R-LANE-1, so the rule is about names alone).
- **Lane half fixed (E3, 2026-10-09):** a lane made for an instrument is its TRACK (R-LANE-3) with no name
  of its own and shows the strip's (`laneTitle`); older songs' lanes are adopted, a name they were made
  with cleared. Guard: `test_instrument_tracks`, `test_a_lane_is_an_instruments_track`. The pattern half
  remains: a pattern made by `clip add` is still named after its strip as it was then.

### D-3 A kit's parameter names are not its pad names
- **Found:** 2026-10-09, making the canon (C5).
- **Symptom:** `pattern steps pt_2 --pitch closed-hat …` is accepted, but `set dv_1.closed-hat.level=-3`
  is refused — the parameters are `chat.level`, `ohat.level`, `ltom.*`. The refusal lists every field,
  so an agent recovers, but the same pad has two names in one grammar.
- **Cause:** the registry's note names (the pads) and its parameter prefixes (`DrumMachine`'s
  `DeviceRegistry` entry in the DSP library) were named apart.
- **Recommended fix:** resolve a pad's note name as an alias of its parameter prefix in `setAddress` /
  `getAddress` (or rename one side in the registry, which would change saved `.slp` keys — not that).
  (**2026-10-09, E6:** the registry now publishes the join — `DeviceType::notePrefixes`, DSP REQ-device-9:
  42 "Closed Hat" ↔ `chat` — and the plugins' editor uses it for its pads. The fix above is now a lookup.)

## Closed

### D-23 `version rebase --dry-run` was an edit: the song marked unsaved, a history step added — closed in N38 part 1
- **Found:** 2026-10-17, designing N38's `--dry-run` flags: `dispatch` decides "an edit" by the verb alone, and
  `version rebase` (N37) is an edit verb — so its dry run, which changes nothing, still snapshotted, added a history
  node ("version rebase vr_1 --dry-run") and set the song unsaved.
- **Fix:** a `--dry-run` is never an edit, whatever the verb (`core/service/SolarisService.cpp`, `dispatch`'s
  `editing`): no snapshot, no history step, not unsaved — for `version rebase`, `project collect` and `project relink`.
- **Guard:** `test_collect_and_relink` — after `project save`, `project collect --dry-run` and `version rebase Alt
  --dry-run` leave the song saved and the history without a rebase (mutant: the verb alone decides — seen red).

### D-20 The frozen-plugin test could fail with its helper left stopped — and ctest waited on it for 20 minutes — closed in N34 (the pitch shifter's bump)
- **Found:** 2026-10-17, the ctest run after the pitch shifter's bump (`-j6`): `solaris_plugins` failed and the run
  hung. One `solaris-plughost --serve ArstroBasicSynth.vst3` was left in state T (stopped), re-parented to the user's
  systemd, its stderr still ctest's pipe for that test — so ctest waited for an end that never came. Killed by hand
  (our own test's helper), the run ended at 95%. Three runs of the test alone: green. (Its output was lost: a `ctest -N`
  run beside the hung one rewrote the log — don't.)
- **Cause:** `pluginsTests.cpp`'s frozen block sent SIGSTOP and timed a block at once — but a stop lands when the
  helper's threads next run, which on a busy machine can be after the block; the block then played, `assert(!late …)`
  aborted the test, and an abort runs no cleanup: nothing sent the SIGCONT or the instance's SIGKILL, and the helper
  stayed frozen with the pipe open. (The product's own path — overrun → offline → SIGKILL, PluginInstance.cpp — is
  sound: a frozen helper it times out is killed.)
- **Fix:** the block is timed only once /proc says the helper IS stopped (state T, up to 2 s); its result is read and
  the helper continued BEFORE any assert, so a failing test leaves nothing frozen. A deliberately failing assert there
  now ends the test in 1 s with no helper alive (checked). Test-only.

### D-18 The library's delay line extrapolated between samples — a swept chorus or echo gained treble — closed in N34, DSP `2148541`
- **Found:** 2026-10-17, reading `Delay::read` (core/DigitalSignalProcessing/src/simpleProcessor/Delay.cpp) to build
  N34's flanger on it. For a delay of D + f samples (0 < f < 1) it returns `s[D]·(1 + f) − s[D − 1]·f`: the line
  through the two samples NEARER than the delay, carried past the newer one — linear extrapolation, not
  interpolation. Its gain at a frequency ω is |(1 + f) − f·e^(jω)|, up to 1 + 2f toward Nyquist (f = ½: +5.8 dB at
  20 kHz where interpolation gives −11.7 dB). Whole-sample delays read correctly, so a still delay hid it.
- **Effect:** the "chorus" (its delay swept every sample) and the "delay" (echo times in ms are rarely whole samples)
  add treble that moves with the fraction — a chorus's top end flutters; an echo fed back brightens at every repeat
  instead of darkening as its own filter intends. A flanger built on it with feedback 0.9 would run away (a loop
  gain of 0.9·(1 + 2f) > 1 toward Nyquist).
- **Recommended fix:** read between `s[⌊D⌋]` and `s[⌊D⌋ + 1]` with weights 1 − f and f — the same value at a whole
  sample (nothing that reads whole samples moves), the treble never above unity; a test that a fractional delay
  passes a sine at |cos(ω/2)| (f = ½) and never above 1 at any fraction.
- **Fixed:** DSP `2148541` (REQ-delay-1): `read` now weighs b[i] and b[i + 1] by 1 − f and f — the 20 kHz sine through
  a 20.5-sample delay −11.74 dB (it was +5.80); whole-sample reads unchanged. Guarded by `Delay_reads_between_samples`
  (the DSP's unittest/fxTests.cpp), seen red on the old read. The "chorus" and the "delay" lose the added treble.

### D-19 The plugin-editor test stopped before its plugin could crash — closed in N34 (the phaser's bump)
- **Found:** 2026-10-17, the ctest run after N34's phaser commit was rebased: `test_a_plugins_own_editor` failed at
  `accepted \`device editor dv_1\` (expected a refusal)` — the run before, the same code, green.
- **Cause:** `waitFrames(r, 4800)` compares the fake capture's frames since the test began; an earlier play in the
  same test had waited for 9600, so the wait returned at once and `transport stop` raced the player's first block —
  when it won, the fake plugin never ran a block, never crashed, and its editor opened. The same shape guarded the
  editor-drag play above it.
- **Fix:** the capture's count zeroed before each of those plays (as the tests at serviceTests.cpp:4267 and :5939
  already do), so each wait means "this play has run 4800 frames". Test-only; the product was right.

### D-16 `clip duplicate` read its source clip after the copy may have moved it — closed in N30
- **Found:** 2026-10-17, a ThreadSanitizer build of the service tests (built to check N30's pool) reported a
  heap-use-after-free in `clipCommand`: `ServiceEdit.cpp`'s `clip duplicate` compared `copy.track != src->track`
  after `p.clips.push_back(copy)` — `src` points into `p.clips`, which the push may reallocate.
- **Effect:** a read of freed memory after every duplicate that grew the vector: the "it plays through …" line could
  be said wrongly or not at all; undefined behaviour, crash possible.
- **Fix:** the source's track read before the copies are pushed.
- **Guard:** the TSan run of the suite (no report from it since); the duplicate tests themselves.

### D-15 The shipped-presets test read the model through a reference its own commands freed — closed in N25 part 2
- **Found:** 2026-10-17, building N25 part 2: `test_the_shipped_presets_load` failed once on `assert(t)` (a preset whose
  type the registry did not know) and once refused `preset load dv_4 ""` (a preset with no name) — with no change to
  presets anywhere.
- **Cause:** the test walked `r.svc.model().presets` by reference while each `clip add` / `preset load` in the loop
  rebuilt the model (`mModel = AppModel()`), freeing that vector; it read freed memory, and passed for as long as the
  allocator happened to hand the same block back. N25's model refresh allocates differently, and the luck ran out.
- **Fix:** the loop walks a copy taken before it.
- **Guard:** the test itself — green in every run since; the other loops over the model in the suite dispatch nothing
  inside them.

### D-14 The DSP library's tempo estimate shipped without its sub-frame refinement — closed in N21 part 1, DSP `6f2df21`
- **Found:** 2026-10-16, building N21's time map: `Tempo_is_found` turned red with no change to the tempo code (133 bpm
  read 132.35).
- **Cause:** a mutant (B3, "no parabola": `best + 0.0 * off`) was left in `src/meter/Onsets.cpp` — the mutant run was
  piped through `head`, which ended the script after it applied that mutant and before it put the file back — and DSP
  `c526f8d` was committed without the tests being run again. Solaris N20 (`824957b`, `24b16bf`) was built on it; its own
  tests passed, a groove of whole beats being snapped (B5) whatever (B4) said.
- **Fix:** the line restored (DSP `6f2df21`). A mutant script is never piped to anything that can stop reading early;
  the tests run again after every mutant run, before a commit.
- **Guard:** `Tempo_is_found` (133 → 132.95; red at 132.35 without (B4)).

### D-12 A recorded clip pointed beside the working folder, not the song — closed in N17
- **Found:** 2026-10-16, building N17's takes: the test's decoder now hands back what was recorded, and a take's clip
  played a half-second sine — the file it named was not found.
- **Cause:** `commitAudioTake` gave `clip add` the source `Recordings/<name>`; `clip add` reads a relative source against
  the working folder (as a shell would) and keeps it relative to the song — so the clip named `<cwd>/Recordings/…`.
- **Fix:** the full path is given (`clip add` keeps it relative to the song); takes the same; the decode cache cleared by
  the resolved path.
- **Guard:** `test_record_audio_lands_where_it_was_played` (the clip's source `Recordings/Vox 1.wav`) — red with the old line.

### D-11 The keyboard-recording test failed now and then under a busy machine — closed in N15
- **Found:** 2026-10-16, running the service suite four times while building N15.
- **Symptom:** `test_record_the_keyboard_into_a_clip` asserted a held note of 0.3 … 0.8 beats; one run in four read
  more — a key is stamped where the AUDIO thread drains it, so a test thread descheduled after `waitFrames` lets the
  audio run on before the key reaches it.
- **Fix:** 0.25 … 1.0 beats — half a beat held, still told apart from nothing and from a whole beat.
- **Guard:** the test itself, four runs in a row green.

### D-10 A test held a reference into the model across commands — it read freed memory — closed in N15
- **Found:** 2026-10-16, N15's new model fields moved allocations and `test_notes_move_and_delete_a_selection_as_one_edit`
  began to fail one run in three, on notes that were there.
- **Cause:** `const auto &n = model().patterns[0].notes` kept across `r.ok(…)`; every refresh rebuilds the model, so
  the reference pointed into a freed vector — it had worked by the allocator's luck.
- **Fix:** the test reads the notes afresh each time (`notesNow()`); the other held references in the suite were
  checked — each is read before the next command. (A front end copies what it binds; no product code holds one.)
- **Guard:** the test, four runs in a row green.

### D-9 Nothing typed in the window reached a field — a rename or the tempo typed nothing — closed in J3
- **Found:** 2026-10-16, reading the host's key mapping for the keymap (R-SET-4).
- **Symptom:** in the real window, a rename field (a strip's, a lane's, a clip's) or the tempo's typed value took no
  characters: Backspace and Enter worked, letters and digits did nothing. Every UI test typed by sending a Text event
  straight into `App::key`, so the suite never saw it.
- **Cause:** the GTK host (`linux_main.cpp`, `onKey`) sent only key-down codes; the rename field (cosmo's
  `ContextMenu::handleKey`) takes characters only from Text events.
- **Fix:** the host sends a Text event with what the press types (a printable character, no Ctrl or Alt —
  `keyTypes`, `app/KeyInput.h`) after the key's Down, as Genesis's host does.
- **Guard:** `test_the_keys_are_the_machines` covers `keyTypes` (a letter and a space type; Ctrl+letter, Enter and
  Delete do not). The GTK half is compiled and read, not driven live — as D-5, this GTK build ignores synthetic X
  events and XTest is not to be used on the user's display. Live check owed: rename a strip by typing.

### D-8 A synth's volume, automated or set while a note sounded, did nothing to that note — closed in I4a, DSP `0707803`
- **Found:** 2026-10-15, making the EDM track (R-SVC-12): the riser — one long note — was automated to swell from
  −40 dB to −4 dB through each build; measured, each build's last bars were no louder than its first.
- **Reproduction:** a synth note held 2 s; `set dv_1.volume=-20` (or an automation on it) after 1 s → the note's
  level unchanged until the next note-on.
- **Cause:** `BasicSynth::noteOn` multiplied the volume into the voice's gain; the Sampler did the same with its
  level. (The Drum Machine and the Piano already applied theirs per block.)
- **Fix (DSP, REQ-synth2-4 amended):** the velocity stays the note's; the volume / level is applied per block,
  ramped linearly from where the last block ended — no step.
- **Guard:** DSP `Volume_and_level_move_a_sounding_note` (exactly −20 dB on the held note, no step; a stepped volume
  seen red); the EDM track's measured builds (`demo/edm/measure.py`).

### D-7 A note narrower than 5 px could not be moved — every press grabbed its resize edge — closed in H10c
- **Found:** 2026-10-15, making the Prelude zoomed out (R-SVC-11): a sixteenth is ~5 px wide there, and dragging a
  selected voice up two semitones sent nothing — the press was "on the edge", a resize, which only reads x.
- **Cause:** the resize zone was a note's right 5 px (`kEdge`), the whole of a note that narrow.
- **Fix:** the zone is the right 5 px or the right third of the note, whichever is smaller (`noteAt`), so a short
  note keeps a body to grab; a wide note's edge is as before.
- **Guard:** `test_a_short_drag_lands_where_it_was_let_go` — a ~5 px note grabbed at its body moves up two rows as
  ONE `note move`, no resize; mutant (the old 5 px zone) seen red.

### D-6 A note dragged less than 5 px was a click: the resize or the move was lost — closed in H10b
- **Found:** 2026-10-15, making the Prelude zoomed out (R-SVC-11): a sixteenth is ~6 px there, and pulling the
  tenor's end back one sixteenth did nothing — and every bar after was built on the wrong length.
- **Symptom:** a note's edge or body dragged by under 5 px (the gesture recognizer's drag threshold) sent nothing;
  the release arrived as a Click, which on empty grid ADDED a note.
- **Cause:** the roll took the pointer's position only from Drag gestures, which start past the threshold; the
  Up was ignored for position, and the recognizer's Click after it was handled as a click.
- **Fix:** the drag's tracking (`PianoRoll::track`) runs at every Drag and once more at the release when the
  pointer moved at all; the Click the recognizer still sends after such a release is swallowed. A release where
  the press was (no movement) is a click and moves nothing — an off-grid note is not re-snapped by a click.
- **Guard:** `test_a_short_drag_lands_where_it_was_let_go` (a 4 px pull lands as `note move … --length 1.75` and
  adds nothing; a click on an off-grid note moves nothing) — mutant (no tracking at release) seen red.

### D-5 Shift+wheel and sideways scrolling never reached the app from the window — closed in H10a
- **Found:** 2026-10-15, making the Prelude (R-SVC-11): bar 35 had to be brought into the roll's view, and the
  roll scrolls sideways on Shift+wheel (as the lanes do, NOTES "Lanes › Shift+wheel") — which no person could do.
- **Symptom:** in the real window, Shift+wheel over the roll or the lanes scrolled UP AND DOWN; a tilted wheel's
  or a touchpad's sideways scroll did nothing at all.
- **Cause:** the GTK host (`linux_main.cpp`, `onScroll`) passed only Ctrl to `App::wheel`, which took no Shift,
  and dropped `GDK_SCROLL_LEFT/RIGHT` and a smooth scroll's `delta_x`. The widgets were right; the seam lost it.
- **Fix:** `App::wheel(…, ctrl, shift)` carries Shift into the gesture; the host passes it, and turns a sideways
  scroll into Shift+wheel (left toward the start).
- **Guard:** `test_the_wheel_scrolls_sideways_with_shift` (the roll and the lanes, through `App::wheel` — mutant: Shift
  dropped in `App::wheel`, seen red). The host's half (`onScroll`) is compiled and read, NOT driven live: this GTK
  build ignores synthetic (XSendEvent) pointer events even with `GDK_CORE_DEVICE_EVENTS=1` — a synthetic click on
  a tab did nothing either — XTest would move the user's own pointer on their display, and no virtual X server is
  installed here. Live check owed: Shift+wheel and a touchpad swipe over the lanes and the roll.

### D-4 The Basic Synth's Resonance did nothing — closed in G3, DSP `66b5ecb`
- **Found:** 2026-10-15, building the analog face's filter screen (R-UI-12): its test measures the real filter
  against the documented prototype, and every resonance peaked at −3.01 dB (Q 0.707).
- **Reproduction:** `clip add --instrument synth`, a note, `set dv_1.filter.cutoff=1000 dv_1.filter.res=0` →
  render; again with `res=0.9` → the two renders were byte-identical (band 800–1250 Hz: 43.44 dB both).
- **Cause:** `StateVariableFilter::tick` (the per-sample path the synth and the drum voices use) took `k` from the
  SMOOTHED resonance, which only the block path advances; a resonance written after construction never arrived.
- **Fix:** `tick` derives `k` from the resonance's target, re-derived only when it changes (REQ-svf-2 amended).
- **Guard:** DSP `SVF_tick_follows_a_resonance_set_after_it_was_made`, `BasicSynth_resonance_is_heard` (both seen
  red before the fix); Solaris `test_the_filter_screen_is_the_filter`.

### D-1 A refused edit announced changes it did not make — closed in U2, `413fec4`
- **Found:** 2026-10-08, building U2's one-command instrument drop (reading `changed()`).
- **Symptom:** `clip add --src new.wav --in -1` is refused and the project is restored, but the
  stream already carried `project.changed what=strip.added node=ch_3` — an agent following the
  stream believes a strip exists that does not.
- **Cause:** `changed()` emitted at once, inside the command, before validation could still refuse
  it; only `set` held its events.
- **Fix:** every edit's events are held in `mPending` and emitted only once the command has landed
  (DR-SVC-1).
- **Guard:** `solaris_service` — `test_an_instrument_drop_is_one_command_and_a_refusal_says_nothing_changed`
  (fails with `changed()` emitting directly: checked).
