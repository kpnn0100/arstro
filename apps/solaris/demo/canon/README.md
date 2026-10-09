# Canon in D — a song made with `solaris-cc` alone (R-SVC-9)

Pachelbel's Canon (c. 1680, public domain) arranged as a 128 bpm EDM track: 128 beats, one minute.
An agent wrote it, with no window open, from `docs/API.md` and `docs/AGENTS.md`.

| file | what it is |
|---|---|
| `make_script.py` | spells the notes (the ground, the chords, the canon's three lines) and writes `song.txt` |
| `song.txt` | **the song** — about 90 `solaris-cc` lines, committed; ends in `project save` and `render` |
| `measure.py` | measures the render: not silent, under 0 dBFS, energy rising section by section, the kick on all 64 beats, the sidechain pumping the bass, line A in tune, the pad's filter opening |

```bash
cd <a scratch folder>
cp <repo>/apps/solaris/demo/canon/song.txt .
SOLARIS_SETTINGS=$PWD/settings.txt SOLARIS_RECENTS=$PWD/recents solaris-cc --script song.txt
python3 <repo>/apps/solaris/demo/canon/measure.py .     # needs numpy
```

It writes `canon.slp` (open it in `solaris`), `canon.wav`, and stems for the kick, bass, pad and canon.
`ctest -R solaris_demo_canon` does all of this, and fails if `song.txt` is not what `make_script.py`
writes. Change the song in `make_script.py`, then run `python3 make_script.py` here.

The form: intro (0–16, the pad alone, its filter opening on a bezier curve), hats (16–32), verse (32–64:
kick, groove, off-beat bass sidechained to the kick, line A then line B), build (64–80: a clap roll, the
filter rising), drop (80–112: line C's eighth notes, an arp, the filter held open), outro (112–128).
