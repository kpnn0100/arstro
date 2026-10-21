#!/usr/bin/env python3
"""Porch Light — an original future bass track that tells a story, in the style of San Holo and Illenium (Solaris
R-SVC-17), written as a solaris-cc script.

THE STORY (written for this track). After her mother died she drove back to the house to pack it up. The porch light
was on — her mother always left it on for her. Inside were the rooms, a voicemail she had never deleted, the grief, a
photograph. When she left, she left the light on.

    intro      1–8    the key in the door   the guitar alone, her chords
    verse      9–24   her rooms             HER THEME (the leitmotif), stated on the guitar
    build      25–32  the voicemail         a dominant pedal; her voice (vowel chops) calling the motif, rising
    drop       33–48  grief                 the theme on a wide lead over chords BORROWED from the minor (iv, ♭VI),
                                            sagging on the pitch wheel; a talking growl at each phrase's end
    breakdown  49–64  the photograph        the theme AUGMENTED on a bare piano; the guitar answers it INVERTED;
                                            V goes to ♭VI instead of home — a DECEPTIVE cadence: she is not there
    build      65–72  the porch light       that ♭VI (C♭ = B) turned into the door to a new key; the motif climbing
    drop       73–88  carrying her          a MAJOR THIRD up (E♭ → G): her theme on the guitar, its borrowed chords now
                                            diatonic, its sigh gone, its climax a third higher
    outro      89–96  leaving the light on  the guitar; a minor PLAGAL close (IV – iv – I): bittersweet, then major

150 bpm, half time, 96 bars, 2:34. Every sound is the DSP library's.

    python3 make_script.py [out.txt]      # writes song.txt (or out.txt)
"""
import os, sys

BPM = 150
BAR = 4
INTRO, VERSE, BUILD1, DROP1, BREAK, BUILD2, DROP2, OUTRO, END = 0, 32, 96, 128, 192, 256, 288, 352, 384
GAP = 1.0

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
KICK, RIM, SNARE, CLAP, CHAT, OHAT, CRASH = 36, 37, 38, 39, 42, 46, 49
AH, EH, EE, OH, OO = 0, 1, 2, 3, 4


def m(n):
    """'Eb2' / 'F#5' / 'Cb4' → MIDI (C4 = 60)."""
    i, acc = 1, 0
    while i < len(n) and n[i] in '#b':
        acc += 1 if n[i] == '#' else -1
        i += 1
    return 12 * (int(n[i:]) + 1) + NAMES[n[0]] + acc


# ── the harmony ─────────────────────────────────────────────────────────────────────────────────────────────────────
# E♭ major: I – V6 – vi7 – IVmaj7 – iv6 (BORROWED) – I6 – ♭VImaj7 (BORROWED) – V(4–3). A bar a chord; the bass falls by
# step (E♭ D C A♭ · A♭ G C♭ B♭) — the borrowed iv and ♭VI carry the loss: their C♭ and G♭ are not in E♭ major
PROG1 = ['Eb', 'Bb/D', 'Cm7', 'Abmaj7', 'Abm6', 'Eb/G', 'Cbmaj7', 'Bbsus']
# G major, the second drop: the same shape a major third up, its two borrowed chords turned diatonic (ii for iv, IV for
# ♭VI) — the grief's colours gone
PROG2 = ['G', 'D/F#', 'Em7', 'Cmaj7', 'Am7', 'G/B', 'Cadd9', 'Dsus']
CHORD = {
    'Eb': ['Eb3', 'Bb3', 'D4', 'G4', 'Bb4'], 'Bb/D': ['D3', 'Bb3', 'D4', 'F4', 'Bb4'], 'Cm7': ['C3', 'G3', 'Bb3', 'Eb4', 'G4'],
    'Abmaj7': ['Ab2', 'Eb3', 'G3', 'C4', 'Eb4'], 'Abm6': ['Ab2', 'Eb3', 'F3', 'Cb4', 'Eb4'], 'Eb/G': ['G2', 'Eb3', 'Bb3', 'Eb4', 'G4'],
    'Cbmaj7': ['Cb3', 'Gb3', 'Bb3', 'Eb4', 'Gb4'], 'Bbsus': ['Bb2', 'F3', 'Bb3', 'Eb4', 'F4'], 'Bb': ['Bb2', 'F3', 'Bb3', 'D4', 'F4'],
    'Bb7': ['Bb2', 'F3', 'Ab3', 'D4', 'F4'],
    'Eb/Bb': ['Bb2', 'G3', 'Bb3', 'Eb4', 'G4'], 'Cm/Bb': ['Bb2', 'G3', 'C4', 'Eb4', 'G4'], 'Ab/Bb': ['Bb2', 'Ab3', 'C4', 'Eb4', 'Ab4'],
    'G': ['G3', 'D4', 'F#4', 'B4', 'D5'], 'D/F#': ['F#3', 'D4', 'F#4', 'A4', 'D5'], 'Em7': ['E3', 'B3', 'D4', 'G4', 'B4'],
    'Cmaj7': ['C3', 'G3', 'B3', 'E4', 'G4'], 'Am7': ['A2', 'E3', 'G3', 'C4', 'E4'], 'G/B': ['B2', 'G3', 'D4', 'G4', 'B4'],
    'Cadd9': ['C3', 'G3', 'D4', 'E4', 'G4'], 'Dsus': ['D3', 'A3', 'D4', 'G4', 'A4'], 'D': ['D3', 'A3', 'D4', 'F#4', 'A4'],
    'Bmaj7': ['B2', 'F#3', 'A#3', 'D#4', 'F#4'], 'Em': ['E3', 'B3', 'E4', 'G4', 'B4'], 'C': ['C3', 'G3', 'C4', 'E4', 'G4'],
    'Cm': ['C3', 'G3', 'C4', 'Eb4', 'G4'], 'Gmaj': ['G2', 'D3', 'G3', 'B3', 'D4'],
}
ROOT = {'Eb': 'Eb2', 'Bb/D': 'D2', 'Cm7': 'C2', 'Abmaj7': 'Ab1', 'Abm6': 'Ab1', 'Eb/G': 'G1', 'Cbmaj7': 'Cb2', 'Bbsus': 'Bb1',
        'Bb': 'Bb1', 'Bb7': 'Bb1', 'G': 'G1', 'D/F#': 'F#1', 'Em7': 'E2', 'Cmaj7': 'C2', 'Am7': 'A1', 'G/B': 'B1',
        'Cadd9': 'C2', 'Dsus': 'D2', 'D': 'D2', 'Bmaj7': 'B1', 'Em': 'E2', 'C': 'C2', 'Cm': 'C2', 'Gmaj': 'G1'}


def chords_of(prog, bar_beats=BAR, last_split=True):
    """(name, start, length) a chord a bar; a '…sus' bar is its suspension for two beats, then its resolution."""
    out = []
    for i, c in enumerate(prog):
        if c.endswith('sus') and last_split:
            out += [(c, i * bar_beats, bar_beats / 2), (c[:-3], i * bar_beats + bar_beats / 2, bar_beats / 2)]
        else:
            out.append((c, i * bar_beats, bar_beats))
    return out


# ── HER THEME, the leitmotif — eight bars over PROG1 (beat, length, note), written for this track ─────────────────
# bar 1 the MOTIF: degrees 3-4-5-8 (G A♭ B♭ E♭), dotted quarter, eighth, quarter, quarter — a reach upward
# bar 2 its answer, down by step to the fifth
# bar 3 the motif SEQUENCED a third up (E♭ F G C — the same steps, +1 +1 +3)
# bar 4 an APPOGGIATURA: B♭ leaning on the downbeat over A♭maj7, resolving up by step to C
# bar 5 the SIGH over the borrowed iv: C♭ (the ♭6) falling a semitone to B♭ — the lament's figure
# bar 6 the motif returns and leaps a sixth, reaching
# bar 7 the CLIMAX, B♭5, once, over the borrowed ♭VI — its major seventh — falling through G♭ (the borrowed ♭3)
# bar 8 a 4–3 SUSPENSION into the dominant: E♭ held over B♭sus falls to D — the theme ends on V, a question
THEME = [
    (0, 1.5, 'G4'), (1.5, 0.5, 'Ab4'), (2, 1, 'Bb4'), (3, 1, 'Eb5'),
    (4, 1.5, 'D5'), (5.5, 0.5, 'C5'), (6, 2, 'Bb4'),
    (8, 1.5, 'Eb5'), (9.5, 0.5, 'F5'), (10, 1, 'G5'), (11, 1, 'C5'),
    (12, 1.5, 'Bb4'), (13.5, 0.5, 'C5'), (14, 2, 'Eb5'),
    (16, 2, 'Cb5'), (18, 1, 'Bb4'), (19, 1, 'Ab4'),
    (20, 1.5, 'G4'), (21.5, 0.5, 'Ab4'), (22, 1, 'Bb4'), (23, 1, 'G5'),
    (24, 2, 'Bb5'), (26, 1, 'Gb5'), (27, 1, 'Eb5'),
    (28, 1.5, 'F5'), (29.5, 0.5, 'Eb5'), (30, 2, 'D5'),
]
# the theme a major third up, in G — bars 5 and 7 rewritten for their diatonic chords: no sigh (whole steps over ii),
# and the climax a third higher, D6, over IV
TRANSPOSE = 4


def up(n, st):
    """A note name moved by st semitones, spelled from G major's scale (sharps)."""
    names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    k = m(n) + st
    return f"{names[k % 12]}{k // 12 - 1}"


THEME2 = sorted([(a, l, up(n, TRANSPOSE)) for a, l, n in THEME if not (16 <= a < 20 or 24 <= a < 28)]
                + [(16, 2, 'E5'), (18, 1, 'D5'), (19, 1, 'C5'), (24, 2, 'D6'), (26, 1, 'B5'), (27, 1, 'G5')])

# E♭ major's scale, for the diatonic inversion
SCALE = ['Eb', 'F', 'G', 'Ab', 'Bb', 'C', 'D']


def diatonic(n):
    """A note of E♭ major → its scale index, counted in scale octaves from E♭: C and D sit in the scale octave of the
    E♭ BELOW them (E♭4 F4 G4 A♭4 B♭4 C5 D5 is one scale octave)."""
    name, octv = n[:-1], int(n[-1])
    return (octv - (1 if name in ('C', 'D') else 0)) * 7 + SCALE.index(name)


def from_diatonic(i):
    k, octv = i % 7, i // 7
    return f"{SCALE[k]}{octv + (1 if SCALE[k] in ('C', 'D') else 0)}"


def invert(notes, pivot='Bb4'):
    """A DIATONIC inversion: each note mirrored about the pivot by scale steps — an interval up becomes the same number
    of steps down."""
    p = diatonic(pivot)
    return [(a, l, from_diatonic(2 * p - diatonic(n))) for a, l, n in notes]


def augment(notes, k=2):
    """AUGMENTATION: every onset and length times k."""
    return [(a * k, l * k, n) for a, l, n in notes]


FIRST_HALF = [x for x in THEME if x[0] < 16]          # bars 1–4: the motif, its answer, its sequence, its appoggiatura
AUGMENTED = augment(FIRST_HALF)                        # the breakdown's piano: eight bars
INVERTED = augment(invert(FIRST_HALF))                 # the guitar's answer: eight bars, mirrored

# the breakdown's harmony: the theme's first four chords at half speed, then again — the last two bars a dominant
# seventh, which goes … to ♭VI (the deceptive cadence), where the second build begins
BREAK_PROG = [('Eb', 0, 8), ('Bb/D', 8, 8), ('Cm7', 16, 8), ('Abmaj7', 24, 8),
              ('Eb', 32, 8), ('Bb/D', 40, 8), ('Cm7', 48, 8), ('Bb7', 56, 8)]
# the second build: C♭maj7 (= Bmaj7, V of E minor) → Em → Cmaj7 → D: the deceptive chord becomes the door to G
BUILD2_PROG = [('Bmaj7', 0, 8), ('Em', 8, 8), ('Cmaj7', 16, 8), ('D', 24, 8)]
# the first build: a dominant PEDAL — B♭ in the bass under E♭, Cm, A♭, then B♭ itself
BUILD1_PROG = [('Eb/Bb', 0, 8), ('Cm/Bb', 8, 8), ('Ab/Bb', 16, 8), ('Bb', 24, 8)]
# the outro: I – IV – iv – I, the minor iv borrowed — the "amen" with her shadow in it, then the major third
OUTRO_PROG = [('Gmaj', 0, 8), ('C', 8, 8), ('Cm', 16, 8), ('Gmaj', 24, 8)]
# the second build's climbing motif (3-4-5-8 in each chord's key, two bars each): B, E minor, C, D — up to D6
CLIMB = [(0, 1.5, 'D#5'), (1.5, 0.5, 'E5'), (2, 1, 'F#5'), (3, 1, 'B5'),
         (8, 1.5, 'G4'), (9.5, 0.5, 'A4'), (10, 1, 'B4'), (11, 1, 'E5'),
         (16, 1.5, 'E5'), (17.5, 0.5, 'F#5'), (18, 1, 'G5'), (19, 1, 'C6'),
         (24, 1.5, 'F#5'), (25.5, 0.5, 'G5'), (26, 1, 'A5'), (27, 3, 'D6')]
# the outro's guitar: the motif in G once more, held over IV, the borrowed E♭ over iv, home on B (the major third)
OUTRO_TUNE = [(0, 1.5, 'B4'), (1.5, 0.5, 'C5'), (2, 1, 'D5'), (3, 1, 'G5'), (4, 1.5, 'F#5'), (5.5, 0.5, 'E5'), (6, 2, 'D5'),
              (8, 4, 'E5'), (12, 4, 'G5'), (16, 4, 'Eb5'), (20, 4, 'D5'), (24, 6, 'B4')]
# her voice in the first build: the motif's first three notes, sequenced up a third every two bars
CALLS = [(0, 1.5, 'G4', AH), (1.5, 0.5, 'Ab4', OH), (2, 2, 'Bb4', AH),
         (8, 1.5, 'Bb4', EE), (9.5, 0.5, 'C5', OH), (10, 2, 'D5', AH),
         (16, 1.5, 'C5', AH), (17.5, 0.5, 'D5', OH), (18, 2, 'Eb5', EE),
         (24, 1.5, 'D5', OH), (25.5, 0.5, 'Eb5', AH), (26, 3, 'F5', AH)]

# the drop's drums and 808 — four bars, half time
KICKS = [0, 1.25, 2.75, 4, 5.5, 8, 9.25, 10.75, 12, 13.0, 14.5]
WONK = "0:0 14.9:0 15.3:-1 15.6:-1 15.9:0 30.9:0 31.3:-1 31.6:-1 31.9:0"   # the chords sag two semitones, hold, spring back
# a syllable every sixteenth, open and closed in turn — "ya-ee-yo-ee-yu-ee…": its mouth opens and shuts every eighth
GROWL_VOWELS = [AH, EE, OH, EE, OO, EE, AH, EE, OH, EE, AH, EE, OO, EE, OH, EE]


class Ids:
    def __init__(self):
        self.n = {'ch': 1, 'dv': 0, 'pt': 0, 'ac': 0, 'au': 0}

    def new(self, kind):
        self.n[kind] += 1
        return f"{kind}_{self.n[kind]}"


L = []
ids = Ids()


def line(s=""):
    L.append(s)


def num(x):
    return f"{x:.6g}"


def tokens(notes):
    return " ".join(f"{p}@{num(a)}:{num(l)}:{v}" for p, a, l, v in notes)


def notes_add(pt, notes):
    notes = sorted(notes, key=lambda n: (n[1], n[0]))
    for i in range(0, len(notes), 48):
        line(f'notes add {pt} "{tokens(notes[i:i + 48])}"')


def instrument(kind, name, at, length):
    ch, dv, pt = ids.new('ch'), ids.new('dv'), ids.new('pt')
    ids.new('ac')
    line(f"clip add --instrument {kind} --at {num(at)} --length {num(length)}")
    line(f'set {ch}.name="{name}" {pt}.name="{name}"')
    return ch, dv, pt


def pattern(name, length):
    pt = ids.new('pt')
    line(f'pattern new --name "{name}" --length {num(length)}')
    return pt


def clip(ch, pt, at, length):
    ids.new('ac')
    line(f"clip add --strip {ch} --pattern {pt} --at {num(at)} --length {num(length)}")


def setp(dv, **params):
    line("set " + " ".join(f"{dv}.{k.replace('__', '.')}={v}" for k, v in params.items()))


def device(ch, kind, **params):
    dv = ids.new('dv')
    line(f"device add {ch} --type {kind}")
    if params:
        setp(dv, **params)
    return dv


def automate(address, points):
    """points: (beat, value[, shape]) — a shape governs the segment AFTER its point."""
    au = ids.new('au')
    line(f"auto create {address}")
    for i, p in enumerate(points):
        at, value = p[0], p[1]
        shape = f" --shape {p[2]}" if len(p) > 2 else ""
        if i == 0 and at != 0:
            line(f"auto point move {au} --at 0 --to {num(at)} --value {num(value)}")
            if shape:
                line(f"auto point shape {au} --at {num(at)}{shape}")
        else:
            line(f"auto point add {au} --at {num(at)} --value {num(value)}{shape}")
    return au


def steps(address, points):
    au = ids.new('au')
    line(f"auto create {address}")
    for at, value in points:
        line(f"auto point add {au} --at {num(at)} --value {num(value)} --shape hold")
    return au


def roll(pitch, a, b, div, v0, v1):
    n = int(round((b - a) / div))
    return [(pitch, a + k * div, min(div, 0.25), int(round(v0 + (v1 - v0) * k / max(1, n - 1)))) for k in range(n)]


def chord_notes(spans, vel, cut_last=0.0):
    out = []
    for k, (c, a, l) in enumerate(spans):
        ln = l - (cut_last if k == len(spans) - 1 else 0)
        out += [(m(n), a, ln, vel) for n in CHORD[c]]
    return out


def main(out):
    line("# Porch Light — an original future bass track that tells a story, in the style of San Holo and Illenium,")
    line("# written by make_script.py (R-SVC-17). 150 bpm half time; E♭ major, then G; 96 bars.")
    line('project new porchlight.slp --bpm 150 --name "Porch Light"')
    line()

    drop1 = chords_of(PROG1)
    drop2 = chords_of(PROG2)

    # ── the guitar: San Holo's clean electric ──
    line("# the guitar (R-INST-12): clean, chorused — her chords picked in the intro, HER THEME in the verse, its inversion")
    line("# in the breakdown, the theme in G in the second drop, the outro")
    gtr, gdv, gpt = instrument("guitar", "Guitar", INTRO, 32)
    setp(gdv, pluck__position=0.16, pluck__pick=3600, string__sustain=3.5, string__brightness=0.45, string__stiffness=0.4,
         body=0.12, release=60, volume=-4, velocity=0.5)
    line(f"set {gpt}.length=32")
    picking = []                                   # an eighth-note pattern through each chord's tones, two bars a chord
    for k, (c, a, l) in enumerate([('Eb', 0, 8), ('Bb/D', 8, 8), ('Cm7', 16, 8), ('Abmaj7', 24, 8)]):
        tones = CHORD[c][1:] + CHORD[c][-2:0:-1]
        for j in range(16):
            picking.append((m(tones[j % len(tones)]), a + 0.5 * j, 1.0, 84 if j % 4 == 0 else 66))
    notes_add(gpt, picking)
    gth = pattern("Guitar, her theme", 32)
    notes_add(gth, [(m(n), a, l, 100 if a % 1 == 0 else 88) for a, l, n in THEME])
    for at in (VERSE, VERSE + 32):
        clip(gtr, gth, at, 32)
    ginv = pattern("Guitar, the theme inverted", 32)
    notes_add(ginv, [(m(n), a, l, 96) for a, l, n in INVERTED])
    clip(gtr, ginv, BREAK + 32, 32)
    gth2 = pattern("Guitar, her theme in G", 32)
    notes_add(gth2, [(m(n), a, l, 104 if a % 1 == 0 else 92) for a, l, n in THEME2])
    for at in (DROP2, DROP2 + 32):
        clip(gtr, gth2, at, 32)
    gout = pattern("Guitar, the outro", 32)
    notes_add(gout, [(m(n), a, l, 96) for a, l, n in OUTRO_TUNE])
    clip(gtr, gout, OUTRO, 32)
    device(gtr, "chorus")
    steps(f"{gdv}.volume", [(0, -6), (VERSE, 0), (BREAK, 4), (DROP2, 6), (OUTRO, 0)])
    steps(f"{gdv}.pluck.pick", [(0, 3600), (DROP2, 6500), (OUTRO, 3600)])   # carrying her: the pick brighter
    line()

    # ── the piano ──
    line("# the piano (R-INST-5): her chords under the verse; the photograph — the theme AUGMENTED, bare — and its chords")
    pno, pdv, ppt = instrument("piano", "Piano", VERSE, 64)
    setp(pdv, strings__brightness=0.9, strings__decay=1.3, body__mix=0.55, hammer__hardness=0.85, volume=-6)
    line(f"set {ppt}.length=32")
    notes_add(ppt, [(m(CHORD[c][0]) - 12, a, l, 62) for c, a, l in drop1]
              + [(m(n), a, l, 58) for c, a, l in drop1 for n in CHORD[c][1:4]])
    paug = pattern("Piano, the photograph", 64)
    notes_add(paug, [(m(CHORD[c][0]) - 12, a, l, 60) for c, a, l in BREAK_PROG]
              + [(m(n), a, l, 54) for c, a, l in BREAK_PROG for n in CHORD[c][1:4]])
    clip(pno, paug, BREAK, 64)
    pend = pattern("Piano, the outro", 32)
    notes_add(pend, [(m(CHORD[c][0]) - 12, a, l, 58) for c, a, l in OUTRO_PROG]
              + [(m(n), a, l, 52) for c, a, l in OUTRO_PROG for n in CHORD[c][1:4]])
    clip(pno, pend, OUTRO, 32)
    line("# … and its right hand alone, on a piano of its own: the theme AUGMENTED (a melody on its own track can be read)")
    pnm, pmdv, pmpt = instrument("piano", "Piano, her theme", BREAK, 32)
    setp(pmdv, strings__brightness=1.0, strings__decay=1.4, body__mix=0.5, hammer__hardness=0.95, volume=-4)
    line(f"set {pmpt}.length=32")
    notes_add(pmpt, [(m(n), a, l, 86) for a, l, n in AUGMENTED])
    line()

    # ── the future bass chords ──
    line("# the chords: two five-voice saws, wide — the builds and the drops; in the drops they PUMP on the shaper and SAG")
    line("# two semitones on the pitch wheel at the end of every fourth bar, and spring back (the wonk)")
    chd, cdv, cpt = instrument("synth", "Chords", BUILD1, 32)
    setp(cdv, osc1__wave="saw", osc1__voices=5, osc1__detune=32, osc1__width=30, osc1__level=0.7, osc2__wave="saw",
         osc2__octave=0, osc2__voices=5, osc2__detune=20, osc2__width=24, osc2__fine=7, osc2__level=0.5,
         filter__cutoff=5200, filter__res=0.12, filter__env=0.5, filter__keytrack=0.3, fenv__decay=600, fenv__sustain=0.6,
         amp__attack=6, amp__decay=400, amp__sustain=0.9, amp__release=160, voice__bend=2, volume=-10)
    line(f"set {cpt}.length=32")
    notes_add(cpt, chord_notes(BUILD1_PROG, 92, GAP))
    line(f'expr set {cpt} "0:0" --lane bend')
    c1 = pattern("Chords, grief", 32)
    notes_add(c1, chord_notes(drop1, 100))
    line(f'expr set {c1} "{WONK}" --lane bend')
    for at in (DROP1, DROP1 + 32):
        clip(chd, c1, at, 32)
    cb2 = pattern("Chords, the porch light", 32)
    notes_add(cb2, chord_notes(BUILD2_PROG, 92, GAP))
    line(f'expr set {cb2} "0:0" --lane bend')
    clip(chd, cb2, BUILD2, 32)
    c2 = pattern("Chords, carrying her", 32)
    notes_add(c2, chord_notes(drop2, 100))
    line(f'expr set {c2} "{WONK}" --lane bend')
    for at in (DROP2, DROP2 + 32):
        clip(chd, c2, at, 32)
    pump = device(chd, "shaper", rate='"1/4"', shape='"pump"', depth=0, smooth=2)
    steps(f"{pump}.depth", [(0, 0), (DROP1, 0.7), (BREAK, 0), (DROP2, 0.7), (OUTRO, 0)])
    automate(f"{cdv}.volume", [(0, -30), (BUILD1, -30), (DROP1 - GAP, -12, 'hold'), (DROP1, -10, 'hold'),
                                (BUILD2, -30), (DROP2 - GAP, -12, 'hold'), (DROP2, -9, 'hold')])
    # the grief a little closed, carrying her open — light after grief
    automate(f"{cdv}.filter.cutoff", [(0, 700), (BUILD1, 700), (DROP1 - GAP, 7000, 'hold'), (DROP1, 4200, 'hold'),
                                       (BUILD2, 700), (DROP2 - GAP, 9000, 'hold'), (DROP2, 9000, 'hold')])
    line()

    # ── the lead ──
    line("# the lead: a wide saw, one voice at a time — HER THEME through the grief; the climbing motif of the second build;")
    line("# an octave over the guitar in the second drop's second half")
    lead, ldv, lpt = instrument("synth", "Lead", DROP1, 64)
    setp(ldv, osc1__wave="saw", osc1__voices=5, osc1__detune=18, osc1__width=18, osc1__level=0.75, osc2__wave="saw",
         osc2__octave=1, osc2__voices=1, osc2__level=0.25, filter__cutoff=5000, filter__res=0.15, filter__env=1.0,
         filter__keytrack=0.3, fenv__decay=260, fenv__sustain=0.6, amp__attack=4, amp__decay=220, amp__sustain=0.85,
         amp__release=110, voice__mode="legato", voice__glide=0, vibrato__depth=0.08, vibrato__rate=5.2, volume=-8)
    line(f"set {lpt}.length=32")
    notes_add(lpt, [(m(n), a, l, 108 if a % 1 == 0 else 96) for a, l, n in THEME])
    lc = pattern("Lead, the climb", 32)
    notes_add(lc, [(m(n), a, l, 100) for a, l, n in CLIMB])
    clip(lead, lc, BUILD2, 32 - GAP)
    lo = pattern("Lead, over the guitar", 32)
    notes_add(lo, [(m(n) + 12, a, l, 96) for a, l, n in THEME2])
    clip(lead, lo, DROP2 + 32, 32)
    automate(f"{ldv}.filter.cutoff", [(0, 3800, 'hold'), (BUILD2, 900), (DROP2 - GAP, 7000, 'hold'), (DROP2, 7000, 'hold')])
    automate(f"{ldv}.volume", [(0, -8, 'hold'), (BUILD2, -22), (DROP2 - GAP, -8, 'hold'), (DROP2, -8, 'hold')])
    line()

    # ── her voice ──
    line("# her voice — the voicemail: a bright voice through the vowel filter (R-FX-19), calling the motif up the first")
    line("# build, each note its syllable")
    voc, vdv, vpt = instrument("synth", "Her Voice", BUILD1, 32 - GAP)
    setp(vdv, osc1__wave="saw", osc1__voices=1, osc1__level=0.8, osc2__wave="square", osc2__octave=0, osc2__level=0.3,
         noise=0.04, filter__cutoff=8000, filter__res=0.05, filter__env=0.2, filter__keytrack=0, amp__attack=8,
         amp__decay=200, amp__sustain=0.85, amp__release=200, voice__mode="legato", voice__glide=40,
         vibrato__depth=0.15, vibrato__rate=5.4, velocity=0.3, volume=-6)
    line(f"set {vpt}.length=32")
    notes_add(vpt, [(m(n), a, l, 100) for a, l, n, _ in CALLS])
    vw = device(voc, "vowel", resonance=0.6, glide=12, mix=1, output=8)
    au = ids.new('au')
    line('auto add --name "Her Voice · Vowel" --min 0 --max 4 --length 32 --rest 0')
    for a, _, _, v in CALLS:
        line(f"auto point add {au} --at {num(a)} --value {v} --shape hold")
    line(f'set {vw}.vowel="={au}"')
    ids.new('ac')
    line(f"clip add --auto {au} --at {BUILD1} --length 32")
    line()

    # ── the growl ──
    line("# the growl: a driven saw and square through the vowel filter, its vowel moved every sixteenth — it TALKS, at the")
    line("# end of every phrase of the grief")
    grl, grdv, grpt = instrument("synth", "Growl", DROP1, 64)
    setp(grdv, osc1__wave="saw", osc1__voices=2, osc1__detune=12, osc1__level=0.8, osc2__wave="square", osc2__octave=-1,
         osc2__level=0.6, filter__cutoff=3200, filter__res=0.2, filter__env=0.4, filter__keytrack=0, amp__attack=2,
         amp__decay=300, amp__sustain=1, amp__release=60, voice__mode="legato", voice__glide=25, volume=-8)
    line(f"set {grpt}.length=32")
    fills = []
    for bar, c in ((3, 'Abmaj7'), (7, 'Bb')):     # the fourth bar of every four: the phrase's end
        r = m(ROOT[c]) + 12
        fills += [(r, bar * 4 + 0.5, 1.5, 104), (r + 12, bar * 4 + 2, 0.5, 100), (r, bar * 4 + 2.5, 1.5, 104)]
    notes_add(grpt, fills)
    device(grl, "saturator", drive=14, curve="tube", mix=0.8, output=-6)
    gv = device(grl, "vowel", resonance=1.3, glide=30, mix=1, output=4)
    au_g = ids.new('au')
    line('auto add --name "Growl · Vowel" --min 0 --max 4 --length 4 --rest 0')
    for k, v in enumerate(GROWL_VOWELS):
        line(f"auto point add {au_g} --at {num(k * 0.25)} --value {v} --shape hold")
    line(f'set {gv}.vowel="={au_g}"')
    for at in (DROP1 + 12, DROP1 + 28, DROP1 + 44, DROP1 + 60):
        ids.new('ac')
        line(f"clip add --auto {au_g} --at {at} --length 4")
    line()

    # ── the 808 ──
    line("# the 808 (R-INST-11): the roots — soft through the verse, the PEDAL B♭ through the first build, gliding into")
    line("# each bar of the drops; the deceptive C♭ where the second build begins")
    b8, b8dv, b8pt = instrument("808", "808", DROP1, 64)
    setp(b8dv, punch=10, punch__time=30, glide=80, amp__attack=1, amp__decay=2400, amp__release=160, click=0.2, drive=0.35,
         tone=3800, volume=-2, velocity=0.25)
    line(f"set {b8pt}.length=32")

    def bassline(spans):
        """The root on the downbeat and again on the and of three, held a hair into the next bar: one voice, so it
        GLIDES into each new root"""
        out = []
        for k, (c, a, l) in enumerate(spans):
            r = m(ROOT[c])
            tie = 0.15 if k < len(spans) - 1 else 0.0
            out += [(r, a, 2.5, 116), (r, a + 2.5, l - 2.5 + tie, 108)] if l >= 4 else [(r, a, l + tie, 112)]
        return out

    notes_add(b8pt, bassline(drop1))
    b2 = pattern("808, carrying her", 32)
    notes_add(b2, bassline(drop2))
    for at in (DROP2,):
        clip(b8, b2, at, 64)
    bv = pattern("808, her rooms", 32)
    notes_add(bv, [(m(ROOT[c]), a, 3, 84) for c, a, l in drop1 if l == 4] + [(m(ROOT['Bbsus']), 28, 3, 84)])
    clip(b8, bv, VERSE + 32, 32)
    bp = pattern("808, the pedal", 32)
    notes_add(bp, [(m('Bb1'), a, 7.5 if a < 24 else 6.5, 104) for a in (0, 8, 16, 24)])
    clip(b8, bp, BUILD1, 32)
    steps(f"{b8dv}.volume", [(0, -2), (BUILD1, -10), (DROP1, -2), (BUILD2, -10), (DROP2, -2)])
    bb2 = pattern("808, the porch light", 32)
    notes_add(bb2, [(m(ROOT[c]), a, l - (GAP if a == 24 else 0.5), 104) for c, a, l in BUILD2_PROG])
    clip(b8, bb2, BUILD2, 32)
    line()

    # ── drums ──
    line("# drums: half time — the kick, the snare and clap on the third beat; hats rolling in triplets; the verse lighter;")
    line("# a kick climb and a snare roll through each build, a crash and an impact on each drop")
    kick, kdv, kpt = instrument("drums", "Kick", DROP1, 64)
    setp(kdv, kick__tune=1, kick__decay=280, kick__tone=0.6, kick__level=3, volume=0)
    line(f"set {kpt}.length=16")
    notes_add(kpt, [(KICK, k, 0.25, 124 if k % 4 == 0 else 110) for k in KICKS])
    clip(kick, kpt, DROP2, 64)
    kv = pattern("Kick, her rooms", 8)
    notes_add(kv, [(KICK, 0, 0.25, 100), (KICK, 2.75, 0.25, 88), (KICK, 4, 0.25, 100)])
    clip(kick, kv, VERSE + 32, 32)
    climb = pattern("Kick, the build", 32)
    notes_add(climb, [(KICK, b, 0.25, 84) for b in range(0, 16, 4)] + roll(KICK, 16, 31, 1.0, 84, 112))
    for b in (BUILD1, BUILD2):
        clip(kick, climb, b, 32)
    snr, sdv, spt = instrument("drums", "Snare", DROP1, 64)
    setp(sdv, snare__tune=3, snare__decay=240, snare__tone=0.72, snare__level=2, clap__decay=340, clap__tone=0.64,
         clap__level=1, volume=1)
    line(f"set {spt}.length=4")
    notes_add(spt, [(SNARE, 2, 0.25, 118), (CLAP, 2, 0.25, 110)])
    clip(snr, spt, DROP2, 64)
    sv = pattern("Snare, her rooms", 4)
    notes_add(sv, [(CLAP, 2, 0.25, 76)])
    clip(snr, sv, VERSE + 32, 32)
    hat, hdv, hpt = instrument("drums", "Hats", DROP1, 64)
    setp(hdv, chat__tone=0.86, chat__decay=34, chat__level=0, chat__pan=0.2, ohat__decay=220, ohat__level=-3, volume=0)
    line(f"set {hpt}.length=16")
    h = []
    for b in range(4):
        base = [(CHAT, b * 4 + 0.5 * k, 0.25, 78 if k % 2 == 0 else 54) for k in range(8)]   # the grief's hats held back
        if b % 2 == 1:
            base = [x for x in base if x[1] < b * 4 + 3] + roll(CHAT, b * 4 + 3, b * 4 + 4, 1 / 6, 56, 88)
        h += base
    notes_add(hpt, h + [(OHAT, 7.5, 0.5, 74), (OHAT, 15.5, 0.5, 74)])
    h2 = pattern("Hats, carrying her", 16)       # sixteenths and open hats: the light comes in
    notes_add(h2, [(CHAT, 0.25 * k, 0.25, 108 if k % 4 == 0 else (88 if k % 2 == 0 else 70)) for k in range(64)]
              + [(OHAT, b + 1.5, 0.5, 96) for b in range(0, 16, 2)])
    clip(hat, h2, DROP2, 64)
    hl = pattern("Hats, light", 8)
    notes_add(hl, [(CHAT, 0.5 * k, 0.25, 36 if k % 2 else 50) for k in range(16)])
    for at, ln in [(VERSE + 32, 32), (BUILD1 + 16, 15), (BUILD2 + 16, 15)]:
        clip(hat, hl, at, ln)
    cym, cydv, cypt = instrument("drums", "Crash", DROP1, 4)
    setp(cydv, crash__level=-1, crash__decay=2800, crash__tone=0.7, volume=-3)
    line(f"set {cypt}.length=4")
    notes_add(cypt, [(CRASH, 0, 0.25, 118)])
    for at in (DROP1 + 32, DROP2, DROP2 + 32):
        clip(cym, cypt, at, 4)
    rl, rdv, rpt = instrument("drums", "Roll", BUILD1, 32)
    setp(rdv, snare__tune=5, snare__decay=150, snare__tone=0.62, volume=-8)
    line(f"set {rpt}.length=32")
    notes_add(rpt, roll(SNARE, 0, 8, 1.0, 46, 56) + roll(SNARE, 8, 16, 0.5, 56, 70) + roll(SNARE, 16, 24, 0.25, 70, 90)
              + roll(SNARE, 24, 28, 1 / 6, 90, 106) + roll(SNARE, 28, 32 - GAP, 0.125, 106, 124))
    clip(rl, rpt, BUILD2, 32)
    line()

    # ── the pad, the riser ──
    line("# the pad: warm, under the intro's second half, the verse, the breakdown and the outro — out for each gap")
    pad, pddv, pdpt = instrument("synth", "Pad", VERSE, 64)
    setp(pddv, osc1__wave="saw", osc1__voices=5, osc1__detune=18, osc1__width=34, osc1__level=0.5, osc2__wave="triangle",
         osc2__octave=1, osc2__level=0.3, filter__cutoff=1600, filter__res=0.12, filter__env=0.2, filter__keytrack=0.25,
         amp__attack=1500, amp__decay=1500, amp__sustain=0.85, amp__release=1800, volume=-18)
    line(f"set {pdpt}.length=32")
    notes_add(pdpt, [(m(n), a, l, 80) for c, a, l in drop1 for n in CHORD[c][1:4]])
    pdoor = pattern("Pad, the door", 16)
    notes_add(pdoor, [(m(n), a, l, 76) for c, a, l in [('Cm7', 0, 8), ('Abmaj7', 8, 8)] for n in CHORD[c][1:4]])
    clip(pad, pdoor, INTRO + 16, 16)
    pb = pattern("Pad, the photograph", 64)
    notes_add(pb, [(m(n), a, l, 80) for c, a, l in BREAK_PROG for n in CHORD[c][1:4]])
    clip(pad, pb, BREAK, 64)
    po = pattern("Pad, leaving", 32)
    notes_add(po, [(m(n), a, l, 80) for c, a, l in OUTRO_PROG for n in CHORD[c][1:4]])
    clip(pad, po, OUTRO, 32)
    rise, rsdv, rspt = instrument("synth", "Riser", BUILD1, 32)
    setp(rsdv, osc1__wave="saw", osc1__voices=3, osc1__detune=40, osc1__width=30, osc1__level=0.3, osc2__level=0, noise=0.85,
         filter__res=0.35, filter__env=0, filter__keytrack=0, amp__attack=1500, amp__sustain=1, amp__release=40, volume=-15)
    line(f"set {rspt}.length=32")
    notes_add(rspt, [(m('Bb3'), 0, 32 - GAP, 100)])
    clip(rise, rspt, BUILD2, 32)
    automate(f"{rsdv}.filter.cutoff", [(BUILD1, 200), (DROP1 - GAP, 9000, 'hold'), (DROP1, 200, 'hold'),
                                        (BUILD2, 200), (DROP2 - GAP, 9000, 'hold'), (DROP2, 200, 'hold')])
    automate(f"{rsdv}.volume", [(BUILD1, -40), (DROP1 - GAP, -12, 'hold'), (DROP1, -40, 'hold'),
                                 (BUILD2, -40), (DROP2 - GAP, -12, 'hold'), (DROP2, -40, 'hold')])
    line()

    # ── the sparkle ──
    line("# the sparkle: a glassy wavetable pluck two octaves up, sixteenths through the chord's tones — the second drop")
    line("# only: the light coming in")
    spk, spdv, sppt = instrument("wavetable", "Sparkle", DROP2, 64)
    setp(spdv, osc1__table="harmonics", osc1__position=0.7, osc1__posenv=-0.3, osc1__voices=1, osc1__level=0.8,
         osc2__level=0, filter__cutoff=9000, filter__res=0.15, filter__env=1.5, fenv__decay=180, fenv__sustain=0,
         amp__attack=1, amp__decay=240, amp__sustain=0, amp__release=150, volume=-8)
    line(f"set {sppt}.length=32")
    sp = []
    for c, a, l in drop2:
        tones = [m(n) + 12 for n in CHORD[c][2:]]
        for j in range(int(l * 4)):
            sp.append((tones[(j * 2) % len(tones)] + (12 if j % 4 == 3 else 0), a + 0.25 * j, 0.25, 84 if j % 4 == 0 else 64))
    notes_add(sppt, sp)
    line()

    # ── tone, space, the master ──
    line("# tone: the low end the 808's and the kick's; space: a long reverb and a dotted-eighth delay; the master: a limiter")
    for ch, hz in [(chd, 180), (lead, 250), (gtr, 140), (pno, 60), (pnm, 120), (pad, 120), (voc, 250), (grl, 70), (hat, 400), (snr, 150),
                   (cym, 400), (rise, 180), (rl, 200), (spk, 600)]:
        device(ch, "eq", lowcut__on="on", lowcut__freq=hz)
    device(b8, "eq", lowcut__on="on", lowcut__freq=28)
    verb = ids.new('ch')
    line('strip add --kind bus --name Reverb')
    device(verb, "reverb", decay=4200, size=190, mix=1, lowcut=300, highcut=10000, width=1)
    dly = ids.new('ch')
    line('strip add --kind bus --name Delay')
    device(dly, "delay", time='"=60000/bpm*0.75"', feedback=0.36, tone=4200, mix=1)
    for ch, to, g in [(gtr, verb, -10), (gtr, dly, -12), (pno, verb, -6), (pnm, verb, -5), (lead, verb, -14), (lead, dly, -16), (chd, verb, -18),
                      (voc, verb, -12), (voc, dly, -16), (pad, verb, -6), (snr, verb, -14), (cym, verb, -12), (rise, verb, -12), (spk, dly, -8), (spk, verb, -10)]:
        line(f"send add {ch} --to {to} --gain {g}")
    # measured per strip, each section: a plucked string reads far quieter than a held saw at the same settings
    gains = {gtr: 8, pno: 1, pnm: 6, chd: -5, lead: 0, voc: -4, grl: 4, b8: -9, kick: -1, snr: 2, hat: -2, cym: -9, rl: -5,
             pad: -10, rise: -6, verb: -5, dly: -9}
    line("set " + " ".join(f"{ch}.gain={g}" for ch, g in gains.items()))
    device("master", "limiter", gain=9, ceiling=-1.3, release=50, truePeak="on")
    line()
    line("ls")
    line("audit")
    line("project save")
    stems = {"guitar": gtr, "piano": pnm, "chords": chd, "lead": lead, "growl": grl, "808": b8, "snare": snr}
    line(f"render --out porchlight.wav --stems {','.join(stems.values())}")
    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    return stems


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "song.txt")
    stems = main(out)
    print(f"{len(L)} lines → {out}; stems {stems}")
