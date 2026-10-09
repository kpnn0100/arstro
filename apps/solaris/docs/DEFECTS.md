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
