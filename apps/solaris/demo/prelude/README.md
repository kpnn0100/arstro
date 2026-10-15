# Prelude in C — a song made in the window (R-SVC-10, R-SVC-11)

J. S. Bach's Prelude in C major, BWV 846 (The Well-Tempered Clavier I, 1722 — public domain), on the
DSP library's physical piano (R-INST-5), made through Solaris's **window**: every note entered by the
gestures a person makes — clicks, drags, keys and the wheel at the geometry the app draws — never a
command line. Where the canon (`../canon`) proves an agent can make a song with `solaris-cc` alone, this
proves a person can make one with the window, and measured what it cost (`../../docs/ux-audit.md`).

| file | what it is |
|---|---|
| `score.py` | the score as a table: 32 bars of one figure over five-note chords, two runs over the pedal, the last chord — 549 notes, 140 beats. Writes `score.txt`; `--reference <file>` writes the score as a `solaris-cc` song |
| `score.txt` | **the score** — one note a line, `pitch at length`; the replica is diffed against it |
| `makePrelude.cpp` | the hand (`solaris_demo_prelude <dir>`): the real app driven gesture by gesture — Home › New song, the tempo dragged to 66, the Piano dragged in, its clip's roll opened, moved, resized and zoomed; the first half bar with the length chip; then each bar a box over the bar before, Ctrl+D, and only the voices that change boxed and moved; the runs and the last chord clicked in; a reverb from "+ Effect"; Ctrl+S; File › Render… — and every gesture counted |
| `measure.py` | the notes against the score (99 % at least, the goal all of them), and the render against the score's own render bar by bar, its length, level and pulse |

```bash
SOLARIS_SETTINGS=$PWD/s.txt SOLARIS_RECENTS=$PWD/r build/apps/solaris/app/solaris_demo_prelude <dir> [--shots]
ctest --test-dir build -L long                      # all of it, measured: prelude_made_in_the_window (~7 min, Debug)
```

The driver runs the app headlessly through the test rig (`app/tests/Rig.h`) — the same `App` and service
the window runs, minus GTK; its host is the rig's, whose WAV writer writes nothing, so the WAV is rendered
from the saved song by `solaris-cc` (the same service, compiler and engine).

**Measured (2026-10-15):** 549 of 549 notes at their pitch, start and length, nothing extra; all 35 bars
the same samples as the score's own render; 128.2 s; peak −2.8 dBFS; the pulse a sixteenth at 66 bpm.
**376 gestures** — the first two bars, with the app as it was, cost 50 (about 860 for the song).
