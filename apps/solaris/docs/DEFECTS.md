# Solaris — Defects

`D-<n>`, sequential, never reused, never deleted; a resolved entry moves whole to **Closed** with its
commit hash and the test that now guards it. Entry format: `arstro.solaris.debug` §4 (filed by that
skill, which never fixes; fixed by `arstro.solaris.implement`). Reproductions live in `tests/repro/`.

## Open

### D-13 The looped-recording test once counted fewer takes, run straight after a parallel build
- **Found:** 2026-10-16, running N18's mutants: one run of `solaris_service_tests`, started the moment a `-j16` build
  finished, failed `test_a_looped_recording_makes_takes_and_a_comp_chooses` at `contains(said, "recorded 3 takes of
  1.0 s")`; the mutant under test (in `clip consolidate`) touches nothing it runs. Twelve runs since, idle or not, green.
- **Suspected cause:** as D-11 — the fake capture is paced by the audio thread, and `waitFrames` lets the test thread
  stop the transport after 3.4 passes as COUNTED; a descheduled audio thread can leave the passes HEARD short of three,
  the third then under half and dropped. Not reproduced, so not yet measured.
- **Recommended fix:** wait on the heard position (the song past the third turn) rather than on captured frames, or
  4.4 passes with the fourth dropped by rule — then four runs under a parallel build.

### D-2 A strip's new name does not reach its lane or its pattern — lane half FIXED in E3 (DR-LANE-3), pattern half open
- **Found:** 2026-10-09, making the canon (C5) with `solaris-cc` alone.
- **Symptom:** after the reproduction below, `ls` shows `ch_2 "Bass"` but still
  `lane ln_1 "Basic Synth"` and `pattern pt_1 "Basic Synth"`. A song of four synths shows four lanes
  called "Basic Synth" — the timeline cannot tell the bass from the pad.
- **Reproduction:** `project new d.slp` · `clip add --instrument synth --at 0 --length 4` ·
  `set ch_2.name=Bass` · `ls`.
- **Cause:** the lane and the pattern copy the strip's name when they are made (`laneFor`,
  `core/service/ServiceEdit.cpp:462`); nothing links them after.
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
