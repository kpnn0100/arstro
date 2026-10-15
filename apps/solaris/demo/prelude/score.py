#!/usr/bin/env python3
"""The score of J. S. Bach's Prelude in C major, BWV 846 (The Well-Tempered Clavier I, 1722 — public
domain), as a table: the replica is diffed against it note by note (Solaris R-SVC-10).

Bars 1–32 are one figure: a half bar holds a bass (a half note), a tenor from the second sixteenth
(tied to the half bar's end), and three upper notes as sixteenths played twice — so a bar is five
pitches. Bars 33–34 break the figure over the tonic pedal; bar 35 is the final chord. The 35 bars of
Bach's autograph (no Schwencke bar). Pitches are MIDI numbers, C4 = 60; a beat is a quarter note.

    python3 score.py            # writes score.txt: one note a line, `pitch at length`
    python3 score.py --reference ref.txt   # a solaris-cc script: the score as a song, for measure.py
"""
import os

N = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def p(name):
    """`F#4`, `Bb3` → MIDI."""
    letter, rest = name[0], name[1:]
    acc = 0
    while rest and rest[0] in '#b':
        acc += 1 if rest[0] == '#' else -1
        rest = rest[1:]
    return 12 * (int(rest) + 1) + N[letter] + acc


# bars 1–32: bass, tenor, then the three upper notes of the figure
CHORDS = [
    "C4 E4 G4 C5 E5",    # 1  C
    "C4 D4 A4 D5 F5",    # 2  Dm7/C
    "B3 D4 G4 D5 F5",    # 3  G7/B
    "C4 E4 G4 C5 E5",    # 4  C
    "C4 E4 A4 E5 A5",    # 5  Am/C
    "C4 D4 F#4 A4 D5",   # 6  D7/C
    "B3 D4 G4 D5 G5",    # 7  G/B
    "B3 C4 E4 G4 C5",    # 8  Cmaj7/B
    "A3 C4 E4 G4 C5",    # 9  Am7
    "D3 A3 D4 F#4 C5",   # 10 D7
    "G3 B3 D4 G4 B4",    # 11 G
    "G3 Bb3 E4 G4 C#5",  # 12 C#dim7/G
    "F3 A3 D4 A4 D5",    # 13 Dm/F
    "F3 Ab3 D4 F4 B4",   # 14 Bdim7/F
    "E3 G3 C4 G4 C5",    # 15 C/E
    "E3 F3 A3 C4 F4",    # 16 Fmaj7/E
    "D3 F3 A3 C4 F4",    # 17 Dm7
    "G2 D3 G3 B3 F4",    # 18 G7
    "C3 E3 G3 C4 E4",    # 19 C
    "C3 G3 Bb3 C4 E4",   # 20 C7
    "F2 F3 A3 C4 E4",    # 21 Fmaj7
    "F#2 C3 A3 C4 Eb4",  # 22 F#dim7
    "Ab2 F3 B3 C4 D4",   # 23 Bdim7/Ab
    "G2 F3 G3 B3 D4",    # 24 G7
    "G2 E3 G3 C4 E4",    # 25 C/G
    "G2 D3 G3 C4 F4",    # 26 G7sus4
    "G2 D3 G3 B3 F4",    # 27 G7
    "G2 Eb3 A3 C4 F#4",  # 28 F#dim7/G
    "G2 E3 G3 C4 G4",    # 29 C/G
    "G2 D3 G3 C4 F4",    # 30 G7sus4
    "G2 D3 G3 B3 F4",    # 31 G7
    "C2 C3 G3 Bb3 E4",   # 32 C7
]
# bars 33–34: the pedal C2 and an inner voice held the whole bar, a run of fourteen sixteenths above
RUNS = [
    ("C2", "C3", "F3 A3 C4 F4 C4 A3 C4 A3 F3 A3 F3 D3 F3 D3"),   # 33
    ("C2", "B2", "G4 B4 D5 F5 D5 B4 D5 B4 G4 B4 D4 F4 E4 D4"),   # 34
]
FINAL = "C2 C3 E4 G4 C5"                                          # 35: a whole-note chord


def notes():
    out = []  # (pitch, at, length) in beats
    for b, chord in enumerate(CHORDS):
        bass, tenor, a, c, d = (p(x) for x in chord.split())
        for half in (0, 2):
            t = b * 4 + half
            out.append((bass, t, 2.0))
            out.append((tenor, t + 0.25, 1.75))
            for k, q in enumerate((a, c, d, a, c, d)):
                out.append((q, t + 0.5 + 0.25 * k, 0.25))
    for i, (bass, inner, run) in enumerate(RUNS):
        t = (32 + i) * 4
        out.append((p(bass), t, 4.0))
        out.append((p(inner), t + 0.25, 3.75))
        for k, q in enumerate(run.split()):
            out.append((p(q), t + 0.5 + 0.25 * k, 0.25))
    for q in FINAL.split():
        out.append((p(q), 34 * 4, 4.0))
    return sorted(out, key=lambda n: (n[1], n[0]))


def reference_script(out):
    """The score as a song, straight from the table (no window): the same piano, reverb and tempo the window's
    song has — so where the notes are the same, the two renders are the same samples (render is pure)."""
    lines = ["project new reference.slp --bpm 66",
             "clip add --instrument piano --at 0 --length 140",
             "set pt_1.length=140"]
    ns = notes()
    for i in range(0, len(ns), 64):
        toks = " ".join(f"{q}@{t:g}:{l:g}" for q, t, l in ns[i:i + 64])
        lines.append(f'notes add pt_1 "{toks}"')
    lines += ["device add ch_2 --type reverb", "project save", "render --out reference.wav"]
    with open(out, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == '__main__':
    import sys
    here = os.path.dirname(os.path.abspath(__file__))
    if len(sys.argv) > 2 and sys.argv[1] == "--reference":
        reference_script(sys.argv[2])
        sys.exit(0)
    ns = notes()
    with open(os.path.join(here, 'score.txt'), 'w') as f:
        f.write("# J. S. Bach, Prelude in C major BWV 846 — pitch at length (beats), written by score.py\n")
        for q, t, l in ns:
            f.write(f"{q} {t:g} {l:g}\n")
    print(f"{len(ns)} notes, {35 * 4} beats")
