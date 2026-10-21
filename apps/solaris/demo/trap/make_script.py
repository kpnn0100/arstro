#!/usr/bin/env python3
"""Magnetar — an original trap / EDM track (Solaris R-SVC-13), written as a solaris-cc script.

Nothing here is anyone else's song: the progression (i–VI–iv–V in F minor, the V major for its pull home), the hook,
the 808 line, the drums and the form were written for this track. 150 bpm in half time, 80 bars (320 beats, 2:08):

    intro 1–8 · build 9–16 · drop 17–32 · breakdown 33–48 · build 49–56 · drop 57–72 · outro 73–80

Every sound is the DSP library's: a short trap kick; an 808 (R-INST-11) that glides between held notes, driven
through a saturator; a snare and a clap together on the third beat of every bar (half time); hi-hats in eighths
with thirty-second and triplet rolls; a crash and a noise impact on each drop; brass-like stabs on the hook (a
wavetable's sync table, saturated); a supersaw under the drops; a dark pad; a glassy pluck arpeggio; a riser; a
screaming lead an octave up through the second drop's second half. A reverb and a dotted-eighth delay; the kick
keys a sidechain; a limiter on the master. Each build ends in a beat of near-silence — then the drop.

    python3 make_script.py [out.txt]      # writes song.txt (or out.txt)
"""
import os, sys

BPM = 150
BAR = 4
INTRO, BUILD1, DROP1, BREAK, BUILD2, DROP2, OUTRO, END = 0, 32, 64, 128, 192, 224, 288, 320
GAP = 1.0           # the beat of near-silence before each drop

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
KICK, RIM, SNARE, CLAP, CHAT, OHAT, CRASH = 36, 37, 38, 39, 42, 46, 49


def m(n):
    """'F1' / 'Db2' / 'Bb4' → MIDI (C4 = 60)."""
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


def roll(pitch, a, b, div, v0, v1):
    """Hits from beat a to b every `div` beats, the velocity climbing v0 → v1."""
    n = int(round((b - a) / div))
    return [(pitch, a + k * div, min(div, 0.25), int(round(v0 + (v1 - v0) * k / max(1, n - 1)))) for k in range(n)]


# ── the harmony: a bar a chord, four bars a turn — i, VI, iv, V (the major V pulls home) ─────────────────────────
PROG = ['Fm', 'Db', 'Bbm', 'C']
ROOT808 = {'Fm': 'F1', 'Db': 'Db2', 'Bbm': 'Bb1', 'C': 'C2'}
CHORD = {'Fm': ['Ab3', 'C4', 'F4', 'Ab4'], 'Db': ['Ab3', 'Db4', 'F4', 'Ab4'],
         'Bbm': ['Bb3', 'Db4', 'F4', 'Bb4'], 'C': ['G3', 'C4', 'E4', 'G4']}
PAD = {'Fm': ['F2', 'C3', 'Ab3'], 'Db': ['Db2', 'Ab2', 'F3'], 'Bbm': ['Bb1', 'F2', 'Db3'], 'C': ['C2', 'G2', 'E3']}
ARP = {'Fm': ['C5', 'Ab4', 'F4', 'Ab4', 'C5', 'Eb5', 'C5', 'Ab4'], 'Db': ['Db5', 'Ab4', 'F4', 'Ab4', 'Db5', 'F5', 'Db5', 'Ab4'],
       'Bbm': ['Db5', 'Bb4', 'F4', 'Bb4', 'Db5', 'F5', 'Eb5', 'Db5'], 'C': ['C5', 'G4', 'E4', 'G4', 'C5', 'E5', 'G5', 'E5']}

# the hook: four bars of stabs over the turn (beat, length, note) — written for this track
HOOK = [
    (0.0, 0.5, 'C5'), (0.75, 0.25, 'C5'), (1.5, 0.25, 'Ab4'), (2.0, 0.5, 'C5'), (2.75, 0.25, 'Db5'), (3.25, 0.5, 'C5'),
    (4.0, 0.5, 'F5'), (4.75, 0.25, 'Eb5'), (5.5, 0.25, 'Db5'), (6.0, 0.5, 'C5'), (6.75, 0.75, 'Ab4'),
    (8.0, 0.5, 'Db5'), (8.75, 0.25, 'Db5'), (9.5, 0.25, 'Bb4'), (10.0, 0.5, 'Db5'), (10.75, 0.25, 'Eb5'), (11.25, 0.5, 'F5'),
    (12.0, 0.5, 'E5'), (12.75, 0.25, 'F5'), (13.5, 0.5, 'G5'), (14.0, 0.5, 'E5'), (14.75, 1.0, 'C5'),
]

# the drop's kick and 808, four bars: the 808 re-attacks on each kick; a note held over the next one's start GLIDES
# into it (one voice, R-INST-11): up the octave, down a fourth, up a third, down a fourth
KICKS = [0, 0.75, 2.5, 4, 5.5, 8, 8.75, 10.5, 12, 13.5, 14.5]
B808 = [
    (0.0, 0.75, 'F1'), (0.75, 1.75, 'F1'), (2.5, 1.1, 'F1'), (3.5, 0.5, 'F2'),                 # F1 → F2, a glide up
    (4.0, 1.5, 'Db2'), (5.5, 1.3, 'Db2'), (6.75, 1.25, 'Ab1'),                                # Db2 → Ab1, down
    (8.0, 0.75, 'Bb1'), (8.75, 1.75, 'Bb1'), (10.5, 1.1, 'Bb1'), (11.5, 0.5, 'Db2'),            # Bb1 → Db2, up
    (12.0, 1.5, 'C2'), (13.5, 1.0, 'C2'), (14.5, 0.85, 'C2'), (15.25, 0.75, 'G1'),             # C2 → G1, down
]
GLIDES = [(3.5, 'F1', 'F2'), (6.75, 'Db2', 'Ab1'), (11.5, 'Bb1', 'Db2'), (15.25, 'C2', 'G1')]  # measure.py checks these


def hats_bar(b, kind):
    """One bar of hats (beats b … b+4): eighths, with a roll in its last beat or two."""
    h = [(CHAT, b + 0.5 * k, 0.25, 96 if k % 2 == 0 else 70) for k in range(8)]
    if kind == 0:     # thirty-seconds into the next bar
        h = [x for x in h if x[1] < b + 3.5] + roll(CHAT, b + 3.5, b + 4.0, 0.125, 62, 104)
    elif kind == 1:   # a triplet skip, then sixteenths
        h = [x for x in h if not (b + 1.5 <= x[1] < b + 2.0 or x[1] >= b + 3.0)] + roll(CHAT, b + 1.5, b + 2.0, 1 / 6, 70, 92) \
            + roll(CHAT, b + 3.0, b + 4.0, 0.25, 74, 100)
    elif kind == 2:   # triplet eighths across the last beat
        h = [x for x in h if x[1] < b + 3.0] + roll(CHAT, b + 3.0, b + 4.0, 1 / 3, 84, 100)
    else:             # the turn: sixteenths, thirty-seconds, then triplet thirty-seconds
        h = [x for x in h if x[1] < b + 2.0] + roll(CHAT, b + 2.0, b + 3.0, 0.25, 72, 92) + roll(CHAT, b + 3.0, b + 3.5, 0.125, 76, 98) \
            + roll(CHAT, b + 3.5, b + 4.0, 1 / 12, 70, 110)
    return h


def main(out):
    line("# Magnetar — an original trap / EDM track, written by make_script.py (R-SVC-13). 150 bpm half time, F minor, 80 bars.")
    line('project new magnetar.slp --bpm 150 --name "Magnetar"')
    line()

    # ── the kick ──
    line("# the kick: short and hard — the 808 is the low end; the drops' pattern, and the builds' climb")
    kick, kdv, kpt = instrument("drums", "Kick", DROP1, 64)
    setp(kdv, kick__tune=3, kick__decay=240, kick__tone=0.72, kick__level=3, volume=0)
    line(f"set {kpt}.length=16")
    notes_add(kpt, [(KICK, k, 0.25, 124 if k % 4 == 0 else 112) for k in KICKS])
    kpt2 = pattern("Kick, second drop", 16)    # the same, and a double at the turn
    notes_add(kpt2, [(KICK, k, 0.25, 124 if k % 4 == 0 else 112) for k in KICKS + [15.25, 15.75]])
    clip(kick, kpt2, DROP2, 64)
    climb = pattern("Kick, the build", 32)     # one a bar, then every beat — and out for the last
    notes_add(climb, [(KICK, b, 0.25, 110) for b in range(0, 16, 4)] + [(KICK, b, 0.25, 116) for b in range(16, 31)])
    for b in (BUILD1, BUILD2):
        clip(kick, climb, b, 32)
    last = pattern("Kick, the last", 4)
    notes_add(last, [(KICK, 0, 0.25, 124)])
    clip(kick, last, OUTRO, 4)
    line()

    # ── the 808 ──
    line("# the 808 (R-INST-11): one voice, punched, driven — re-attacking on the kicks, gliding where a note is held into the next")
    b8, b8dv, b8pt = instrument("808", "808", DROP1, 64)
    setp(b8dv, punch=14, punch__time=32, glide=85, amp__attack=1, amp__decay=1900, amp__release=140, click=0.32, drive=0.55,
         tone=5200, volume=-1, velocity=0.25)
    line(f"set {b8pt}.length=16")
    notes_add(b8pt, [(m(n), a, l, 118) for a, l, n in B808])
    b8pt2 = pattern("808, second drop", 16)    # the turn's last beat dives an octave and climbs back
    notes_add(b8pt2, [(m(n), a, l, 118) for a, l, n in B808[:-1]] + [(m('C2'), 15.25, 0.45, 118), (m('C1'), 15.5, 0.5, 112)])
    clip(b8, b8pt2, DROP2, 64)
    tail = pattern("808, the last", 8)
    notes_add(tail, [(m('F1'), 0, 6, 120)])
    clip(b8, tail, OUTRO, 8)
    line()

    # ── snare and clap: half time ──
    line("# the snare and the clap together on the third beat of every bar — half time; a fill closes the second drop")
    snr, sdv, spt = instrument("drums", "Snare", DROP1, 64)
    setp(sdv, snare__tune=2, snare__decay=230, snare__tone=0.78, snare__level=3, clap__decay=320, clap__tone=0.6,
         clap__level=1, clap__pan=0.1, volume=2)
    line(f"set {spt}.length=4")
    notes_add(spt, [(SNARE, 2, 0.25, 120), (CLAP, 2, 0.25, 112)])
    fill = pattern("Snare, the fill", 64)
    notes_add(fill, [(p, b * 4 + 2, 0.25, v) for b in range(16) for p, v in ((SNARE, 120), (CLAP, 112))]
              + roll(SNARE, 63.0, 64.0, 0.25, 70, 110))
    clip(snr, fill, DROP2, 64)
    soft = pattern("Clap, the breakdown", 4)
    notes_add(soft, [(CLAP, 2, 0.25, 62)])
    clip(snr, soft, BREAK + 32, 32)
    line()

    # ── hats ──
    line("# hats: eighths, with thirty-second and triplet rolls; the open hat on the turn; a lighter pattern for the intro")
    hat, hdv, hpt = instrument("drums", "Hats", DROP1, 64)
    setp(hdv, chat__tone=0.88, chat__decay=38, chat__level=2, chat__pan=0.18, ohat__decay=240, ohat__level=-2,
         ohat__pan=-0.15, volume=2)
    line(f"set {hpt}.length=16")
    h = []
    for i, kind in enumerate([0, 1, 2, 3]):
        h += hats_bar(i * BAR, kind)
    h += [(OHAT, 7.5, 0.5, 80), (OHAT, 15.5, 0.25, 84)]
    notes_add(hpt, [x for x in h if not (x[0] == CHAT and x[1] in (7.5, 15.5))])
    hpt2 = pattern("Hats, second drop", 16)    # more rolls: every bar turns
    h2 = []
    for i, kind in enumerate([1, 0, 3, 3]):
        h2 += hats_bar(i * BAR, kind)
    notes_add(hpt2, h2 + [(OHAT, 3.5, 0.25, 78), (OHAT, 11.5, 0.25, 78)])
    clip(hat, hpt2, DROP2, 64)
    light = pattern("Hats, light", 8)
    notes_add(light, [(CHAT, 0.5 * k, 0.25, 40 if k % 2 else 54) for k in range(16)])
    for at, ln in [(INTRO + 16, 16), (BUILD1, 31), (BREAK + 32, 32), (BUILD2, 31), (OUTRO, 16)]:
        clip(hat, light, at, ln)
    line()

    # ── percussion: rims in the second drop ──
    line("# rims: a syncopated tick through the second drop")
    perc, pcdv, pcpt = instrument("drums", "Rims", DROP2, 64)
    setp(pcdv, rim__tune=5, rim__decay=60, rim__level=-6, rim__pan=-0.35, volume=-4)
    line(f"set {pcpt}.length=8")
    notes_add(pcpt, [(RIM, a, 0.25, v) for a, v in [(1.25, 90), (3.75, 80), (5.25, 90), (6.5, 76), (7.75, 86)]])
    line()

    # ── cymbals, the impact ──
    line("# cymbals: a crash on each drop's downbeat and phrase; an impact — a noise burst sweeping down — on each drop")
    cym, cdv, cpt = instrument("drums", "Crash", DROP1, 4)
    setp(cdv, crash__level=-1, crash__decay=2600, crash__tone=0.7, volume=-2)
    line(f"set {cpt}.length=4")
    notes_add(cpt, [(CRASH, 0, 0.25, 122)])
    for at in [DROP1 + 32, BREAK, DROP2, DROP2 + 32, OUTRO]:
        clip(cym, cpt, at, 4)
    imp, idv, ipt = instrument("synth", "Impact", DROP1, 8)
    setp(idv, osc1__level=0, osc2__level=0, noise=1, filter__cutoff=260, filter__res=0.1, filter__env=5.2, filter__keytrack=0,
         fenv__attack=0, fenv__decay=1100, fenv__sustain=0, amp__attack=0, amp__decay=1800, amp__sustain=0, amp__release=900,
         volume=-6)
    line(f"set {ipt}.length=8")
    notes_add(ipt, [(m('F2'), 0, 4, 127)])
    clip(imp, ipt, DROP2, 8)
    line()

    # ── the roll: a snare that speeds up through each build, then the gap ──
    line("# the build's roll: quarters, eighths, sixteenths, thirty-seconds, triplets — then a beat of near-silence")
    rl, rdv, rpt = instrument("drums", "Roll", BUILD1, 32)
    setp(rdv, snare__tune=4, snare__decay=170, snare__tone=0.65, volume=-7)
    line(f"set {rpt}.length=32")
    notes_add(rpt, roll(SNARE, 0, 8, 1.0, 54, 64) + roll(SNARE, 8, 16, 0.5, 64, 76) + roll(SNARE, 16, 24, 0.25, 76, 90)
              + roll(SNARE, 24, 28, 0.125, 90, 104) + roll(SNARE, 28, 32 - GAP, 1 / 6, 104, 124))
    clip(rl, rpt, BUILD2, 32)
    line()

    # ── the stabs: the hook ──
    line("# the stabs: the hook on a wavetable's sync table and a saw an octave under, a snapping filter, saturated")
    horn, hrdv, hrpt = instrument("wavetable", "Stabs", DROP1, 64)
    setp(hrdv, osc1__table="sync", osc1__position=0.35, osc1__posenv=0.35, osc1__voices=3, osc1__detune=18, osc1__width=22,
         osc1__level=0.8, osc2__table="basic", osc2__position=0.7, osc2__octave=-1, osc2__level=0.45, filter__cutoff=1500,
         filter__res=0.22, filter__env=3.4, fenv__decay=200, fenv__sustain=0.18, amp__attack=2, amp__decay=280,
         amp__sustain=0.5, amp__release=110, volume=-9)
    line(f"set {hrpt}.length=16")
    notes_add(hrpt, [(m(n), a, l, 112 if a % 1 == 0 else 98) for a, l, n in HOOK])
    clip(horn, hrpt, DROP2, 64)
    device(horn, "saturator", drive=10, curve="tube", mix=0.7, output=-3)
    line()

    # ── the scream: the hook an octave up, the second drop's second half ──
    line("# the scream: a square and a saw a fifth apart through a resonant band-pass, vibrato — the hook an octave up")
    scr, scdv, scpt = instrument("synth", "Scream", DROP2 + 32, 32)
    setp(scdv, osc1__wave="square", osc1__voices=2, osc1__detune=12, osc1__width=18, osc2__wave="saw", osc2__semi=7,
         osc2__level=0.5, filter__mode="bandpass", filter__cutoff=2400, filter__res=0.55, filter__env=1.2, fenv__decay=160,
         fenv__sustain=0.4, amp__attack=3, amp__decay=200, amp__sustain=0.6, amp__release=120, vibrato__depth=0.12,
         vibrato__rate=6.5, volume=-18)
    line(f"set {scpt}.length=16")
    notes_add(scpt, [(m(n) + 12, a, l, 100) for a, l, n in HOOK])
    line()

    # ── the supersaw: under the drops, and the breakdown's second half ──
    line("# the supersaw: two five-voice saws, wide, held a bar each under the drops and the breakdown's second half")
    sup, sudv, supt = instrument("synth", "Supersaw", DROP1, 64)
    setp(sudv, osc1__wave="saw", osc1__voices=5, osc1__detune=40, osc1__width=26, osc1__level=0.7, osc2__wave="saw",
         osc2__voices=5, osc2__detune=24, osc2__width=18, osc2__fine=7, osc2__level=0.55, filter__cutoff=4200,
         filter__res=0.1, filter__env=0.6, fenv__decay=800, fenv__sustain=0.6, amp__attack=8, amp__decay=500,
         amp__sustain=0.88, amp__release=380, volume=-15)
    line(f"set {supt}.length=16")
    notes_add(supt, [(m(n), i * BAR, BAR, 92) for i, c in enumerate(PROG) for n in CHORD[c]])
    for at, ln in [(BREAK + 32, 32), (DROP2, 64)]:
        clip(sup, supt, at, ln)
    line()

    # ── the pad ──
    line("# the pad: dark and slow, its filter opening through the intro and the builds")
    pad, pdv, ppt = instrument("synth", "Pad", INTRO, 64)
    setp(pdv, osc1__wave="saw", osc1__voices=5, osc1__detune=22, osc1__width=34, osc1__level=0.6, osc2__wave="triangle",
         osc2__octave=1, osc2__level=0.3, filter__res=0.18, filter__env=0.2, filter__keytrack=0.25, amp__attack=900,
         amp__decay=1200, amp__sustain=0.85, amp__release=1600, volume=-14)
    line(f"set {ppt}.length=16")
    notes_add(ppt, [(m(n), i * BAR, BAR, 88) for i, c in enumerate(PROG) for n in PAD[c]])
    for at, ln in [(BREAK, 96), (OUTRO, 32)]:
        clip(pad, ppt, at, ln)
    automate(f"{pdv}.filter.cutoff", [(0, 220), (31, 1600, 'smooth'), (63, 6500, 'smooth'), (64, 700, 'hold'),
                                       (BREAK, 900), (BREAK + 32, 2600, 'smooth'), (DROP2 - GAP, 7000, 'smooth'),
                                       (DROP2, 700, 'hold'), (OUTRO, 2400), (END, 200, 'smooth')])
    # … and out for each gap: the drop's beat of near-silence has no pad ringing in it
    automate(f"{pdv}.volume", [(0, -14), (DROP1 - GAP - 0.5, -14), (DROP1 - GAP, -60), (BREAK - 1, -60), (BREAK, -16),
                                (BUILD2, -16), (BUILD2 + 8, -14), (DROP2 - GAP - 0.5, -14), (DROP2 - GAP, -60), (OUTRO - 1, -60),
                                (OUTRO, -14)])
    line()

    # ── the pluck: a glassy arpeggio ──
    line("# the pluck: a wavetable's harmonics, short and bright — eighths through the intro, the breakdown and the outro; the")
    line("# breakdown's second half plays the hook on it, an octave up")
    plk, pldv, plpt = instrument("wavetable", "Pluck", INTRO + 8, 56 - GAP)
    setp(pldv, osc1__table="harmonics", osc1__position=0.62, osc1__posenv=-0.3, osc1__level=0.8, osc2__level=0,
         filter__cutoff=2600, filter__res=0.2, filter__env=2.0, fenv__decay=260, fenv__sustain=0, amp__attack=1,
         amp__decay=420, amp__sustain=0, amp__release=260, volume=-12)
    line(f"set {plpt}.length=16")
    notes_add(plpt, [(m(n), i * BAR + 0.5 * k, 0.5, 86 if k % 2 == 0 else 72) for i, c in enumerate(PROG)
                     for k, n in enumerate(ARP[c])])
    for at, ln in [(BREAK, 32), (OUTRO, 32)]:
        clip(plk, plpt, at, ln)
    phook = pattern("Pluck, the hook", 16)
    notes_add(phook, [(m(n) + 12, a, max(l, 0.5), 90) for a, l, n in HOOK])
    clip(plk, phook, BREAK + 32, 32)
    line()

    # ── the riser ──
    line("# the riser: noise and a saw, its filter opening and its pitch climbing an octave through each build — cut for the gap")
    rise, rsdv, rspt = instrument("synth", "Riser", BUILD1, 32)
    setp(rsdv, osc1__wave="saw", osc1__voices=3, osc1__detune=40, osc1__width=30, osc1__level=0.3, osc2__level=0, noise=0.85,
         filter__res=0.35, filter__env=0, filter__keytrack=0, amp__attack=1500, amp__sustain=1, amp__release=40,
         volume=-15)
    line(f"set {rspt}.length=32")
    notes_add(rspt, [(m('F3'), 0, 32 - GAP, 100)])
    clip(rise, rspt, BUILD2, 32)
    automate(f"{rsdv}.filter.cutoff", [(BUILD1, 200), (DROP1 - GAP, 9000, 'smooth'), (DROP1, 200, 'hold'),
                                        (BUILD2, 200), (DROP2 - GAP, 9000, 'smooth'), (DROP2, 200, 'hold')])
    automate(f"{rsdv}.osc1.semi", [(BUILD1, -12), (DROP1 - GAP, 12), (DROP1, -12, 'hold'),
                                    (BUILD2, -12), (DROP2 - GAP, 12), (DROP2, -12, 'hold')])
    automate(f"{rsdv}.volume", [(BUILD1, -40), (DROP1 - GAP, -12, 'smooth'), (DROP1, -40, 'hold'),
                                 (BUILD2, -40), (DROP2 - GAP, -12, 'smooth'), (DROP2, -40, 'hold')])
    line()

    # ── the pump ──
    line("# the pump: the kick keys a compressor on the supersaw, the pad, the stabs and the scream — and, lightly, the 808")
    for ch, thr, ratio, rel in [(sup, -26, 6, 140), (pad, -26, 4, 180), (horn, -22, 2.5, 110), (scr, -22, 2.5, 110), (b8, -18, 2, 70)]:
        device(ch, "compressor", sidechain="on", threshold=thr, ratio=ratio, attack=1, release=rel)
        line(f"send add {kick} --to {ch} --sidechain")
    line()

    # ── tone ──
    line("# tone: the 808 driven and its rumble cut; everything else gives the low end to the kick and the 808")
    device(b8, "saturator", drive=8, curve="tube", mix=0.55, output=-2)
    device(b8, "eq", lowcut__on="on", lowcut__freq=28)
    for ch, hz in [(sup, 200), (horn, 160), (scr, 300), (pad, 120), (plk, 250), (rise, 180), (hat, 400), (snr, 140),
                   (cym, 400), (imp, 60), (perc, 300)]:
        device(ch, "eq", lowcut__on="on", lowcut__freq=hz)
    line()

    # ── space ──
    line("# space: a long, wide reverb and a dotted-eighth delay, each on a bus")
    verb = ids.new('ch')
    line('strip add --kind bus --name Reverb')
    device(verb, "reverb", decay=2800, size=150, mix=1, lowcut=350, highcut=11000, width=1)
    dly = ids.new('ch')
    line('strip add --kind bus --name Delay')
    device(dly, "delay", time='"=60000/bpm*0.75"', feedback=0.34, tone=4200, mix=1)
    for ch, to, g in [(horn, verb, -13), (horn, dly, -16), (scr, dly, -12), (sup, verb, -12), (pad, verb, -6),
                      (plk, verb, -7), (plk, dly, -10), (snr, verb, -15), (cym, verb, -12), (imp, verb, -4), (rise, verb, -8)]:
        line(f"send add {ch} --to {to} --gain {g}")
    line()

    # ── the mix and the master ──
    line("# the mix: faders; the master: a limiter, its ceiling under −1 dBTP")
    gains = {kick: -1, b8: -7, snr: 0, hat: -7, perc: -9, cym: -9, imp: -5, rl: -4, horn: -2, scr: -4, sup: -5,
             pad: -7, plk: -8, rise: -5, verb: -6, dly: -10}
    line("set " + " ".join(f"{ch}.gain={g}" for ch, g in gains.items()))
    device("master", "limiter", gain=10, ceiling=-1.3, release=60, truePeak="on")
    line()
    line("ls")
    line("audit")
    line("project save")
    line(f"render --out magnetar.wav --stems {kick},{b8},{snr},{hat},{sup}")

    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    return {"kick": kick, "808": b8, "snare": snr, "hats": hat, "supersaw": sup}


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "song.txt")
    stems = main(out)
    print(f"{len(L)} lines → {out}; stems {stems}")
