# Solaris — Defects

`D-<n>`, sequential, never reused, never deleted; a resolved entry moves whole to **Closed** with its
commit hash and the test that now guards it. Entry format: `arstro.solaris.debug` §4 (filed by that
skill, which never fixes; fixed by `arstro.solaris.implement`). Reproductions live in `tests/repro/`.

## Open

### D-2 A strip's new name does not reach its lane or its pattern — lane half FIXED in E3 (DR-LANE-3), pattern half open
- **Found:** 2026-10-09, making the canon (C5) with `solaris-cc` alone.
- **Symptom:** after the reproduction below, `ls` shows `ch_2 "Bass"` but still
  `lane ln_1 "Basic Synth"` and `pattern pt_1 "Basic Synth"`. A song of four synths shows four lanes
  called "Basic Synth" — the timeline cannot tell the bass from the pad.
- **Reproduction:** `project new d.slp` · `clip add --instrument synth --at 0 --length 4` ·
  `set ch_2.name=Bass` · `ls`.
- **Cause:** the lane and the pattern copy the strip's name when they are made (`laneFor`,
  `core/service/ServiceEdit.cpp:418`); nothing links them after.
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
