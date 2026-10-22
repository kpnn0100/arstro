#!/usr/bin/env python3
"""futurebass.py — future bass sound design, section by section, for the approver to judge in the Sound Lab.

    python3 futurebass.py --out ~/Music/Solaris/sound-lab/fb-NNN [--only verse,build,drop] [--cc <solaris-cc>]
                          [--no-render]

Three clips, each a section of a future bass track built from Paper Lanterns (the melody skill's songbook, D major),
at 150 bpm in half time (the kick on 1, the snare on 3):
  verse — a reese bass, an airy bell, the main synth (a pluck and a soft saw lead) and light drums;
  build — the main synth and the bell, then everything rising: the chords' filter, a snare roll pitching up, a kick
          roll, a white-noise riser, a pitch riser — a breath, a clap — and the drop's first bar, so the landing is heard;
  drop  — supersaw chords carrying the melody, a sub and a mid bass struck WITH every kick, both ducked by it, the
          snare and clap on 3, and percussion filling between: hats with rolls, snaps, a shaker, tom fills, a ride, crashes.

Every element is its own strip with its space INSIDE its rack, so each solo is the element as designed. The clip and
every element's solo are rendered to MP3 (the lab's version switch plays them); each element's loudness and the duck
the kick puts on the bass are measured into batch.json. The recipes, their numbers and their reasons are
references/future-bass.md; the sources are references/research.md.

Runs solaris-cc with scratch settings in the output folder — never the user's own settings or recents.
"""
import argparse, json, math, os, subprocess, sys, wave

HERE = os.path.dirname(os.path.abspath(__file__))
MEL = os.path.normpath(os.path.join(HERE, "..", "..", "arstro.solaris.compose.melody", "scripts"))
sys.path.insert(0, MEL)
from melody import Key, chord  # noqa: E402
from song import Script, num, loudness, DEFAULT_CC  # noqa: E402
import songbook  # noqa: E402

BPM = 150
KEY = Key("D", "major")
SPB = 60 / BPM                                                      # seconds a beat
KICK, RIM, SNARE, CLAP, LTOM, CHAT, MTOM, OHAT, HTOM, CRASH, RIDE = 36, 37, 38, 39, 41, 42, 45, 46, 48, 49, 51
KICK_TUNE = round(12 * math.log2(55 / 48), 2)                       # the kick settles on 48·2^(tune/12) Hz: A1, the 5th


def sec(beats):
    return beats * SPB


def beats(seconds):
    return seconds / SPB


# ── the material: Paper Lanterns at 150 (twice its 75 bpm) ─────────────────────────────────────────────────────────
def part(name, shift=0):
    """A songbook part at 150 bpm: (chords — one per 8 beats, notes (t, d, midi, stressed)) from `shift` beats."""
    p = songbook.PAPER["parts"][name]
    notes = [(shift + 2 * (4 * b + a), 2 * d, songbook.midi(pitch), syl.startswith("*"))
             for b, a, d, pitch, syl, v in p["notes"]]
    return [chord(r, KEY) for r in p["chords"]], notes


def chord_at(chords, t, shift=0):
    return chords[max(0, min(len(chords) - 1, int((t - shift) // 8)))]


def place(pcs, lo, hi):
    """Every chord tone once between lo and hi (inclusive), sorted."""
    return sorted(m for m in range(lo, hi + 1) if m % 12 in pcs)


def root_in(ch, lo):
    return lo + (ch["root"] - lo) % 12


# ── the designs: every element's patch, with its reason (shown on the page) ────────────────────────────────────────
REESE = dict(osc1__wave="saw", osc1__voices=2, osc1__detune=24, osc1__width=0, osc1__level=0.75, osc2__wave="sine",
             osc2__octave=-1, osc2__voices=1, osc2__fine=0, osc2__level=0.55, filter__cutoff=750, filter__res=0.2,
             filter__env=0.6, filter__keytrack=0.3, fenv__attack=60, fenv__decay=900, fenv__sustain=0.4,
             amp__attack=25, amp__decay=500, amp__sustain=0.9, amp__release=250, voice__mode="legato",
             voice__glide=70, velocity=0.3, volume=-12.5)
BELL = dict(algorithm=5, feedback=0, op1__ratio=1, op1__level=1, op1__attack=1, op1__decay=2600, op1__sustain=0,
            op1__release=1600, op2__ratio=3.5, op2__level=0.42, op2__attack=1, op2__decay=650, op2__sustain=0,
            op2__release=400, op3__ratio=2, op3__fine=7, op3__level=0.5, op3__attack=1, op3__decay=1900, op3__sustain=0,
            op3__release=1200, op4__ratio=5, op4__level=0.18, op4__attack=1, op4__decay=380, op4__sustain=0,
            op4__release=300, volume=-9)
PLUCK = dict(osc1__table="harmonics", osc1__position=0.55, osc1__posenv=-0.3, osc1__voices=2, osc1__detune=10,
             osc2__level=0, filter__cutoff=3800, filter__env=1.2, fenv__decay=260, fenv__sustain=0, amp__attack=1,
             amp__decay=520, amp__sustain=0.15, amp__release=220, volume=-7)
LEAD = dict(osc1__wave="saw", osc1__voices=3, osc1__detune=14, osc1__width=20, osc1__level=0.75, osc2__wave="square",
            osc2__octave=0, osc2__fine=0, osc2__level=0.2, filter__cutoff=2600, filter__res=0.1, filter__env=0.4,
            fenv__decay=400, fenv__sustain=0.6, amp__attack=18, amp__decay=300, amp__sustain=0.8, amp__release=260,
            voice__mode="legato", voice__glide=40, vibrato__depth=0.35, vibrato__rate=5.4, velocity=0.4, volume=-15)
SAW = dict(osc1__wave="saw", osc1__voices=5, osc1__detune=32, osc1__width=45, osc1__level=0.7, osc2__wave="saw",
           osc2__octave=1, osc2__voices=5, osc2__detune=24, osc2__width=40, osc2__fine=0, osc2__level=0.3, noise=0.02,
           filter__cutoff=6500, filter__res=0.12, filter__env=1.2, filter__keytrack=0.2, fenv__attack=2,
           fenv__decay=260, fenv__sustain=0.55, amp__attack=2, amp__decay=700, amp__sustain=0.8, amp__release=220,
           voice__bend=2, velocity=0.4, volume=-13)
SUB = dict(osc1__wave="sine", osc1__level=0.9, osc2__level=0, filter__cutoff=300, filter__res=0, filter__env=0,
           amp__attack=2, amp__decay=300, amp__sustain=0.9, amp__release=60, voice__mode="mono", voice__glide=0,
           velocity=0.3, volume=-11)
MID = dict(osc1__wave="saw", osc1__voices=3, osc1__detune=18, osc1__level=0.8, osc2__wave="square", osc2__octave=0,
           osc2__fine=0, osc2__level=0.4, filter__cutoff=1400, filter__res=0.25, filter__env=1.0, fenv__decay=180,
           fenv__sustain=0.35, amp__attack=3, amp__decay=300, amp__sustain=0.8, amp__release=80, voice__mode="mono",
           velocity=0.4, volume=-14)
NOISE = dict(osc1__level=0, osc2__level=0, noise=1, filter__cutoff=300, filter__res=0.2, filter__env=0,
             filter__keytrack=0, amp__attack=20, amp__decay=100, amp__sustain=1, amp__release=40, velocity=0, volume=-12)


class Clip:
    """A section's script: its elements (name → strip, recipe) for the solos and the page."""
    def __init__(self, title, length):
        self.S, self.length, self.elements = Script(), length, []
        self.S.line(f"# {title} (arstro.solaris.sound.design futurebass.py) — D major, {BPM} bpm, half time")
        self.S.line(f'project new clip.slp --bpm {BPM} --name "{title}"')

    def element(self, kind, name, recipe, params):
        ch, dv, pt = self.S.instrument(kind, name, self.length)
        self.S.set(dv, **params)
        self.elements.append({"name": name, "strip": ch, "recipe": recipe})
        return ch, dv, pt

    def drums(self, name, recipe, params, hits):
        ch, dv, pt = self.element("drums", name, recipe, params)
        self.S.notes(pt, hits)
        return ch, dv

    def space(self, ch, reverb=None, delay=None):
        """The element's own space, in its rack, so its solo is the element as designed."""
        if delay:
            self.S.device(ch, "delay", time=num(0.75 * SPB * 1000), feedback=delay[1], tone=3500, mix=delay[0])
        if reverb:
            self.S.device(ch, "reverb", decay=reverb[1], size=reverb[2] if len(reverb) > 2 else 120, mix=reverb[0],
                          lowcut=300, highcut=11000, width=0.9)

    def duck(self, ch, under, amount, release=None):
        """A side-chain (`strip duck`); its release shortened where the element must come back before the next kick
        (measured: at the default half beat the kick's tail held the sub down ~600 ms, never recovering)."""
        self.S.line(f"strip duck {ch} --under {under} --amount {amount}")
        dv = self.S.new('dv')
        if release:
            self.S.set(dv, release=release)

    def finish(self, out):
        S = self.S
        S.line("device add master --type limiter")
        S.set(S.new('dv'), gain=2, ceiling=-1.0, truePeak="on")
        S.line("project save")
        S.line(f"render --out {out}.mp3 --format mp3 --kbps 192 --stems " + ",".join(e["strip"] for e in self.elements))
        S.line(f"render --out {out}.wav --stems " + ",".join(e["strip"] for e in self.elements))
        return "\n".join(S.L) + "\n"


def mod_lane(notes, depth=0.8):
    """A late vibrato on held notes: the mod wheel 0 at the onset, rising after ~0.3 s (the synth's vibrato sounds only
    with the wheel up)."""
    pts = [(0, 0)]
    for t, d, *_ in notes:
        if sec(d) >= 0.5:
            pts += [(t, 0), (t + beats(min(0.3, 0.4 * sec(d))), 0), (t + d - beats(0.05), depth), (t + d, 0)]
    return pts


# ── the verse ───────────────────────────────────────────────────────────────────────────────────────────────────────
def verse(out):
    lead_in = 4
    chords, mel = part("verse", lead_in)
    n = len(chords) * 8
    C = Clip("Future bass — verse", lead_in + n + 8)
    S = C.S
    # light drums: the kick on 1, a snap on 3; hats come in for the second half
    C.drums("Kick", f"the kick: tuned to A1 (55 Hz, the key's 5th: tune {KICK_TUNE}), 380 ms, on 1 — the verse breathes",
            dict(kick__tune=KICK_TUNE, kick__decay=380, kick__tone=0.6, volume=-4),
            [(KICK, lead_in + 4 * b, .25, 96) for b in range(n // 4)])
    snap, _ = C.drums("Snap", "a rim snap with a soft clap on 3 (half time), a short room",
                      dict(rim__tune=5, rim__decay=70, rim__tone=0.8, clap__decay=260, clap__tone=0.6, volume=3),
                      [(RIM, lead_in + 4 * b + 2, .25, 92) for b in range(n // 4)] +
                      [(CLAP, lead_in + 4 * b + 2, .25, 58) for b in range(n // 4)])
    C.space(snap, reverb=(0.22, 1300, 80))
    hats = [(CHAT, lead_in + 32 + 0.5 * k, .2, 54 if k % 2 == 0 else 40) for k in range(64)]
    hats += [(CHAT, lead_in + n - 1 + 0.125 * k, .1, 40 + 6 * k) for k in range(8)]          # a 32nd roll into the drop
    hh, _ = C.drums("Hats", "closed hats on eighths from bar 9, accents on the beat, a 32nd roll at the end",
                    dict(chat__tune=3, chat__decay=45, chat__tone=0.75, chat__pan=-0.15, volume=1), hats)
    C.space(hh, reverb=(0.12, 900, 60))
    # the reese: two saws ±12 cents beating against each other, a sine an octave under, low-passed and moving
    reese, rdv, rpt = C.element("synth", "Reese bass",
                                "two saws ±12 ct beating (the reese), a sine an octave under (the sub), low-pass 750 → 1300 "
                                "Hz opening through the verse, a slow phaser (0.12 Hz), soft saturation, mono under 140 Hz",
                                REESE)
    rn = []
    for i, ch in enumerate(chords):
        t, r = lead_in + 8 * i, root_in(ch, 38)
        nxt = root_in(chords[(i + 1) % len(chords)], 38)
        rn += [(r, t, 6, 92), (r + 7 if abs(nxt - r) > 2 else r + 12, t + 6, 1.5, 84)]  # root held, a lift into the next
    rn = [(47, 0, lead_in - 0.25, 80)] + rn                       # the lead-in bar: B2, the verse's vi
    S.notes(rpt, rn)
    S.device(reese, "phaser", rate=0.12, center=600, depth=1.5, stages=4, feedback=0.4, stereo=60, mix=0.4)
    S.device(reese, "saturator", drive=8, curve='"soft"', mix=0.5, output=-3)
    S.device(reese, "imager", width=110, mono="on", frequency=140)
    S.automate(f"{rdv}.filter.cutoff", [(0, 650, "smooth"), (lead_in + n, 1300, "hold")])
    C.duck(reese, "ch_2", 4, release=120)                          # the kick (ch_2) cuts through
    # the airy bell: FM — a 3.5 ratio's clang over the fundamental, an octave's shimmer 7 cents sharp; high-passed,
    # delayed, a big room
    bell, bdv, bpt = C.element("fm", "Airy bell",
                               "FM bell (algorithm 5): a 3.5-ratio modulator's clang fading in 650 ms over the "
                               "fundamental, an octave pair 7 cents sharp for shimmer; high-passed at 600 Hz, a dotted-8th "
                               "echo, a 4.5 s room — arpeggiating the chord above everything (D6–D7)", BELL)
    bn = []
    for i, ch in enumerate(chords):
        tones = place(ch["pcs"], 86, 98)
        t = lead_in + 8 * i
        pattern = [(0, 0), (1.5, 1), (3, 2), (4.5, 3), (6, 2)] if i >= 4 else [(0, 0), (1.5, 1), (3, 2)]
        bn += [(tones[min(k, len(tones) - 1)], t + a, 1.5, 70 if a == 0 else 58) for a, k in pattern]
    S.notes(bpt, bn)
    S.device(bell, "filter", mode='"highpass"', cutoff=600, res=0.1, mix=1)
    C.space(bell, reverb=(0.42, 4500, 180), delay=(0.28, 0.42))
    # the main synth: a pluck for the attack, a soft saw lead for the sustain — the melody, an octave up
    mel = [(t, d, p + 12, s) for t, d, p, s in mel]
    mbus = S.bus("Main synth")
    for name, kind, params, recipe in (
            ("Main pluck", "wavetable", PLUCK, "the attack: a harmonics wavetable sweeping down, plucked (520 ms)"),
            ("Main lead", "synth", LEAD, "the sustain: three saws ±7 ct and a square, legato, a late vibrato")):
        ch, dv, pt = S.instrument(kind, name, C.length)
        S.set(dv, **params)
        S.notes(pt, [(p, t, max(0.1, d - 0.05), 104 if s else 90) for t, d, p, s in mel])
        if name == "Main lead":
            S.expr(pt, "mod", mod_lane(mel))
        S.line(f"route {ch} --to {mbus}")
    S.device(mbus, "chorus", rate=0.6, depth=3, delay=12, mix=0.3)
    C.space(mbus, reverb=(0.25, 2400, 130), delay=(0.16, 0.3))
    S.line(f"set {mbus}.gain=6")                                 # measured: it must lead the verse
    C.elements.append({"name": "Main synth", "strip": mbus,
                       "recipe": "a pluck (the attack) layered with a soft saw lead (the sustain, a late vibrato), "
                                 "chorused, a dotted-8th echo, a 2.4 s room — the melody, an octave up"})
    return C, {"kick": "ch_2", "bass": reese}


# ── the build-up ────────────────────────────────────────────────────────────────────────────────────────────────────
def build(out):
    lead_in, bars = 4, 8
    chords, mel = part("pre", lead_in)
    land = lead_in + 32                                            # the drop's first beat
    C = Clip("Future bass — build-up", land + 8)
    S = C.S
    # kick: on 1, then quarters, eighths, a roll — stopping for the breath
    kicks = [(KICK, lead_in + 4 * b, .25, 92) for b in range(4)]
    kicks += [(KICK, lead_in + 16 + k, .25, 96) for k in range(8)]
    kicks += [(KICK, lead_in + 24 + 0.5 * k, .25, 100) for k in range(8)]
    kicks += [(KICK, lead_in + 28 + 0.25 * k, .2, 104 + 2 * k) for k in range(8)]
    kicks += [(KICK, land, .25, 124)]
    C.drums("Kick", "the kick: on 1, then quarters (bar 5), eighths (bar 7), sixteenths — a roll into the breath",
            dict(kick__tune=KICK_TUNE, kick__decay=380, kick__tone=0.65, volume=-5), kicks)
    # the snare roll: quarters → eighths → sixteenths → thirty-seconds, its velocity rising and its pitch climbing an
    # octave; claps on 3; then nothing until one big clap on the last eighth
    roll = [(SNARE, lead_in + 4 * b + 2, .25, 90) for b in range(2)]
    roll += [(SNARE, lead_in + 8 + k, .25, 70 + 3 * k) for k in range(8)]
    roll += [(SNARE, lead_in + 16 + 0.5 * k, .2, 80 + k) for k in range(16)]
    roll += [(SNARE, lead_in + 24 + 0.25 * k, .15, 96 + k) for k in range(16)]
    roll += [(SNARE, lead_in + 28 + 0.125 * k, .1, 112 + k // 2) for k in range(16)]
    roll += [(CLAP, lead_in + 4 * b + 2, .25, 70) for b in range(6)] + [(CLAP, land - 0.5, .25, 127)]
    sn, sdv = C.drums("Snare roll", "a snare roll — quarters, eighths, sixteenths, 32nds — rising in velocity and an "
                      "octave in pitch; claps on 3; silence for a beat and a half; one big clap",
                      dict(snare__tune=1, snare__decay=200, snare__tone=0.6, clap__decay=420, clap__tone=0.55,
                           volume=2.5), roll)
    S.automate(f"{sdv}.snare.tune", [(0, 1, "hold"), (lead_in + 16, 1, "linear"), (lead_in + 30, 12, "hold")])
    C.space(sn, reverb=(0.25, 1800, 110))
    hats = [(CHAT, lead_in + 16 + 0.25 * k, .15, 40 + k // 2) for k in range(56)]
    hh, _ = C.drums("Hats", "sixteenth hats from bar 5, rising", dict(chat__tune=3, chat__decay=40, chat__tone=0.8,
                                                                     volume=-2), hats)
    C.space(hh, reverb=(0.15, 900, 60))
    # the reese under the first six bars, then out — the floor drops away
    reese, rdv, rpt = C.element("synth", "Reese bass", "the verse's reese under bars 1–6, then gone: the floor drops away",
                                REESE)
    S.notes(rpt, [(45, 0, lead_in - 0.25, 80)] +                   # the lead-in bar: A2, the verse's last chord (V)
            [(root_in(ch, 38), lead_in + 8 * i, 7.75, 92) for i, ch in enumerate(chords[:3])])
    S.device(reese, "saturator", drive=8, curve='"soft"', mix=0.5, output=-3)
    S.device(reese, "imager", width=110, mono="on", frequency=140)
    S.automate(f"{rdv}.filter.cutoff", [(0, 900, "smooth"), (lead_in + 20, 2400, "hold")])
    C.duck(reese, "ch_2", 4, release=120)
    # the chords: a filtered supersaw, its stabs quickening — halves, quarters, eighths, sixteenths — the filter opening
    saw, wdv, wpt = C.element("synth", "Chords", "the drop's supersaw, filtered: stabs quickening (halves, quarters, "
                              "eighths, sixteenths) as the low-pass opens 350 Hz → 7 kHz", dict(SAW, volume=-18))
    stabs = []
    for t0, t1, step in ((0, 8, 2), (8, 16, 1), (16, 24, 0.5), (24, 30, 0.25)):
        t = t0
        while t < t1:
            ch = chord_at(chords, lead_in + t, lead_in)
            stabs += [(p, lead_in + t, step * 0.8, 70 + int(40 * t / 30)) for p in place(ch["pcs"], 60, 72)]
            t += step
    stabs += [(p, land, 1.5, 120) for p in place(chord(songbook.PAPER["parts"]["chorus"]["chords"][0], KEY)["pcs"], 60, 74)]
    S.notes(wpt, stabs)
    S.automate(f"{wdv}.filter.cutoff", [(0, 350, "smooth"), (lead_in + 30, 7000, "hold"), (land, 7000, "hold")])
    S.device(saw, "imager", width=160, mono="on", frequency=180)
    C.space(saw, reverb=(0.2, 2200, 120))
    # the risers: white noise through an opening low-pass, louder; one saw note bent up two octaves
    nz, ndv, npt = C.element("synth", "Noise riser", "white noise through a low-pass opening 300 Hz → 12 kHz over the "
                             "last four bars, swelling in, cut for the breath", NOISE)
    S.notes(npt, [(60, lead_in + 16, 14, 100)])
    S.automate(f"{ndv}.filter.cutoff", [(0, 300, "hold"), (lead_in + 16, 300, "smooth"), (lead_in + 30, 12000, "hold")])
    S.automate(f"{ndv}.volume", [(0, -40, "hold"), (lead_in + 16, -40, "smooth"), (lead_in + 30, -8, "hold")])
    C.space(nz, reverb=(0.3, 2500, 150))
    pr, pdv, ppt = C.element("synth", "Pitch riser", "one supersaw note bent up two octaves over the last four bars",
                             dict(SAW, voice__bend=24, volume=-15, filter__cutoff=3000))
    S.notes(ppt, [(62, lead_in + 16, 14, 90)])
    S.expr(ppt, "bend", [(0, 0), (lead_in + 16, 0), (lead_in + 30, 1)])
    C.space(pr, reverb=(0.3, 2500, 150))
    # the main synth carries the pre-chorus; the bell answers in the first half
    mel = [(t, d, p + 12, s) for t, d, p, s in mel if t < land - 2]
    mbus = S.bus("Main synth")
    for name, kind, params in (("Main pluck", "wavetable", PLUCK), ("Main lead", "synth", LEAD)):
        ch, dv, pt = S.instrument(kind, name, C.length)
        S.set(dv, **params)
        S.notes(pt, [(p, t, max(0.1, min(d, land - 2 - t) - 0.05), 104 if s else 90) for t, d, p, s in mel])
        if name == "Main lead":
            S.expr(pt, "mod", mod_lane(mel))
        S.line(f"route {ch} --to {mbus}")
    S.device(mbus, "chorus", rate=0.6, depth=3, delay=12, mix=0.3)
    C.space(mbus, reverb=(0.25, 2400, 130), delay=(0.16, 0.3))
    S.line(f"set {mbus}.gain=6")
    C.elements.append({"name": "Main synth", "strip": mbus, "recipe": "the verse's main synth, carrying the pre-chorus"})
    bell, bdv, bpt = C.element("fm", "Airy bell", "the verse's bell, answering in the first half", BELL)
    bn = []
    for i, ch in enumerate(chords[:2]):
        tones = place(ch["pcs"], 86, 98)
        bn += [(tones[min(k, len(tones) - 1)], lead_in + 8 * i + a, 1.5, 64) for a, k in ((0, 0), (1.5, 1), (3, 2), (4.5, 3))]
    S.notes(bpt, bn)
    S.device(bell, "filter", mode='"highpass"', cutoff=600, res=0.1, mix=1)
    C.space(bell, reverb=(0.42, 4500, 180), delay=(0.28, 0.42))
    # the landing: an impact — a boom, a crash
    imp, _ = C.drums("Impact", "the landing: a crash", dict(crash__decay=2600, crash__tone=0.6, volume=-1),
                     [(CRASH, land, 1, 120)])
    C.space(imp, reverb=(0.3, 3000, 160))
    boom, bodv, bopt = C.element("808", "Boom", "an 808 boom on the drop's downbeat: punched down two octaves, 2.5 s",
                                 dict(punch=24, amp__decay=2500, click=0.4, drive=0.45, tone=2500, volume=-9.5))
    S.notes(bopt, [(38, land, 4, 120)])
    return C, {"kick": "ch_2", "bass": reese}


# ── the drop ────────────────────────────────────────────────────────────────────────────────────────────────────────
KICK_A = [0, 1.5, 2.75]                       # bar A: 1, the and of 2, the a of 3 (after the snare): the bounce
KICK_B = [0, 0.75, 2.5, 3.5]                  # bar B: 1, the a of 1, the and of 3, the and of 4 (pushing into bar A)


def drop(out):
    chords, mel = part("chorus", 0)
    n = len(chords) * 8                                            # 64 beats, 16 bars
    C = Clip("Future bass — drop", n + 4)
    S = C.S
    kicks = []
    for b in range(n // 4):
        kicks += [4 * b + a for a in (KICK_A if b % 2 == 0 else KICK_B)]
    C.drums("Kick", f"the kick: tuned to A1 (55 Hz), 300 ms, a punchier tone — 1, the and of 2, after the snare; then 1, "
            "the a of 1, the and of 3, the and of 4: the rhythm the bass plays too",
            dict(kick__tune=KICK_TUNE, kick__decay=300, kick__tone=0.7, volume=-4),
            [(KICK, t, .25, 124 if t % 4 == 0 else 112) for t in kicks])
    S.device("ch_2", "transient", attack=35, sustain=-10, output=0)
    sn, _ = C.drums("Snare + clap", "a snare a semitone up and a clap together on 3 (half time), one room for both",
                    dict(snare__tune=1, snare__decay=240, snare__tone=0.62, clap__decay=360, clap__tone=0.55,
                         clap__level=-2, volume=4),
                    [(SNARE, 4 * b + 2, .25, 118) for b in range(n // 4)] + [(CLAP, 4 * b + 2, .25, 110) for b in range(n // 4)]
                    + [(CLAP, 4 * b + 3.75, .25, 72) for b in (7, 15) if 4 * b < n])
    C.space(sn, reverb=(0.24, 1800, 110))
    # the percussion that fills between: hats with rolls, snaps, toms, a ride, crashes
    perc = []
    for b in range(n // 4):
        t = 4 * b
        if b < 8:                                                  # sixteenth hats, accented on the eighths
            perc += [(CHAT, t + 0.25 * k, .15, 74 if k % 2 == 0 else 46) for k in range(16)
                     if not (b % 4 == 3 and k >= 12)]
        else:                                                      # eighths with open hats between (the closed one chokes it)
            perc += [(CHAT, t + 0.5 * k, .15, 72) for k in range(8) if not (b % 4 == 3 and k >= 6)]
            perc += [(OHAT, t + 0.5 * k + 0.25, .2, 56) for k in range(8) if k % 2 == 1 and not (b % 4 == 3 and k >= 6)]
            perc += [(RIDE, t + k, .3, 50) for k in range(4)]
        if b % 4 == 3:                                             # a hat roll into the next phrase — triplets every 8 bars
            perc += ([(CHAT, t + 3 + k / 6, .1, 50 + 8 * k) for k in range(6)] if b % 8 == 7 else
                     [(CHAT, t + 3 + 0.125 * k, .1, 46 + 7 * k) for k in range(8)])
        perc += [(RIM, t + 1.25, .2, 74), (RIM, t + 3.25, .2, 66)]  # snaps between the kick and the snare
        if b % 2 == 1:                                             # a tom fill at the end of every second bar, after the snare
            perc += [(HTOM, t + 3, .25, 96), (MTOM, t + 3.25, .25, 92), (MTOM, t + 3.5, .25, 98), (LTOM, t + 3.75, .25, 104)]
        if b % 8 == 0:
            perc += [(CRASH, t, 1, 112)]
    pc, _ = C.drums("Percussion", "between the kick and the snare: sixteenth hats accented on the eighths (then eighths "
                    "with open hats and a ride in the second half), 32nd and triplet rolls into each phrase, rim snaps on "
                    "the e of 2 and 4, a tom fill (high, mid, low) after every second snare, crashes on the phrase",
                    dict(chat__tune=3, chat__decay=45, chat__tone=0.75, chat__pan=-0.2, ohat__decay=260, ohat__pan=0.25,
                         rim__tune=6, rim__decay=60, rim__pan=0.35, htom__pan=-0.4, mtom__pan=0, ltom__pan=0.4,
                         ride__tune=4, ride__pan=0.45, crash__decay=2200, volume=-4), perc)
    C.space(pc, reverb=(0.16, 1200, 80))
    shaker, _ = C.drums("Shaker", "a shaker: a hat tuned up, 35 ms, on the off-sixteenths, right of centre",
                        dict(chat__tune=9, chat__decay=35, chat__tone=0.95, chat__pan=0.5, volume=-2),
                        [(CHAT, 4 * b + 0.25 * k, .1, 60 if k % 4 == 2 else 44) for b in range(n // 4) for k in range(16)
                         if k % 2 == 1 or k % 4 == 2])
    # the bass: a sub and a mid bass struck WITH every kick, on the chord's root, ducked by the kick
    bass_notes = []
    for i, t in enumerate(kicks):
        nxt = kicks[i + 1] if i + 1 < len(kicks) else n
        ch = chord_at(chords, t)
        r = root_in(ch, 30)                                        # F♯1 … F2 (46–87 Hz)
        if t % 16 == 15.5:                                         # into each phrase: the octave, a lift
            r += 12
        bass_notes.append((r, t, min(nxt - t, 2.0) - 0.05))
    sub, sdv, spt = C.element("synth", "Sub", "a sine sub, mono, on the chord's root WITH every kick (the kick's rhythm "
                              "is the bassline's), ducked 14 dB by the kick and back within ~200 ms — the kick's punch first, then the sub", SUB)
    S.notes(spt, [(p, t, d, 110) for p, t, d in bass_notes])
    C.duck(sub, "ch_2", 14, release=70)
    mid, mdv, mpt = C.element("synth", "Mid bass", "the bass's voice an octave up: three saws and a square, low-passed "
                              "1.4 kHz with a pluck of filter, tube-driven, high-passed at 160 Hz so the sub owns the low "
                              "end; ducked 14 dB by the kick", dict(MID, volume=-13))
    S.notes(mpt, [(p + 12, t, d, 104) for p, t, d in bass_notes])
    S.device(mid, "saturator", drive=10, curve='"tube"', mix=0.6, output=-4)
    S.device(mid, "filter", mode='"highpass"', cutoff=160, res=0.1, mix=1)
    S.device(mid, "chorus", rate=0.5, depth=2, delay=10, mix=0.2)
    C.duck(mid, "ch_2", 14, release=70)
    # the supersaw: the chorus melody on top of every stab, the chord's tones under it; a pitch rise into each stab;
    # a sixteenth tremolo (the future bass 'wub'); wide, mono low; ducked by the kick and the snare
    saw, wdv, wpt = C.element("synth", "Supersaw", "two saw stacks (5 voices ±16 ct, 45 ct wide; 5 voices an octave up), "
                              "the melody on top of every chord stab, a pitch rise of a semitone into each stab (70 ms), "
                              "a sixteenth tremolo on held chords (the wub), saturated, 160 % wide and mono under 180 "
                              "Hz, pumped 12 dB by the kick (back in ~250 ms) and 5 by the snare", dict(SAW, volume=-8.5))
    stabs, bend = [], [(0, 0)]
    for t, d, p, s in mel:
        ch = chord_at(chords, t)
        under = [q for q in range(p - 11, p) if q % 12 in ch["pcs"] and q % 12 != p % 12]
        voice = [p] + sorted(under, reverse=True)[:3]
        stabs += [(q, t, max(0.2, d - 0.1), 118 if s else 104) for q in voice]
        bend += ([(t - 0.02, 0)] if t >= 0.02 else []) + [(t, -0.5), (t + beats(0.07), 0)]
    S.notes(wpt, stabs)
    S.expr(wpt, "bend", bend)
    S.device(saw, "shaper", rate='"1/16"', shape='"square"', depth=0.45, smooth=6)
    S.device(saw, "saturator", drive=5, curve='"soft"', mix=0.5, output=-2)
    S.device(saw, "imager", width=160, mono="on", frequency=180)
    C.space(saw, reverb=(0.18, 2200, 120))
    C.duck(saw, "ch_2", 12, release=120)
    C.duck(saw, sn, 5, release=80)
    # the impact on the first beat, and a reversed cymbal into the second phrase
    boom, _, bopt = C.element("808", "Impact", "the drop's downbeat: an 808 boom punched down two octaves (2.5 s) — and "
                              "a reversed-cymbal swell (noise, 1.2 s attack) into bar 9",
                              dict(punch=24, amp__decay=2500, click=0.4, drive=0.45, tone=2500, volume=-11))
    S.notes(bopt, [(38, 0, 4, 120)])
    rev, rvdv, rvpt = C.element("synth", "Reverse cymbal", "noise, a 1.2 s attack, high-passed: a cymbal backwards into "
                                "bar 9", dict(NOISE, filter__mode="highpass", filter__cutoff=2500, amp__attack=1200,
                                              amp__release=20, volume=-16))
    S.notes(rvpt, [(60, 32 - beats(1.2), beats(1.2), 110)])
    C.space(rev, reverb=(0.3, 2000, 140))
    return C, {"kick": "ch_2", "bass": sub}


SECTIONS = {"verse": verse, "build": build, "drop": drop}
TITLES = {"verse": "Verse", "build": "Build-up", "drop": "Drop"}
WANT = {"verse": "a reese bass, an airy bell, the main synth", "build": "rising to the drop: rolls, risers, a breath",
        "drop": "a supersaw, the bass and the kick in sync, percussion filling between"}


def read_wav(path):
    w = wave.open(path)
    sr, ch, sw, n = w.getframerate(), w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n)
    import array
    if sw == 3:
        b = memoryview(raw)
        vals = [int.from_bytes(b[i:i + 3], "little", signed=True) for i in range(0, len(raw), 3)]
    else:
        vals = array.array("h" if sw == 2 else "i", raw).tolist()
    mono = [sum(vals[i:i + ch]) / ch for i in range(0, len(vals), ch)]
    return sr, mono


def duck_depth(path, kick_beats):
    """How far the kick ducks the bass and how soon it is back: its level 5–35 ms after each kick against 200–280 ms
    after, in dB (negative: ducked at the kick, back up before the next)."""
    sr, x = read_wav(path)
    rms = lambda a, b: math.sqrt(sum(v * v for v in x[a:b]) / max(1, b - a)) + 1e-9
    ds = []
    for kb in kick_beats:
        k = int(sec(kb) * sr)
        if k + int(0.25 * sr) < len(x):
            ds.append(20 * math.log10(rms(k + int(0.005 * sr), k + int(0.035 * sr)) / rms(k + int(0.2 * sr), k + int(0.28 * sr))))
    return round(sum(ds) / len(ds), 1) if ds else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--only", default="verse,build,drop")
    ap.add_argument("--cc", default=DEFAULT_CC)
    ap.add_argument("--no-render", action="store_true")
    a = ap.parse_args()
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, SOLARIS_SETTINGS=os.path.join(out, ".settings.txt"), SOLARIS_RECENTS=os.path.join(out, ".recents"))
    env.pop("DISPLAY", None)
    items, failed = [], 0
    path = os.path.join(out, "batch.json")
    old = {}
    if os.path.exists(path):
        with open(path) as f:
            old = {m["id"]: m for m in json.load(f).get("melodies", [])}
    for name in ("verse", "build", "drop"):
        if name not in a.only.split(","):
            if name in old:
                items.append(old[name])
            continue
        C, keys = SECTIONS[name](name)
        text = C.finish(name).replace("project new clip.slp", f"project new {name}.slp")
        with open(os.path.join(out, name + ".txt"), "w") as f:
            f.write(text)
        item = {"id": name, "title": f"{TITLES[name]} — {WANT[name]}", "sound": True, "section": name,
                "style": "future-bass", "style_label": "Future bass sound design", "bpm": BPM, "key": KEY.name,
                "form_label": f"sound — {name}", "devices": [f"{e['name']}: {e['recipe']}" for e in C.elements],
                "elements": C.elements, "seconds": round(sec(C.length)),
                "audio": [{"label": "full", "file": f"{name}.mp3"}] +
                         [{"label": e["name"].lower(), "file": f"{name}.{e['strip']}.mp3"} for e in C.elements],
                "metrics": {}}
        if not a.no_render:
            for stale in [name + ".slp"]:
                if os.path.exists(os.path.join(out, stale)):
                    os.remove(os.path.join(out, stale))
            r = subprocess.run([a.cc, "--script", name + ".txt"], cwd=out, env=env, capture_output=True, text=True)
            ok = r.returncode == 0 and os.path.exists(os.path.join(out, name + ".mp3"))
            failed += not ok
            print(f"{name}: {'ok' if ok else 'FAILED'}" + ("" if ok else f"\n{r.stdout[-1500:]}{r.stderr[-600:]}"), flush=True)
            if ok:
                lufs = {"mix": loudness(os.path.join(out, name + ".wav"))}
                for e in C.elements:
                    lufs[e["name"]] = loudness(os.path.join(out, f"{name}.{e['strip']}.wav"))
                item["metrics"]["lufs"] = lufs
                if name == "drop":
                    kicks = [4 * b + x for b in range(16) for x in (KICK_A if b % 2 == 0 else KICK_B)]
                    item["metrics"]["sub_duck_db"] = duck_depth(os.path.join(out, f"{name}.{keys['bass']}.wav"), kicks)
                print("   LUFS " + " · ".join(f"{k} {v}" for k, v in lufs.items()) +
                      (f" · the kick ducks the sub {item['metrics'].get('sub_duck_db')} dB" if name == "drop" else ""),
                      flush=True)
                for f in os.listdir(out):                          # the WAVs were for measuring only
                    if f.startswith(name + ".") and f.endswith(".wav"):
                        os.remove(os.path.join(out, f))
        items.append(item)
    with open(path, "w") as f:
        json.dump({"batch": os.path.basename(out), "kind": "sound", "melodies": items}, f, indent=1, ensure_ascii=False)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
