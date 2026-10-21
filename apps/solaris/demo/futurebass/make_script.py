#!/usr/bin/env python3
"""Halation — an original melodic trap / future bass track in the Trap Nation style (Solaris R-SVC-15), written as a
solaris-cc script.

The STYLE, nobody's song: the progression (VI–III–VII–i in C♯ minor), the vocal-chop melody and its syllables, the 808
line, the drums and the form were written for this track. 140 bpm in half time, 80 bars (320 beats, 2:17):

    intro 1–8 · verse 9–16 · build 17–24 · drop 25–40 · breakdown 41–48 · build 49–56 · drop 57–72 · outro 73–80

Every sound is the DSP library's. What makes the style: future bass chords — two five-voice saws, wide — CHOPPED on
the song's grid by shapers (R-FX-18): a gate that speeds up through each build (a quarter, an eighth, a sixteenth, a
thirty-second) and a triplet-eighth wobble in the drops; a lead that reads as a pitched vocal chop — a bright legato
synth through the vowel filter (R-FX-19), each note given its syllable by a held automation; an 808 that glides; a
half-time snare and clap; hats rolling in triplets; a piano for the intro and the breakdown; a long reverb. The chords
dive an octave on the pitch wheel out of the first drop.

    python3 make_script.py [out.txt]      # writes song.txt (or out.txt)
"""
import os, sys

BPM = 140
BAR = 4
INTRO, VERSE, BUILD1, DROP1, BREAK, BUILD2, DROP2, OUTRO, END = 0, 32, 64, 96, 160, 192, 224, 288, 320
GAP = 1.0           # the beat of near-silence before each drop

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
KICK, RIM, SNARE, CLAP, CHAT, OHAT, CRASH = 36, 37, 38, 39, 42, 46, 49
AH, EH, EE, OH, OO = 0, 1, 2, 3, 4    # the vowel filter's `vowel`


def m(n):
    """'A1' / 'C#5' / 'Bb4' → MIDI (C4 = 60)."""
    i, acc = 1, 0
    while i < len(n) and n[i] in '#b':
        acc += 1 if n[i] == '#' else -1
        i += 1
    return 12 * (int(n[i:]) + 1) + NAMES[n[0]] + acc


class Ids:
    """The service numbers what it makes; the script names it the same way (a fresh song: ch_1 is Main)."""
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
    """A track with its instrument and a first MIDI clip: returns (strip, device, pattern)."""
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
    """points: (beat, value[, shape]) — the first moves the one `auto create` makes at 0."""
    au = ids.new('au')
    line(f"auto create {address}")
    for i, p in enumerate(points):
        at, value = p[0], p[1]
        shape = p[2] if len(p) > 2 else None
        if i == 0:
            line(f"auto point move {au} --at 0" + (f" --to {num(at)}" if at != 0 else "") + f" --value {num(value)}")
        else:
            line(f"auto point add {au} --at {num(at)} --value {num(value)}" + (f" --shape {shape}" if shape else ""))
    return au


def steps(address, points):
    """A value that STEPS: every point held until the next — (beat, value), the first at 0."""
    au = ids.new('au')
    line(f"auto create {address}")
    for at, value in points:
        line(f"auto point add {au} --at {num(at)} --value {num(value)} --shape hold")
    return au


def roll(pitch, a, b, div, v0, v1):
    """Hits from beat a to b every `div` beats, the velocity climbing v0 → v1."""
    n = int(round((b - a) / div))
    return [(pitch, a + k * div, min(div, 0.25), int(round(v0 + (v1 - v0) * k / max(1, n - 1)))) for k in range(n)]


# ── the harmony: a bar a chord, four bars a turn — VI, III, VII, i in C♯ minor ─────────────────────────────────────
PROG = ['A', 'E', 'B', 'C#m']
ROOTS = {'A': 'A1', 'E': 'E1', 'B': 'B1', 'C#m': 'C#2'}
CHORD = {'A': ['A3', 'E4', 'G#4', 'C#5', 'E5'], 'E': ['G#3', 'B3', 'E4', 'F#4', 'B4'],          # Amaj7 · E add9 / G♯
         'B': ['A3', 'D#4', 'F#4', 'B4', 'C#5'], 'C#m': ['G#3', 'C#4', 'E4', 'G#4', 'B4']}       # B9 / A · C♯m7
PIANO_L = {'A': ['A1', 'A2'], 'E': ['E2', 'B2'], 'B': ['B1', 'F#2'], 'C#m': ['C#2', 'G#2']}
PIANO_R = {'A': ['C#4', 'E4', 'G#4', 'B4'], 'E': ['B3', 'E4', 'G#4', 'B4'], 'B': ['B3', 'D#4', 'F#4', 'A4'],
           'C#m': ['C#4', 'E4', 'G#4', 'B4']}
PAD = {'A': ['A2', 'E3', 'C#4'], 'E': ['E2', 'B2', 'G#3'], 'B': ['B1', 'F#2', 'D#3'], 'C#m': ['C#2', 'G#2', 'E3']}
ARP = {'A': ['E5', 'C#5', 'A4', 'C#5', 'E5', 'G#5', 'E5', 'C#5'], 'E': ['E5', 'B4', 'G#4', 'B4', 'E5', 'F#5', 'E5', 'B4'],
       'B': ['F#5', 'D#5', 'B4', 'D#5', 'F#5', 'A5', 'F#5', 'D#5'], 'C#m': ['E5', 'C#5', 'G#4', 'C#5', 'E5', 'G#5', 'E5', 'C#5']}

# the vocal-chop melody, eight bars (beat, length, note, syllable) — written for this track. The first phrase ASKS (it
# leans on the sixth and lands on the tonic held as "oo"), the second climbs to G♯5, the major seventh over A, and
# answers home on C♯5, "ah"; the stutters (sixteenths on one pitch) are the chops
CHOPS = [
    (0.0, 0.5, 'E5', AH), (0.5, 0.5, 'C#5', OH), (1.0, 0.5, 'B4', AH), (1.5, 1.0, 'C#5', EE), (2.75, 0.25, 'E5', AH),
    (3.0, 0.5, 'E5', AH), (3.5, 0.5, 'F#5', OH),
    (4.0, 0.75, 'E5', AH), (4.75, 0.25, 'B4', OH), (5.0, 0.5, 'B4', AH), (5.5, 1.0, 'G#4', OO), (6.75, 0.25, 'B4', AH),
    (7.0, 0.5, 'C#5', EE), (7.5, 0.5, 'B4', OH),
    (8.0, 0.5, 'D#5', AH), (8.5, 0.5, 'F#5', EE), (9.0, 0.5, 'D#5', OH), (9.5, 1.0, 'B4', AH), (10.75, 0.25, 'F#5', EE),
    (11.0, 0.25, 'F#5', EE), (11.25, 0.25, 'E5', AH), (11.5, 0.5, 'D#5', OH),
    (12.0, 1.5, 'C#5', AH), (13.5, 0.5, 'B4', OH), (14.0, 0.5, 'G#4', AH), (14.5, 1.5, 'C#5', OO),
    (16.0, 0.5, 'E5', AH), (16.5, 0.5, 'C#5', OH), (17.0, 0.5, 'B4', AH), (17.5, 0.5, 'C#5', EE), (18.0, 0.5, 'E5', AH),
    (18.5, 1.0, 'G#5', EE), (19.5, 0.5, 'F#5', OH),
    (20.0, 0.75, 'E5', AH), (20.75, 0.25, 'E5', AH), (21.0, 0.5, 'F#5', EE), (21.5, 1.0, 'G#5', AH), (22.75, 0.25, 'F#5', OH),
    (23.0, 0.5, 'E5', AH), (23.5, 0.5, 'D#5', EE),
    (24.0, 0.5, 'D#5', OH), (24.5, 0.5, 'F#5', AH), (25.0, 0.25, 'F#5', EE), (25.25, 0.25, 'F#5', EE), (25.5, 0.5, 'E5', AH),
    (26.0, 1.0, 'D#5', OH), (27.25, 0.25, 'B4', AH), (27.5, 0.5, 'D#5', EE),
    (28.0, 0.75, 'E5', AH), (28.75, 0.25, 'D#5', OH), (29.0, 2.5, 'C#5', AH),
]
CHOP_AT = [(VERSE, 32), (DROP1, 64), (BREAK, 32), (DROP2, 64), (OUTRO, 16)]   # where the chops sing (start, beats)

# the chords' chop: one shaper per rate, each on (its depth) only where it plays — a choice cannot be automated, a
# depth can. (rate, shape, [(from, to)])
CHOP_RATES = [
    ('1/4', 'square', [(BUILD1, BUILD1 + 8), (BUILD2, BUILD2 + 8)]),
    ('1/8', 'square', [(BUILD1 + 8, BUILD1 + 16), (BUILD2 + 8, BUILD2 + 16)]),
    ('1/16', 'square', [(BUILD1 + 16, BUILD1 + 24), (BUILD2 + 16, BUILD2 + 24)]),
    ('1/32', 'square', [(BUILD1 + 24, DROP1), (BUILD2 + 24, DROP2)]),
    ('1/8T', 'sine', [(DROP1, BREAK), (DROP2, DROP2 + 32)]),
    ('1/16', 'sine', [(DROP2 + 32, OUTRO)]),
]
GATE, WOBBLE = 0.9, 0.85    # the build's gate and the drops' wobble, as depths
SMOOTH = 3                  # ms — every shaper's edge
PIECE = DROP1 + 1.5         # where the piece render starts — off the beat, off the chop's cycle

# the drop's kick and 808, four bars: the 808 re-attacks on each kick; a note held over the next one's start GLIDES
# into it (one voice, R-INST-11): up a third, up a fifth, up a fourth, down a fifth
KICKS = [0, 1.0, 2.75, 4, 5.75, 8, 9.0, 10.5, 12, 13.25, 14.75]
B808 = [
    (0.0, 1.0, 'A1'), (1.0, 1.75, 'A1'), (2.75, 0.85, 'A1'), (3.5, 0.5, 'C#2'),               # A1 → C♯2, a glide up
    (4.0, 1.75, 'E1'), (5.75, 1.05, 'E1'), (6.75, 1.25, 'B1'),                                # E1 → B1, up
    (8.0, 1.0, 'B1'), (9.0, 1.5, 'B1'), (10.5, 1.1, 'B1'), (11.5, 0.5, 'E2'),                 # B1 → E2, up
    (12.0, 1.25, 'C#2'), (13.25, 1.5, 'C#2'), (14.75, 0.6, 'C#2'), (15.25, 0.75, 'F#1'),      # C♯2 → F♯1, down
]
GLIDES = [(3.5, 'A1', 'C#2'), (6.75, 'E1', 'B1'), (11.5, 'B1', 'E2'), (15.25, 'C#2', 'F#1')]  # measure.py checks these


def hats_bar(b, kind):
    """One bar of hats (beats b … b+4): eighths, with a roll — triplets lead, as the style does."""
    h = [(CHAT, b + 0.5 * k, 0.25, 92 if k % 2 == 0 else 66) for k in range(8)]
    if kind == 0:     # triplet sixteenths into the next bar
        h = [x for x in h if x[1] < b + 3.5] + roll(CHAT, b + 3.5, b + 4.0, 1 / 6, 64, 100)
    elif kind == 1:   # triplet eighths across the third beat, then thirty-seconds
        h = [x for x in h if not (b + 2.0 <= x[1] < b + 3.0 or x[1] >= b + 3.5)] + roll(CHAT, b + 2.0, b + 3.0, 1 / 3, 80, 96) \
            + roll(CHAT, b + 3.5, b + 4.0, 0.125, 66, 104)
    elif kind == 2:   # a skip of triplet sixteenths on the second beat
        h = [x for x in h if not (b + 1.5 <= x[1] < b + 2.0)] + roll(CHAT, b + 1.5, b + 2.0, 1 / 6, 70, 90)
    else:             # the turn: triplet eighths, then triplet thirty-seconds
        h = [x for x in h if x[1] < b + 2.0] + roll(CHAT, b + 2.0, b + 3.0, 1 / 3, 76, 92) + roll(CHAT, b + 3.0, b + 4.0, 1 / 12, 64, 112)
    return h


def main(out):
    line("# Halation — an original melodic trap / future bass track in the Trap Nation style, written by make_script.py")
    line("# (R-SVC-15). 140 bpm half time, C# minor, 80 bars.")
    line('project new halation.slp --bpm 140 --name "Halation"')
    line()

    # ── the kick ──
    line("# the kick: short and round — the 808 is the low end; the drops' pattern, the verse's, the builds' climb")
    kick, kdv, kpt = instrument("drums", "Kick", DROP1, 64)
    setp(kdv, kick__tune=2, kick__decay=260, kick__tone=0.62, kick__level=3, volume=0)
    line(f"set {kpt}.length=16")
    notes_add(kpt, [(KICK, k, 0.25, 124 if k % 4 == 0 else 110) for k in KICKS])
    clip(kick, kpt, DROP2, 64)
    kv = pattern("Kick, the verse", 8)
    notes_add(kv, [(KICK, 0, 0.25, 104), (KICK, 2.75, 0.25, 92), (KICK, 4, 0.25, 104), (KICK, 5.75, 0.25, 90)])
    clip(kick, kv, VERSE, 32)
    steps(f"{kdv}.volume", [(0, 0), (VERSE, -4), (BUILD1, 0)])
    climb = pattern("Kick, the build", 32)     # one a bar, then every beat — and out for the last
    notes_add(climb, [(KICK, b, 0.25, 108) for b in range(0, 16, 4)] + [(KICK, b, 0.25, 114) for b in range(16, 31)])
    for b in (BUILD1, BUILD2):
        clip(kick, climb, b, 32)
    line()

    # ── the 808 ──
    line("# the 808 (R-INST-11): one voice, punched, warm — re-attacking on the kicks, gliding where a note is held into the")
    line("# next; the verse holds the roots; the second drop's last turn dives an octave")
    b8, b8dv, b8pt = instrument("808", "808", DROP1, 64)
    setp(b8dv, punch=12, punch__time=30, glide=90, amp__attack=1, amp__decay=2200, amp__release=160, click=0.25, drive=0.4,
         tone=4200, volume=-1, velocity=0.25)
    line(f"set {b8pt}.length=16")
    notes_add(b8pt, [(m(n), a, l, 118) for a, l, n in B808])
    b8pt2 = pattern("808, second drop", 16)
    notes_add(b8pt2, [(m(n), a, l, 118) for a, l, n in B808[:-1]] + [(m('C#2'), 15.25, 0.45, 118), (m('C#1'), 15.5, 0.5, 112)])
    clip(b8, b8pt2, DROP2, 64)
    b8v = pattern("808, the verse", 16)
    notes_add(b8v, [(m(ROOTS[c]), i * BAR, 1.5, 64) for i, c in enumerate(PROG)])
    clip(b8, b8v, VERSE, 32)
    steps(f"{b8dv}.volume", [(0, -1), (VERSE, -11), (BUILD1, -1)])     # the verse's roots under the piano
    tail = pattern("808, the last", 8)
    notes_add(tail, [(m('C#2'), 0, 6, 118)])
    clip(b8, tail, OUTRO, 8)
    line()

    # ── snare and clap: half time ──
    line("# the snare and the clap together on the third beat of every bar — half time; the verse a clap alone; a fill")
    line("# closes the second drop")
    snr, sdv, spt = instrument("drums", "Snare", DROP1, 64)
    setp(sdv, snare__tune=3, snare__decay=260, snare__tone=0.72, snare__level=2, clap__decay=360, clap__tone=0.62,
         clap__level=2, clap__pan=0.08, volume=2)
    line(f"set {spt}.length=4")
    notes_add(spt, [(SNARE, 2, 0.25, 118), (CLAP, 2, 0.25, 114)])
    fill = pattern("Snare, the fill", 64)
    notes_add(fill, [(p, b * 4 + 2, 0.25, v) for b in range(16) for p, v in ((SNARE, 118), (CLAP, 114))]
              + roll(SNARE, 63.0, 64.0, 1 / 6, 70, 112))
    clip(snr, fill, DROP2, 64)
    soft = pattern("Clap, the verse", 4)
    notes_add(soft, [(CLAP, 2, 0.25, 84)])
    clip(snr, soft, VERSE, 32)
    line()

    # ── hats ──
    line("# hats: eighths rolling in triplets and thirty-seconds; the open hat on the turn; a light pattern elsewhere")
    hat, hdv, hpt = instrument("drums", "Hats", DROP1, 64)
    setp(hdv, chat__tone=0.86, chat__decay=36, chat__level=2, chat__pan=0.2, ohat__decay=260, ohat__level=-3,
         ohat__pan=-0.18, volume=1)
    line(f"set {hpt}.length=16")
    h = []
    for i, kind in enumerate([2, 0, 1, 3]):
        h += hats_bar(i * BAR, kind)
    notes_add(hpt, h + [(OHAT, 7.5, 0.5, 78)])
    hpt2 = pattern("Hats, second drop", 16)    # more rolls: every bar turns
    h2 = []
    for i, kind in enumerate([0, 1, 0, 3]):
        h2 += hats_bar(i * BAR, kind)
    notes_add(hpt2, h2 + [(OHAT, 7.5, 0.25, 76)])
    clip(hat, hpt2, DROP2, 64)
    light = pattern("Hats, light", 8)
    notes_add(light, [(CHAT, 0.5 * k, 0.25, 38 if k % 2 else 52) for k in range(16)])
    for at, ln in [(VERSE, 32), (BUILD1, 31), (BUILD2, 31), (OUTRO, 16)]:
        clip(hat, light, at, ln)
    line()

    # ── cymbals, the impact ──
    line("# cymbals: a crash on each drop's downbeat and halfway; an impact — a noise burst sweeping down — on each drop")
    cym, cdv, cpt = instrument("drums", "Crash", DROP1, 4)
    setp(cdv, crash__level=-1, crash__decay=2800, crash__tone=0.66, volume=-2)
    line(f"set {cpt}.length=4")
    notes_add(cpt, [(CRASH, 0, 0.25, 120)])
    for at in [DROP1 + 32, DROP2, DROP2 + 32, OUTRO]:
        clip(cym, cpt, at, 4)
    imp, idv, ipt = instrument("synth", "Impact", DROP1, 8)
    setp(idv, osc1__level=0, osc2__level=0, noise=1, filter__cutoff=240, filter__res=0.1, filter__env=5.4, filter__keytrack=0,
         fenv__attack=0, fenv__decay=1300, fenv__sustain=0, amp__attack=0, amp__decay=2200, amp__sustain=0, amp__release=1000,
         volume=-6)
    line(f"set {ipt}.length=8")
    notes_add(ipt, [(m('C#2'), 0, 4, 127)])
    clip(imp, ipt, DROP2, 8)
    line()

    # ── the roll: a snare that speeds up through each build, then the gap ──
    line("# the build's roll: quarters, eighths, sixteenths, triplets — then a beat of near-silence")
    rl, rdv, rpt = instrument("drums", "Roll", BUILD1, 32)
    setp(rdv, snare__tune=5, snare__decay=160, snare__tone=0.6, volume=-8)
    line(f"set {rpt}.length=32")
    notes_add(rpt, roll(SNARE, 0, 8, 1.0, 50, 60) + roll(SNARE, 8, 16, 0.5, 60, 74) + roll(SNARE, 16, 24, 0.25, 74, 90)
              + roll(SNARE, 24, 28, 1 / 6, 90, 106) + roll(SNARE, 28, 32 - GAP, 0.125, 106, 124))
    clip(rl, rpt, BUILD2, 32)
    line()

    # ── the future bass chords ──
    line("# the chords: two five-voice saws, wide — held a bar each from the build on; a shaper per rate chops them on the")
    line("# song's grid (R-FX-18): the build's gate speeds up, the drops wobble in triplet eighths; out of the first drop they")
    line("# dive an octave on the pitch wheel")
    chd, chdv, chpt = instrument("synth", "Chords", BUILD1, 32)
    setp(chdv, osc1__wave="saw", osc1__voices=5, osc1__detune=34, osc1__width=32, osc1__level=0.7, osc2__wave="saw",
         osc2__octave=0, osc2__voices=5, osc2__detune=22, osc2__width=24, osc2__fine=8, osc2__level=0.55,
         filter__cutoff=5200, filter__res=0.1, filter__env=0.5, filter__keytrack=0.3, fenv__decay=600, fenv__sustain=0.6,
         amp__attack=5, amp__decay=400, amp__sustain=0.9, amp__release=150, voice__bend=12, volume=-14)
    line(f"set {chpt}.length=32")
    bars = [PROG[i % 4] for i in range(8)]
    notes_add(chpt, [(m(n), i * BAR, BAR - (GAP if i == 7 else 0), 96) for i, c in enumerate(bars) for n in CHORD[c]])
    line(f'expr set {chpt} "0:0" --lane bend')
    clip(chd, chpt, BUILD2, 32)
    chd1 = pattern("Chords, the drop", 16)
    notes_add(chd1, [(m(n), i * BAR, BAR, 100) for i, c in enumerate(PROG) for n in CHORD[c]])
    line(f'expr set {chd1} "0:0" --lane bend')
    clip(chd, chd1, DROP1, 48)
    clip(chd, chd1, DROP2, 64)
    dive = pattern("Chords, the dive", 16)    # the first drop's last turn: down an octave over its last beat and a half
    notes_add(dive, [(m(n), i * BAR, BAR, 100) for i, c in enumerate(PROG) for n in CHORD[c]])
    line(f'expr set {dive} "0:0 14.5:0 15.95:-1" --lane bend')
    clip(chd, dive, DROP1 + 48, 16)
    shapers = []
    for rate, shape, spans in CHOP_RATES:
        sh = device(chd, "shaper", rate=f'"{rate}"', shape=f'"{shape}"', depth=0, smooth=SMOOTH)
        pts, depth = [(0, 0)], GATE if shape == 'square' else WOBBLE
        for a, b in spans:
            pts += [(a, depth), (b, 0)]
        steps(f"{sh}.depth", pts)
        shapers.append(sh)
    automate(f"{chdv}.filter.cutoff", [(0, 900), (BUILD1, 900), (DROP1 - GAP, 6500, 'smooth'), (DROP1, 5200, 'hold'),
                                        (BUILD2, 900), (DROP2 - GAP, 6500, 'smooth'), (DROP2, 5600, 'hold')])
    automate(f"{chdv}.volume", [(0, -6), (BUILD1, -22), (DROP1 - GAP, -6, 'smooth'), (DROP1, -6, 'hold'),       # swelling in
                                 (BUILD2, -22), (DROP2 - GAP, -6, 'smooth'), (DROP2, -6, 'hold')])
    line()

    # ── the vocal chops ──
    line("# the chops: a bright legato synth through the vowel filter (R-FX-19) — the melody, each note its syllable by a")
    line("# held automation placed under every clip of it; soft in the verse, full in the drops, bare in the breakdown. ONE")
    line("# voice, as a sung chop is: a detuned unison beats its harmonics in and out of the formants")
    chp, chpdv, chppt = instrument("synth", "Chops", VERSE, 32)
    setp(chpdv, osc1__wave="saw", osc1__voices=1, osc1__level=0.8, osc2__wave="square",
         osc2__octave=0, osc2__level=0.3, osc2__fine=0, noise=0.03, filter__cutoff=8000, filter__res=0.05, filter__env=0.3,
         filter__keytrack=0, fenv__decay=200, amp__attack=3, amp__decay=160, amp__sustain=0.85, amp__release=80,
         voice__mode="legato", voice__glide=35, vibrato__depth=0.12, vibrato__rate=5.6, velocity=0.3, volume=-10)
    line(f"set {chppt}.length=32")
    notes_add(chppt, [(m(n), a, l, 104 if a % 1 == 0 else 92) for a, l, n, _ in CHOPS])
    for at, ln in CHOP_AT[1:]:
        clip(chp, chppt, at, ln)
    # wide formants, so a high note's harmonics find them; a CUT between syllables, as a chop is — the vowel's line runs
    # A E I O U, and "oh" gliding to "ah" would say "ee" on the way
    vw = device(chp, "vowel", resonance=0.6, glide=8, mix=1, output=9)
    au = ids.new('au')
    line(f'auto add --name "Chops · Vowel" --min 0 --max 4 --length 32 --rest 0')
    for a, _, _, v in CHOPS:
        line(f"auto point add {au} --at {num(a)} --value {v} --shape hold")
    line(f'set {vw}.vowel="={au}"')
    for at, ln in CHOP_AT:                    # the first makes the automation's lane; the rest join it
        ids.new('ac')
        line(f"clip add --auto {au} --at {num(at)} --length {num(ln)}")
    steps(f"{chpdv}.volume", [(0, -2), (VERSE, -11), (DROP1, -2), (BREAK, -11), (DROP2, -2), (OUTRO, -10)])
    line()

    # ── the piano ──
    line("# the piano (R-INST-5): the chords, a bass and a 3-3-2 right hand — the intro opening through a filter, the verse,")
    line("# the breakdown bare, the outro")
    pno, pnodv, pnopt = instrument("piano", "Piano", INTRO, 64)
    setp(pnodv, strings__brightness=0.85, strings__decay=1.4, body__mix=0.55, hammer__hardness=0.85, volume=-4)
    line(f"set {pnopt}.length=16")
    pn = []
    for i, c in enumerate(PROG):
        pn += [(m(n), i * BAR, BAR, 70) for n in PIANO_L[c]]
        pn += [(m(n), i * BAR + a, ln, v) for a, ln, v in [(0, 1.5, 78), (1.5, 1.5, 66), (3.0, 1.0, 70)] for n in PIANO_R[c]]
    notes_add(pnopt, pn)
    for at, ln in [(BREAK, 32), (OUTRO, 32)]:
        clip(pno, pnopt, at, ln)
    steps(f"{pnodv}.volume", [(0, -4), (VERSE, -10), (BREAK, -6), (OUTRO, -5)])
    pf = device(pno, "filter", mode='"lowpass"', cutoff=600, res=0.15)
    automate(f"{pf}.cutoff", [(0, 500), (28, 9000, 'smooth'), (32, 20000, 'hold'), (OUTRO + 8, 20000), (END, 400, 'smooth')])
    line()

    # ── the pad ──
    line("# the pad: warm and slow under the intro, the verse and the breakdown — out for each gap")
    pad, pdv, ppt = instrument("synth", "Pad", INTRO, DROP1 - GAP)
    setp(pdv, osc1__wave="saw", osc1__voices=5, osc1__detune=20, osc1__width=34, osc1__level=0.55, osc2__wave="triangle",
         osc2__octave=1, osc2__level=0.3, filter__cutoff=1400, filter__res=0.15, filter__env=0.2, filter__keytrack=0.25,
         amp__attack=1200, amp__decay=1400, amp__sustain=0.85, amp__release=1800, volume=-16)
    line(f"set {ppt}.length=16")
    notes_add(ppt, [(m(n), i * BAR, BAR, 86) for i, c in enumerate(PROG) for n in PAD[c]])
    for at, ln in [(BREAK, 64 - GAP), (OUTRO, 32)]:
        clip(pad, ppt, at, ln)
    automate(f"{pdv}.volume", [(0, -16), (BUILD1 - 0.5, -16), (BUILD1, -22), (DROP1 - GAP - 0.5, -22), (DROP1 - GAP, -60),
                                (BREAK - 1, -60), (BREAK, -16), (BUILD2, -18), (DROP2 - GAP - 0.5, -22), (DROP2 - GAP, -60),
                                (OUTRO - 1, -60), (OUTRO, -16)])
    line()

    # ── the pluck: the second drop's sparkle ──
    line("# the pluck: a wavetable's harmonics, short and bright — eighths through the second drop")
    plk, pldv, plpt = instrument("wavetable", "Pluck", DROP2, 64)
    setp(pldv, osc1__table="harmonics", osc1__position=0.6, osc1__posenv=-0.3, osc1__level=0.8, osc2__level=0,
         filter__cutoff=3000, filter__res=0.2, filter__env=2.0, fenv__decay=240, fenv__sustain=0, amp__attack=1,
         amp__decay=380, amp__sustain=0, amp__release=240, volume=-14)
    line(f"set {plpt}.length=16")
    notes_add(plpt, [(m(n), i * BAR + 0.5 * k, 0.5, 84 if k % 2 == 0 else 70) for i, c in enumerate(PROG)
                     for k, n in enumerate(ARP[c])])
    line()

    # ── the riser ──
    line("# the riser: noise and a saw, its filter opening and its pitch climbing an octave through each build — cut for the gap")
    rise, rsdv, rspt = instrument("synth", "Riser", BUILD1, 32)
    setp(rsdv, osc1__wave="saw", osc1__voices=3, osc1__detune=40, osc1__width=30, osc1__level=0.3, osc2__level=0, noise=0.85,
         filter__res=0.35, filter__env=0, filter__keytrack=0, amp__attack=1500, amp__sustain=1, amp__release=40,
         volume=-15)
    line(f"set {rspt}.length=32")
    notes_add(rspt, [(m('C#3'), 0, 32 - GAP, 100)])
    clip(rise, rspt, BUILD2, 32)
    automate(f"{rsdv}.filter.cutoff", [(BUILD1, 200), (DROP1 - GAP, 9000, 'smooth'), (DROP1, 200, 'hold'),
                                        (BUILD2, 200), (DROP2 - GAP, 9000, 'smooth'), (DROP2, 200, 'hold')])
    automate(f"{rsdv}.osc1.semi", [(BUILD1, -12), (DROP1 - GAP, 12), (DROP1, -12, 'hold'),
                                    (BUILD2, -12), (DROP2 - GAP, 12), (DROP2, -12, 'hold')])
    automate(f"{rsdv}.volume", [(BUILD1, -40), (DROP1 - GAP, -12, 'smooth'), (DROP1, -40, 'hold'),
                                 (BUILD2, -40), (DROP2 - GAP, -12, 'smooth'), (DROP2, -40, 'hold')])
    line()

    # ── the pump ──
    line("# the pump: the kick keys a compressor on the chords, the pad, the pluck, the chops — and, lightly, the 808")
    for ch, thr, ratio, rel in [(chd, -24, 4, 150), (pad, -26, 4, 180), (plk, -24, 3, 120), (chp, -20, 2, 110), (b8, -18, 2, 70)]:
        device(ch, "compressor", sidechain="on", threshold=thr, ratio=ratio, attack=1, release=rel)
        line(f"send add {kick} --to {ch} --sidechain")
    line()

    # ── tone ──
    line("# tone: the 808 warmed and its rumble cut; everything else gives the low end to the kick and the 808")
    device(b8, "saturator", drive=6, curve="tube", mix=0.5, output=-2)
    device(b8, "eq", lowcut__on="on", lowcut__freq=28)
    for ch, hz in [(chd, 190), (chp, 280), (pad, 120), (plk, 250), (rise, 180), (hat, 400), (snr, 150), (cym, 400),
                   (imp, 60), (pno, 70)]:
        device(ch, "eq", lowcut__on="on", lowcut__freq=hz)
    line()

    # ── space ──
    line("# space: a long, wide reverb and a dotted-eighth delay, each on a bus — the style's wash")
    verb = ids.new('ch')
    line('strip add --kind bus --name Reverb')
    device(verb, "reverb", decay=3600, size=180, mix=1, lowcut=300, highcut=10000, width=1)
    dly = ids.new('ch')
    line('strip add --kind bus --name Delay')
    device(dly, "delay", time='"=60000/bpm*0.75"', feedback=0.38, tone=4000, mix=1)
    for ch, to, g in [(chp, verb, -16), (chp, dly, -19), (chd, verb, -18), (pno, verb, -5), (pad, verb, -6), (plk, verb, -8),
                      (plk, dly, -10), (snr, verb, -13), (cym, verb, -12), (imp, verb, -4), (rise, verb, -12)]:
        line(f"send add {ch} --to {to} --gain {g}")
    line()

    # ── the mix and the master ──
    line("# the mix: faders; the master: a limiter, its ceiling under −1 dBTP")
    gains = {kick: -1, b8: -8, snr: 2, hat: -4, cym: -9, imp: -5, rl: -4, chd: -4, chp: -3, pno: -3, pad: -6, plk: -9,
             rise: -5, verb: -5, dly: -9}
    line("set " + " ".join(f"{ch}.gain={g}" for ch, g in gains.items()))
    device("master", "limiter", gain=9.5, ceiling=-1.3, release=60, truePeak="on")
    line()
    line("ls")
    line("audit")
    line("project save")
    line(f"render --out halation.wav --stems {kick},{b8},{snr},{hat},{chd},{chp}")
    line(f"# a piece of the first drop from half a beat past its second beat: the chop still on the SONG's grid (R-FX-18)")
    line(f"render --out halation-piece.wav --from {num(PIECE)} --to {num(DROP1 + 32)} --stems {chd}")

    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    return {"kick": kick, "808": b8, "snare": snr, "hats": hat, "chords": chd, "chops": chp}


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "song.txt")
    stems = main(out)
    print(f"{len(L)} lines → {out}; stems {stems}")
