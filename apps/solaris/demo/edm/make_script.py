#!/usr/bin/env python3
"""Perihelion — an original EDM track (Solaris R-SVC-12), written as a solaris-cc script.

Nothing here is anyone else's song: the progression (i–VI–III–VII in A minor, a common harmonic frame), the
hook, the bass lines, the drums and the form were written for this track. 126 bpm, 72 bars (288 beats, 2:17):

    intro 1–8 · build 9–16 · drop 17–32 · breakdown 33–40 · build 41–48 · drop 49–64 · outro 65–72

Every sound is the DSP library's: a kick tuned to the key; claps, house hats, a crash and a ride (REQ-drum-6);
a sine sub and a saw mid bass sidechained to the kick; WIDE supersaw chords (REQ-synth2-6) pumping; a lead with
a dotted-eighth delay; a pad opening through the intro; the physical piano in the breakdown; a noise-and-saw
riser climbing a fifth-octave in pitch and twelve in filter through each build. A reverb and a delay bus; a
limiter on the master.

    python3 make_script.py [out.txt]      # writes song.txt (or out.txt): ~200 solaris-cc lines
"""
import os, sys

BPM = 126
BAR = 4
INTRO, BUILD1, DROP1, BREAK, BUILD2, DROP2, OUTRO, END = 0, 32, 64, 128, 160, 192, 256, 288

NAMES = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def m(n):
    """'A3' / 'F#2' / 'Bb3' → MIDI (C4 = 60)."""
    i, acc = 1, 0
    while i < len(n) and n[i] in '#b':
        acc += 1 if n[i] == '#' else -1
        i += 1
    return 12 * (int(n[i:]) + 1) + NAMES[n[0]] + acc


class Ids:
    """The service numbers what it makes; the script names it the same way (a fresh song: ch_1 is Main)."""
    def __init__(self):
        self.n = {'ch': 1, 'dv': 0, 'pt': 0, 'ac': 0, 'au': 0, 'sd': 0}

    def new(self, kind):
        self.n[kind] += 1
        return f"{kind}_{self.n[kind]}"


L = []          # the script
ids = Ids()


def line(s=""):
    L.append(s)


def tokens(notes):
    return " ".join(f"{p}@{a:g}:{l:g}:{v}" for p, a, l, v in notes)


def notes_add(pt, notes):
    for i in range(0, len(notes), 48):
        line(f'notes add {pt} "{tokens(notes[i:i + 48])}"')


def instrument(kind, name, at, length):
    """A track with its instrument and a first MIDI clip: returns (strip, device, pattern, clip)."""
    ch, dv, pt, ac = ids.new('ch'), ids.new('dv'), ids.new('pt'), ids.new('ac')
    line(f"clip add --instrument {kind} --at {at:g} --length {length:g}")
    line(f'set {ch}.name="{name}" {pt}.name="{name}"')
    return ch, dv, pt, ac


def pattern(name, length):
    pt = ids.new('pt')
    line(f'pattern new --name "{name}" --length {length:g}')
    return pt


def clip(ch, pt, at, length):
    ac = ids.new('ac')
    line(f"clip add --strip {ch} --pattern {pt} --at {at:g} --length {length:g}")
    return ac


def device(ch, kind, **params):
    dv = ids.new('dv')
    line(f"device add {ch} --type {kind}")
    if params:
        line("set " + " ".join(f"{dv}.{k.replace('__', '.')}={v}" for k, v in params.items()))
    return dv


def automate(address, points):
    """points: (beat, value[, shape]) — the first moves the one `auto create` makes at 0."""
    au = ids.new('au')
    line(f"auto create {address}")
    for i, pt in enumerate(points):
        at, value = pt[0], pt[1]
        shape = pt[2] if len(pt) > 2 else None
        if i == 0: # `auto create` makes one point at beat 0: moved to where the curve starts
            line(f"auto point move {au} --at 0" + (f" --to {at:g}" if at != 0 else "") + f" --value {value:g}")
        else:
            line(f"auto point add {au} --at {at:g} --value {value:g}" + (f" --shape {shape}" if shape else ""))
    return au


# ── the harmony: one bar a chord, four bars a turn ──────────────────────────────────────────────────────────
PROG = ['Am', 'F', 'C', 'G']
ROOT = {'Am': 'A', 'F': 'F', 'C': 'C', 'G': 'G'}
VOICING = {  # wide supersaw chords: root, third, fifth, octave — voice-led around C4
    'Am': ['A3', 'C4', 'E4', 'A4'], 'F': ['A3', 'C4', 'F4', 'A4'],
    'C': ['G3', 'C4', 'E4', 'G4'], 'G': ['G3', 'B3', 'D4', 'G4']}
PAD = {'Am': ['A2', 'E3', 'C4'], 'F': ['F2', 'C3', 'A3'], 'C': ['C3', 'G3', 'E4'], 'G': ['G2', 'D3', 'B3']}

# the hook: four bars over the turn, sixteenths (beat, length, note) — written for this track
HOOK = [
    (0.0, 0.5, 'A4'), (0.75, 0.25, 'C5'), (1.0, 0.5, 'E5'), (1.5, 0.5, 'D5'), (2.0, 0.75, 'C5'), (3.0, 0.5, 'A4'), (3.5, 0.5, 'G4'),
    (4.0, 0.5, 'A4'), (4.75, 0.25, 'C5'), (5.0, 0.75, 'F5'), (6.0, 0.5, 'E5'), (6.5, 0.5, 'C5'), (7.0, 1.0, 'A4'),
    (8.0, 0.5, 'G4'), (8.5, 0.5, 'C5'), (9.0, 0.75, 'E5'), (10.0, 0.5, 'G5'), (10.5, 0.5, 'E5'), (11.0, 0.75, 'D5'),
    (12.0, 0.5, 'D5'), (12.5, 0.5, 'B4'), (13.0, 0.5, 'G4'), (13.75, 0.25, 'D5'), (14.0, 1.0, 'E5'), (15.0, 0.5, 'D5'), (15.5, 0.5, 'B4'),
]


def main(out):
    line("# Perihelion — an original EDM track, written by make_script.py (R-SVC-12). 126 bpm, A minor, 72 bars.")
    line('project new perihelion.slp --bpm 126 --name "Perihelion"')
    line()

    # ── the kick: four on the floor, tuned near A1 ──
    line("# the kick — tuned near the key's root (48 Hz · 2^(2/12) = 54 Hz), every beat; out for the builds' last bars")
    kick, kdv, kpt, _ = instrument("drums", "Kick", INTRO, 32)
    line(f"set {kdv}.kick.tune=2 {kdv}.kick.decay=420 {kdv}.kick.tone=0.45 {kdv}.volume=0")
    line(f'pattern steps {kpt} --pitch kick "x...x...x...x..." --vel 120')
    line(f"set {kpt}.length=4")
    for at, ln in [(DROP1, 64), (DROP2, 64), (OUTRO, 32)]:
        clip(kick, kpt, at, ln)
    sparse = pattern("Kick, a bar", 4)   # a build accelerates: one kick a bar, then every beat, then a breath
    line(f'pattern steps {sparse} --pitch kick "x..............." --vel 116')
    for b in (BUILD1, BUILD2):
        clip(kick, sparse, b, 16)
        clip(kick, kpt, b + 16, 8)
    line()

    # ── the groove: claps, house hats ──
    line("# the groove: clap on 2 and 4, open hats on the off-beats choked by the closed sixteenths around them")
    drums, ddv, gpt, _ = instrument("drums", "Groove", 16, 16)
    line(f"set {ddv}.clap.level=-3 {ddv}.clap.decay=260 {ddv}.chat.level=-5 {ddv}.chat.tone=0.8 {ddv}.chat.pan=0.25 "
         f"{ddv}.ohat.level=-6 {ddv}.ohat.decay=300 {ddv}.ohat.pan=-0.2 {ddv}.volume=-2")
    line(f"set {gpt}.length=4")
    line(f'pattern steps {gpt} --pitch clap "....x.......x..." --vel 115')
    line(f'pattern steps {gpt} --pitch closed-hat "xx.xxx.xxx.xxx.x" --vel 52')
    line(f'pattern steps {gpt} --pitch open-hat "..x...x...x...x." --vel 80')
    hats = pattern("Intro hats", 4)
    line(f'pattern steps {hats} --pitch closed-hat "..x...x...x...x." --vel 64')
    clip(drums, hats, INTRO, 16)
    for at, ln in [(DROP1, 64), (DROP2, 64), (OUTRO, 24)]:
        clip(drums, gpt, at, ln)
    for at in (BUILD1, BUILD2):          # a build starts lean — the light hats — and climbs on the roll and the riser
        clip(drums, hats, at, 24)
    line()

    # ── cymbals: a crash on every phrase of the drops, a ride through the second ──
    line("# cymbals (REQ-drum-6): a crash on each drop's phrases and into the breakdown; a ride through the second drop")
    cym, cdv, cpt, _ = instrument("drums", "Cymbals", DROP1, 4)
    line(f"set {cdv}.crash.level=-2 {cdv}.crash.decay=2200 {cdv}.ride.level=-9 {cdv}.ride.pan=0.3 {cdv}.volume=-3")
    line(f"set {cpt}.length=4")
    line(f'pattern steps {cpt} --pitch crash "x..............." --vel 118')
    for at in [DROP1 + 32, BREAK, DROP2, DROP2 + 32, OUTRO]:
        clip(cym, cpt, at, 4)
    ride = pattern("Ride", 4)
    line(f'pattern steps {ride} --pitch ride "x...x...x...x..." --vel 70')
    line(f'pattern steps {ride} --pitch ride "..x...x...x...x." --vel 50 --at 0')
    clip(cym, ride, DROP2 + 4, 60)
    line()

    # ── the roll: a snare that speeds up through each build, louder as it goes, then a breath ──
    line("# the build's snare roll: quarters, eighths, sixteenths, thirty-seconds — then a beat of silence before the drop")
    roll, rdv, rpt, _ = instrument("drums", "Roll", BUILD1, 32)
    line(f"set {rdv}.snare.tune=3 {rdv}.snare.decay=180 {rdv}.snare.tone=0.6 {rdv}.volume=-6")
    line(f"set {rpt}.length=32")
    line(f'pattern steps {rpt} --pitch snare "x...x...x...x...x...x...x...x..." --vel 58 --at 0')
    line(f'pattern steps {rpt} --pitch snare "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x." --vel 72 --at 8')
    line(f'pattern steps {rpt} --pitch snare "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" --vel 86 --at 16')
    line(f'pattern steps {rpt} --pitch snare "xxxxxxxxxxxxxxxx" --vel 100 --at 24')
    line(f'pattern steps {rpt} --pitch snare "xxxxxxxxxxxxxxxxxxxxxxxx" --step 0.125 --vel 112 --at 28')
    clip(roll, rpt, BUILD2, 32)
    line()

    # ── the sub: a sine on the roots, held, pumped by the kick ──
    line("# the sub: a sine on each bar's root (A1 F1 C2 G1), mono, ducked by the kick")
    sub, sdv, spt, _ = instrument("synth", "Sub", DROP1, 64)
    line(f"set {sdv}.osc1.wave=sine {sdv}.osc1.octave=0 {sdv}.osc2.level=0 {sdv}.filter.cutoff=280 {sdv}.filter.res=0 "
         f"{sdv}.filter.env=0 {sdv}.filter.keytrack=0 {sdv}.amp.attack=4 {sdv}.amp.decay=60 {sdv}.amp.sustain=1 "
         f"{sdv}.amp.release=90 {sdv}.volume=-5")
    line(f"set {spt}.length=16")
    notes_add(spt, [(m(r), i * BAR, BAR, 110) for i, r in enumerate(['A1', 'F1', 'C2', 'G1'])])
    clip(sub, spt, DROP2, 64)
    line()

    # ── the mid bass: an off-beat saw, a sixteenth pickup ──
    line("# the mid bass: off-beat eighths on the roots an octave up, a pickup into each bar")
    mid, mdv, mpt, _ = instrument("synth", "Mid Bass", DROP1, 64)
    line(f"set {mdv}.osc1.wave=saw {mdv}.osc2.wave=square {mdv}.osc2.octave=-1 {mdv}.osc2.level=0.35 "
         f"{mdv}.filter.cutoff=650 {mdv}.filter.res=0.28 {mdv}.filter.env=2.2 {mdv}.fenv.decay=150 {mdv}.fenv.sustain=0.1 "
         f"{mdv}.amp.attack=1 {mdv}.amp.decay=170 {mdv}.amp.sustain=0.35 {mdv}.amp.release=60 {mdv}.volume=-9")
    line(f"set {mpt}.length=16")
    bass = []
    for i, c in enumerate(PROG):
        r = m(ROOT[c] + '2')
        for k in range(4):
            bass.append((r, i * BAR + k + 0.5, 0.375, 100 if k % 2 == 0 else 92))
        bass.append((r + 12, i * BAR + 3.75, 0.25, 84))
    notes_add(mpt, bass)
    clip(mid, mpt, DROP2, 64)
    line()

    # ── the chords: the wide supersaw ──
    line("# the chords: two five-voice saws, detuned and WIDE (REQ-synth2-6), held a bar each, pumping on the kick")
    chords, chdv, chpt, _ = instrument("synth", "Supersaw", DROP1, 64)
    line(f"set {chdv}.osc1.wave=saw {chdv}.osc1.voices=5 {chdv}.osc1.detune=38 {chdv}.osc1.width=24 {chdv}.osc1.level=0.7 "
         f"{chdv}.osc2.wave=saw {chdv}.osc2.octave=0 {chdv}.osc2.voices=5 {chdv}.osc2.detune=22 {chdv}.osc2.width=16 "
         f"{chdv}.osc2.fine=6 {chdv}.osc2.level=0.55 {chdv}.filter.cutoff=5200 {chdv}.filter.res=0.12 {chdv}.filter.env=0.8 "
         f"{chdv}.fenv.decay=700 {chdv}.fenv.sustain=0.55 {chdv}.amp.attack=6 {chdv}.amp.decay=400 {chdv}.amp.sustain=0.9 "
         f"{chdv}.amp.release=320 {chdv}.volume=-16")
    line(f"set {chpt}.length=16")
    notes_add(chpt, [(m(n), i * BAR, BAR, 96) for i, c in enumerate(PROG) for n in VOICING[c]])
    clip(chords, chpt, DROP2, 64)
    line()

    # ── the lead: the hook ──
    line("# the lead: the hook — a bright three-voice saw with an octave square, a dotted-eighth delay")
    lead, ldv, lpt, _ = instrument("synth", "Lead", DROP1, 64)
    line(f"set {ldv}.osc1.wave=saw {ldv}.osc1.voices=3 {ldv}.osc1.detune=14 {ldv}.osc1.width=12 {ldv}.osc1.level=0.75 "
         f"{ldv}.osc2.wave=square {ldv}.osc2.octave=1 {ldv}.osc2.level=0.18 {ldv}.filter.cutoff=2400 {ldv}.filter.res=0.22 "
         f"{ldv}.filter.env=2.4 {ldv}.fenv.decay=260 {ldv}.fenv.sustain=0.35 {ldv}.amp.attack=2 {ldv}.amp.decay=320 "
         f"{ldv}.amp.sustain=0.6 {ldv}.amp.release=200 {ldv}.volume=-14")
    line(f"set {lpt}.length=16")
    notes_add(lpt, [(m(n), a, l, 104 if a % 1 == 0 else 92) for a, l, n in HOOK])
    clip(lead, lpt, DROP2, 64)
    line()

    # ── the pad: opens through the intro and the builds, holds the breakdown ──
    line("# the pad: slow, wide, low-passed — its filter opening through the intro and the builds")
    pad, pdv, ppt, _ = instrument("synth", "Pad", INTRO, 64)
    line(f"set {pdv}.osc1.wave=saw {pdv}.osc1.voices=5 {pdv}.osc1.detune=26 {pdv}.osc1.width=30 {pdv}.osc1.level=0.6 "
         f"{pdv}.osc2.wave=triangle {pdv}.osc2.octave=1 {pdv}.osc2.level=0.25 {pdv}.filter.res=0.2 {pdv}.filter.env=0.3 "
         f"{pdv}.filter.keytrack=0.3 {pdv}.amp.attack=650 {pdv}.amp.decay=900 {pdv}.amp.sustain=0.85 {pdv}.amp.release=1400 "
         f"{pdv}.volume=-15")
    line(f"set {ppt}.length=16")
    notes_add(ppt, [(m(n), i * BAR, BAR, 90) for i, c in enumerate(PROG) for n in PAD[c]])
    for at, ln in [(BREAK, 64), (OUTRO, 32)]:
        clip(pad, ppt, at, ln)
    automate(f"{pdv}.filter.cutoff", [(0, 260), (31.5, 1400, 'smooth'), (32, 1400), (63.5, 7000, 'smooth'), (64, 900, 'hold'),
                                       (BREAK, 2200), (BUILD2, 2200), (DROP2 - 0.5, 8000, 'smooth'), (DROP2, 900, 'hold'),
                                       (OUTRO, 3000), (END, 240, 'smooth')])
    line()

    # ── the piano: the breakdown — broken chords, then the hook in its right hand ──
    line("# the piano (R-INST-5) in the breakdown: broken chords in eighths, the hook joining an octave up")
    piano, pidv, pipt, _ = instrument("piano", "Piano", BREAK, 16)
    line(f"set {pidv}.volume=-4 {pidv}.strings.decay=1.2 {pidv}.hammer.hardness=0.9")
    line(f"set {pipt}.length=16")
    arp = []
    for i, c in enumerate(PROG):
        lo = [m(n) for n in PAD[c]]
        figure = [lo[0], lo[1] + 12, lo[2] + 12, lo[1] + 24, lo[2] + 12, lo[1] + 12, lo[2], lo[1] + 12]
        for k, p in enumerate(figure):
            arp.append((p, i * BAR + 0.5 * k, 0.5, 78 if k else 92))
    notes_add(pipt, arp)
    phook = pattern("Piano + hook", 16)
    notes_add(phook, arp + [(m(n) + 12, a, l, 88) for a, l, n in HOOK])
    clip(piano, phook, BREAK + 16, 16)
    line()

    # ── the riser: noise and a saw, climbing through each build ──
    line("# the riser: a noise-and-saw sweep, its filter opening and its pitch climbing an octave through each build")
    rise, rsdv, rspt, _ = instrument("synth", "Riser", BUILD1, 32)
    line(f"set {rsdv}.osc1.wave=saw {rsdv}.osc1.voices=3 {rsdv}.osc1.detune=40 {rsdv}.osc1.width=30 {rsdv}.osc1.level=0.3 "
         f"{rsdv}.osc2.level=0 {rsdv}.noise=0.8 {rsdv}.filter.res=0.35 {rsdv}.filter.env=0 {rsdv}.filter.keytrack=0 "
         f"{rsdv}.amp.attack=1500 {rsdv}.amp.sustain=1 {rsdv}.amp.release=300 {rsdv}.volume=-15")
    line(f"set {rspt}.length=32")
    notes_add(rspt, [(m('A3'), 0, 31.5, 100)])
    clip(rise, rspt, BUILD2, 32)
    automate(f"{rsdv}.filter.cutoff", [(BUILD1, 200), (DROP1 - 0.5, 8500, 'smooth'), (DROP1, 200, 'hold'),
                                        (BUILD2, 200), (DROP2 - 0.5, 8500, 'smooth'), (DROP2, 200, 'hold')])
    automate(f"{rsdv}.osc1.semi", [(BUILD1, -12), (DROP1 - 0.5, 12), (DROP1, -12, 'hold'),
                                    (BUILD2, -12), (DROP2 - 0.5, 12), (DROP2, -12, 'hold')])
    automate(f"{rsdv}.volume", [(BUILD1, -40), (DROP1 - 0.5, -13, 'smooth'), (DROP1, -40, 'hold'),
                                 (BUILD2, -40), (DROP2 - 0.5, -13, 'smooth'), (DROP2, -40, 'hold')])
    line()

    # ── the sidechain: the kick keys a compressor on everything that should breathe ──
    line("# the pump: the kick keys a compressor on the sub, the mid bass, the chords, the lead and the pad")
    for ch, thr, ratio, rel in [(sub, -28, 8, 120), (mid, -26, 5, 120), (chords, -22, 4, 130), (lead, -20, 2, 120), (pad, -24, 3, 160)]:
        device(ch, "compressor", sidechain="on", threshold=thr, ratio=ratio, attack=1, release=rel)
        line(f"send add {kick} --to {ch} --sidechain")
    line()

    # ── tone: the lows belong to the kick and the sub ──
    line("# tone: the chords, the lead, the pad and the piano give the low end to the kick and the sub")
    for ch, hz in [(chords, 160), (lead, 220), (pad, 140), (piano, 70), (rise, 180), (drums, 300), (cym, 400)]:
        device(ch, "eq", lowcut__on="on", lowcut__freq=hz)
    line()

    # ── space: a reverb and a tempo-synced delay ──
    line("# space: a long, wide reverb and a dotted-eighth delay, each on a bus")
    verb = ids.new('ch')
    line('strip add --kind bus --name Reverb')
    device(verb, "reverb", decay=3200, size=160, mix=1, lowcut=300, highcut=12000, width=1)
    dly = ids.new('ch')
    line('strip add --kind bus --name Delay')
    device(dly, "delay", time='"=60000/bpm*0.75"', feedback=0.38, tone=4500, mix=1)
    for ch, to, g in [(chords, verb, -11), (lead, verb, -8), (lead, dly, -11), (pad, verb, -6), (piano, verb, -7),
                      (rise, verb, -5), (drums, verb, -16), (cym, verb, -14)]:
        line(f"send add {ch} --to {to} --gain {g}")
    line()

    # ── the mix and the master ──
    line("# the mix: faders; the master: a limiter, its ceiling under −1 dBTP")
    gains = {kick: -2, drums: -6, cym: -9, roll: -1, sub: -7, mid: -6, chords: -3, lead: -4, pad: -7, piano: -4,
             rise: -6, verb: -7, dly: -11}
    line("set " + " ".join(f"{ch}.gain={g}" for ch, g in gains.items()))
    limiter = device("master", "limiter", gain=12, ceiling=-2.0, release=80)
    line()
    line("ls")
    line("audit")
    line("project save")
    line(f"render --out perihelion.wav --stems {kick},{chords},{sub},{lead}")

    with open(out, "w") as f:
        f.write("\n".join(L) + "\n")
    return {"kick": kick, "chords": chords, "sub": sub, "lead": lead, "limiter": limiter}


if __name__ == "__main__":
    here = os.path.dirname(os.path.abspath(__file__))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "song.txt")
    stems = main(out)
    print(f"{len(L)} lines → {out}; stems {stems}")
