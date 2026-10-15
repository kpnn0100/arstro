# UX audit — what making a song in the window found (R-SVC-11)

The user, 2026-10-15: *"during making that song, analyze the UX, if any point that don't make sense to user
fix it — the goal is a production ready solaris app."* The song is J. S. Bach's Prelude in C, BWV 846
(public domain), made through the window's gestures by `demo/prelude/makePrelude.cpp` — the real app,
driven as a hand drives it, every gesture counted.

Each finding: **what was being done · what got in the way · what a person expects · how bad · what
became of it.** Severity: **high** — a person cannot do it, or loses work without being told; **medium**
— it can be done, at a cost no one would pay twice; **low** — it reads wrong. A finding is closed by a
task with its own requirement (ledger `PROGRESS.md`, H6…) or deferred with the reason.

## Attempt 1 — the app as it was (2026-10-15, after H4)

Home › New song; Browser › Instruments › Piano dragged onto the lanes; its MIDI clip double-clicked; the
first two bars entered in the roll. **32 notes cost 50 gestures** — 34 clicks, 13 drags, 2 wheel notches,
a double-click. At that rate the 549 notes of the Prelude cost about 860.

| id | doing | in the way | expected | how bad | became |
|---|---|---|---|---|---|
| U-1 | starting the song at its tempo (66) | the song bar shows "120 bpm" and nothing changes it; only Settings › New-song tempo, for the NEXT song | the tempo is a control where it is shown — dragged, or typed | high | **closed** — H6, R-UI-13 (DR-UI-13) |
| U-2 | entering a figure that repeats 70 times | the roll has no selection: no copy, no duplicate, no moving or transposing several notes — every note of every bar is a click | select (click, Shift+click, a box), duplicate after itself, copy and paste, move and transpose the selection, delete it — one edit each | high | **closed** — H7, R-ROLL-8 (DR-ROLL-8) |
| U-3 | the second bar | a note past the pattern's end (4 beats) is drawn dimmed and NEVER PLAYS; nothing says so; the clip on the lanes stays 4 beats | the pattern grows to hold what is written in it, and a clip that plays the whole pattern grows with it | high (silent loss) | **closed** — H8, R-ROLL-9 (DR-ROLL-9) |
| U-4 | the bass (2 beats), the tenor (1¾), the figure (¼) | a click makes a note of the LAST length, shown nowhere; changing it is dragging some note's edge — 13 of the 50 gestures | the length a click makes is shown, and chosen in one gesture | medium | **closed** — H9, R-ROLL-10 (DR-ROLL-10) |
| U-5 | a piece from C2 to A5 (46 rows) | the roll shows ~27 rows and no window can be resized — the wheel, again and again | a window is resized by its corner, as every window is | medium | **closed** — H9, R-WIN-5 (DR-WIN-5) |
| U-6 | reading the piano's knobs | unitless physics constants read as percentages ("Bass Growl 1 %", "Drive 22 %"), and "Treble Shimm…" is cut | a number as its range reads (0.008, 0.22); a label that fits | low | deferred: the registry gives no unit for these — R-DSP-2's description should say "a coefficient" (a DSP-side unit, not a Solaris guess); recorded for the next registry round |
