#!/usr/bin/env python3
"""Starward — an original electro / future house track in the NCS style (Solaris R-SVC-16), written as a solaris-cc
script.

The STYLE, nobody's song: the progression (vi–IV–I–V in G major), the hook, the bass line, the drums and the form were
written for this track. 128 bpm, 80 bars (320 beats, 2:30):

    intro 1–8 · verse 9–16 · build 17–24 · drop 25–40 · breakdown 41–48 · build 49–56 · drop 57–72 · outro 73–80

Every sound is the DSP library's. What makes the style: a kick on every beat, a clap on two and four, open hats on the
off-beats; a bouncy future house bass — an FM bass on the off-beats between the kicks, jumping octaves; chords and a pad
PUMPING on the shaper's pump shape a beat long, on the song's grid (R-FX-18) — the sidechain sound with no compressor;
a HOOK, a two-bar motif in one rhythm every bar, on a bright lead through the drops and on a pluck in the intro and the
breakdown; builds with a kick roll, a snare roll and a riser, and a beat of near-silence before each drop.

    python3 make_script.py [out.txt]      # writes song.txt (or out.txt)
"""
import os, sys

BPM = 128
BAR = 4
INTRO, VERSE, BUILD1, DROP1, BREAK, BUILD2, DROP2, OUTRO, END = 0, 32, 64, 96, 160, 192, 224, 288, 320
GAP = 1.0           # the beat of near-silence before each drop

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
KICK, RIM, SNARE, CLAP, CHAT, OHAT, CRASH = 36, 37, 38, 39, 42, 46, 49


def m(n):
    """'E2' / 'F#5' / 'Bb4' → MIDI (C4 = 60)."""
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
    """points: (beat, value[, shape]) — a shape governs the segment AFTER its point; the first point takes the place
    of the one `auto create` makes at 0."""
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


# ── the harmony: a bar a chord, four bars a turn — vi, IV, I, V in G major ─────────────────────────────────────────
PROG = ['Em', 'C', 'G', 'D']
ROOTS = {'Em': 'E2', 'C': 'C2', 'G': 'G2', 'D': 'D2'}
CHORD = {'Em': ['E3', 'B3', 'D4', 'G4', 'B4'], 'C': ['C3', 'G3', 'D4', 'E4', 'G4'],           # Em7 · Cadd9
         'G': ['G3', 'B3', 'D4', 'G4', 'B4'], 'D': ['D3', 'A3', 'D4', 'F#4', 'A4']}            # G · D
PAD = {'Em': ['E3', 'B3', 'G4'], 'C': ['C3', 'G3', 'E4'], 'G': ['G2', 'D3', 'B3'], 'D': ['D3', 'A3', 'F#4']}
PIANO_L = {'Em': ['E2', 'E3'], 'C': ['C2', 'C3'], 'G': ['G1', 'G2'], 'D': ['D2', 'D3']}
PIANO_R = {'Em': ['G4', 'B4', 'E5'], 'C': ['G4', 'C5', 'E5'], 'G': ['G4', 'B4', 'D5'], 'D': ['F#4', 'A4', 'D5']}

# the hook: eight bars of one rhythm — four eighths, two dotted eighths, an eighth — written for this track. A two-bar
# motif (bars 1–2) returns at bars 5–6; bar 4 ASKS (it climbs to A5 over D), bar 8 ANSWERS (it falls to B4, the fifth of
# the Em it loops back to)
RHYTHM = [(0.0, 0.5), (0.5, 0.5), (1.0, 0.5), (1.5, 0.5), (2.0, 0.75), (2.75, 0.75), (3.5, 0.5)]
BARS = [['B4', 'B4', 'D5', 'B4', 'E5', 'D5', 'B4'], ['C5', 'C5', 'E5', 'C5', 'G5', 'E5', 'C5'],
        ['B4', 'B4', 'D5', 'B4', 'G5', 'F#5', 'D5'], ['A4', 'A4', 'D5', 'F#5', 'A5', 'G5', 'F#5'],
        ['B4', 'B4', 'D5', 'B4', 'E5', 'D5', 'B4'], ['C5', 'C5', 'E5', 'C5', 'G5', 'E5', 'C5'],
        ['B4', 'B4', 'D5', 'B4', 'G5', 'F#5', 'D5'], ['A4', 'A4', 'D5', 'F#5', 'E5', 'D5', 'B4']]
HOOK = [(b * BAR + a, l, n) for b, bar in enumerate(BARS) for (a, l), n in zip(RHYTHM, bar)]

# the bass: every bar on the off-beats between the kicks, the root and its octave — the bounce (beat, length, +octave)
BOUNCE = [(0.5, 0.25, 0), (1.5, 0.25, 12), (1.75, 0.25, 0), (2.5, 0.25, 0), (3.5, 0.25, 12), (3.75, 0.25, 0)]
BOUNCE2 = BOUNCE + [(0.75, 0.25, 12), (2.75, 0.25, 12)]     # the second drop's: two more sixteenths
PUMP = 0.85                 # the pump's depth (the shaper's `pump`, a beat long)
SMOOTH = 2                  # ms — the shaper's edge


def main(out):
    line("# Starward — an original electro / future house track in the NCS style, written by make_script.py (R-SVC-16).")
    line("# 128 bpm, G major, 80 bars.")
    line('project new starward.slp --bpm 128 --name "Starward"')
    line()

    # ── the kick: four on the floor ──
    line("# the kick: every beat — from the intro's second half, the verse, the drops; in each build quarters, eighths, then")
    line("# a sixteenth roll — and out for the gap")
    kick, kdv, kpt = instrument("drums", "Kick", DROP1, 64)
    setp(kdv, kick__tune=0, kick__decay=330, kick__tone=0.7, kick__level=3, volume=0)
    line(f"set {kpt}.length=4")
    notes_add(kpt, [(KICK, b, 0.25, 124 if b == 0 else 116) for b in range(4)])
    for at, ln in [(INTRO + 16, 16), (VERSE, 32), (DROP2, 64), (OUTRO, 16)]:
        clip(kick, kpt, at, ln)
    climb = pattern("Kick, the build", 32)
    notes_add(climb, [(KICK, b, 0.25, 92) for b in range(16)] + roll(KICK, 16, 24, 0.5, 92, 100)
              + roll(KICK, 24, 28, 0.25, 92, 102) + roll(KICK, 28, 32 - GAP, 0.125, 90, 108))
    steps(f"{kdv}.volume", [(0, 0), (VERSE, -7), (BUILD1, -3), (DROP1, 0), (BUILD2, -3), (DROP2, 0)])
    for b in (BUILD1, BUILD2):
        clip(kick, climb, b, 32)
    line()

    # ── the clap: two and four ──
    line("# the clap and a snare under it on two and four — the drops, the verse (a clap alone)")
    clp, cldv, clpt = instrument("drums", "Clap", DROP1, 64)
    setp(cldv, clap__decay=300, clap__tone=0.7, clap__level=2, snare__tune=4, snare__decay=180, snare__tone=0.8,
         snare__level=-4, volume=0)
    line(f"set {clpt}.length=4")
    notes_add(clpt, [(p, b, 0.25, v) for b in (1, 3) for p, v in ((CLAP, 118), (SNARE, 100))])
    clip(clp, clpt, DROP2, 64)
    cv = pattern("Clap, the verse", 4)
    notes_add(cv, [(CLAP, b, 0.25, 96) for b in (1, 3)])
    clip(clp, cv, VERSE, 32)
    line()

    # ── hats ──
    line("# hats: an open hat on every off-beat, closed sixteenths around it — the drops; the verse and the intro lighter")
    hat, hdv, hpt = instrument("drums", "Hats", DROP1, 64)
    setp(hdv, chat__tone=0.9, chat__decay=30, chat__level=0, chat__pan=0.22, ohat__decay=180, ohat__tone=0.85,
         ohat__level=1, ohat__pan=-0.12, volume=0)
    line(f"set {hpt}.length=4")
    notes_add(hpt, [(OHAT, b + 0.5, 0.25, 110) for b in range(4)]
              + [(CHAT, b + s, 0.25, 72 if s == 0.25 else 58) for b in range(4) for s in (0.25, 0.75)])
    clip(hat, hpt, DROP2, 64)
    light = pattern("Hats, light", 4)
    notes_add(light, [(OHAT, b + 0.5, 0.25, 84) for b in range(4)])
    for at, ln in [(INTRO + 16, 16), (VERSE, 32), (OUTRO, 16)]:
        clip(hat, light, at, ln)
    line()

    # ── cymbals, the impact, the roll ──
    line("# a crash on each drop's downbeat and halfway; an impact — noise sweeping down — on each drop")
    cym, cdv, cpt = instrument("drums", "Crash", DROP1, 4)
    setp(cdv, crash__level=0, crash__decay=2600, crash__tone=0.75, volume=-2)
    line(f"set {cpt}.length=4")
    notes_add(cpt, [(CRASH, 0, 0.25, 120)])
    for at in [DROP1 + 32, DROP2, DROP2 + 32, OUTRO]:
        clip(cym, cpt, at, 4)
    imp, idv, ipt = instrument("synth", "Impact", DROP1, 8)
    setp(idv, osc1__level=0, osc2__level=0, noise=1, filter__cutoff=300, filter__res=0.1, filter__env=5.2, filter__keytrack=0,
         fenv__attack=0, fenv__decay=1000, fenv__sustain=0, amp__attack=0, amp__decay=1600, amp__sustain=0, amp__release=800,
         volume=-8)
    line(f"set {ipt}.length=8")
    notes_add(ipt, [(m('G2'), 0, 4, 127)])
    clip(imp, ipt, DROP2, 8)
    rl, rdv, rpt = instrument("drums", "Roll", BUILD1, 32)
    line("# the build's snare roll: quarters, eighths, sixteenths, thirty-seconds — then the gap")
    setp(rdv, snare__tune=6, snare__decay=150, snare__tone=0.7, volume=-6)
    line(f"set {rpt}.length=32")
    notes_add(rpt, roll(SNARE, 0, 8, 1.0, 52, 62) + roll(SNARE, 8, 16, 0.5, 62, 76) + roll(SNARE, 16, 24, 0.25, 76, 94)
              + roll(SNARE, 24, 32 - GAP, 0.125, 94, 124))
    clip(rl, rpt, BUILD2, 32)
    line()

    # ── the bass ──
    line("# the bass: an FM bass (REQ-fm) — a sine and its octave-down sine, each struck bright by a fast modulator — on")
    line("# the off-beats between the kicks, jumping octaves: the bounce")
    bass, bdv, bpt = instrument("fm", "Bass", DROP1, 64)
    setp(bdv, algorithm=5, feedback=0.12, op1__ratio=1, op1__level=1, op1__attack=1, op1__decay=240, op1__sustain=0.5,
         op1__release=50, op2__ratio=2, op2__level=0.5, op2__attack=0, op2__decay=110, op2__sustain=0, op2__release=40,
         op3__ratio=0.5, op3__level=0.8, op3__attack=1, op3__decay=300, op3__sustain=0.6, op3__release=50, op4__ratio=1,
         op4__level=0.3, op4__attack=0, op4__decay=70, op4__sustain=0, op4__release=40, vibrato__depth=0, volume=-6)
    line(f"set {bpt}.length=16")
    notes_add(bpt, [(m(ROOTS[c]) + o, i * BAR + a, l, 112) for i, c in enumerate(PROG) for a, l, o in BOUNCE])
    b2 = pattern("Bass, second drop", 16)
    notes_add(b2, [(m(ROOTS[c]) + o, i * BAR + a, l, 112) for i, c in enumerate(PROG) for a, l, o in BOUNCE2])
    clip(bass, b2, DROP2, 64)
    bv = pattern("Bass, the verse", 16)
    notes_add(bv, [(m(ROOTS[c]), i * BAR + a, 0.25, 96) for i, c in enumerate(PROG) for a in (0.5, 1.5, 2.5, 3.5)])
    clip(bass, bv, VERSE, 32)
    steps(f"{bdv}.volume", [(0, -6), (VERSE, -12), (DROP1, -6)])
    line()

    # ── the chords and the pad, pumping on the shaper ──
    line("# the chords: two five-voice saws, wide, a bar each — through the verse, the builds, the drops; the pad under the")
    line("# intro and the breakdown. Both PUMP on the shaper's pump shape a beat long, on the song's grid (R-FX-18): the")
    line("# sidechain sound, with no compressor and no key")
    chd, chdv, chpt = instrument("synth", "Chords", VERSE, 32)
    setp(chdv, osc1__wave="saw", osc1__voices=5, osc1__detune=30, osc1__width=30, osc1__level=0.7, osc2__wave="saw",
         osc2__octave=0, osc2__voices=5, osc2__detune=20, osc2__width=22, osc2__fine=7, osc2__level=0.5,
         filter__cutoff=4800, filter__res=0.12, filter__env=0.6, filter__keytrack=0.3, fenv__decay=500, fenv__sustain=0.6,
         amp__attack=4, amp__decay=400, amp__sustain=0.9, amp__release=140, volume=-10)
    line(f"set {chpt}.length=16")
    notes_add(chpt, [(m(n), i * BAR, BAR, 96) for i, c in enumerate(PROG) for n in CHORD[c]])
    cb = pattern("Chords, the build", 32)
    notes_add(cb, [(m(n), i * BAR, BAR - (GAP if i == 7 else 0), 96) for i, c in enumerate(PROG * 2) for n in CHORD[c]])
    for at, pt, ln in [(BUILD1, cb, 32), (DROP1, chpt, 64), (BUILD2, cb, 32), (DROP2, chpt, 64)]:
        clip(chd, pt, at, ln)
    device(chd, "shaper", rate='"1/4"', shape='"pump"', depth=PUMP, smooth=SMOOTH)
    automate(f"{chdv}.filter.cutoff", [(0, 3000, 'hold'), (BUILD1, 700), (DROP1 - GAP, 7000, 'smooth'), (DROP1, 4800, 'hold'),
                                        (BUILD2, 700), (DROP2 - GAP, 7000, 'smooth'), (DROP2, 4800, 'hold')])
    automate(f"{chdv}.volume", [(0, -22, 'hold'), (BUILD1, -26), (DROP1 - GAP, -13, 'hold'), (DROP1, -10, 'hold'),
                                 (BUILD2, -26), (DROP2 - GAP, -13, 'hold'), (DROP2, -10, 'hold')])
    pad, pdv, ppt = instrument("synth", "Pad", INTRO, 32)
    setp(pdv, osc1__wave="saw", osc1__voices=5, osc1__detune=18, osc1__width=34, osc1__level=0.55, osc2__wave="triangle",
         osc2__octave=1, osc2__level=0.3, filter__cutoff=2200, filter__res=0.12, filter__env=0.2, filter__keytrack=0.25,
         amp__attack=600, amp__decay=1200, amp__sustain=0.85, amp__release=900, volume=-14)
    line(f"set {ppt}.length=16")
    notes_add(ppt, [(m(n), i * BAR, BAR, 86) for i, c in enumerate(PROG) for n in PAD[c]])
    for at, ln in [(BREAK, 32), (OUTRO, 32)]:
        clip(pad, ppt, at, ln)
    device(pad, "shaper", rate='"1/4"', shape='"pump"', depth=PUMP, smooth=SMOOTH)
    line()

    # ── the hook ──
    line("# the hook on the lead: a five-voice saw and its octave, bright, one voice at a time — the drops")
    lead, ldv, lpt = instrument("synth", "Lead", DROP1, 64)
    setp(ldv, osc1__wave="saw", osc1__voices=5, osc1__detune=22, osc1__width=20, osc1__level=0.75, osc2__wave="saw",
         osc2__octave=1, osc2__voices=1, osc2__level=0.3, filter__cutoff=5200, filter__res=0.15, filter__env=1.2,
         filter__keytrack=0.3, fenv__decay=220, fenv__sustain=0.55, amp__attack=2, amp__decay=200, amp__sustain=0.8,
         amp__release=90, voice__mode="legato", voice__glide=0, vibrato__depth=0, volume=-8)
    line(f"set {lpt}.length=32")
    notes_add(lpt, [(m(n), a, l, 112 if a % 1 == 0 else 100) for a, l, n in HOOK])
    clip(lead, lpt, DROP2, 64)
    line("# the pluck: the hook teased — the intro's second half, the breakdown, the second build opening; an octave over")
    line("# the lead in the second drop")
    plk, pldv, plpt = instrument("wavetable", "Pluck", INTRO + 16, 16)
    setp(pldv, osc1__table="harmonics", osc1__position=0.55, osc1__posenv=-0.3, osc1__voices=1, osc1__level=0.8,
         osc2__level=0, filter__cutoff=3200, filter__res=0.2, filter__env=2.0, fenv__decay=230, fenv__sustain=0,
         amp__attack=1, amp__decay=360, amp__sustain=0, amp__release=200, volume=-10)
    line(f"set {plpt}.length=32")
    notes_add(plpt, [(m(n), a, l, 100) for a, l, n in HOOK])
    for at, ln in [(BREAK, 32), (BUILD2, 32 - GAP), (DROP2, 64)]:
        clip(plk, plpt, at, ln)
    steps(f"{pldv}.osc1.octave", [(0, 0), (DROP2, 1), (OUTRO, 0)])
    automate(f"{pldv}.filter.cutoff", [(0, 600), (INTRO + 31, 3200, 'hold'), (BUILD2, 1200), (DROP2 - GAP, 6000, 'hold'),
                                        (DROP2, 3200, 'hold')])
    line()

    # ── the piano ──
    line("# the piano (R-INST-5): the chords, the verse and the breakdown")
    pno, pnodv, pnopt = instrument("piano", "Piano", VERSE, 32)
    setp(pnodv, strings__brightness=1.1, strings__decay=1.2, body__mix=0.5, hammer__hardness=1.0, volume=-8)
    line(f"set {pnopt}.length=16")
    pn = []
    for i, c in enumerate(PROG):
        pn += [(m(n), i * BAR, BAR, 72) for n in PIANO_L[c]]
        pn += [(m(n), i * BAR + a, ln, v) for a, ln, v in [(0, 1.5, 80), (1.5, 1.0, 68), (2.5, 1.5, 72)] for n in PIANO_R[c]]
    notes_add(pnopt, pn)
    clip(pno, pnopt, BREAK, 32)
    steps(f"{pnodv}.volume", [(0, -12), (BREAK, -8)])
    line()

    # ── the riser ──
    line("# the riser: noise and a saw, its filter opening and its pitch climbing an octave through each build — cut for the gap")
    rise, rsdv, rspt = instrument("synth", "Riser", BUILD1, 32)
    setp(rsdv, osc1__wave="saw", osc1__voices=3, osc1__detune=40, osc1__width=30, osc1__level=0.3, osc2__level=0, noise=0.85,
         filter__res=0.35, filter__env=0, filter__keytrack=0, amp__attack=1500, amp__sustain=1, amp__release=40,
         volume=-15)
    line(f"set {rspt}.length=32")
    notes_add(rspt, [(m('G3'), 0, 32 - GAP, 100)])
    clip(rise, rspt, BUILD2, 32)
    automate(f"{rsdv}.filter.cutoff", [(BUILD1, 200), (DROP1 - GAP, 10000, 'smooth'), (DROP1, 200, 'hold'),
                                        (BUILD2, 200), (DROP2 - GAP, 10000, 'smooth'), (DROP2, 200, 'hold')])
    automate(f"{rsdv}.osc1.semi", [(BUILD1, -12), (DROP1 - GAP, 12), (DROP1, -12, 'hold'),
                                    (BUILD2, -12), (DROP2 - GAP, 12), (DROP2, -12, 'hold')])
    automate(f"{rsdv}.volume", [(BUILD1, -40), (DROP1 - GAP, -12, 'smooth'), (DROP1, -40, 'hold'),
                                 (BUILD2, -40), (DROP2 - GAP, -12, 'smooth'), (DROP2, -40, 'hold')])
    line()

    # ── tone ──
    line("# tone: the bass alone under 120 Hz but for the kick; the top bright — a shelf on the lead and the hats")
    device(bass, "eq", lowcut__on="on", lowcut__freq=30)
    for ch, hz in [(chd, 200), (pad, 150), (lead, 250), (plk, 300), (rise, 180), (hat, 500), (clp, 180), (cym, 400),
                   (imp, 60), (pno, 80), (rl, 200)]:
        device(ch, "eq", lowcut__on="on", lowcut__freq=hz)
    line()

    # ── space ──
    line("# space: a reverb and a dotted-eighth delay, each on a bus")
    verb = ids.new('ch')
    line('strip add --kind bus --name Reverb')
    device(verb, "reverb", decay=2400, size=140, mix=1, lowcut=350, highcut=12000, width=1)
    dly = ids.new('ch')
    line('strip add --kind bus --name Delay')
    device(dly, "delay", time='"=60000/bpm*0.75"', feedback=0.32, tone=5000, mix=1)
    for ch, to, g in [(lead, verb, -14), (lead, dly, -16), (chd, verb, -16), (plk, verb, -8), (plk, dly, -10),
                      (pad, verb, -8), (pno, verb, -8), (clp, verb, -16), (cym, verb, -12), (imp, verb, -6), (rise, verb, -12)]:
        line(f"send add {ch} --to {to} --gain {g}")
    line()

    # ── the mix and the master ──
    line("# the mix: faders; the master: a limiter, its ceiling under −1 dBTP")
    gains = {kick: -3, bass: 1, clp: 1, hat: -4, cym: -9, imp: -6, rl: -5, chd: -4.5, pad: -5, lead: -2, plk: -6,
             pno: -4, rise: -5, verb: -6, dly: -10}
    line("set " + " ".join(f"{ch}.gain={g}" for ch, g in gains.items()))
    device("master", "limiter", gain=11, ceiling=-1.3, release=30, truePeak="on")
    line()
    line("ls")
    line("audit")
    line("project save")
    line(f"render --out starward.wav --stems {kick},{bass},{clp},{chd},{lead}")

    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    return {"kick": kick, "bass": bass, "clap": clp, "chords": chd, "lead": lead}


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "song.txt")
    stems = main(out)
    print(f"{len(L)} lines → {out}; stems {stems}")
