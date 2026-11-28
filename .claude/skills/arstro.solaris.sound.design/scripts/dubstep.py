#!/usr/bin/env python3
"""dubstep.py — the MELODIC DUBSTEP drop, to the user's own specification (2026-11-27): "melodic dubstep need to drop
kick on 1 3, snare on 2 4 and need hard snare, saw stop on snare, there will be 8 bit pluck arp run with chord, heavy
bass, remove wobble bass, use heavy lead to make emotional melody".

    python3 dubstep.py --out ~/Music/Solaris/sound-lab/dubstep-NNN --kick <a kick .wav> [--only 1,3] [--quick]

- KICK on 1 and 3, a HARD SNARE on 2 and 4. Counted slow (75 to the minute — the genre's half time): in this
  150 bpm project a kick on beat 1 and a snare on beat 3 of every bar. One of DROPS (`fast`) counts it at 150 instead.
- the SAW (the approved wide supersaw, the mood chords) strikes with the kick, holds flat and STOPS at the snare;
- an 8-BIT PLUCK ARP — one square wave, crushed to 6 bits — runs the chord's notes in sixteenths, high, and stops at the snare with the saw;
- a HEAVY BASS: the 808 under a driven saw-and-square an octave over it, straight — NO wobble, no LFO at all;
- a HEAVY LEAD (the loved Glass stack with saws an octave below and drive) plays an emotional melody (drops.py MELODIES).

Each drop is rendered twice — the second time with every element's fader at its target (TARGETS) — and every element
alone for the lab. Runs solaris-cc with scratch settings in the output folder; the kick is never committed.
"""
import argparse, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import drops as D  # noqa: E402
from drops import Script, num, loudness, DEFAULT_CC, midi, BPM, BARS  # noqa: E402

CHAT, OHAT, RIM = 42, 46, 37
TARGETS = {"Kick": -10.0, "Snare": -13.0, "Hats": -28.0, "Saw": -14.5, "Bass": -14.0, "Arp": -21.0, "Lead": -13.5}
# the two-bar unit (8 beats): (kicks, snares, which of the mood's three voicings each kick strikes)
GRIDS = {"half": ([0, 4], [2, 6], [0, 2]),                 # kick 1, 3 · snare 2, 4, counted slow (the genre's half time)
         "fast": ([0, 2, 4, 6], [1, 3, 5, 7], [0, 0, 1, 2])}  # the same words counted at 150
HEAVY_BASS = dict(osc1__wave="saw", osc1__octave=-1, osc1__unison=1, osc1__level=0.8, osc2__wave="pulse", osc2__width=0.5,
                  osc2__octave=-1, osc2__unison=1, osc2__level=0.55, sub__level=0, filter__mode='"lowpass 24"',
                  filter__cutoff=320, filter__res=0.1, filter__drive=14, filter__env=1.6, filter__keytrack=0.3,
                  fenv__attack=0, fenv__decay=320, fenv__sustain=0.4, fenv__release=80, amp__attack=1, amp__decay=0,
                  amp__sustain=1, amp__release=40, velocity=0, voice__mode="mono", voice__glide=0, voice__phase="reset",
                  voice__drift=0, volume=-8)
BIT = dict(osc1__wave="pulse", osc1__width=0.5, osc1__unison=1, osc1__level=0.9, osc2__level=0, sub__level=0,
           filter__mode="off", amp__attack=0, amp__decay=140, amp__sustain=0, amp__release=60, velocity=0.2,
           voice__mode="poly", voice__glide=0, voice__drift=0, volume=-6)
# the heavy lead: the Glass stack's two layers, and three saws an octave BELOW through a driven ladder
HEAVY_LEAD = D.LEADS[0]["layers"] + [
    ("analog", "weight", -12, dict(D._SLIDE, osc1__wave="saw", osc1__unison=3, osc1__detune=0.2, osc1__blend=0.6,
                                   osc1__spread=0.4, osc1__level=0.8, osc2__wave="pulse", osc2__width=0.45, osc2__level=0.35,
                                   filter__mode='"lowpass 24"', filter__cutoff=1800, filter__res=0.1, filter__drive=10,
                                   filter__env=1.5, filter__keytrack=0.5, fenv__attack=4, fenv__decay=420, fenv__sustain=0.5,
                                   voice__mode="mono", voice__glide=140, volume=-13), "slide")]
ARPS = {"up": [0, 1, 2, 3], "updown": [0, 1, 2, 3, 2, 1], "jump": [0, 2, 1, 3, 0, 3, 1, 2], "gallop": None}
# SIMPLE melodies (the user, 2026-11-28: "please make more simple melody, you are not ready for advanced melody"):
# two or three long notes a unit, every one a note of the chord under it, ONE rhythm repeated in all four units, steps
# and thirds, inside an octave. In drops.py MELODIES' shape.
SIMPLE = [
    dict(mood="Longing", shape="Three notes", how="a long note, a step down, a long note — the same rhythm every two "
         "bars, a step higher over the third chord",
         units=["B5:3 A5:1 G5:4", "B5:3 A5:1 G5:4", "C6:3 B5:1 A5:4", "B5:3 A5:1 F#5:2 A5:.5 B5:.5 D#6:1"], last="B5:3 A5:1 F#5:2 B5:.5 D#6:.5 F#6:1", home="E6"),
    dict(mood="Longing", shape="One note a bar", how="one held note in each bar, falling a step or a third — eight "
         "notes in eight bars",
         units=["G5:4 F#5:4", "G5:4 E5:4", "E5:4 C5:4", "F#5:4 D#5:2 F#5:1 A5:1"], last="F#5:4 D#5:2 F#5:1 D#5:1", home="E5"),
    dict(mood="Falling", shape="The rise", how="two short steps up the chord and a long note at the top — the same "
         "climb each time, one note higher over the third chord",
         units=["E5:2 G5:2 B5:4", "E5:2 G5:2 B5:4", "E5:2 G5:2 C6:4", "F#5:2 A5:2 B5:2 A5:.5 F#5:.5 D#5:1"], last="F#5:2 A5:2 B5:2 D#6:.5 F#6:.5 D#6:1", home="E6"),
    dict(mood="Dreamy", shape="Hold and fall", how="one long high note, then two notes falling away; over the borrowed "
         "chord the long note is its E♭",
         units=["D6:5 B5:1 A5:2", "D6:5 A5:1 F#5:2", "E6:5 D6:1 B5:2", "Eb6:4 D6:1 C6:1 A5:.5 C6:.5 Eb6:1"], last="Eb6:4 D6:1 C6:1 Eb6:.5 D6:.5 A5:1", home="G5"),
    dict(mood="Longing", shape="Say it, wait", how="three notes falling down the chord in the first bar, then silence "
         "for a whole bar — the drums, the chords and the arp answer",
         units=["B5:1 G5:1 E5:2 r:4", "B5:1 G5:1 E5:2 r:4", "C6:1 A5:1 E5:2 r:4", "B5:1 A5:1 F#5:2 r:2 F#5:.5 A5:.5 B5:.5 D#6:.5"],
         last="B5:1 A5:1 F#5:2 r:2 B5:.5 A5:.5 F#5:.5 D#5:.5", home="E5"),
]
def _u(v0, v2, bass, bass2=None):
    """One two-bar unit in drops.py MOODS' shape: the first bar's voicing (twice) and the second bar's."""
    return [(v0, bass)] * 2 + [(v2, bass2 or bass)]


# FIVE NEW SONGS (the user, 2026-11-28: "try 5 new chord progression, melody and style, the final note run should be
# more slow, maybe run at the last bar not last beat"). Each: its own key, tempo, progression, arp and a simple melody
# (lesson 20) whose LAST BAR of every eight is a slow run — four quarter notes — instead of a flurry in the last beat.
SONGS = [
    dict(name="Anthem", key="G major", bpm=150, arp="up", chords="G · D/F♯ · Em7 · Cadd9 → D",
         style="bright and open — the pop progression I–V–vi–IV, the arp climbing",
         units=[_u("G3 D4 G4 B4 D5 G5", "G3 D4 A4 B4 D5 G5", "G1"), _u("F#3 D4 F#4 A4 D5 F#5", "F#3 D4 E4 A4 D5 F#5", "F#1"),
                _u("E3 B3 D4 G4 B4 E5 G5", "E3 B3 D4 G4 B4 D5 G5", "E1"),
                _u("C3 G3 D4 E4 G4 C5 E5", "D3 A3 D4 F#4 A4 D5 F#5", "C2", "D2")],
         lead=dict(shape="Anthem", how="two steps up the chord and a long note on top, every two bars",
                   units=["D5:2 G5:2 B5:4", "D5:2 F#5:2 A5:4", "E5:2 G5:2 B5:4", "E5:2 G5:2 A5:1 B5:1 D6:1 A5:1"],
                   last="E5:2 G5:2 D6:1 B5:1 A5:1 F#5:1", home="G5")),
    dict(name="Heartbreak", key="A minor", bpm=140, arp="updown", chords="Am9 · Fmaj7 · Cadd9 · G → E7",
         style="slower and sad — i–VI–III–VII, the last chord turning to the dominant E7; the arp rocks up and down",
         units=[_u("A2 E3 G3 C4 E4 B4 E5", "A2 E3 G3 C4 E4 B4 C5", "A1"), _u("F3 C4 E4 A4 C5 E5", "F3 C4 E4 G4 C5 E5", "F1"),
                _u("C3 G3 D4 E4 G4 C5 E5", "C3 G3 D4 E4 G4 D5 G5", "C2"),
                _u("G2 D3 G3 B3 D4 G4 D5", "E3 B3 D4 G#4 B4 E5", "G1", "E1")],
         lead=dict(shape="Heartbreak", how="one long note, then two shorter ones falling — a sigh every two bars",
                   units=["E5:4 C5:2 B4:2", "A5:4 G5:2 E5:2", "G5:4 E5:2 D5:2", "D5:4 B5:1 G#5:1 E5:1 D5:1"],
                   last="D5:4 E5:1 G#5:1 B5:1 G#5:1", home="A5")),
    dict(name="Epic", key="D minor", bpm=145, arp="jump", chords="Dm9 · B♭maj7 · Fadd9 · A7sus4 → A7",
         style="cinematic — i–VI–III and a suspended dominant that resolves; the arp leaps",
         units=[_u("D3 A3 C4 E4 F4 A4 F5", "D3 A3 C4 E4 F4 A4 E5", "D2"), _u("Bb2 F3 A3 D4 F4 A4 D5", "Bb2 F3 A3 D4 F4 A4 F5", "Bb1"),
                _u("F3 C4 F4 G4 A4 C5 F5", "F3 C4 F4 G4 A4 C5 A5", "F1"), _u("A2 G3 D4 E4 A4 D5", "A2 G3 C#4 E4 A4 C#5", "A1")],
         lead=dict(shape="Epic", how="a quick climb of three notes to a high D, then a long note below it",
                   units=["F5:1 A5:1 D6:2 A5:4", "F5:1 A5:1 D6:2 Bb5:4", "F5:1 A5:1 C6:2 A5:4", "E5:1 A5:1 D6:2 C#6:1 A5:1 G5:1 E5:1"],
                   last="E5:1 A5:1 D6:2 E6:1 C#6:1 A5:1 C#6:1", home="D6")),
    dict(name="Nostalgia", key="C major", bpm=150, arp="gallop", chords="Fmaj9 · G · Am7 · Em7 → Gsus4",
         style="warm — IV–V–vi–iii, never quite landing home until the end; the arp gallops",
         units=[_u("F3 C4 E4 G4 A4 C5 E5", "F3 C4 E4 G4 A4 C5 G5", "F1"), _u("G3 D4 G4 A4 B4 D5 G5", "G3 D4 G4 A4 B4 D5 B5", "G1"),
                _u("A2 E3 G3 C4 E4 G4 E5", "A2 E3 G3 C4 E4 G4 C5", "A1"),
                _u("E3 B3 D4 G4 B4 D5 G5", "G3 D4 G4 C5 D5 G5", "E1", "G1")],
         lead=dict(shape="Nostalgia", how="a long note, a short step, and a long note a leap away",
                   units=["E5:3 G5:1 C6:4", "D6:3 B5:1 G5:4", "C6:3 A5:1 E5:4", "D5:3 G5:1 C6:1 D6:1 G5:1 D5:1"],
                   last="D5:3 G5:1 D6:1 C6:1 G5:1 B5:1", home="C6")),
    dict(name="Dark hero", key="E Dorian", bpm=142, arp="up", chords="Em(add9) · G · Dadd9 · A → B",
         style="dark but rising — a minor key with a MAJOR fourth chord (A), then B to pull back home; two long notes a unit",
         units=[_u("E3 B3 E4 F#4 G4 B4 E5", "E3 B3 E4 F#4 G4 B4 G5", "E1"), _u("G3 D4 G4 B4 D5 G5", "G3 D4 G4 B4 D5 B5", "G1"),
                _u("D3 A3 D4 E4 F#4 A4 D5 F#5", "D3 A3 D4 E4 F#4 A4 D5 A5", "D2"),
                _u("A2 E3 A3 C#4 E4 A4 C#5", "B2 F#3 B3 D#4 F#4 B4 D#5", "A1", "B1")],
         lead=dict(shape="Dark hero", how="two whole-bar notes every two bars, wide apart",
                   units=["E5:4 B5:4", "D6:4 B5:4", "A5:4 F#5:4", "E5:4 F#5:1 A5:1 B5:1 D#6:1"],
                   last="E5:4 F#5:1 A5:1 B5:1 D#6:1", home="E6")),
]
DROPS = [dict(name=x["name"], mood=x["name"], lead=x["lead"], arp=x["arp"], grid="half", bpm=x["bpm"], key=x["key"],
              about=f"{x['key']}, {x['bpm']} bpm — {x['chords']}: {x['style']}") for x in SONGS]


def drop_hits(drop):
    """(beat, length to the snare, voicing, bass note) of every chord hit of the 16 bars, and the snares' grid."""
    mood = next(m for m in SONGS + D.MOODS if m["name"] == drop["mood"])
    kicks, snares, voic = GRIDS[drop["grid"]]
    hits = []
    for u in range(BARS // 2):
        for k, v in zip(kicks, voic):
            notes, root = mood["units"][u % 4][v]
            hits.append((8 * u + k, min(x for x in snares if x > k) - k, notes, root))
        if u % 4 == 3:                                               # the loop's ENDING: one more stab on its last beat
            hits.append((8 * u + 7, 1.0) + tuple(mood["units"][3][2]))
    hits.append((64, 4.0) + tuple(mood["units"][0][0]))               # and home, held a whole bar, to finish
    return mood, hits, snares


def fit(drop):
    """The share of the lead's long notes (a beat or more) that are notes of the chord last struck under them."""
    _, hits, _ = drop_hits(drop)
    strong = ok = 0
    for t, d, p in D.melody_notes(drop["lead"]):
        if d >= 1:
            pcs = {midi(n) % 12 for n in [h for h in hits if h[0] <= t + 1e-9][-1][2].split()}
            strong, ok = strong + 1, ok + (p % 12 in pcs)
    return round(ok / max(1, strong), 2)


def arrange(drop, kick_file, trim=None):
    S, E, L = Script(), [], 4 * BARS + 8
    line = S.line
    mood, hits, snares = drop_hits(drop)
    bpm = drop.get("bpm", BPM)
    design = next(o for o in D.OPTIONS if o["name"] == "808")
    units = range(BARS // 2)
    count = "1 and 3 … 2 and 4, counted slow" if drop["grid"] == "half" else "1 and 3 … 2 and 4 of every bar at 150"
    line(f"# melodic dubstep drop — {drop['name']} (arstro.solaris.sound.design dubstep.py) — {bpm} bpm")
    line(f'project new drop.slp --bpm {bpm} --name "Dubstep — {drop["name"]}"')
    kick, kdv, kpt = S.new('ch'), S.new('dv'), S.new('pt')
    S.new('ac')
    line(f'clip add --instrument sampler --sample "{kick_file}" --at 0 --length {num(L)}')
    line(f'set {kick}.name="Kick" {kpt}.name="Kick" {kpt}.length={num(L)} {kdv}.mode=one-shot {kdv}.release=200 {kdv}.velocity=0')
    S.notes(kpt, [(60, t, 1.0, 127) for t, _, _, _ in hits])
    E.append({"name": "Kick", "strip": kick, "recipe": f"your sample {os.path.basename(kick_file)} on {count.split(' … ')[0]}"})
    # the snare: HARD — a short, bright crack with a rim and a clap in it, its attack doubled, clipped, a short room
    sn, sdv, spt = S.instrument("drums", "Snare", L)
    S.set(sdv, snare__tune=2, snare__decay=210, snare__tone=0.85, clap__decay=260, clap__tone=0.7, clap__level=-5,
          rim__tune=0, rim__decay=60, rim__level=-6, volume=4)
    S.notes(spt, [(p, 8 * u + t, 0.4, v) for u in units for t in snares for p, v in ((D.SNARE, 127), (D.CLAP, 112), (RIM, 120))] +
                 # the ending's fill: three snares in eighths after the last backbeat of every loop, each louder
                 [(D.SNARE, 8 * u + 6.5 + 0.5 * i, 0.3, 88 + 16 * i) for u in units if u % 4 == 3 for i in range(3)])
    S.device(sn, "transient", attack=70, sustain=-20, output=0)
    S.device(sn, "saturator", drive=9, curve="hard", mix=1, output=-5)
    S.device(sn, "eq", lowcut__on="on", lowcut__freq=120, peak1__freq=200, peak1__gain=3, peak1__q=1.2, highshelf__freq=6000,
             highshelf__gain=3)
    S.device(sn, "reverb", decay=1100, size=90, mix=0.13, lowcut=300, highcut=9000, width=1)
    E.append({"name": "Snare", "strip": sn, "recipe": "HARD: the snare tuned up 2 st, 210 ms, bright, a rim shot and a clap "
              "inside it; the attack pushed (+70), the tail pulled back, clipped 9 dB, +3 dB of body at 200 Hz and of "
              f"crack over 6 kHz, a short room — on {count.split(' … ')[1]}; and a slow FILL at the end of every "
              "eight bars: three more snares in eighths after the last backbeat, each louder"})
    ht, hdv, hpt = S.instrument("drums", "Hats", L)
    S.set(hdv, chat__decay=40, chat__tone=0.7, volume=-6)
    S.notes(hpt, [(CHAT, 8 * u + 0.5 * k, 0.2, 80 if k % 2 == 0 else 54) for u in units for k in range(16)])
    S.device(ht, "eq", lowcut__on="on", lowcut__freq=600)
    E.append({"name": "Hats", "strip": ht, "recipe": "closed hats in eighths, very quiet — only to keep time"})
    # the saw: strikes with the kick, holds flat, stops at the snare
    bus = S.bus("Saw")
    for kind, part, shift, patch in design["saw"]:
        ch, dv, pt = S.instrument(kind, f"Saw {part}", L)
        S.set(dv, volume=-10, **dict(D.FLAT, **patch))
        S.notes(pt, [(midi(n) + shift, t, max(0.1, d - 0.04), 110) for t, d, notes, _ in hits for n in notes.split()])
        line(f"route {ch} --to {bus}")
    S.device(bus, "eq", lowcut__on="on", lowcut__freq=150, peak1__freq=350, peak1__gain=-2, peak1__q=1)
    S.device(bus, "imager", width=140, mono="on", frequency=200)
    S.device(bus, "reverb", decay=1200, size=90, mix=0.07, lowcut=400, highcut=10000, width=0.9)
    line(f"strip duck {bus} --under {kick} --amount 12")
    S.set(S.new('dv'), release=90)
    E.append({"name": "Saw", "strip": bus, "recipe": design["saw_note"] + f" ({mood['chords']}). It strikes with every "
              "kick, holds flat and STOPS at the snare; at the END of every eight bars one more stab of the loop's last chord on the "
              "last beat (kick, bass and arp with it), and the piece finishes on the home chord held a whole bar; "
              "nearly dry; ducked 12 dB by the kick"})
    # the heavy bass: the 808 and a driven saw-and-square over it, with the saw — no LFO anywhere
    bbus = S.bus("Bass")
    kind, shift, patch, fx = design["bass"]
    sub, bdv, bpt = S.instrument(kind, "Bass 808", L)
    S.set(bdv, **patch)
    S.notes(bpt, [(midi(root) + shift, t, max(0.1, d - 0.04), 115) for t, d, _, root in hits])
    line(f"route {sub} --to {bbus}")
    hv, hvd, hvp = S.instrument("analog", "Bass heavy", L)
    S.set(hvd, **HEAVY_BASS)
    S.notes(hvp, [(midi(root) + (12 if midi(root) < midi("C2") else 0) + 12, t, max(0.1, d - 0.04), 115) for t, d, _, root in hits])
    S.device(hv, "saturator", drive=12, curve="tube", mix=1, output=-6)
    S.device(hv, "eq", lowcut__on="on", lowcut__freq=90)
    line(f"set {hv}.gain=-4")
    line(f"route {hv} --to {bbus}")
    S.device(bbus, "imager", width=0, mono="on", frequency=250)
    line(f"strip duck {bbus} --under {kick} --amount 12")
    S.set(S.new('dv'), release=70)
    E.append({"name": "Bass", "strip": bbus, "recipe": "HEAVY, no wobble: the 808 (a sine with a 5-semitone thump, driven) "
              "and, an octave over it, a saw and a square through a 24 dB ladder at 320 Hz driven 14 dB, its filter "
              "falling 1.6 octaves in 0.3 s at each note, then tube saturation — no LFO at all. With the saw, kick to "
              "snare; mono; ducked 12 dB by the kick"})
    # the 8-bit pluck arp: one square wave crushed to 6 bits, the chord's notes in sixteenths, high
    ar, adv, apt = S.instrument("analog", "Arp", L)
    S.set(adv, **BIT)
    an, pat = [], ARPS[drop["arp"]]
    for u in units:
        for i in range(32):
            t = 8 * u + 0.25 * i
            if pat is None and i % 4 == 1:                           # the gallop: an eighth and two sixteenths
                continue
            h = [h for h in hits if h[0] <= t + 1e-9][-1]
            if t >= h[0] + h[1] - 1e-9:                              # the arp stops at the snare, with the saw
                continue
            notes = h[2].split()
            pcs = sorted({midi(n) % 12 for n in notes[-4:]})
            tones = sorted(84 + (pc - 84) % 12 for pc in pcs)        # C6 … B6
            k = i if pat is None else pat[i % len(pat)]
            an.append((tones[k % len(tones)], t, 0.2, 110 if i % 4 == 0 else 86))
    S.notes(apt, an)
    S.device(ar, "bitcrusher", bits=6, rate=11025, mix=1, output=0)
    S.device(ar, "eq", lowcut__on="on", lowcut__freq=500, highcut__on="on", highcut__freq=9000)
    S.device(ar, "imager", width=120)
    S.device(ar, "delay", time=num(0.75 * 60000 / bpm), feedback=0.25, tone=5000, mix=0.12)
    line(f"strip duck {ar} --under {kick} --amount 4")
    S.set(S.new('dv'), release=90)
    E.append({"name": "Arp", "strip": ar, "recipe": "8-BIT: one square wave, no filter, each note gone in 140 ms, crushed to "
              f"6 bits at 11 kHz; it runs the top four notes of whatever chord is sounding in sixteenths ({drop['arp']}), "
              "C6–B6; it starts with the kick and STOPS at the snare, with the saw; a faint echo"})
    # the heavy lead
    mel = drop["lead"]
    lbus = S.bus("Lead")
    mn = D.melody_notes(mel)
    tied = [i + 1 < len(mn) and abs(mn[i + 1][0] - (t + d)) < 1e-6 for i, (t, d, p) in enumerate(mn)]
    for kind, part, shift, patch, how in HEAVY_LEAD:
        ch, dv, pt = S.instrument(kind, f"Lead {part}", L)
        S.set(dv, **dict(dict(D.FLAT, volume=-10), **patch))
        S.notes(pt, [(p + shift, t, d + 0.06 if tie else max(0.1, d - 0.06), 112) for (t, d, p), tie in zip(mn, tied)])
        line(f"route {ch} --to {lbus}")
    S.device(lbus, "saturator", drive=5, curve="tube", mix=1, output=-3)
    S.device(lbus, "eq", lowcut__on="on", lowcut__freq=150, peak1__freq=2800, peak1__gain=2, peak1__q=0.8)
    S.device(lbus, "delay", time=num(0.75 * 60000 / bpm), feedback=0.34, tone=4000, mix=0.18)
    S.device(lbus, "reverb", decay=2800, size=130, mix=0.2, lowcut=400, highcut=10000, width=1)
    line(f"strip duck {lbus} --under {kick} --amount 4")
    S.set(S.new('dv'), release=100)
    E.append({"name": "Lead", "strip": lbus, "recipe": "HEAVY: the lead you loved (the Glass stack — saws, pulses, a filter "
              "that opens at each note, a 140 ms slide) with three saws and a pulse an OCTAVE BELOW through a driven "
              f"ladder, the whole lead through a tube; 1 dB louder than the chords. The melody — {mel['shape']}: "
              f"{mel['how']}. THE ENDING: the last BAR of every eight is a slow run, four quarter notes — the first time "
              "turning the loop round, the second time into the home note; "
              f"{int(100 * fit(drop))} % of its long notes are in the chord"})
    for e in E:
        gdb = (trim or {}).get(e["name"], 0)
        if gdb:
            line(f"set {e['strip']}.gain={num(round(gdb, 1))}")
    line("device add master --type limiter")
    S.set(S.new('dv'), gain=0, ceiling=-1.5, truePeak="on")
    line("project save")
    return "\n".join(S.L) + "\n", E


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--kick", required=True, help="the kick sample (a .wav)")
    ap.add_argument("--only", default="")
    ap.add_argument("--cc", default=DEFAULT_CC)
    ap.add_argument("--quick", action="store_true", help="one render, no levelling pass")
    a = ap.parse_args()
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(out, exist_ok=True)
    kick = os.path.join(out, "kick.wav")
    if os.path.abspath(os.path.expanduser(a.kick)) != kick:
        shutil.copyfile(os.path.expanduser(a.kick), kick)
    env = dict(os.environ, SOLARIS_SETTINGS=os.path.join(out, ".settings.txt"), SOLARIS_RECENTS=os.path.join(out, ".recents"))
    env.pop("DISPLAY", None)
    only = set(x for x in a.only.split(",") if x)
    path = os.path.join(out, "batch.json")
    old = {}
    if os.path.exists(path):
        with open(path) as f:
            old = {m["id"]: m for m in json.load(f).get("melodies", [])}
    items, failed = [], 0
    for k, drop in enumerate(DROPS, 1):
        sid = f"dub-{k}"
        if only and str(k) not in only:
            if sid in old:
                items.append(old[sid])
            continue
        trim, ok, E, lufs = {}, True, [], {}
        for attempt in ((1, 2) if not a.quick else (2,)):
            text, E = arrange(drop, kick, trim)
            text = text.replace("project new drop.slp", f"project new {sid}.slp")
            text += f"render --out {sid}.mp3 --format mp3 --kbps 192 --stems " + ",".join(e["strip"] for e in E) + "\n"
            with open(os.path.join(out, sid + ".txt"), "w") as f:
                f.write(text)
            for stale in (sid + ".slp", sid + ".mp3"):
                if os.path.exists(os.path.join(out, stale)):
                    os.remove(os.path.join(out, stale))
            r = subprocess.run([a.cc, "--script", sid + ".txt"], cwd=out, env=env, capture_output=True, text=True)
            ok = r.returncode == 0 and os.path.exists(os.path.join(out, sid + ".mp3"))
            if not ok:
                print(f"{sid}: FAILED\n{r.stdout[-1200:]}{r.stderr[-600:]}", flush=True)
                break
            lufs = {e["name"]: loudness(os.path.join(out, f"{sid}.{e['strip']}.mp3")) for e in E}
            if attempt == 1:
                trim = {n: max(-18, min(12, round(TARGETS[n] - v, 1))) for n, v in lufs.items() if v is not None and v > -60}
        failed += not ok
        item = {"id": sid, "title": f"Dubstep {k} — {drop['name']} ({drop['mood']}): {drop['about']}", "sound": True,
                "section": "drop", "style": "melodic-dubstep", "style_label": "Melodic dubstep drop", "bpm": drop.get("bpm", BPM),
                "key": drop.get("key", "E minor"), "form_label": "sound — drop", "seconds": round((4 * BARS + 8) * 60 / drop.get("bpm", BPM)),
                "devices": [f"{e['name']}: {e['recipe']}" for e in E], "elements": E,
                "audio": [{"label": "full", "file": f"{sid}.mp3"}] +
                         [{"label": e["name"].lower(), "file": f"{sid}.{e['strip']}.mp3"} for e in E], "metrics": {}}
        if ok:
            lufs["mix"] = loudness(os.path.join(out, sid + ".mp3"))
            item["metrics"] = {"lufs": lufs, "trim": trim}
            print(f"{sid} {drop['name']}: ok — " + " · ".join(f"{n} {v}" for n, v in lufs.items()), flush=True)
        items.append(item)
        with open(path, "w") as f:
            json.dump({"batch": os.path.basename(out), "kind": "sound", "melodies": items}, f, indent=1, ensure_ascii=False)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
