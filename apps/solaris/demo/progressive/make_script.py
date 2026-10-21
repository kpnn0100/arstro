#!/usr/bin/env python3
"""Afterglow — an original progressive house track (Solaris R-SVC-14), written as a solaris-cc script.

Nothing here is anyone else's song: the progression (IV–I–V–vi in D major, two bars a chord), the melody, the bass
line, the drums and the form were written for this track. 126 bpm, 104 bars (416 beats, 3:18):

    intro 1–16 · verse 17–32 · breakdown 33–40 (the piano) · build 41–48 · drop 49–64 · break 65–72 ·
    build 73–80 · drop 81–96 (a whole tone up: E major) · outro 97–104

The melody is written to be sung: sixteen bars in two phrases, mostly steps, its climax in the second half of the
phrase, landing on the tonic. It is heard first on a bare piano in the breakdown, rises filtered through the build
on the lead, carries the drop on a wide lead — and comes back in the last drop a whole tone higher, with an octave.

Every sound is the DSP library's: a kick on every beat, claps, sixteenth hats and open off-beats; a rolling sixteenth
bass and a sine sub under the pump; a sixteenth pluck arpeggio through a dotted-eighth delay, opening over the intro;
a warm pad; wide supersaw chords; a legato lead with a touch of vibrato; the physical piano (R-INST-5) — its chords
and, on a track of its own, the melody; a riser and a snare roll; a crash, a ride. A reverb, a delay, a limiter.

    python3 make_script.py [out.txt]      # writes song.txt (or out.txt)
"""
import os, sys

BPM = 126
BAR = 4
INTRO, VERSE, BREAK1, BUILD1, DROP1, BREAK2, BUILD2, DROP2, OUTRO, END = 0, 64, 128, 160, 192, 256, 288, 320, 384, 416
LIFT = 2            # the last drop, a whole tone up

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
KICK, SNARE, CLAP, CHAT, OHAT, CRASH, RIDE = 36, 38, 39, 42, 46, 49, 51


def m(n):
    """'D4' / 'F#5' / 'Bb3' → MIDI (C4 = 60)."""
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


def notes_add(pt, notes):
    notes = sorted(notes, key=lambda n: (n[1], n[0]))
    for i in range(0, len(notes), 48):
        line(f'notes add {pt} "' + " ".join(f"{p}@{num(a)}:{num(l)}:{v}" for p, a, l, v in notes[i:i + 48]) + '"')


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
    n = int(round((b - a) / div))
    return [(pitch, a + k * div, min(div, 0.25), int(round(v0 + (v1 - v0) * k / max(1, n - 1)))) for k in range(n)]


def up(notes, semis):
    return [(p + semis, a, l, v) for p, a, l, v in notes]


# ── the harmony: IV–I–V–vi in D major, two bars a chord, eight bars a turn ──────────────────────────────────────
PROG = ['G', 'D', 'A', 'Bm']
CHORD = {'G': ['G3', 'B3', 'D4', 'G4'], 'D': ['F#3', 'A3', 'D4', 'F#4'], 'A': ['E3', 'A3', 'C#4', 'E4'],
         'Bm': ['F#3', 'B3', 'D4', 'F#4']}
TONES = {'G': ['G3', 'B3', 'D4', 'G4', 'B4'], 'D': ['D3', 'F#3', 'A3', 'D4', 'F#4'], 'A': ['A3', 'C#4', 'E4', 'A4', 'C#5'],
         'Bm': ['B3', 'D4', 'F#4', 'B4', 'D5']}
PAD = {'G': ['G2', 'D3', 'B3'], 'D': ['D2', 'A2', 'F#3'], 'A': ['A1', 'E2', 'C#3'], 'Bm': ['B1', 'F#2', 'D3']}
BASS = {'G': 'G2', 'D': 'D2', 'A': 'A2', 'Bm': 'B2'}
SUB = {'G': 'G1', 'D': 'D1', 'A': 'A1', 'Bm': 'B1'}

# the melody: sixteen bars, two phrases — the second answers the first and lands on the tonic (beat, length, note)
PHRASE = [
    (0.0, 1.5, 'B4'), (1.5, 0.5, 'A4'), (2.0, 1.0, 'B4'), (3.0, 1.0, 'D5'),        # G
    (4.0, 1.5, 'E5'), (5.5, 0.5, 'D5'), (6.0, 2.0, 'B4'),
    (8.0, 1.5, 'A4'), (9.5, 0.5, 'F#4'), (10.0, 1.0, 'A4'), (11.0, 1.0, 'D5'),     # D
    (12.0, 2.0, 'F#5'), (14.0, 1.0, 'E5'), (15.0, 1.0, 'D5'),
    (16.0, 1.5, 'C#5'), (17.5, 0.5, 'B4'), (18.0, 1.0, 'C#5'), (19.0, 1.0, 'E5'),  # A
    (20.0, 1.5, 'A5'), (21.5, 0.5, 'F#5'), (22.0, 2.0, 'E5'),                      # the climax
]
ASK = [(24.0, 1.5, 'D5'), (25.5, 0.5, 'C#5'), (26.0, 1.0, 'B4'), (27.0, 1.0, 'F#5'),  # Bm — it asks…
       (28.0, 2.0, 'E5'), (30.0, 1.0, 'D5'), (31.0, 1.0, 'C#5')]
ANSWER = [(24.0, 1.5, 'F#5'), (25.5, 0.5, 'E5'), (26.0, 1.0, 'D5'), (27.0, 1.0, 'E5'),  # … and it answers, home
          (28.0, 4.0, 'D5')]
MELODY = PHRASE + ASK + [(a + 32, l, n) for a, l, n in PHRASE + ANSWER]   # 64 beats


def mel(notes, vel=100):
    return [(m(n), a, l, vel) for a, l, n in notes]


def main(out):
    line("# Afterglow — an original progressive house track, written by make_script.py (R-SVC-14). 126 bpm, D major, 104 bars.")
    line('project new afterglow.slp --bpm 126 --name "Afterglow"')
    line()

    # ── the kick ──
    line("# the kick: every beat, but for the breakdown; the builds climb back")
    kick, kdv, kpt = instrument("drums", "Kick", INTRO, BREAK1)
    setp(kdv, kick__tune=0, kick__decay=380, kick__tone=0.5, kick__level=0, volume=0)
    line(f"set {kpt}.length=4")
    notes_add(kpt, [(KICK, b, 0.25, 118) for b in range(4)])
    for at, ln in [(DROP1, 64), (DROP2, 64), (OUTRO, 32)]:
        clip(kick, kpt, at, ln)
    climb = pattern("Kick, the build", 32)
    notes_add(climb, [(KICK, b, 0.25, 108) for b in range(0, 16, 4)] + [(KICK, b, 0.25, 114) for b in range(16, 31)])
    for b in (BUILD1, BUILD2):
        clip(kick, climb, b, 32)
    line()

    # ── the groove ──
    line("# the groove: claps on two and four; sixteenth hats; open hats on the off-beats")
    grv, gdv, gpt = instrument("drums", "Groove", INTRO + 32, VERSE - INTRO - 32)
    setp(gdv, clap__level=-4, clap__decay=280, clap__tone=0.55, chat__level=-6, chat__tone=0.75, chat__decay=45, chat__pan=0.2,
         ohat__level=-6, ohat__decay=260, ohat__pan=-0.2, volume=-2)
    line(f"set {gpt}.length=4")
    notes_add(gpt, [(CLAP, 1, 0.25, 110), (CLAP, 3, 0.25, 110)]
              + [(CHAT, k * 0.25, 0.25, 70 if k % 2 else 52) for k in range(16) if k % 4 != 2]
              + [(OHAT, b + 0.5, 0.25, 84) for b in range(4)])
    for at, ln in [(DROP1, 64), (DROP2, 64), (OUTRO, 24)]:
        clip(grv, gpt, at, ln)
    verse = pattern("Groove, the verse", 4)     # the same steps, held back — the drop is where it opens up
    notes_add(verse, [(CLAP, 1, 0.25, 86), (CLAP, 3, 0.25, 86)]
              + [(CHAT, k * 0.25, 0.25, 54 if k % 2 else 40) for k in range(16) if k % 4 != 2]
              + [(OHAT, b + 0.5, 0.25, 64) for b in range(4)])
    clip(grv, verse, VERSE, 64)
    shaker = pattern("Hats, light", 4)
    notes_add(shaker, [(CHAT, k * 0.25, 0.25, 46 if k % 2 else 34) for k in range(16)])
    clip(grv, shaker, INTRO + 16, 16)
    line()

    # ── cymbals ──
    line("# cymbals: a crash every eight bars of the drops; a ride through the last")
    cym, cdv, cpt = instrument("drums", "Cymbals", DROP1, 4)
    setp(cdv, crash__level=-2, crash__decay=2400, ride__level=-10, ride__pan=0.3, volume=-3)
    line(f"set {cpt}.length=4")
    notes_add(cpt, [(CRASH, 0, 0.25, 116)])
    for at in [VERSE, BREAK1, DROP1 + 32, DROP2, DROP2 + 32, OUTRO]:
        clip(cym, cpt, at, 4)
    ride = pattern("Ride", 4)
    notes_add(ride, [(RIDE, b, 0.25, 72) for b in range(4)] + [(RIDE, b + 0.5, 0.25, 52) for b in range(4)])
    clip(cym, ride, DROP2 + 4, 60)
    line()

    # ── the roll ──
    line("# the builds' snare roll: quarters to thirty-seconds over the last eight bars, then a half-beat breath")
    rl, rdv, rpt = instrument("drums", "Roll", BUILD1, 32)
    setp(rdv, snare__tune=3, snare__decay=170, snare__tone=0.6, volume=-8)
    line(f"set {rpt}.length=32")
    notes_add(rpt, roll(SNARE, 0, 8, 1.0, 50, 60) + roll(SNARE, 8, 16, 0.5, 60, 72) + roll(SNARE, 16, 24, 0.25, 72, 88)
              + roll(SNARE, 24, 31.5, 0.125, 88, 116))
    clip(rl, rpt, BUILD2, 32)
    line()

    # ── the rolling bass and the sub ──
    line("# the bass: sixteenths around the kick (off, on, on, on) on each chord's root; a sine sub under it")
    bass, bdv, bpt = instrument("synth", "Bass", INTRO + 32, 96)
    setp(bdv, osc1__wave="saw", osc2__wave="square", osc2__octave=-1, osc2__level=0.4, filter__cutoff=520, filter__res=0.3,
         filter__env=2.0, fenv__decay=120, fenv__sustain=0.12, amp__attack=1, amp__decay=150, amp__sustain=0.45,
         amp__release=40, volume=-10)
    line(f"set {bpt}.length=32")
    bl = []
    for i, c in enumerate(PROG):
        r = m(BASS[c])
        for beat in range(8):
            for k in (1, 2, 3):
                bl.append((r, i * 8 + beat + 0.25 * k, 0.2, 96 if k == 2 else 84))
    notes_add(bpt, bl)
    bpt2 = pattern("Bass, lifted", 32)
    notes_add(bpt2, up(bl, LIFT))
    for at, ln in [(DROP1, 64)]:
        clip(bass, bpt, at, ln)
    for at, ln in [(DROP2, 64), (OUTRO, 32)]:
        clip(bass, bpt2, at, ln)
    automate(f"{bdv}.filter.cutoff", [(0, 300), (INTRO + 32, 300), (VERSE, 520, 'smooth'), (VERSE + 1, 520), (BREAK1, 520),
                                       (DROP1, 900), (BREAK2, 900), (DROP2, 1000), (OUTRO, 1000), (END, 260, 'smooth')])
    sub, sdv, spt = instrument("synth", "Sub", DROP1, 64)
    setp(sdv, osc1__wave="sine", osc2__level=0, filter__cutoff=240, filter__res=0, filter__env=0, filter__keytrack=0,
         amp__attack=4, amp__decay=80, amp__sustain=1, amp__release=90, volume=-6)
    line(f"set {spt}.length=32")
    sl = [(m(SUB[c]), i * 8, 8, 108) for i, c in enumerate(PROG)]
    notes_add(spt, sl)
    spt2 = pattern("Sub, lifted", 32)
    notes_add(spt2, up(sl, LIFT))
    clip(sub, spt2, DROP2, 64)
    line()

    # ── the pluck ──
    line("# the pluck: a sixteenth arpeggio on the chord's tones, through a dotted-eighth delay — opening over the intro")
    plk, pldv, plpt = instrument("synth", "Pluck", INTRO, 128)
    setp(pldv, osc1__wave="saw", osc1__voices=2, osc1__detune=10, osc1__width=20, osc2__wave="square", osc2__octave=1,
         osc2__level=0.2, filter__res=0.3, filter__env=3.0, fenv__decay=170, fenv__sustain=0, amp__attack=1,
         amp__decay=230, amp__sustain=0, amp__release=150, volume=-15)
    line(f"set {plpt}.length=32")
    arp = []
    order = [0, 1, 2, 3, 2, 1, 2, 3, 0, 1, 2, 3, 4, 3, 2, 1]
    for i, c in enumerate(PROG):
        t = [m(n) for n in TONES[c]]
        for bar in range(2):
            for k, o in enumerate(order):
                arp.append((t[o], i * 8 + bar * 4 + 0.25 * k, 0.25, 88 if k % 4 == 0 else 70))
    notes_add(plpt, arp)
    plpt2 = pattern("Pluck, lifted", 32)
    notes_add(plpt2, up(arp, LIFT))
    for at, ln in [(BUILD1 + 16, 16), (DROP1, 64), (BREAK2, 32), (BUILD2 + 16, 16)]: # each build starts lean
        clip(plk, plpt, at, ln)
    for at, ln in [(DROP2, 64), (OUTRO, 32)]:
        clip(plk, plpt2, at, ln)
    automate(f"{pldv}.filter.cutoff", [(0, 260), (VERSE, 1500, 'smooth'), (BREAK1, 1800), (BUILD1, 900), (DROP1, 2600, 'smooth'),
                                        (BREAK2, 2600), (BREAK2 + 1, 1200), (DROP2, 2800, 'smooth'), (OUTRO, 2800),
                                        (END, 300, 'smooth')])
    line()

    # ── the pad ──
    line("# the pad: warm and wide, slow in and out — the breakdown's bed")
    pad, pdv, ppt = instrument("synth", "Pad", INTRO + 32, 96)
    setp(pdv, osc1__wave="saw", osc1__voices=5, osc1__detune=24, osc1__width=32, osc1__level=0.6, osc2__wave="triangle",
         osc2__octave=1, osc2__level=0.3, filter__cutoff=1400, filter__res=0.15, filter__env=0.3, filter__keytrack=0.3,
         amp__attack=900, amp__decay=1200, amp__sustain=0.85, amp__release=1800, volume=-16)
    line(f"set {ppt}.length=32")
    pl = [(m(n), i * 8, 8, 86) for i, c in enumerate(PROG) for n in PAD[c]]
    notes_add(ppt, pl)
    ppt2 = pattern("Pad, lifted", 32)
    notes_add(ppt2, up(pl, LIFT))
    clip(pad, ppt, BREAK1, DROP2 - BREAK1)          # the breakdown's bed, on through the drop, the break and the build
    for at, ln in [(DROP2, 96)]:
        clip(pad, ppt2, at, ln)
    line()

    # ── the piano: chords, and — on its own track — the melody ──
    line("# the piano (R-INST-5): chords in the left hand, the melody in the right on a track of its own — the breakdown")
    pno, pnodv, pnopt = instrument("piano", "Piano", BREAK1, 32)
    setp(pnodv, volume=-6, strings__decay=1.4, hammer__hardness=0.8, body__mix=0.6)
    line(f"set {pnopt}.length=32")
    lh = []
    for i, c in enumerate(PROG):
        lo = [m(n) for n in PAD[c]]
        for bar in range(2):
            b0 = i * 8 + bar * 4
            lh += [(lo[0], b0, 2.0, 74), (lo[1] + 12, b0 + 1, 1.0, 62), (lo[2], b0 + 2, 2.0, 66), (lo[1] + 12, b0 + 3, 1.0, 58)]
    notes_add(pnopt, lh)
    clip(pno, pnopt, BREAK2, 32)
    pmel, pmdv, pmpt = instrument("piano", "Piano, melody", BREAK1, 32)
    setp(pmdv, volume=-4, strings__decay=1.6, hammer__hardness=1.0, body__mix=0.55)
    line(f"set {pmpt}.length=32")
    notes_add(pmpt, mel(PHRASE + ASK, 92))
    clip(pmel, pmpt, BREAK2, 32)
    line()

    # ── the lead: the melody ──
    line("# the lead: the melody, legato, a wide five-voice saw and a saw an octave up, a touch of vibrato — rising filtered")
    line("# through the builds (the second phrase), whole in the drops; a whole tone up in the last, an octave over it")
    lead, ldv, lpt = instrument("synth", "Lead", DROP1, 64)
    setp(ldv, osc1__wave="saw", osc1__voices=5, osc1__detune=26, osc1__width=28, osc1__level=0.75, osc2__wave="saw",
         osc2__octave=1, osc2__level=0.28, filter__cutoff=4200, filter__res=0.15, filter__env=1.4, fenv__decay=420,
         fenv__sustain=0.55, amp__attack=6, amp__decay=320, amp__sustain=0.85, amp__release=320, voice__mode="legato",
         voice__glide=45, vibrato__depth=0.05, vibrato__rate=5.2, volume=-10.5)
    line(f"set {lpt}.length=64")
    notes_add(lpt, mel(MELODY, 104))
    lpt2 = pattern("Lead, lifted", 64)
    notes_add(lpt2, up(mel(MELODY, 104), LIFT))
    clip(lead, lpt2, DROP2, 64)
    rising = pattern("Lead, the build", 32)
    notes_add(rising, mel(PHRASE + ANSWER, 96))
    for at in (BUILD1, BUILD2):
        clip(lead, rising, at, 32)
    automate(f"{ldv}.filter.cutoff", [(0, 4200), (BUILD1, 300), (DROP1 - 0.5, 4200, 'smooth'), (BREAK2, 4200), (BUILD2, 300),
                                       (DROP2 - 0.5, 4200, 'smooth')])
    automate(f"{ldv}.volume", [(0, -10.5), (BUILD1, -30), (DROP1 - 0.5, -10.5, 'smooth'), (BREAK2, -10.5), (BUILD2, -30),
                                (DROP2 - 0.5, -10.5, 'smooth')])   # … and swelling as it opens
    high, hdv, hpt = instrument("synth", "Lead, octave", DROP2, 64)
    setp(hdv, osc1__wave="saw", osc1__voices=3, osc1__detune=18, osc1__width=34, osc1__level=0.7, osc2__level=0,
         filter__cutoff=5200, filter__res=0.1, amp__attack=8, amp__decay=300, amp__sustain=0.8, amp__release=320,
         voice__mode="legato", voice__glide=45, volume=-22)
    line(f"set {hpt}.length=64")
    notes_add(hpt, up(mel(MELODY, 96), LIFT + 12))
    line()

    # ── the chords ──
    line("# the chords: two five-voice saws, wide, held two bars each under the drops, pumping")
    sup, sudv, supt = instrument("synth", "Supersaw", DROP1, 64)
    setp(sudv, osc1__wave="saw", osc1__voices=5, osc1__detune=36, osc1__width=26, osc1__level=0.7, osc2__wave="saw",
         osc2__voices=5, osc2__detune=20, osc2__width=18, osc2__fine=6, osc2__level=0.5, filter__cutoff=3600,
         filter__res=0.1, filter__env=0.6, fenv__decay=900, fenv__sustain=0.6, amp__attack=10, amp__decay=500,
         amp__sustain=0.88, amp__release=420, volume=-16)
    line(f"set {supt}.length=32")
    cl = [(m(n), i * 8, 8, 90) for i, c in enumerate(PROG) for n in CHORD[c]]
    notes_add(supt, cl)
    supt2 = pattern("Supersaw, lifted", 32)
    notes_add(supt2, up(cl, LIFT))
    clip(sup, supt2, DROP2, 64)
    line()

    # ── the riser ──
    line("# the riser: noise and a saw, its filter opening and its pitch climbing an octave through each build")
    rise, rsdv, rspt = instrument("synth", "Riser", BUILD1, 32)
    setp(rsdv, osc1__wave="saw", osc1__voices=3, osc1__detune=40, osc1__width=30, osc1__level=0.3, osc2__level=0, noise=0.8,
         filter__res=0.35, filter__env=0, filter__keytrack=0, amp__attack=1500, amp__sustain=1, amp__release=300, volume=-15)
    line(f"set {rspt}.length=32")
    notes_add(rspt, [(m('D3'), 0, 31.5, 100)])
    clip(rise, rspt, BUILD2, 32)
    automate(f"{rsdv}.filter.cutoff", [(BUILD1, 200), (DROP1 - 0.5, 8500, 'smooth'), (DROP1, 200, 'hold'),
                                        (BUILD2, 200), (DROP2 - 0.5, 8500, 'smooth'), (DROP2, 200, 'hold')])
    automate(f"{rsdv}.osc1.semi", [(BUILD1, -12), (DROP1 - 0.5, 12), (DROP1, -12, 'hold'),
                                    (BUILD2, -12), (DROP2 - 0.5, 12), (DROP2, -12, 'hold')])
    automate(f"{rsdv}.volume", [(BUILD1, -40), (DROP1 - 0.5, -13, 'smooth'), (DROP1, -40, 'hold'),
                                 (BUILD2, -40), (DROP2 - 0.5, -13, 'smooth'), (DROP2, -40, 'hold')])
    line()

    # ── the pump ──
    line("# the pump: the kick keys a compressor on the bass, the sub, the chords, the pad, the pluck and the leads")
    for ch, thr, ratio, rel in [(bass, -24, 5, 110), (sub, -28, 8, 120), (sup, -24, 5, 140), (pad, -26, 4, 170),
                                (plk, -22, 2.5, 110), (lead, -20, 2, 120), (high, -20, 2, 120)]:
        device(ch, "compressor", sidechain="on", threshold=thr, ratio=ratio, attack=1, release=rel)
        line(f"send add {kick} --to {ch} --sidechain")
    line()

    # ── tone ──
    line("# tone: the lows belong to the kick, the bass and the sub")
    for ch, hz in [(sup, 180), (lead, 220), (high, 400), (pad, 140), (plk, 200), (pno, 60), (pmel, 150), (rise, 180),
                   (grv, 280), (cym, 400)]:
        device(ch, "eq", lowcut__on="on", lowcut__freq=hz)
    line()

    # ── space ──
    line("# space: a long, wide reverb and a dotted-eighth delay, each on a bus")
    verb = ids.new('ch')
    line('strip add --kind bus --name Reverb')
    device(verb, "reverb", decay=3600, size=170, mix=1, lowcut=280, highcut=11000, width=1)
    dly = ids.new('ch')
    line('strip add --kind bus --name Delay')
    device(dly, "delay", time='"=60000/bpm*0.75"', feedback=0.4, tone=4200, mix=1)
    for ch, to, g in [(lead, verb, -9), (lead, dly, -13), (high, verb, -8), (plk, dly, -8), (plk, verb, -12),
                      (sup, verb, -12), (pad, verb, -6), (pno, verb, -6), (pmel, verb, -5), (pmel, dly, -14),
                      (rise, verb, -5), (grv, verb, -16), (cym, verb, -14)]:
        line(f"send add {ch} --to {to} --gain {g}")
    line()

    # ── the mix and the master ──
    line("# the mix: faders; the master: a true-peak limiter")
    gains = {kick: -2, grv: -5, cym: -8, rl: -2, bass: -6, sub: -6, plk: -6, pad: -6, pno: -3, pmel: -2, lead: -3, high: -6,
             sup: -4, rise: -6, verb: -6, dly: -10}
    line("set " + " ".join(f"{ch}.gain={g}" for ch, g in gains.items()))
    device("master", "limiter", gain=10, ceiling=-1.3, release=80, truePeak="on")
    line()
    line("ls")
    line("audit")
    line("project save")
    line(f"render --out afterglow.wav --stems {kick},{bass},{pmel},{lead},{sup}")

    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    return {"kick": kick, "bass": bass, "piano melody": pmel, "lead": lead, "supersaw": sup}


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "song.txt")
    stems = main(out)
    print(f"{len(L)} lines → {out}; stems {stems}")
