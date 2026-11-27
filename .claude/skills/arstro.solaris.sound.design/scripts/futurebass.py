#!/usr/bin/env python3
"""futurebass.py — whole future bass SONGS on the analog synth, for the approver to judge in the Sound Lab.

    python3 futurebass.py --check                                   # the writing, before a note is rendered
    python3 futurebass.py --out ~/Music/Solaris/sound-lab/songs-NNN [--songs sky,wildfire] [--stems] [--quick]

A song (SONGS) is its own music — key, tempo, chords, a verse melody, a drop hook (call, response, last) with its
counter-melody, a build line — and a small DESIGN (which instrument sings the verse, which plays the chords, how the
supersaw moves). One form, so two songs differ in themselves:

    intro 8 · verse 16 · build 8 · drop 16 · break 8 · build 8 · drop 16 (bigger) · outro 8      (bars, half time)

Every pitched synth sound is the DSP library's ANALOG synth (registry `analog`; its math: core/DigitalSignalProcessing/
src/instrument/README.md ## AnalogSynth). The patches and why: references/analog-patches.md. The drums are the Drum
Machine, the bell the FM synth, the keys the piano or the guitar, the chops the analog synth through the vowel filter.

The mix is MEASURED, not guessed: a first render, every element's loudness alone in the section it matters in, each
fader set to its target (TARGETS), a second render. Then measured into batch.json: each section's loudness (the drop
must stand over the verse), the kick's hit over the mix, the supersaw's spectral centre, the kick's duck on the sub.

Runs solaris-cc with scratch settings in the output folder — never the user's own settings or recents. A song of
2:20 is two renders of ~40 strips' worth of oscillators at twice the sample rate: allow several minutes each.
"""
import argparse, json, math, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MEL = os.path.normpath(os.path.join(HERE, "..", "..", "arstro.solaris.compose.melody", "scripts"))
sys.path.insert(0, MEL)
from melody import Key, chord, surprise  # noqa: E402
from song import Script, num, loudness, DEFAULT_CC  # noqa: E402

KICK, RIM, SNARE, CLAP, LTOM, CHAT, MTOM, OHAT, HTOM, CRASH, RIDE = 36, 37, 38, 39, 41, 42, 45, 46, 48, 49, 51
NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
FORM = [("intro", "intro", 8), ("verse", "verse", 16), ("build", "build", 8), ("drop", "drop", 16),
        ("break", "break", 8), ("build 2", "build", 8), ("drop 2", "drop", 16), ("outro", "outro", 8)]


def midi(name):
    i, acc = 1, 0
    while name[i] in '#b':
        acc += 1 if name[i] == '#' else -1
        i += 1
    return 12 * (int(name[i:]) + 1) + NOTE[name[0]] + acc


def seq(text, start=0):
    """A melody in order: 'A5:1.5 G5:.5 r:1 …' — a note (or r, a rest) and its length in beats; '|' is a bar line for
    the eye. Returns ([(beat, length, midi)], total length)."""
    out, t = [], start
    for tok in text.replace("|", " ").split():
        name, d = tok.split(":")
        d = float(d)
        if name != "r":
            out.append((t, d, midi(name)))
        t += d
    return out, t - start


# ── the songs: original music, each its own ────────────────────────────────────────────────────────────────────────
# A line is 16 beats (4 bars) over two chords (8 beats each). The drop's lines leave the last two beats of every
# second bar empty: the counter-melody (a vocal chop) answers there.
SONGS = {
    "sky": dict(
        title="Hold the Sky", feel="emotional and wide — Illenium", tonic="E", mode="minor", bpm=150,
        verse_chords=["i7", "VImaj7", "IIIadd9", "VII"] * 2, build_chords=["VImaj7", "VII", "i7", "VII"],
        drop_chords=["VImaj7", "VII", "i7", "III", "VImaj7", "VII", "i7", "i7"],
        verse=["r:1 B4:.5 D5:.5 E5:1.5 D5:.5 | B4:2 r:1 B4:.5 D5:.5 | E5:1.5 G5:.5 E5:1 D5:1 | E5:3 r:1",
               "r:1 B4:.5 D5:.5 G5:1.5 F#5:.5 | D5:2 r:1 D5:.5 E5:.5 | F#5:1.5 A5:.5 F#5:1 E5:1 | D5:3 r:1",
               "r:1 B4:.5 D5:.5 E5:1.5 D5:.5 | B4:2 r:1 B4:.5 D5:.5 | E5:1.5 G5:.5 B5:1 A5:1 | G5:3 r:1",
               "r:1 G5:.5 A5:.5 B5:1.5 A5:.5 | G5:2 r:1 E5:.5 G5:.5 | F#5:1.5 E5:.5 D5:1 E5:1 | F#5:2 r:2"],
        call="E5:.75 E5:.75 G5:1 E5:.5 B5:1 | A5:.5 G5:1.5 r:2 | F#5:.75 F#5:.75 A5:1 F#5:.5 D6:1 | B5:.5 A5:1.5 r:2",
        response="G5:.75 G5:.75 B5:1 G5:.5 E6:1 | D6:.5 B5:1.5 r:2 | D6:.75 B5:.75 A5:1 G5:.5 A5:1 | B5:2 r:2",
        last="G5:.75 G5:.75 B5:1 G5:.5 E6:1 | D6:.5 E6:1.5 r:2 | B5:.75 A5:.75 G5:1 E5:.5 G5:1 | E5:4",
        counter_call="r:6 B4:.5 C5:.5 E5:1 r:6 A4:.5 D5:.5 F#5:1",
        counter_response="r:6 B4:.5 D5:.5 E5:1 r:6 D5:.5 G5:.5 B5:1",
        counter_last="r:6 E5:.5 D5:.5 B4:1 r:8",
        build_rise="B4:2 D5:2 | E5:2 G5:2 | A5:2 B5:2 | D6:2 r:2",
        design=dict(main="chop", keys="piano", bend=(-1, 0.06), sway=None, perc="full",
                    kicks=([0, 1.5, 2.75], [0, 0.75, 2.5, 3.5])),
        sound=["verse: piano chords, a vocal chop singing the line, a deep reese and a soft bell from its second half",
               "drop: a three-layer supersaw (a small rise into each stab), a sub and mid bass on every kick"]),
    "sunday": dict(
        title="Sunday Lights", feel="warm and bouncy, a guitar — San Holo", tonic="A", mode="major", bpm=150,
        verse_chords=["Imaj7", "iii7", "vi7", "IVmaj7"] * 2, build_chords=["IVmaj7", "V", "vi7", "V"],
        drop_chords=["IVmaj7", "V", "vi7", "Iadd9", "IVmaj7", "V", "vi7", "V"],
        verse=["C#5:1 E5:1 A5:1.5 G#5:.5 | E5:3 r:1 | E5:1 G#5:1 B5:1.5 G#5:.5 | E5:3 r:1",
               "C#5:1 E5:1 A5:1.5 F#5:.5 | E5:2 C#5:1 r:1 | D5:1 F#5:1 A5:1.5 F#5:.5 | E5:3 r:1",
               "C#5:1 E5:1 A5:1.5 G#5:.5 | E5:3 r:1 | E5:1 G#5:1 B5:1.5 C#6:.5 | B5:3 r:1",
               "A5:1.5 G#5:.5 F#5:1 E5:1 | C#5:3 r:1 | D5:1 E5:1 F#5:1 A5:1 | F#5:2 r:2"],
        call="F#5:1.5 A5:1.5 C#6:1 | B5:1 A5:1 r:2 | G#5:1.5 B5:1.5 E6:1 | C#6:1 B5:1 r:2",
        response="E6:1.5 C#6:1.5 A5:1 | B5:1 C#6:1 r:2 | C#6:1.5 B5:1.5 A5:1 | A5:2 r:2",
        last="E6:1.5 C#6:1.5 A5:1 | B5:1 C#6:1 r:2 | B5:1.5 C#6:1.5 E6:1 | B5:4",
        counter_call="r:6 A4:.5 D5:.5 F#5:1 r:6 B4:.5 E5:.5 G#5:1",
        counter_response="r:6 C#5:.5 E5:.5 F#5:1 r:6 E5:.5 C#5:.5 A4:1",
        counter_last="r:6 A4:.5 C#5:.5 E5:1 r:8",
        build_rise="A4:2 C#5:2 | E5:2 F#5:2 | G#5:2 B5:2 | E6:2 r:2",
        design=dict(main="guitar", keys="guitar", bend=(-2, 0.12), sway=("1/8", 0.35), perc="light",
                    kicks=([0, 2.5], [0, 1.5, 2.75])),
        sound=["verse: a picked guitar on the chords and a second guitar singing the line, a pad, a reese, a bell",
               "drop: the supersaw scooped up 2 semitones into each long chord and swaying at 1/8; lighter percussion"]),
    "wildfire": dict(
        title="Wildfire Hearts", feel="bright, staccato, a pop hook — Martin Garrix", tonic="D", mode="major", bpm=140,
        verse_chords=["I", "V", "vi7", "IVmaj7"] * 2, build_chords=["IVmaj7", "I", "vi7", "V"],
        drop_chords=["IVmaj7", "I", "V", "vi7", "IVmaj7", "I", "V", "V"],
        verse=["F#5:.5 F#5:.5 A5:1 F#5:.5 E5:.5 D5:1 | E5:1 F#5:2 r:1 | E5:.5 E5:.5 A5:1 E5:.5 C#5:.5 E5:1 | C#5:1 E5:2 r:1",
               "D5:.5 D5:.5 F#5:1 D5:.5 C#5:.5 B4:1 | D5:1 F#5:2 r:1 | B4:.5 B4:.5 D5:1 B4:.5 A4:.5 G4:1 | B4:1 D5:2 r:1",
               "F#5:.5 F#5:.5 A5:1 F#5:.5 E5:.5 D5:1 | E5:1 F#5:2 r:1 | E5:.5 E5:.5 A5:1 E5:.5 A5:.5 C#6:1 | B5:1 A5:2 r:1",
               "D6:.5 D6:.5 B5:1 A5:.5 F#5:.5 D5:1 | F#5:1 B5:2 r:1 | B5:.5 B5:.5 A5:1 G5:.5 F#5:.5 D5:1 | E5:1 F#5:1 r:2"],
        call="B5:.5 B5:.5 D6:.75 B5:.75 A5:.5 B5:1 | D6:.75 B5:.75 G5:.5 r:2 | A5:.5 A5:.5 D6:.75 A5:.75 F#5:.5 A5:1 | D6:.75 A5:.75 F#5:.5 r:2",
        response="C#6:.5 C#6:.5 E6:.75 C#6:.75 B5:.5 C#6:1 | E6:.75 C#6:.75 A5:.5 r:2 | B5:.5 B5:.5 D6:.75 B5:.75 A5:.5 F#5:1 | A5:.75 B5:.75 D6:.5 r:2",
        last="C#6:.5 C#6:.5 E6:.75 C#6:.75 B5:.5 C#6:1 | E6:.75 C#6:.75 A5:.5 r:2 | E6:.5 E6:.5 C#6:.75 B5:.75 A5:.5 B5:1 | A5:4",
        counter_call="r:6 D5:.5 G5:.5 B5:1 r:6 A4:.5 D5:.5 F#5:1",
        counter_response="r:6 C#5:.5 E5:.5 A5:1 r:6 F#5:.5 D5:.5 B4:1",
        counter_last="r:6 E5:.5 C#5:.5 A4:1 r:8",
        build_rise="F#5:2 A5:2 | B5:2 D6:2 | C#6:2 E6:2 | A5:2 r:2",
        design=dict(main="pluck", keys="piano", bend=(0, 0), sway=None, perc="full",
                    kicks=([0, 1.75, 2.5], [0, 0.75, 2.5, 3.25])),
        sound=["verse: piano chords and a soft pluck-and-lead singing a pop line",
               "drop: short supersaw stabs on a sixteenth-syncopated hook, no pitch move — tight and bright"]),
    "aurora": dict(
        title="Aurora Drive", feel="euphoric, soaring long notes — Illenium / Said The Sky", tonic="Bb", mode="major", bpm=160,
        verse_chords=["IVmaj7", "V", "iii7", "vi7"] * 2, build_chords=["IVmaj7", "V", "IVmaj7", "V"],
        drop_chords=["IVmaj7", "V", "vi7", "iii7", "IVmaj7", "V", "Iadd9", "I"],
        verse=["r:1 G5:1 G5:1 Bb5:1 | G5:1.5 F5:.5 Eb5:1 r:1 | r:1 F5:1 F5:1 A5:1 | G5:1.5 F5:.5 C5:1 r:1",
               "r:1 F5:1 F5:1 A5:1 | F5:1.5 D5:.5 C5:1 r:1 | r:1 D5:1 D5:1 G5:1 | F5:1.5 G5:.5 D5:1 r:1",
               "r:1 G5:1 G5:1 Bb5:1 | C6:1.5 Bb5:.5 G5:1 r:1 | r:1 F5:1 F5:1 A5:1 | C6:1.5 Bb5:.5 A5:1 r:1",
               "r:1 F5:1 F5:1 A5:1 | C6:1.5 A5:.5 F5:1 r:1 | r:1 D5:1 G5:1 Bb5:1 | D6:2 r:2"],
        call="D6:1.5 Bb5:1 G5:.75 Bb5:.75 | C6:.5 D6:1.5 r:2 | C6:1.5 A5:1 F5:.75 A5:.75 | Bb5:.5 C6:1.5 r:2",
        response="D6:1.5 Bb5:1 G5:.75 Bb5:.75 | D6:.5 F6:1.5 r:2 | F6:1.5 D6:1 A5:.75 C6:.75 | D6:.5 C6:1.5 r:2",
        last="D6:1.5 Bb5:1 F5:.75 Bb5:.75 | C6:.5 D6:1.5 r:2 | F6:1.5 D6:1 C6:.75 Bb5:.75 | Bb5:4",
        counter_call="r:6 Bb4:.5 Eb5:.5 G5:1 r:6 A4:.5 C5:.5 F5:1",
        counter_response="r:6 Bb4:.5 D5:.5 G5:1 r:6 A4:.5 D5:.5 F5:1",
        counter_last="r:6 F5:.5 D5:.5 Bb4:1 r:8",
        build_rise="Eb5:2 G5:2 | Bb5:2 D6:2 | F5:2 A5:2 | C6:2 r:2",
        design=dict(main="pluck", keys="piano", bend=(-1, 0.06), sway=None, perc="full",
                    kicks=([0, 1.5, 2.75], [0, 0.75, 2.5, 3.5])),
        sound=["verse: piano over the royal-road chords (IV–V–iii–vi), a soft pluck-and-lead, a bell answering",
               "drop: long soaring supersaw notes with a syncopated tail; it ends home, on the tonic"]),
    "oceans": dict(
        title="Glass Oceans", feel="minor and driving, a leap in the hook — dark Illenium", tonic="D", mode="minor", bpm=146,
        verse_chords=["i7", "VII", "VImaj7", "VII"] * 2, build_chords=["i7", "VImaj7", "VImaj7", "VII"],
        drop_chords=["i7", "VImaj7", "IIIadd9", "VII", "i7", "VImaj7", "VII", "i7"],
        verse=["D5:.5 F5:.5 F5:1.5 E5:.5 D5:1 | D5:2 r:2 | C5:.5 E5:.5 E5:1.5 D5:.5 C5:1 | C5:2 r:2",
               "D5:.5 F5:.5 F5:1.5 E5:.5 D5:1 | F5:2 r:2 | E5:.5 G5:.5 G5:1.5 F5:.5 E5:1 | G5:2 r:2",
               "D5:.5 F5:.5 F5:1.5 E5:.5 D5:1 | D5:2 r:2 | C5:.5 E5:.5 G5:1.5 A5:.5 G5:1 | E5:2 r:2",
               "F5:.5 A5:.5 A5:1.5 G5:.5 F5:1 | D5:2 r:2 | G5:.5 Bb5:.5 C6:1.5 Bb5:.5 G5:1 | E5:2 r:2"],
        call="A5:.75 D6:.75 D6:.5 C6:1 A5:1 | G5:1 A5:1 r:2 | F5:.75 D6:.75 D6:.5 C6:1 A5:1 | Bb5:1 A5:1 r:2",
        response="A5:.75 C6:.75 C6:.5 A5:1 G5:1 | F5:1 G5:1 r:2 | G5:.75 E6:.75 E6:.5 D6:1 C6:1 | G5:1 C6:1 r:2",
        last="G5:.75 E6:.75 E6:.5 D6:1 C6:1 | D6:1 E6:1 r:2 | F6:.75 D6:.75 D6:.5 C6:1 A5:1 | D6:4",
        counter_call="r:6 A4:.5 D5:.5 F5:1 r:6 Bb4:.5 D5:.5 F5:1",
        counter_response="r:6 C5:.5 F5:.5 A5:1 r:6 G4:.5 C5:.5 E5:1",
        counter_last="r:6 E5:.5 G5:.5 C6:1 r:8",
        build_rise="D5:2 F5:2 | A5:2 D6:2 | E5:2 G5:2 | C6:2 r:2",
        design=dict(main="chop", keys="piano", bend=(-1, 0.05), sway=None, perc="full",
                    kicks=([0, 0.75, 1.5, 2.75], [0, 1, 2.5, 3.5])),
        sound=["verse: piano, a vocal chop on a minor line, the reese deeper under it",
               "drop: a hook that leaps a fourth and falls; a busier kick, the bass with it"]),
}

# ── the patches: the analog synth (registry `analog`, DSP AnalogSynth) — references/analog-patches.md says why ──
SUPER = dict(osc1__wave="saw", osc1__unison=7, osc1__detune=0.42, osc1__blend=0.75, osc1__spread=0.9, osc1__level=0.8,
             osc2__wave="saw", osc2__octave=1, osc2__unison=7, osc2__detune=0.5, osc2__blend=0.8, osc2__spread=1,
             osc2__level=0.45, sub__noise=0.04, filter__mode='"lowpass 24"', filter__cutoff=16000, filter__res=0.05,
             amp__attack=2, amp__decay=700, amp__sustain=0.85, amp__release=180, voice__drift=0.2, velocity=0.3,
             volume=-8)
TOP = dict(osc1__wave="saw", osc1__unison=3, osc1__detune=0.22, osc1__blend=0.5, osc1__spread=0.3, osc1__level=0.8,
           osc2__wave="pulse", osc2__width=0.35, osc2__octave=1, osc2__level=0.2, filter__mode='"lowpass 24"',
           filter__cutoff=11000, filter__res=0.1, amp__attack=2, amp__decay=500, amp__sustain=0.8, amp__release=160,
           voice__drift=0.15, velocity=0.3, volume=-12)
SUB = dict(osc1__wave="sine", osc1__level=1, filter__mode="off", amp__attack=2, amp__decay=300, amp__sustain=0.9,
           amp__release=60, voice__mode="mono", voice__phase="reset", voice__drift=0, velocity=0.3, volume=-12)
MID = dict(osc1__wave="saw", osc1__unison=3, osc1__detune=0.25, osc1__spread=0.4, osc1__level=0.8, osc2__wave="pulse",
           osc2__width=0.4, osc2__level=0.4, filter__mode='"lowpass 24"', filter__cutoff=700, filter__res=0.25,
           filter__drive=9, filter__env=2.2, fenv__attack=1, fenv__decay=220, fenv__sustain=0.3, amp__attack=3,
           amp__decay=300, amp__sustain=0.8, amp__release=80, voice__mode="mono", voice__phase="reset", voice__drift=0.1,
           velocity=0.4, volume=-11)
REESE = dict(osc1__wave="saw", osc1__level=0.7, osc2__wave="saw", osc2__fine=18, osc2__level=0.7, sub__level=0.7,
             filter__mode='"lowpass 24"', filter__cutoff=480, filter__res=0.15, filter__drive=6, filter__keytrack=0.3,
             lfo__shape="sine", lfo__rate=0.15, lfo__cutoff=0.5, amp__attack=25, amp__decay=500, amp__sustain=0.9,
             amp__release=250, voice__mode="legato", voice__glide=70, voice__drift=0.3, velocity=0.3, volume=-16.5)
PAD = dict(osc1__wave="saw", osc1__unison=5, osc1__detune=0.3, osc1__blend=0.6, osc1__spread=1, osc1__level=0.6,
           osc2__wave="triangle", osc2__level=0.3, filter__mode='"lowpass 12"', filter__cutoff=1200, amp__attack=500,
           amp__decay=800, amp__sustain=0.9, amp__release=1200, voice__drift=0.3, velocity=0.2, volume=-19)
PLUCK = dict(osc1__wave="saw", osc1__unison=3, osc1__detune=0.2, osc1__spread=0.5, osc1__level=0.8, osc2__wave="pulse",
             osc2__width=0.3, osc2__octave=1, osc2__level=0.25, filter__mode='"lowpass 24"', filter__cutoff=650,
             filter__res=0.2, filter__env=3.5, filter__keytrack=0.5, fenv__attack=1, fenv__decay=260, fenv__sustain=0.1,
             amp__attack=1, amp__decay=520, amp__sustain=0.15, amp__release=220, volume=-7)
LEAD = dict(osc1__wave="saw", osc1__unison=3, osc1__detune=0.2, osc1__spread=0.3, osc1__level=0.7,
            osc2__wave="triangle", osc2__level=0.3, filter__mode='"lowpass 24"', filter__cutoff=1900, filter__res=0.1,
            filter__keytrack=0.4, amp__attack=35, amp__decay=300, amp__sustain=0.8, amp__release=260,
            voice__mode="legato", voice__glide=40, lfo__shape="sine", lfo__rate=5.4, lfo__fade=350, lfo__pitch=0.18,
            velocity=0.4, volume=-15)
CHOP = dict(osc1__wave="saw", osc1__level=0.8, osc2__wave="pulse", osc2__level=0.3, sub__noise=0.03,
            filter__mode="off", amp__attack=3, amp__decay=160, amp__sustain=0.85, amp__release=80,
            voice__mode="legato", voice__glide=35, lfo__shape="sine", lfo__rate=5.6, lfo__fade=300, lfo__pitch=0.15,
            voice__drift=0.1, velocity=0.3, volume=-13.5)
ARP = dict(PLUCK, filter__cutoff=2200, filter__env=3, fenv__decay=160, amp__decay=220, amp__sustain=0, amp__release=120,
           volume=-6)
NOISE = dict(osc1__level=0, sub__noise=1, filter__mode='"lowpass 24"', filter__cutoff=300, filter__res=0.2,
             amp__attack=20, amp__decay=100, amp__sustain=1, amp__release=60, velocity=0, volume=-40)
BELL = dict(algorithm=5, feedback=0, op1__ratio=1, op1__fine=0, op1__level=1, op1__attack=1, op1__decay=1500,
            op1__sustain=0, op1__release=900, op2__ratio=4, op2__level=0.16, op2__attack=1, op2__decay=180,
            op2__sustain=0, op2__release=150, op3__ratio=4, op3__fine=0, op3__level=0.1, op3__attack=1, op3__decay=500,
            op3__sustain=0, op3__release=400, op4__ratio=1, op4__level=0, volume=-6)
GUITAR = dict(pluck__position=0.2, pluck__pick=4500, string__sustain=4, string__brightness=0.6, body=0.4, volume=6)


# Every element's loudness (LUFS, alone) in the section it matters in — the levelling pass sets the faders to these, so
# no song depends on a patch's luck. The sum lands the drop near −11 LUFS under a limiter that barely works, the verse
# ~5 LU below. The kick is the loudest thing in the drop: at −14.5 its hit stood only 5.4 dB over the mix (measured).
TARGETS = {"Pad": ("verse", -27), "Reese bass": ("verse", -23.5), "Bell": ("verse", -25.5),
           "Supersaw": ("drop", -14.5), "Kick": ("drop", -13), "Kick top": ("drop", -28), "Snare + clap": ("drop", -19.5),
           "Percussion": ("drop", -25.5), "Sub": ("drop", -20.5), "Mid bass": ("drop", -21), "Counter chop": ("drop", -21.5),
           "Impact": ("drop", -18), "Arp": ("drop2", -27)}


def place(pcs, lo, hi):
    return sorted(m for m in range(lo, hi + 1) if m % 12 in pcs)


def root_in(ch, lo):
    return lo + (ch["root"] - lo) % 12


def vel(t, base=100):
    """Accents by where a note falls: the beat strongest, the off-beat eighth next, the sixteenths softest."""
    return base + 8 if t % 1 == 0 else (base if t % 0.5 == 0 else base - 10)


class Song:
    """A song's music read once: key, the chord under every 8 beats, the sections' places, the melodies as notes."""
    def __init__(self, name):
        T = SONGS[name]
        self.name, self.T, self.D = name, T, T["design"]
        self.key, self.bpm = Key(T["tonic"], T["mode"]), T["bpm"]
        self.spb = 60 / self.bpm
        c = {s: [chord(r, self.key) for r in T[s + "_chords"]] for s in ("verse", "build", "drop")}
        home = chord("i" if T["mode"] == "minor" else "I", self.key)
        per = {"intro": c["verse"][:4], "verse": c["verse"], "build": c["build"], "drop": c["drop"],
               "break": c["verse"][4:], "outro": c["verse"][:3] + [home]}
        self.sections, self.slots, t, seen = [], [], 0, {}
        for label, kind, bars in FORM:
            seen[kind] = seen.get(kind, 0) + 1
            self.sections.append({"name": label, "kind": kind, "at": t, "bars": bars, "nth": seen[kind]})
            self.slots += per[kind]
            t += 4 * bars
        self.end = t
        self.length = t + 8                                          # the last chord rings two bars
        hz = lambda pc: min((440 * 2 ** ((m - 69) / 12) for m in range(24, 48) if m % 12 == pc), key=lambda f: abs(f - 52))
        f = min((hz(self.key.tonic), hz((self.key.tonic + 7) % 12)), key=lambda f: abs(f - 52))
        self.kick_hz, self.kick_tune = round(f, 1), round(12 * math.log2(f / 48), 2)

    def sec(self, beats):
        return beats * self.spb

    def beats(self, seconds):
        return seconds / self.spb

    def of(self, kind):
        return [s for s in self.sections if s["kind"] == kind]

    def chord(self, t):
        return self.slots[max(0, min(len(self.slots) - 1, int(t // 8)))]

    def lines(self, texts, start, shift=0):
        out, t = [], start
        for text in texts:
            notes, length = seq(text, t)
            assert abs(length - 16) < 1e-6, f"{self.name}: a line of {length} beats, not 16: {text}"
            out += [(a, d, p + shift) for a, d, p in notes]
            t += length
        return out

    def hook(self, at):
        T = self.T
        return self.lines([T["call"], T["response"], T["call"], T["last"]], at)

    def counter(self, at):
        T = self.T
        return self.lines([T["counter_call"], T["counter_response"], T["counter_call"], T["counter_last"]], at)

    def drop_kicks(self, at, bars=16):
        kA, kB = self.D["kicks"]
        return [at + 4 * b + a for b in range(bars) for a in (kA if b % 2 == 0 else kB)]


def arrange(song, trim=None):
    """The whole song as one solaris-cc script; returns (text, elements [{name, strip, recipe}], keys for measuring).
    `trim`: dB a strip, by element name — the levelling pass's correction (main)."""
    so, S, D, T = song, Script(), song.D, song.T
    L, E, gains = so.length, [], {}
    line = S.line
    line(f"# {T['title']} — future bass, {so.key.name}, {so.bpm} bpm half time (arstro.solaris.sound.design futurebass.py)")
    line("# " + " · ".join(f"{s['name']} {s['bars']}" for s in so.sections))
    line(f'project new {so.name}.slp --bpm {so.bpm} --name "{T["title"]}"')

    def element(kind, name, recipe, params, notes=None):
        ch, dv, pt = S.instrument(kind, name, L)
        S.set(dv, **params)
        if notes is not None:
            S.notes(pt, notes)
        E.append({"name": name, "strip": ch, "recipe": recipe})
        return ch, dv, pt

    def space(ch, reverb=None, delay=None):
        if delay:
            S.device(ch, "delay", time=num(0.75 * so.spb * 1000), feedback=delay[1], tone=3500, mix=delay[0])
        if reverb:
            S.device(ch, "reverb", decay=reverb[1], size=reverb[2] if len(reverb) > 2 else 120, mix=reverb[0],
                     lowcut=300, highcut=10000, width=0.9)

    def duck(ch, under, amount, release):
        line(f"strip duck {ch} --under {under} --amount {amount}")
        S.set(S.new('dv'), release=release)

    verse, brk = so.of("verse")[0], so.of("break")[0]
    builds, drops = so.of("build"), so.of("drop")
    intro, outro = so.of("intro")[0], so.of("outro")[0]
    V2 = verse["at"] + 32                                           # the verse's second half: the rhythm comes in

    # ── drums ──
    kicks = [(V2 + 4 * b + a, 92) for b in range(8) for a in (0, 2.5)]
    drop_k = []
    for b in builds:
        t = b["at"]
        kicks += [(t + 4 * k, 94) for k in range(4)] + [(t + 16 + k, 98) for k in range(8)]
        kicks += [(t + 24 + 0.5 * k, 102) for k in range(8)] + [(t + 28 + 0.25 * k, 106 + 2 * k) for k in range(8)]
    for d in drops:
        dk = so.drop_kicks(d["at"])
        drop_k += dk
        kicks += [(t, 124 if (t - d["at"]) % 4 == 0 else 112) for t in dk]
    kicks += [(outro["at"], 110)]
    kick, kdv, _ = element("drums", "Kick", f"tuned into the key ({so.kick_hz} Hz), 280 ms, a hard transient; with a click "
                         "layer on every drop hit", dict(kick__tune=so.kick_tune, kick__decay=280, kick__tone=0.8, volume=-4.5),
                         [(KICK, t, .25, v) for t, v in kicks])
    S.automate(f"{kdv}.volume", [(0, -15, "hold"), (builds[0]["at"], -4.5, "hold")])   # the verse's kick is soft (measured)
    S.device(kick, "transient", attack=55, sustain=-15, output=0)
    element("drums", "Kick top", "the click: the kick an octave up, 70 ms — the punch heard in a full drop",
            dict(kick__tune=min(12, so.kick_tune + 12), kick__decay=70, kick__tone=1.0, volume=-6),
            [(KICK, t, .1, 112) for t in drop_k])
    sn = [(RIM, V2 + 4 * b + 2, 92) for b in range(8)] + [(CLAP, V2 + 4 * b + 2, 58) for b in range(8)]
    tune = [(0, 1, "hold")]
    for b in builds:
        t = b["at"]
        sn += [(SNARE, t + 4 * k + 2, 88) for k in range(2)] + [(SNARE, t + 8 + k, 70 + 3 * k) for k in range(8)]
        sn += [(SNARE, t + 16 + 0.5 * k, 80 + k) for k in range(16)] + [(SNARE, t + 24 + 0.25 * k, 96 + k) for k in range(16)]
        sn += [(SNARE, t + 28 + 0.125 * k, 112 + k // 2) for k in range(16)]
        sn += [(CLAP, t + 4 * k + 2, 70) for k in range(6)] + [(CLAP, t + 31.5, 127)]
        tune += [(t + 16, 1, "linear"), (t + 30, 12, "hold"), (t + 31.4, 1, "hold")]
    for d in drops:
        sn += [(SNARE, d["at"] + 4 * b + 2, 118) for b in range(16)] + [(CLAP, d["at"] + 4 * b + 2, 110) for b in range(16)]
        sn += [(CLAP, d["at"] + 4 * b + 3.75, 70) for b in (7, 15)]
    snare, sdv, _ = element("drums", "Snare + clap", "verse: a rim snap and a soft clap on 3; builds: a roll (quarters → "
                            "32nds) climbing an octave, the breath, one clap; drops: a snare and a clap together on 3",
                            dict(snare__tune=1, snare__decay=220, snare__tone=0.62, clap__decay=340, clap__tone=0.55,
                                 rim__tune=5, rim__decay=70, volume=3), [(p, t, .2, v) for p, t, v in sn])
    S.automate(f"{sdv}.snare.tune", tune)
    space(snare, reverb=(0.2, 1500, 100))
    pc = [(CHAT, V2 + 0.5 * k, 84 if k % 2 == 0 else 64) for k in range(64)]
    for b in builds:
        pc += [(CHAT, b["at"] + 16 + 0.25 * k, 40 + k // 2) for k in range(56)]
    for d in drops:
        big = d["nth"] > 1
        for b in range(16):
            t, last4 = d["at"] + 4 * b, b % 4 == 3
            if D["perc"] == "light" and not big:
                pc += [(CHAT, t + 0.5 * k, 66 if k % 2 == 0 else 50) for k in range(8)]
            elif b < 8 and not big:
                pc += [(CHAT, t + 0.25 * k, 74 if k % 2 == 0 else 46) for k in range(16) if not (last4 and k >= 12)]
            else:
                pc += [(CHAT, t + 0.5 * k, 72) for k in range(8) if not (last4 and k >= 6)]
                pc += [(OHAT, t + 0.5 * k + 0.25, 56) for k in range(8) if k % 2 == 1 and not (last4 and k >= 6)]
                pc += [(RIDE, t + k, 50) for k in range(4)]
            if last4:
                pc += ([(CHAT, t + 3 + k / 6, 50 + 8 * k) for k in range(6)] if b % 8 == 7 else
                       [(CHAT, t + 3 + 0.125 * k, 46 + 7 * k) for k in range(8)])
            pc += [(RIM, t + 1.25, 72), (RIM, t + 3.25, 64)]
            if b % 2 == 1 and (D["perc"] != "light" or b % 4 == 3):
                pc += [(HTOM, t + 3, 94), (MTOM, t + 3.25, 90), (MTOM, t + 3.5, 96), (LTOM, t + 3.75, 102)]
            if b % 8 == 0 or (big and b % 4 == 0):
                pc += [(CRASH, t, 112 if b % 8 == 0 else 84)]
    pc += [(CRASH, outro["at"], 90)]
    perc, _, _ = element("drums", "Percussion", "hats (eighths in the verse, sixteenths then eighths with open hats and a "
                         "ride in the drops), rolls into each phrase, rim snaps, tom fills after the snare, crashes",
                         dict(chat__tune=3, chat__decay=45, chat__tone=0.75, chat__pan=-0.2, ohat__decay=240,
                              ohat__pan=0.25, rim__tune=6, rim__decay=60, rim__pan=0.35, htom__pan=-0.4, ltom__pan=0.4,
                              ride__tune=4, ride__pan=0.45, crash__decay=2200, volume=-4),
                         [(p, t, .15 if p != CRASH else 1, v) for p, t, v in pc])
    space(perc, reverb=(0.14, 1100, 80))

    # ── the chords of the quiet sections: a piano or a picked guitar; a pad under them ──
    quiet = [(s, 0, s["bars"] // 2) for s in (intro, verse, brk, outro)]
    kn = []
    for s, a, n in quiet:
        for i in range(a, n):
            t = s["at"] + 8 * i
            ch = so.chord(t)
            lastslot = s is outro and i == n - 1
            low, tones = root_in(ch, 43), place(ch["pcs"], 55, 69)
            soft = -8 if s is intro or s is outro else 0
            if D["keys"] == "guitar":                               # picked eighths: root, then up and down the chord
                order = [None, 0, 1, 2, 1, 0, 1, 2] * 2
                kn += [((low if k is None else tones[min(k, len(tones) - 1)]), t + 0.5 * j, 1.0, (84 if j % 4 == 0 else 70) + soft)
                       for j, k in enumerate(order[:1 if lastslot else 16])]
                if lastslot:
                    kn += [(p, t + 0.06 * j, 6, 78) for j, p in enumerate(tones)]
            elif lastslot:
                kn += [(p, t, 8, 70) for p in [low] + tones]
            else:
                kn += [(p, t, 3.4, 72 + soft) for p in [low] + tones] + [(p, t + 3, 0.9, 54 + soft) for p in tones[1:]]
                kn += [(p, t + 4, 2.9, 64 + soft) for p in [low] + tones] + [(p, t + 6.5, 1.4, 52 + soft) for p in tones[1:]]
    if D["keys"] == "guitar":
        keys, _, _ = element("guitar", "Guitar", "a picked guitar on the chords: the root, then up and down the chord in "
                             "eighths; a room and a faint echo", dict(GUITAR), kn)
        space(keys, reverb=(0.22, 2200, 120), delay=(0.1, 0.25))
    else:
        keys, _, _ = element("piano", "Piano", "the chords, two to the bar-pair with a pickup; a soft hammer, a room",
                             dict(strings__brightness=0.85, hammer__hardness=0.8, body__mix=0.5, volume=0), kn)
        gains[keys] = -1
        space(keys, reverb=(0.2, 2400, 130))
    pn = []
    for s, a, n in quiet + [(b, 0, 2) for b in builds]:
        for i in range(a, n):
            t = s["at"] + 8 * i
            pn += [(p, t, 8, 80) for p in place(so.chord(t)["pcs"], 57, 70)]
    pad, _, _ = element("analog", "Pad", "a warm bed under the quiet sections: five unison saws spread wide and a "
                        "triangle, a 12 dB low-pass at 1.2 kHz, slow", PAD, pn)
    space(pad, reverb=(0.3, 3000, 150))

    # ── the reese: the verse's second half, the builds' first six bars, the break's second half ──
    rn = [(root_in(so.chord(V2 + 8 * i), 34), V2 + 8 * i, 7.75, 92) for i in range(4)]
    rn += [(root_in(so.chord(b["at"] + 8 * i), 34), b["at"] + 8 * i, 7.75, 92) for b in builds for i in range(3)]
    rn += [(root_in(so.chord(brk["at"] + 16 + 8 * i), 34), brk["at"] + 16 + 8 * i, 7.75, 88) for i in range(2)]
    reese, rdv, _ = element("analog", "Reese bass", "two saws 18 cents apart beating, a sine sub an octave under, low "
                            "(A♯1–A2), the ladder at 480 Hz driven 6 dB and breathing with a slow LFO; a room on its top "
                            "only, the floor mono", REESE, rn)
    S.device(reese, "imager", width=140, mono="on", frequency=140)
    space(reese, reverb=(0.2, 3000, 150))

    # ── the bell: harmonic, in tune, few notes ──
    bn = []
    for s in (intro, brk, outro):
        for i in range(s["bars"] // 2):
            t = s["at"] + 8 * i
            tones = place(so.chord(t)["pcs"], 76, 91)
            pick = [(0, 2), (1.5, 1), (3, 3), (5, 1)] if not (s is outro and i == 3) else [(0, 2)]
            bn += [(tones[min(k, len(tones) - 1)], t + a, 1.5, 72 if a == 0 else 60) for a, k in pick]
    for i in range(4):                                              # the verse's second half: it answers in the gaps
        t = V2 + 8 * i
        tones = place(so.chord(t)["pcs"], 76, 91)
        bn += [(tones[min(k, len(tones) - 1)], t + a, 1.5, 62) for a, k in ((3.5, 1), (7, 2))]
    bell, _, _ = element("fm", "Bell", "an FM bell, harmonic and in tune with itself (ratio 4, a 180 ms tick; a faint "
                         "partial two octaves up), E5–G6, few notes; a 3.2 s room, a soft echo", BELL, bn)
    S.device(bell, "eq", lowcut__on="on", lowcut__freq=300, highcut__on="on", highcut__freq=8000)
    space(bell, reverb=(0.32, 3200, 160), delay=(0.18, 0.3))

    # ── the main synth: the verse's line, the builds' (the hook teased an octave down, then the rise), the break's ──
    mel = so.lines(T["verse"], verse["at"]) + so.lines(T["verse"][2:], brk["at"])
    for b in builds:
        mel += so.lines([T["call"]], b["at"], -12)
        mel += [(t, min(d, b["at"] + 30 - t), p) for t, d, p in so.lines([T["build_rise"]], b["at"] + 16) if t < b["at"] + 30]
    mel.sort()
    kind = D["main"]
    if kind == "chop":
        main, mdv, mpt = element("analog", "Vocal chop", "the verse's voice: one saw and a square through the vowel filter "
                                 "— 'ah' on held notes, 'oo' and 'eh' between; a late vibrato; an echo, a room", CHOP,
                                 [(p, t, max(0.1, d - 0.05), vel(t, 100)) for t, d, p in mel])
        vw = S.device(main, "vowel", resonance=0.7, glide=25, mix=1, output=9)
        au = S.held("Chop · Vowel", [{"t": t, "vowel": 0 if d >= 1.5 else (4 if i % 2 else 1)}
                                     for i, (t, d, p) in enumerate(mel)], L)
        line(f'set {vw}.vowel="={au}"')
    elif kind == "guitar":
        main, mdv, mpt = element("guitar", "Lead guitar", "a second guitar singing the line: picked nearer the bridge, "
                                 "brighter; a chorus, an echo, a room", dict(GUITAR, pluck__position=0.14, pluck__pick=6500,
                                                                           string__sustain=5),
                                 [(p, t, max(0.2, d), vel(t, 100)) for t, d, p in mel])
        gains[main] = 9
    else:
        main = S.bus("Main synth")
        for nm, inst, params in (("Main pluck", "analog", PLUCK), ("Main lead", "analog", LEAD)):
            ch, dv, pt = S.instrument(inst, nm, L)
            S.set(dv, **params)
            S.notes(pt, [(p, t, max(0.1, d - 0.05), vel(t, 98)) for t, d, p in mel])
            line(f"route {ch} --to {main}")
        gains[main] = 6
        E.append({"name": "Main synth", "strip": main, "recipe": "a soft pluck (the attack) and a dark saw lead with a "
                  "late vibrato (the sustain) as one instrument; a chorus, an echo, a room"})
    S.device(main, "chorus", rate=0.6, depth=3, delay=12, mix=0.25)
    space(main, reverb=(0.24, 2400, 130), delay=(0.15, 0.3))

    # ── the supersaw: three layers on one bus ──
    semis, move = D["bend"]
    stabs_a, top = [], []
    cut = [(0, 400, "hold")]
    for b in builds:                                                # filtered stabs quickening as the filter opens
        t0 = b["at"]
        for a, z, step in ((0, 8, 2), (8, 16, 1), (16, 24, 0.5), (24, 30, 0.25)):
            t = a
            while t < z:
                stabs_a += [(p, t0 + t, step * 0.8, 70 + int(40 * t / 30)) for p in place(so.chord(t0 + t)["pcs"], 60, 72)]
                t += step
        cut += [(t0, 400, "smooth"), (t0 + 30, 9000, "hold"), (t0 + 32, 16000, "hold")]
    for d in drops:
        for t, dur, p in so.hook(d["at"]):
            ch = so.chord(t)
            under = sorted([q for q in range(p - 12, p) if q % 12 in ch["pcs"] and q % 12 != p % 12], reverse=True)[:3]
            ln = max(0.2, dur - 0.1)
            stabs_a += [(q, t, ln, vel(t, 108)) for q in [p] + under]
            top += [(p, t, ln, vel(t, 110))]
    sbus = S.bus("Supersaw")
    rise = dict(penv__amount=semis, penv__decay=max(1, int(move * 1000))) if semis else {}   # the pitch envelope
    for nm, params, notes in (("Saw main", SUPER, stabs_a), ("Saw top", TOP, top)):
        ch, dv, pt = S.instrument("analog", nm, L)
        S.set(dv, **dict(params, **rise))
        S.notes(pt, notes)
        if nm == "Saw main":
            S.automate(f"{dv}.filter.cutoff", cut)
        line(f"route {ch} --to {sbus}")
    S.device(sbus, "eq", lowcut__on="on", lowcut__freq=160, peak1__freq=350, peak1__gain=-2.5, peak1__q=1,
             peak3__freq=3200, peak3__gain=1.5, peak3__q=0.8, highshelf__freq=9000, highshelf__gain=2)
    if D["sway"]:
        sh = S.device(sbus, "shaper", rate=f'"{D["sway"][0]}"', shape='"sine"', depth=0, smooth=8)
        S.automate(f"{sh}.depth", [(0, 0, "hold")] + [x for d in drops for x in ((d["at"], D["sway"][1], "hold"),
                                                                               (d["at"] + 64, 0, "hold"))])
    S.device(sbus, "imager", width=150, mono="on", frequency=200)
    space(sbus, reverb=(0.14, 1800, 110))
    duck(sbus, kick, 10, 130)
    duck(sbus, snare, 3, 80)
    E.append({"name": "Supersaw", "strip": sbus, "recipe": "the analog synth: seven free-running saws by the JP-8000's detune "
              "law and seven more an octave up, a breath of noise, through the ladder; and a three-saw lead on the top "
              "note — the hook on top of every chord; "
              + (f"each stab rises {abs(semis):g} st into the note over {int(move * 1000)} ms (its pitch envelope); " if semis
                 else "no pitch move; ") + (f"a {D['sway'][0]} sine sway; " if D["sway"] else "") + "EQ: no low end, −2.5 dB at 350 Hz, "
              "a touch of bite and air; 150 % wide, mono under 200 Hz; pumped 10 dB by the kick"})

    # ── the bass under the drops: struck with every kick ──
    bass = []
    for d in drops:
        dk = so.drop_kicks(d["at"])
        for i, t in enumerate(dk):
            nxt = dk[i + 1] if i + 1 < len(dk) else d["at"] + 64
            bass.append((root_in(so.chord(t), 28), t, min(nxt - t, 3.0) - 0.05))
    sub, _, _ = element("analog", "Sub", "a sine from phase zero (every note the same punch), mono, the chord's root with every kick; ducked 12 dB by the kick, back "
                        "within ~200 ms", SUB, [(p, t, d, 110) for p, t, d in bass])
    duck(sub, kick, 12, 70)
    mid, _, _ = element("analog", "Mid bass", "the bass's voice an octave above the sub: three saws and a pulse through "
                        "the ladder — 700 Hz, plucked open 2 octaves, driven 9 dB — high-passed 150 Hz so the sub owns the "
                        "floor", MID,
                        [(p + 12, t, d, 104) for p, t, d in bass])
    S.device(mid, "filter", mode='"highpass"', cutoff=150, res=0.1, mix=1)
    duck(mid, kick, 12, 70)

    # ── the counter-melody (a vocal chop in the hook's rests), and an arpeggio in the second drop ──
    cm = [n for d in drops for n in so.counter(d["at"])]
    for b in builds:                                                # the pickup: the hook's first two notes, in the breath
        h = so.hook(b["at"] + 32)[:2]
        cm += [(b["at"] + 30.5 + 0.5 * i, 0.4, p - 12) for i, (_, _, p) in enumerate(h)]
    cm.sort()
    chop2, _, _ = element("analog", "Counter chop", "a vocal chop answering the supersaw in its rests, and calling the hook "
                          "in the breath before each drop; an echo, a room", dict(CHOP, volume=-14),
                          [(p, t, max(0.1, d - 0.05), vel(t, 100)) for t, d, p in cm])
    vw2 = S.device(chop2, "vowel", resonance=0.7, glide=25, mix=1, output=9)
    line(f'set {vw2}.vowel="={S.held("Counter · Vowel", [{"t": t, "vowel": 0 if i % 2 == 0 else 3} for i, (t, d, p) in enumerate(cm)], L)}"')
    S.device(chop2, "imager", width=130)
    space(chop2, reverb=(0.22, 2200, 130), delay=(0.2, 0.35))
    d2 = drops[-1]
    an = []
    for k in range(256):                                            # sixteenths, up the chord and round again
        t = d2["at"] + 0.25 * k
        tones = place(so.chord(t)["pcs"], 72, 88)
        an.append((tones[(k % 8 if k % 8 < 5 else 8 - k % 8) % len(tones)], t, 0.2, 86 if k % 4 == 0 else 66))
    arp, _, _ = element("analog", "Arp", "the second drop only: a bright pluck in sixteenths up and down the chord, "
                        "high-passed, an echo — the lift", ARP, an)
    S.device(arp, "filter", mode='"highpass"', cutoff=500, res=0.1, mix=1)
    S.device(arp, "imager", width=160)
    space(arp, reverb=(0.2, 1800, 110), delay=(0.2, 0.3))
    duck(arp, kick, 6, 100)

    # ── risers, falls, impacts ──
    nz, ndv, npt = element("analog", "Noise FX", "white noise: a swell into the verse, a riser through each build's last "
                           "four bars (low-pass 300 Hz → 12 kHz), a fall after each drop lands", NOISE)
    nn, ncut, nvol = [(60, verse["at"] - 4, 4, 100)], [(0, 300, "hold")], [(0, -40, "hold")]
    ncut += [(verse["at"] - 4, 800, "smooth"), (verse["at"], 6000, "hold")]
    nvol += [(verse["at"] - 4, -40, "smooth"), (verse["at"], -16, "hold")]
    for b, d in zip(builds, drops):
        nn += [(60, b["at"] + 16, 14, 100), (60, d["at"], 6, 100)]
        ncut += [(b["at"] + 16, 300, "smooth"), (b["at"] + 30, 12000, "hold"), (d["at"], 9000, "smooth"), (d["at"] + 6, 300, "hold")]
        nvol += [(b["at"] + 16, -40, "smooth"), (b["at"] + 30, -9, "hold"), (d["at"], -14, "smooth"), (d["at"] + 6, -45, "hold")]
    S.notes(npt, nn)
    S.automate(f"{ndv}.filter.cutoff", ncut)
    S.automate(f"{ndv}.volume", nvol)
    space(nz, reverb=(0.3, 2500, 150))
    pr, _, ppt = element("analog", "Pitch riser", "one supersaw note bent up two octaves over each build's last four bars",
                         dict(SUPER, voice__bend=24, filter__cutoff=3000, volume=-17),
                         [(root_in(so.chord(0), 60), b["at"] + 16, 14, 90) for b in builds])
    S.expr(ppt, "bend", [(0, 0)] + [x for b in builds for x in ((b["at"] + 16, 0), (b["at"] + 30, 1), (b["at"] + 31, 0))])
    space(pr, reverb=(0.3, 2500, 150))
    element("808", "Impact", "an 808 boom under each drop's first beat, punched down two octaves, 2.5 s",
            dict(punch=24, amp__decay=2500, click=0.4, drive=0.3, tone=2500, volume=-11),
            [(root_in(so.chord(d["at"]), 36), d["at"], 4, 120) for d in drops])

    for e in E:                                                    # the faders: a base, and the levelling pass's trim
        g = gains.get(e["strip"], 0) + (trim or {}).get(e["name"], 0)
        if g:
            line(f"set {e['strip']}.gain={num(round(g, 1))}")
    line("device add master --type limiter")                       # a ceiling, not a loudness tool: the mix is set by the faders
    S.set(S.new('dv'), gain=1.5, ceiling=-1.0, truePeak="on")
    line("project save")
    line(f"render --out {so.name}.mp3 --format mp3 --kbps 192")
    line(f"render --out {so.name}-stem.mp3 --format mp3 --kbps 112 --stems " + ",".join(e["strip"] for e in E))
    return "\n".join(S.L) + "\n", E, {"kick": kick, "sub": sub, "saw": sbus, "drop_kicks": so.drop_kicks(drops[0]["at"]),
                                      "drop": drops[0], "drop2": drops[-1], "verse": verse, "keys": E[4]["name"],
                                      "main": next(e["name"] for e in E if e["strip"] == main)}


# ── the checks and the measures ────────────────────────────────────────────────────────────────────────────────────
def check(song):
    """The writing, checked before a note is rendered: chord tones on the strong notes, the registers, the surprise."""
    so, T, out = song, song.T, {}
    for name, notes in (("verse", so.lines(T["verse"], so.of("verse")[0]["at"])), ("hook", so.hook(so.of("drop")[0]["at"])),
                        ("counter", so.counter(so.of("drop")[0]["at"]))):
        strong = [(t, d, p) for t, d, p in notes if t % 4 in (0, 2) or d >= 1]
        ct = sum(1 for t, d, p in strong if p % 12 in so.chord(t)["pcs"])
        ps = [p for _, _, p in notes]
        out[name] = {"notes": len(notes), "chord_tones": round(ct / max(1, len(strong)), 2), "low": min(ps), "high": max(ps),
                     "surprise": round(surprise(ps), 2) if len(ps) > 8 else None}
    h, c = so.hook(0), so.counter(0)
    out["collisions"] = sum(1 for a in c for b in h if a[0] < b[0] + b[1] and b[0] < a[0] + a[1])
    return out


def pcm(path):
    import numpy as np
    r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "1", "-ar", "48000", "-"], capture_output=True)
    return 48000, np.frombuffer(r.stdout, dtype=np.float32).astype(float)


def sharpness(path, t0, t1):
    """A sound's spectral centre (Hz) over [t0, t1) s — "the supersaw needs to be more sharp" (fb-002), measured."""
    import numpy as np
    sr, x = pcm(path)
    x, n, cs = x[int(t0 * sr):int(t1 * sr)], 4096, []
    f = np.fft.rfftfreq(n, 1 / sr)
    for i in range(0, len(x) - n, n):
        X = np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n))) ** 2
        if X.sum() > 1e-9:
            cs.append((f * X).sum() / X.sum())
    return round(float(np.mean(cs))) if cs else None


def punch(path, times, mix, t0, t1):
    """The kick's first 50 ms of each hit against the whole mix's RMS over [t0, t1) s, in dB — how far the hit stands
    out ("kick is not punchy enough", fb-002)."""
    import numpy as np
    sr, x = pcm(path)
    _, m = pcm(mix)
    rms = np.sqrt(np.mean(m[int(t0 * sr):int(t1 * sr)] ** 2))
    hits = [np.sqrt(np.mean(x[int(t * sr):int((t + 0.05) * sr)] ** 2)) for t in times if int((t + 0.05) * sr) < len(x)]
    return round(20 * math.log10(float(np.mean(hits)) / rms), 1) if hits else None


def duck_depth(path, times):
    """The bass 5–35 ms after each kick against 200–280 ms after, in dB (negative: ducked, then back)."""
    import numpy as np
    sr, x = pcm(path)
    rms = lambda a, b: np.sqrt(np.mean(x[a:b] ** 2)) + 1e-9
    ds = [20 * math.log10(rms(int((t + 0.005) * sr), int((t + 0.035) * sr)) / rms(int((t + 0.2) * sr), int((t + 0.28) * sr)))
          for t in times if int((t + 0.28) * sr) < len(x)]
    return round(float(np.mean(ds)), 1) if ds else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out")
    ap.add_argument("--songs", default=",".join(SONGS))
    ap.add_argument("--cc", default=DEFAULT_CC)
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--check", action="store_true", help="print the writing's checks and stop")
    ap.add_argument("--quick", action="store_true", help="one render, no levelling pass")
    ap.add_argument("--stems", action="store_true", help="print every element's loudness in the verse and the drop")
    a = ap.parse_args()
    names = [n for n in SONGS if n in a.songs.split(",")]
    if a.check:
        for n in names:
            print(n, json.dumps(check(Song(n))))
        return
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, SOLARIS_SETTINGS=os.path.join(out, ".settings.txt"), SOLARIS_RECENTS=os.path.join(out, ".recents"))
    env.pop("DISPLAY", None)
    path = os.path.join(out, "batch.json")
    old = {}
    if os.path.exists(path):
        with open(path) as f:
            old = {m["id"]: m for m in json.load(f).get("melodies", [])}
    items, failed = [], 0
    for k, n in enumerate(SONGS):
        if n not in names:
            if n in old:
                items.append(old[n])
            continue
        so = Song(n)
        trim, ok = {}, True
        for attempt in ((1, 2) if not (a.no_render or a.quick) else (2,)):
            text, E, keys = arrange(so, trim)
            with open(os.path.join(out, n + ".txt"), "w") as f:
                f.write(text)
            if a.no_render:
                break
            for stale in (n + ".slp", n + ".mp3"):
                if os.path.exists(os.path.join(out, stale)):
                    os.remove(os.path.join(out, stale))
            r = subprocess.run([a.cc, "--script", n + ".txt"], cwd=out, env=env, capture_output=True, text=True)
            ok = r.returncode == 0 and os.path.exists(os.path.join(out, n + ".mp3"))
            if not ok:
                print(f"{n}: FAILED\n{r.stdout[-1500:]}{r.stderr[-600:]}", flush=True)
                break
            stem = lambda ch: os.path.join(out, f"{n}-stem.{ch}.mp3")
            win = lambda s, a0=0: (so.sec(s["at"] + a0), so.sec(4 * s["bars"] - a0))
            if attempt == 1:                                        # LEVEL: every element to its target, in its section
                want = dict(TARGETS, **{keys["keys"]: ("verse", -23.5), keys["main"]: ("verse", -20.5)})
                wins = {"verse": win(keys["verse"], 32), "drop": win(keys["drop"]), "drop2": win(keys["drop2"])}
                for e in E:
                    if e["name"] in want:
                        w, target = want[e["name"]]
                        got = loudness(stem(e["strip"]), *wins[w])
                        if got is not None and got > -60:
                            trim[e["name"]] = max(-14, min(14, round(target - got, 1)))
                print(f"{n}: levelled " + " · ".join(f"{k} {v:+g}" for k, v in trim.items()), flush=True)
        item = {"id": n, "title": f"{so.T['title']} — {so.T['feel']}", "sound": True, "section": "song", "track": n,
                "style": "future-bass", "style_label": "Future bass song", "bpm": so.bpm, "key": so.key.name,
                "form_label": "sound — song", "seconds": round(so.sec(so.length)),
                "sections": [{"name": s["name"], "bar": s["at"] // 4, "bars": s["bars"]} for s in so.sections],
                "devices": [f"design: {d}" for d in so.T["sound"]] + [f"{e['name']}: {e['recipe']}" for e in E],
                "elements": E, "audio": [{"label": "full", "file": f"{n}.mp3"}] +
                [{"label": e["name"].lower(), "file": f"{n}-stem.{e['strip']}.mp3"} for e in E],
                "metrics": {"writing": check(so), "trim": trim}}
        failed += not ok
        if ok and not a.no_render:
            junk = os.path.join(out, n + "-stem.mp3")
            if os.path.exists(junk):
                os.remove(junk)
            mix = os.path.join(out, n + ".mp3")
            lufs = {"song": loudness(mix)}
            for s in so.sections:
                lufs[s["name"]] = loudness(mix, *win(s))
            d, v = keys["drop"], keys["verse"]
            t0, t1 = so.sec(d["at"]), so.sec(d["at"] + 64)
            m = item["metrics"]
            m["lufs"] = lufs
            m["sub_duck_db"] = duck_depth(stem(keys["sub"]), [so.sec(t) for t in keys["drop_kicks"]])
            m["supersaw_centroid_hz"] = sharpness(stem(keys["saw"]), t0, t1)
            m["kick_hit_db"] = punch(stem(keys["kick"]), [so.sec(t) for t in keys["drop_kicks"]], mix, t0, t1)
            m["drop_over_verse_lu"] = round(lufs["drop"] - lufs["verse"], 1)
            print(f"{n}: ok — LUFS " + " · ".join(f"{k} {x}" for k, x in lufs.items()) + f" · duck {m['sub_duck_db']} dB · "
                  f"saw {m['supersaw_centroid_hz']} Hz · kick hit {m['kick_hit_db']} dB", flush=True)
            if a.stems:
                for label, w in (("verse 2nd half", win(v, 32)), ("drop", win(d))):
                    print(f"   {label}: " + " · ".join(f"{e['name']} {loudness(stem(e['strip']), *w)}" for e in E), flush=True)
        items.append(item)
    with open(path, "w") as f:
        json.dump({"batch": os.path.basename(out), "kind": "sound", "melodies": items}, f, indent=1, ensure_ascii=False)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
