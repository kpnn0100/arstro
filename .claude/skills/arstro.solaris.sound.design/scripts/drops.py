#!/usr/bin/env python3
"""drops.py — the bare drop, to judge two sounds: the SAW and the BASS. (The user, 2026-11-27: "design me only basic
kick snare and saw lead drop with common chord progression … use trap kick and snare … use already made kick first …
just give me option to judge your bass and saw, don't do gliding, just straight powerful saw play the chord that end
when the snare come and play along the kick. 5 drop option".)

    python3 drops.py --out ~/Music/Solaris/sound-lab/drops-NNN --kick <a kick .wav> [--only 1,3] [--quick]

Every option is the same music, so only the sounds differ:
- 16 bars at 150 bpm, half time; E minor: Em · C · G · D (vi–IV–I–V of G major), a bar each;
- the KICK is the user's own sample (the Sampler, one-shot), the same in every option;
- the SNARE (a tight snare and a clap, the Drum Machine) on beat 3;
- the SAW strikes its chord WITH every kick and holds it flat — no pitch move, no glide, no decay — until the next
  kick, or until the snare, where it stops dead;
- the BASS plays the chord's root with the saw, the same lengths.
What changes is OPTIONS: five saw designs and five bass designs, paired.

Each option is rendered twice — the second time with every element's fader set to its target (TARGETS), so the options
are compared at one balance — and each element is rendered alone for the lab's switch. Runs solaris-cc with scratch
settings in the output folder; the kick is copied beside the song, never into the repository.
"""
import argparse, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
MEL = os.path.normpath(os.path.join(HERE, "..", "..", "arstro.solaris.compose.melody", "scripts"))
sys.path.insert(0, MEL)
from song import Script, num, loudness, DEFAULT_CC  # noqa: E402

BPM, BARS = 150, 16
SNARE, CLAP = 38, 39
NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def midi(name):
    i, acc = 1, 0
    while name[i] in '#b':
        acc += 1 if name[i] == '#' else -1
        i += 1
    return 12 * (int(name[i:]) + 1) + NOTE[name[0]] + acc


# Em · C · G · D — the top line holds G, G, G, F♯; the bass E1, C2, G1, D2
CHORDS = [("Em", "E3 B3 E4 G4 B4 E5 G5", "E1"), ("C", "C3 G3 E4 G4 C5 E5 G5", "C2"),
          ("G", "G3 D4 G4 B4 D5 G5", "G1"), ("D", "D3 A3 F#4 A4 D5 F#5", "D2")]
KICKS = ([0, 1.5, 3.5], [0, 0.75, 1.5, 2.75, 3.5])     # the bar, and the bar after it (trap: sparse, then busier)

# what stays the same in every saw: it speaks at once, holds flat, stops at once
FLAT = dict(amp__attack=1, amp__decay=0, amp__sustain=1, amp__release=30, velocity=0, voice__glide=0)
MONO = dict(FLAT, voice__mode="mono", voice__phase="reset", voice__drift=0, amp__release=40)

OPTIONS = [
    dict(name="Classic", about="the JP-8000 supersaw and a clean sine sub",
         saw=[("analog", "all", 0, dict(osc1__wave="saw", osc1__unison=7, osc1__detune=0.42, osc1__blend=0.75, osc1__spread=0.9,
                                       osc1__level=0.8, osc2__wave="saw", osc2__octave=1, osc2__unison=7, osc2__detune=0.5,
                                       osc2__blend=0.8, osc2__spread=1, osc2__level=0.45, sub__noise=0.04,
                                       filter__mode="off", voice__drift=0.2))],
         saw_note="seven saws (detune 0.42, blend 0.75) and seven more an octave up at 0.45, a breath of noise, no filter",
         bass=("analog", 0, dict(osc1__wave="sine", osc1__level=1, filter__mode="off"), []),
         bass_note="a pure sine at the root (E1–D2), from phase zero: only weight, no voice of its own"),
    dict(name="808", about="a wider, brighter saw and an 808",
         saw=[("analog", "all", 0, dict(osc1__wave="saw", osc1__unison=7, osc1__detune=0.5, osc1__blend=0.85, osc1__spread=1,
                                       osc1__level=0.75, osc2__wave="saw", osc2__octave=1, osc2__unison=7, osc2__detune=0.55,
                                       osc2__blend=0.85, osc2__spread=1, osc2__level=0.6, sub__noise=0.06,
                                       filter__mode="off", voice__drift=0.25))],
         saw_note="wider (detune 0.5, blend 0.85), the octave stack louder (0.6), more noise: bright and big",
         bass=("808", 0, dict(punch=5, **{"punch.time": 18}, glide=0, amp__attack=1, amp__decay=2600, amp__release=40,
                              click=0.35, drive=0.55, tone=1800, velocity=0), []),
         bass_note="the 808: a sine with a 5-semitone thump in its first 18 ms, driven (0.55) so it is heard on small "
                   "speakers, no glide between notes"),
    dict(name="Stack", about="a three-octave saw stack and a driven saw bass",
         saw=[("analog", "all", 0, dict(osc1__wave="saw", osc1__unison=7, osc1__detune=0.4, osc1__blend=0.7, osc1__spread=0.9,
                                       osc1__level=0.8, osc2__level=0, filter__mode="off", voice__drift=0.2)),
              ("analog", "low", -12, dict(osc1__wave="saw", osc1__unison=3, osc1__detune=0.2, osc1__blend=0.5, osc1__spread=0.3,
                                         osc1__level=0.8, osc2__level=0, filter__mode='"lowpass 12"', filter__cutoff=2500)),
              ("analog", "top", 12, dict(osc1__wave="saw", osc1__unison=5, osc1__detune=0.35, osc1__blend=0.7, osc1__spread=1,
                                        osc1__level=0.7, sub__noise=0.05, filter__mode="off"))],
         saw_note="three layers: the chord on seven saws, its root and fifth an octave DOWN on three (body), its top note "
                  "an octave UP on five (air)",
         bass=("analog", 12, dict(osc1__wave="saw", osc1__level=0.8, sub__level=0.95, filter__mode='"lowpass 24"',
                                  filter__cutoff=240, filter__res=0.1, filter__drive=12, filter__env=1.6, fenv__attack=1,
                                  fenv__decay=160, fenv__sustain=0.35), []),
         bass_note="a saw an octave above a sine sub, through the ladder at 240 Hz driven 12 dB, plucked open 1.6 "
                   "octaves: a bass with teeth"),
    dict(name="Tight", about="a narrow, centred saw and a hollow square bass",
         saw=[("analog", "all", 0, dict(osc1__wave="saw", osc1__unison=5, osc1__detune=0.28, osc1__blend=0.5, osc1__spread=0.6,
                                       osc1__level=0.8, osc2__wave="pulse", osc2__width=0.5, osc2__level=0.3,
                                       filter__mode="off", voice__drift=0.1))],
         saw_note="five saws close together (detune 0.28) with a strong centre (blend 0.5) and a square under them: "
                  "focused, the pitch plain",
         bass=("analog", 12, dict(osc1__wave="pulse", osc1__width=0.5, osc1__level=0.7, sub__level=0.9,
                                  filter__mode='"lowpass 24"', filter__cutoff=320, filter__res=0.05), []),
         bass_note="a square an octave above a sine sub, low-passed 320 Hz, no drive: hollow and round"),
    dict(name="Driven", about="a supersaw pushed into the ladder and a distorted sub",
         saw=[("analog", "all", 0, dict(osc1__wave="saw", osc1__unison=7, osc1__detune=0.38, osc1__blend=0.7, osc1__spread=0.9,
                                       osc1__level=0.85, osc2__wave="saw", osc2__octave=1, osc2__unison=7, osc2__detune=0.45,
                                       osc2__blend=0.8, osc2__spread=1, osc2__level=0.5, filter__mode='"lowpass 24"',
                                       filter__cutoff=11000, filter__res=0.1, filter__drive=10, voice__drift=0.2))],
         saw_note="the classic stack through the ladder at 11 kHz, driven 10 dB: thicker, a little grit",
         bass=("analog", 0, dict(osc1__wave="sine", osc1__level=1, filter__mode='"lowpass 24"', filter__cutoff=500,
                                 filter__drive=20, filter__keytrack=1), [("saturator", dict(drive=8, curve='"hard"', mix=0.6, output=-4))]),
         bass_note="a sine driven 20 dB into the ladder's tanh, then clipped: odd harmonics on a sub — loud on any speaker"),
]

# ── drops-002: one sound (the design the user loved), five MOODS ────────────────────────────────────────────────────
# The user's pattern, counted over two bars: the kick on 1, 4 and 6, the snare on 3 and 7. The saw strikes with each
# kick and holds to the next kick or the snare: 2 beats, 2 beats, 1 beat — then a beat and a half of air.
KICK8, SNARE8 = [0, 3, 5], [2, 6]
# A mood is four two-bar units, played twice; a unit is its three hits: (the chord's notes low to high, the bass note).
MOODS = [
    dict(name="Bittersweet", about="IV–V–vi–I with ninths, the bass walking up to the third",
         chords="Cmaj9 · Dadd9 · Em9 · G/B",
         units=[[("C3 G3 B3 E4 G4 D5 G5", "C2")] * 2 + [("C3 G3 B3 E4 G4 D5 E5", "C2")],
                [("D3 A3 E4 F#4 A4 E5 A5", "D2")] * 2 + [("D3 A3 E4 F#4 A4 D5 F#5", "D2")],
                [("E3 B3 D4 G4 B4 F#5 G5", "E1")] * 2 + [("E3 B3 D4 G4 B4 F#5 B5", "E1")],
                [("B2 G3 D4 F#4 B4 D5 G5", "B1")] * 2 + [("B2 G3 D4 F#4 A4 D5 F#5", "B1")]]),
    dict(name="Longing", about="minor ninths, a Lydian colour, and a dominant that hangs before it resolves",
         chords="Em9 · Cmaj7♯11 · Am9 · B7sus4 → B7",
         units=[[("E3 B3 D4 F#4 B4 G5", "E1")] * 2 + [("E3 B3 D4 F#4 B4 F#5", "E1")],
                [("C3 G3 B3 E4 F#4 B4 G5", "C2")] * 2 + [("C3 G3 B3 E4 F#4 B4 E5", "C2")],
                [("A2 E3 G3 C4 E4 B4 E5", "A1")] * 2 + [("A2 E3 G3 C4 E4 B4 C5", "A1")],
                [("B2 A3 E4 F#4 B4 E5", "B1")] * 2 + [("B2 A3 D#4 F#4 B4 D#5", "B1")]]),
    dict(name="Dreamy", about="G major with sevenths, and the borrowed minor iv — the chord that aches",
         chords="Gmaj9 · Bm11 · Cmaj9 · Cm6",
         units=[[("G3 D4 F#4 A4 B4 F#5", "G1")] * 2 + [("G3 D4 F#4 A4 B4 D5", "G1")],
                [("B2 F#3 A3 D4 E4 A4 F#5", "B1")] * 2 + [("B2 F#3 A3 D4 E4 A4 E5", "B1")],
                [("C3 G3 B3 E4 G4 D5 E5", "C2")] * 2 + [("C3 G3 B3 E4 G4 D5 G5", "C2")],
                [("C3 G3 A3 Eb4 G4 C5 Eb5", "C2")] * 2 + [("C3 G3 A3 Eb4 G4 C5 D5", "C2")]]),
    dict(name="Lifted", about="suspensions under one held top note, each resolving on the third hit",
         chords="Em7 · Cadd9 · Gsus2 → G · Dsus4 → D",
         units=[[("E3 B3 D4 G4 B4 D5 G5", "E1")] * 3,
                [("C3 G3 D4 E4 G4 D5 G5", "C2")] * 3,
                [("G3 D4 A4 D5 G5", "G1")] * 2 + [("G3 D4 B4 D5 G5", "G1")],
                [("D3 A3 D4 G4 A4 D5 G5", "D2")] * 2 + [("D3 A3 D4 F#4 A4 D5 F#5", "D2")]]),
    dict(name="Falling", about="one chord held while the bass steps down under it — a line cliché to the dominant",
         chords="Em(add9) · Em/D♯ · Em7/D · C♯ø7 · Cmaj7 · B7sus4 → B7",
         units=[[("E3 B3 E4 G4 B4 F#5", "E2")] * 2 + [("D#3 B3 E4 G4 B4 F#5", "D#2")],
                [("D3 B3 E4 G4 B4 F#5", "D2")] * 2 + [("C#3 B3 E4 G4 B4 E5", "C#2")],
                [("C3 B3 E4 G4 B4 E5", "C2")] * 2 + [("C3 B3 E4 G4 B4 G5", "C2")],
                [("B2 A3 E4 F#4 B4 E5", "B1")] * 2 + [("B2 A3 D#4 F#4 B4 D#5", "B1")]]),
]


# ── the MELODY over the moods the user loved (drops-003, rewritten for drops-004) ───────────────────────────────────
# A lead on its own strip. drops-003's was as straight as the saw and its lines moved by step; the user: "for the lead
# it need portamento and try different melody that is more emotion". So the lead SLIDES (legato, an 80 ms glide, a
# late vibrato), and each melody is built on a device that carries feeling: a reach (a leap up to a held note), a sigh
# (a leaning note falling a step onto the chord), the dark ♭6 → 5, the leading tone left hanging. Each is four two-bar
# units, then three and a last that lands home. Written in order, 'B4:1 E5:1 r:.5 …', a unit 8 beats; the chords hit
# on beats 1, 4 and 6 of the unit, and from beat 7 the lead is alone.
MELODIES = [
    dict(mood="Longing", shape="the cry", how="a low start, a climb, then a leap up to a note held alone; the third "
         "phrase reaches E6, the fourth falls back in sighs",
         units=["B4:1 E5:1 r:.5 F#5:.5 G5:2 F#5:1 B5:2", "A5:.5 G5:1.5 r:.5 E5:.5 F#5:2 E5:1 G5:2",
                "E5:1 A5:1 r:.5 B5:.5 C6:2 B5:1 E6:2", "E6:.5 B5:1.5 r:.5 A5:.5 F#5:2 D#5:1 F#5:2"],
         last="E6:.5 B5:1.5 r:.5 A5:.5 B5:2 D#6:1 F#6:2", home="E6"),
    dict(mood="Longing", shape="sighs", how="pairs of notes falling a step, the first leaning on the chord, the second "
         "resolving; the dark G → F♯ over the dominant",
         units=["A5:1 G5:1 r:.5 B5:.5 D6:1 B5:1 A5:1 G5:2", "F#5:1 E5:1 r:.5 G5:.5 B5:1 G5:1 F#5:1 E5:2",
                "B5:1 A5:1 r:.5 C6:.5 E6:1 C6:1 B5:1 A5:2", "G5:1 F#5:1 r:.5 A5:.5 B5:1 A5:1 F#5:1 D#5:2"],
         last="G5:1 F#5:1 r:.5 A5:.5 B5:1 A5:1 B5:1 D#6:2", home="E6"),
    dict(mood="Falling", shape="tears", how="a held note, then a line that falls with the bass; each phrase starts a "
         "third higher (B, D, E) before the last climbs to the leading tone and hangs",
         units=["B5:2 r:.5 B5:.5 A5:.5 G5:1.5 F#5:1 E5:2", "D6:2 r:.5 D6:.5 C6:.5 B5:1.5 G5:1 E5:2",
                "E6:2 r:.5 E6:.5 D6:.5 C6:1.5 B5:1 G5:2", "F#5:2 r:.5 F#5:.5 G5:.5 A5:1.5 B5:1 D#6:2"],
         last="F#5:2 r:.5 F#5:.5 G5:.5 A5:1.5 B5:1 D#6:2", home="E6"),
    dict(mood="Falling", shape="the reach", how="one long low note, then a slide up a fifth, a sixth, a minor sixth "
         "to the next — the portamento carries it — and two notes falling away",
         units=["E5:3 B5:2 A5:.5 G5:.5 F#5:2", "D5:3 B5:2 A5:.5 G5:.5 E5:2", "E5:3 C6:2 B5:.5 A5:.5 G5:2",
                "F#5:3 B5:2 A5:.5 B5:.5 D#6:2"],
         last="F#5:3 B5:2 D#6:.5 E6:.5 F#6:2", home="E6"),
    dict(mood="Dreamy", shape="the ache", how="a gentle falling pair, a rise to a held note; over the borrowed Cm6 the "
         "line reaches E♭ — the one note outside the key — and holds it",
         units=["B5:1 A5:1 r:.5 B5:.5 D6:2 B5:1 A5:2", "A5:1 F#5:1 r:.5 A5:.5 B5:2 A5:1 F#5:2",
                "B5:1 G5:1 r:.5 B5:.5 E6:2 D6:1 B5:2", "C6:1 G5:1 r:.5 C6:.5 Eb6:2 D6:1 C6:2"],
         last="C6:1 G5:1 r:.5 C6:.5 Eb6:2 D6:1 A5:2", home="G5"),
]
LEAD = dict(osc1__wave="saw", osc1__unison=3, osc1__detune=0.18, osc1__blend=0.5, osc1__spread=0.3, osc1__level=0.8,
            osc2__wave="saw", osc2__octave=1, osc2__level=0.2, filter__mode='"lowpass 24"', filter__cutoff=9000,
            filter__res=0.05, voice__drift=0.1,
            # the portamento: one voice that slides to the next note in 80 ms without striking again, and a vibrato
            # that comes in a quarter of a second after a phrase begins
            voice__mode="legato", voice__glide=80, amp__attack=12, amp__release=60, lfo__shape="sine", lfo__rate=5.3,
            lfo__fade=280, lfo__pitch=0.14)


_SLIDE = dict(voice__mode="legato", voice__drift=0.1, amp__attack=12, amp__release=60)
_BELL = dict(algorithm=5, feedback=0, op1__ratio=1, op1__fine=0, op1__level=1, op1__attack=1, op1__decay=1100,
             op1__sustain=0, op1__release=700, op2__ratio=4, op2__level=0.16, op2__attack=1, op2__decay=180,
             op2__sustain=0, op2__release=150, op3__ratio=4, op3__fine=0, op3__level=0.1, op3__attack=1, op3__decay=500,
             op3__sustain=0, op3__release=400, op4__ratio=1, op4__level=0, volume=-9)
# Five LEAD designs, learned from how future bass leads are built (references/research.md ## The lead). Each is layers
# (instrument, name, semitone shift, patch, "slide" — one legato voice, tied notes overlap — or "strike" — every note
# struck) on one Lead bus, the bus's own effects, then an echo and a room shared by all five.
LEADS = [
    dict(name="Glass stack", source="the stacked lead of Flume-style tracks: a detuned saw, pulses beside it and an octave up, "
         "a breath of noise, each through a 24 dB low-pass that an envelope opens and a fast LFO flutters",
         layers=[("analog", "body", 0, dict(_SLIDE, osc1__wave="saw", osc1__unison=7, osc1__detune=0.3, osc1__blend=0.6,
                                            osc1__spread=0.7, osc1__level=0.8, osc2__wave="pulse", osc2__width=0.35,
                                            osc2__unison=3, osc2__detune=0.2, osc2__spread=0.5, osc2__level=0.45,
                                            sub__noise=0.05, filter__mode='"lowpass 24"', filter__cutoff=2400,
                                            filter__res=0.22, filter__env=2.2, filter__keytrack=0.5, fenv__attack=4,
                                            fenv__decay=420, fenv__sustain=0.45, fenv__release=200, lfo__shape="sine",
                                            lfo__sync='"1/32"', lfo__cutoff=0.22, lfo__width=0.12, voice__mode="mono",
                                            voice__glide=140, volume=-10), "slide"),
                 ("analog", "octave", 12, dict(_SLIDE, osc1__wave="pulse", osc1__width=0.3, osc1__unison=5, osc1__detune=0.25,
                                               osc1__blend=0.6, osc1__spread=0.9, osc1__level=0.8, osc2__level=0,
                                               filter__mode='"lowpass 24"', filter__cutoff=3200, filter__res=0.15,
                                               filter__env=1.5, fenv__attack=4, fenv__decay=300, fenv__sustain=0.4,
                                               voice__mode="mono", voice__glide=140, volume=-19), "slide")],
         fx=[],
         recipe="a 7-voice saw with a 3-voice pulse beside it and a breath of noise, and a 5-voice pulse an octave up, "
                "quiet; a 24 dB ladder low at 2.4 kHz that an envelope opens 2 octaves at every note and lets fall in "
                "0.4 s, fluttered by an LFO at a 32nd note; it slides 140 ms"),
    dict(name="Seasick", source="the wobbling lead of Flume-style tracks: a saw an octave down under a saw, a square sub, a "
         "slow attack, a slow LFO bending the tuning, and a long portamento — 'the most important part'",
         layers=[("analog", "wobble", 0, dict(_SLIDE, osc1__wave="saw", osc1__octave=-1, osc1__unison=3, osc1__detune=0.25,
                                              osc1__blend=0.6, osc1__spread=0.5, osc1__level=0.7, osc2__wave="saw",
                                              osc2__unison=5, osc2__detune=0.32, osc2__blend=0.7, osc2__spread=0.9,
                                              osc2__level=0.8, sub__level=0.25, sub__wave="square", sub__octave=-1,
                                              filter__mode='"lowpass 24"', filter__cutoff=5200, filter__res=0.1,
                                              filter__keytrack=0.4, amp__attack=110, amp__decay=690, amp__sustain=0.74,
                                              amp__release=500, lfo__shape="sine", lfo__rate=1.1, lfo__pitch=0.3,
                                              voice__glide=220, voice__drift=0.3, volume=-10), "slide")],
         fx=[("chorus", {})],
         recipe="three saws an octave down under five saws, a square sub; it swells in over 110 ms and settles; an LFO "
                "at 1.1 Hz bends the tuning ±0.3 semitone the whole time (the seasick wobble); it slides 220 ms to "
                "every next note; a chorus"),
    dict(name="Voice", source="the vocal-chop lead future bass is known for, made by a formant filter instead of a sample "
         "(and the user has rated the vocal chop good twice)",
         layers=[("analog", "voice", 0, dict(_SLIDE, osc1__wave="saw", osc1__unison=3, osc1__detune=0.12, osc1__blend=0.4,
                                             osc1__spread=0.4, osc1__level=0.8, osc2__wave="pulse", osc2__width=0.3,
                                             osc2__level=0.4, sub__noise=0.03, filter__mode='"lowpass 24"',
                                             filter__cutoff=7000, filter__res=0.05, amp__attack=25, amp__release=120,
                                             lfo__shape="sine", lfo__rate=5.6, lfo__fade=220, lfo__pitch=0.22,
                                             voice__glide=90, volume=-10), "slide")],
         fx=[("vowel", dict(resonance=1.0, glide=45, mix=1, output=8))], vowel=True,
         recipe="three saws and a pulse through the vowel filter: held notes open to AH, quick ones alternate EE and OH, "
                "and the mouth takes 45 ms to move; it slides 90 ms, with a late, deeper vibrato — it sings the line"),
    dict(name="Bite", source="the detuned mono lead of future house and bass (Don Diablo style): two saws a few cents apart, "
         "a low-pass closed low with a short envelope, a hard clip AFTER the filter to bring the harmonics back, a "
         "phaser, a quarter-note echo",
         layers=[("analog", "bite", 0, dict(osc1__wave="saw", osc1__level=0.8, osc1__unison=1, osc2__wave="saw",
                                            osc2__fine=16, osc2__level=0.8, osc2__unison=1, filter__mode='"lowpass 24"',
                                            filter__cutoff=950, filter__res=0.18, filter__env=2.4, filter__keytrack=0.6,
                                            filter__drive=6, fenv__attack=1, fenv__decay=200, fenv__sustain=0.35,
                                            fenv__release=120, amp__attack=3, amp__release=80, lfo__shape="sine",
                                            lfo__rate=6.5, lfo__fade=150, lfo__pitch=0.12, voice__mode="mono",
                                            voice__glide=110, voice__phase="free", volume=-10), "slide")],
         fx=[("saturator", dict(drive=18, curve="hard", mix=1, output=-8)),
             ("phaser", dict(rate=0.25, center=1400, depth=1.5, stages=4, feedback=0.3, stereo=90, mix=0.35)),
             ("eq", dict(highshelf__freq=5000, highshelf__gain=3))], echo=1.0,
         recipe="two saws 16 cents apart, one voice; the ladder closed to 950 Hz and snapped open by a 200 ms envelope "
                "at every note; then clipped hard (18 dB) so it growls, a slow phaser, a quarter-note echo; it slides "
                "110 ms and every note strikes again"),
    dict(name="Bell & air", source="the layered lead the genre's guides describe: a bell or pluck striking each note for the "
         "attack, a soft saw gliding under it for the body, a sine below",
         layers=[("fm", "bell", 12, _BELL, "strike"),
                 ("analog", "air", 0, dict(_SLIDE, osc1__wave="saw", osc1__unison=5, osc1__detune=0.22, osc1__blend=0.6,
                                           osc1__spread=0.9, osc1__level=0.8, osc2__wave="sine", osc2__octave=-1,
                                           osc2__level=0.45, filter__mode='"lowpass 24"', filter__cutoff=3400,
                                           filter__res=0.08, filter__keytrack=0.5, amp__attack=45, amp__release=180,
                                           lfo__shape="sine", lfo__rate=5.3, lfo__fade=300, lfo__pitch=0.14,
                                           voice__glide=100, volume=-13), "slide")],
         fx=[],
         recipe="an FM bell (harmonic, ratio 4) an octave up striking every note, over five soft saws that swell in "
                "45 ms and slide 100 ms, a sine an octave below; the bell gives the note, the saw carries it"),
]
PLAIN = dict(name="Plain", source="", layers=[("analog", "lead", 0, None, "slide")], fx=[],
             recipe="three saws close together and a quiet octave above, one voice with PORTAMENTO (it slides to each "
                    "next note in 80 ms), a late vibrato")


# The PLUCK that answers the lead: struck, gone in a quarter of a second, many voices (so its notes ring over each other)
_PLUCK = dict(osc1__wave="saw", osc1__unison=3, osc1__detune=0.16, osc1__blend=0.5, osc1__spread=0.8, osc1__level=0.8,
              osc2__wave="pulse", osc2__width=0.4, osc2__octave=1, osc2__level=0.25, filter__mode='"lowpass 24"',
              filter__cutoff=500, filter__res=0.2, filter__env=4.6, filter__keytrack=0.7, fenv__attack=0, fenv__decay=170,
              fenv__sustain=0, fenv__release=150, amp__attack=1, amp__decay=320, amp__sustain=0, amp__release=260,
              voice__mode="poly", voice__glide=0, velocity=0.3, volume=0)
PLUCKS = {
    "glass": ("three saws and a pulse an octave up, the ladder shut at 500 Hz and flicked open 4.6 octaves for 170 ms at "
              "every note, gone in 0.3 s", _PLUCK),
    "hollow": ("two pulses (a square and a narrow one a fifth of an octave apart in width), rounder and woodier, the "
               "ladder flicked open for 130 ms with some ring",
               dict(_PLUCK, osc1__wave="pulse", osc1__width=0.5, osc1__unison=3, osc1__detune=0.12, osc2__wave="pulse",
                    osc2__width=0.25, osc2__octave=0, osc2__level=0.5, filter__cutoff=700, filter__res=0.38,
                    filter__env=3.8, fenv__decay=130, amp__decay=260)),
}
# CLEAN AND THICK plucks (the user on drops-006: "not this kind of pluck, should be clean and thick pluck"): no
# resonance, no drive, the filter barely moving — the thickness is unison, octaves and a body under the note, and the
# pluck is the AMP envelope falling, not a filter flicking.
_THICK = dict(osc1__wave="saw", osc1__unison=7, osc1__detune=0.3, osc1__blend=0.7, osc1__spread=0.9, osc1__level=0.8,
              osc2__wave="saw", osc2__unison=7, osc2__detune=0.3, osc2__blend=0.7, osc2__spread=0.9, osc2__octave=1,
              osc2__level=0.4, filter__mode='"lowpass 12"', filter__cutoff=3800, filter__res=0, filter__env=1.4,
              filter__keytrack=0.6, fenv__attack=0, fenv__decay=260, fenv__sustain=0, fenv__release=200, amp__attack=1,
              amp__decay=480, amp__sustain=0, amp__release=300, voice__mode="poly", voice__glide=0, voice__drift=0.1,
              velocity=0.3, volume=-4)
THICK = ["supersaw", "body", "round", "deep", "open"]
PLUCKS.update({
    "supersaw": ("Supersaw pluck — seven saws and seven more an octave up, a gentle 12 dB low-pass at 3.8 kHz that "
                 "closes a little as the note falls away over half a second", _THICK),
    "body": ("Saw and sine — five saws with a sine at the same pitch inside them and a sine an octave below: the sines "
             "are the weight, the saws the edge; falls away in 0.45 s",
             dict(_THICK, osc1__unison=5, osc1__detune=0.24, osc2__wave="sine", osc2__unison=1, osc2__octave=0,
                  osc2__level=0.6, sub__level=0.45, sub__wave="sine", sub__octave=-1, filter__cutoff=3000,
                  amp__decay=450)),
    "round": ("Round — three triangles under five saws an octave up, the low-pass at 2.6 kHz: soft-edged and full, "
              "the cleanest of the five",
              dict(_THICK, osc1__wave="triangle", osc1__unison=3, osc1__detune=0.14, osc1__level=0.9, osc2__unison=5,
                   osc2__detune=0.22, osc2__level=0.3, filter__cutoff=2600, filter__env=1.0, amp__decay=520)),
    "deep": ("Deep — seven saws with three saws an octave BELOW them, and a short tail that keeps ringing quietly: "
             "the thickest",
             dict(_THICK, osc2__octave=-1, osc2__unison=3, osc2__detune=0.18, osc2__spread=0.4, osc2__level=0.55,
                  filter__cutoff=3200, amp__decay=560, amp__sustain=0.12, amp__release=380)),
    "open": ("Open — saws at the note and an octave up with a sine below, the filter off: nothing shaped but the "
             "level falling in 0.38 s; the brightest",
             dict(_THICK, osc2__level=0.5, sub__level=0.4, sub__wave="sine", sub__octave=-1, filter__mode="off",
                  filter__env=0, amp__decay=380)),
})
# CALL AND RESPONSE: the loved lead (LEADS[0], the Glass stack) and a pluck share the melody. Written as two lines over
# the same four units; "r" rests; each unit is 8 beats (two bars: kick 1, 4, 6 — snare 3, 7).
CALLS = [
    dict(mood="Longing", name="Question and answer", sound="glass",
         how="the lead asks in the first bar and holds its last note; the pluck answers in the second bar, a quick "
             "falling figure that turns back up",
         lead=dict(units=["B4:1 E5:1 G5:1 F#5:2 r:3", "G5:1 E5:1 B5:1 G5:2 r:3", "E5:1 A5:1 C6:1 B5:2 r:3",
                          "E6:1 B5:1 A5:1 F#5:2 r:3"], last="E6:1 B5:1 A5:1 B5:2 r:3", home="E6"),
         pluck=dict(units=["r:4.5 B5:.5 G5:.5 F#5:.5 E5:.5 F#5:.5 B5:1", "r:4.5 C6:.5 B5:.5 G5:.5 E5:.5 G5:.5 B5:1",
                           "r:4.5 E6:.5 C6:.5 B5:.5 A5:.5 B5:.5 E6:1", "r:4.5 B5:.5 A5:.5 F#5:.5 D#5:.5 F#5:.5 A5:1"],
                    last="r:4.5 B5:.5 D#6:.5 F#6:.5 D#6:.5 B5:.5 D#6:1", home="E6")),
    dict(mood="Longing", name="Echo", sound="glass",
         how="the lead sings a three-note rise, twice a unit; each time the pluck throws the same three notes back an "
             "octave higher and twice as fast, in the gap",
         lead=dict(units=["B4:1.5 E5:.5 G5:1 r:1 E5:1 F#5:.5 G5:.5 r:2", "B4:1.5 E5:.5 G5:1 r:1 F#5:1 G5:.5 B5:.5 r:2",
                          "C5:1.5 E5:.5 A5:1 r:1 G5:1 A5:.5 B5:.5 r:2", "B4:1.5 E5:.5 F#5:1 r:1 F#5:1 D#5:.5 A5:.5 r:2"],
                   last="B4:1.5 E5:.5 F#5:1 r:1 F#5:1 B5:.5 D#6:.5 r:2", home="E6"),
         pluck=dict(units=["r:3 B5:.25 E6:.25 G6:.5 r:2 E6:.25 F#6:.25 G6:.5 r:1",
                           "r:3 B5:.25 E6:.25 G6:.5 r:2 F#6:.25 G6:.25 B6:.5 r:1",
                           "r:3 C6:.25 E6:.25 A6:.5 r:2 G6:.25 A6:.25 B6:.5 r:1",
                           "r:3 B5:.25 E6:.25 F#6:.5 r:2 F#6:.25 D#6:.25 A6:.5 r:1"],
                    last="r:3 B5:.25 E6:.25 F#6:.5 r:2 B5:.25 D#6:.25 F#6:.5 r:1", home="E6")),
    dict(mood="Falling", name="The pluck calls", sound="hollow",
         how="the other way round: the pluck runs up and down the chord in the first bar, and the lead answers with one "
             "long note that slides down — higher every time, to G6",
         lead=dict(units=["r:4 B5:2.5 A5:.5 G5:1", "r:4 E6:2.5 B5:.5 G5:1", "r:4 G6:2.5 E6:.5 B5:1", "r:4 F#6:1 D#6:2 B5:1"],
                   last="r:4 F#6:1 D#6:1.5 F#6:.5 B5:1", home="E6"),
         pluck=dict(units=["E5:.5 G5:.5 B5:.5 F#6:.5 E6:.5 B5:.5 G5:1 r:4", "E5:.5 G5:.5 B5:.5 G6:.5 F#6:.5 B5:.5 G5:1 r:4",
                           "E5:.5 G5:.5 C6:.5 G6:.5 E6:.5 C6:.5 B5:1 r:4", "F#5:.5 B5:.5 E6:.5 F#6:.5 E6:.5 B5:.5 A5:1 r:4"],
                    last="F#5:.5 B5:.5 E6:.5 F#6:.5 E6:.5 B5:.5 A5:1 r:4", home="B5")),
    dict(mood="Dreamy", name="Trading", sound="hollow",
         how="short turns: the lead two beats, the pluck two beats, four times a unit — the pluck climbing the chord "
             "the lead just leaned on; over the borrowed Cm6 both reach for E♭",
         lead=dict(units=["B5:1 A5:1 r:2 D6:1.5 B5:.5 r:2", "A5:1 F#5:1 r:2 B5:1.5 A5:.5 r:2", "B5:1 G5:1 r:2 E6:1.5 D6:.5 r:2",
                          "C6:1 G5:1 r:2 Eb6:1.5 D6:.5 r:2"], last="C6:1 G5:1 r:2 Eb6:1.5 D6:.5 r:2", home="G5"),
         pluck=dict(units=["r:2 F#5:.5 A5:.5 D6:.5 B5:.5 r:2 A5:.5 B5:.5 D6:.5 F#6:.5",
                           "r:2 D5:.5 F#5:.5 A5:.5 E6:.5 r:2 F#5:.5 A5:.5 B5:.5 D6:.5",
                           "r:2 E5:.5 G5:.5 B5:.5 D6:.5 r:2 G5:.5 B5:.5 D6:.5 G6:.5",
                           "r:2 Eb5:.5 G5:.5 A5:.5 C6:.5 r:2 G5:.5 A5:.5 C6:.5 Eb6:.5"],
                    last="r:2 Eb5:.5 G5:.5 A5:.5 C6:.5 r:2 Eb6:.5 D6:.5 C6:.5 A5:.5", home="D6")),
    dict(mood="Longing", name="Apart, then together", sound="glass",
         how="two units of question and answer, then the pluck stops answering and joins: both play the climb to E6 and "
             "the fall as one sound — the pluck the attack, the lead the body",
         lead=dict(units=["G5:1.5 F#5:.5 E5:1 B5:2 r:3", "G5:1.5 F#5:.5 E5:1 C6:2 r:3", "E5:1 A5:1 B5:1 C6:2 B5:1 E6:2",
                          "E6:.5 B5:1.5 r:.5 A5:.5 F#5:2 D#5:1 F#5:2"], last="E6:.5 B5:1.5 r:.5 A5:.5 B5:2 D#6:1 F#6:2", home="E6"),
         pluck=dict(units=["r:5 B5:.5 D6:.5 F#6:.5 G6:.5 F#6:1", "r:5 C6:.5 E6:.5 F#6:.5 G6:.5 E6:1",
                           "E5:1 A5:1 B5:1 C6:2 B5:1 E6:2", "E6:.5 B5:1.5 r:.5 A5:.5 F#5:2 D#5:1 F#5:2"],
                    last="E6:.5 B5:1.5 r:.5 A5:.5 B5:2 D#6:1 F#6:2", home="E6")),
]


def call_lines(c):
    """A CALLS entry as two melodies (the lead's, the pluck's) in MELODIES' shape, for melody_notes / melody_fit."""
    return (dict(c["lead"], mood=c["mood"], shape=c["name"], how=c["how"]),
            dict(c["pluck"], mood=c["mood"], shape=c["name"] + " (pluck)", how=""))


# THE SUPERSAW WITH AN LFO (the user, 2026-11-28: "try future bass with 1 4 6 kick like before, but super saw lfo"):
# the chord saw's filter or level moved by the synth's own LFO, locked to the beat. Merged over the design's saw patch.
# The LFO's SHAPE (the user on drops-008: "lfo is not correct … should be peak at first then fade to end wave … eight
# note wob is correct"): a falling ramp — the synth's `saw` LFO is 1 − 2t, +1 at the start of every eighth and falling
# to −1 (AnalogSynth (A18)) — so each eighth OPENS at once and closes; on the level the same LFO is full at the start
# and fades. Every option is that shape at an eighth note; they differ in how ELECTRONIC the saw is.
_LP = dict(filter__mode='"lowpass 24"', filter__keytrack=0.3, lfo__fade=0, lfo__shape="saw", lfo__sync='"1/8"')
_DIG = dict(_LP, filter__cutoff=1400, filter__res=0.3, lfo__cutoff=2.4, lfo__amp=0.6, sub__noise=0)


def _dig(bits, rate, mix, clip=8):
    return [("saturator", dict(drive=clip, curve="hard", mix=1, output=-5)),
            ("bitcrusher", dict(bits=bits, rate=rate, mix=mix, output=0))]


# drops-009's "Digital" saw ("option 5's saw have pontential") is now the saw of every option — the falling
# eighth-note filter and level, clipped, crushed — taken a little further each way.
SAW_LFOS = [
    dict(name="Digital", words="the saw you picked, as it was: clipped hard and half-crushed to 9 bits at 16 kHz",
         patch=_DIG, fx=_dig(9, 16000, 0.5)),
    dict(name="Digital, harder", words="the same crushed further — 8 bits at 12 kHz, mixed in more, clipped harder",
         patch=_DIG, fx=_dig(8, 12000, 0.65, 11)),
    dict(name="Digital pulse", words="the octave stack as pulse waves whose width the LFO sweeps, then the same clip "
         "and crush — hollow and gritty",
         patch=dict(_DIG, osc2__wave="pulse", osc2__width=0.5, lfo__width=0.3), fx=_dig(9, 16000, 0.5)),
    dict(name="Digital acid", words="more resonance (0.45) and a driven ladder under the clip and crush — each eighth "
         "zaps down",
         patch=dict(_DIG, filter__res=0.45, filter__drive=6, lfo__cutoff=2.8), fx=_dig(9, 16000, 0.5)),
    dict(name="Digital, cleaner", words="the lightest: 10 bits at 20 kHz, less of it, a softer clip — the grit as a "
         "colour, not the whole sound",
         patch=_DIG, fx=_dig(10, 20000, 0.35, 5)),
]
# a different mood and a different SIMPLE melody for each option (lessons 20–22: a few long chord notes, one rhythm, the
# last bar of every eight a slow run)
LFO_SONGS = [
    dict(mood="Longing", shape="Three notes", how="a long note, a step down, a long note",
         units=["B5:3 A5:1 G5:4", "B5:3 A5:1 G5:4", "C6:3 B5:1 A5:4", "B5:3 A5:1 F#5:1 A5:1 B5:1 D#6:1"],
         last="B5:3 A5:1 F#5:1 B5:1 D#6:1 F#6:1", home="E6"),
    dict(mood="Bittersweet", shape="Climb to the ninth", how="two steps up the chord and a long note on its ninth",
         units=["E5:2 G5:2 D6:4", "F#5:2 A5:2 E6:4", "G5:2 B5:2 F#6:4", "G5:2 B5:2 D6:1 B5:1 A5:1 G5:1"],
         last="G5:2 B5:2 D6:1 B5:1 G5:1 D5:1", home="E5"),
    dict(mood="Dreamy", shape="Hold and fall", how="one long high note, then two notes falling away; E♭ over the borrowed chord",
         units=["D6:5 B5:1 A5:2", "D6:5 A5:1 F#5:2", "E6:5 D6:1 B5:2", "Eb6:4 D6:1 C6:1 A5:1 G5:1"],
         last="Eb6:4 D6:1 C6:1 A5:1 C6:1", home="D6"),
    dict(mood="Lifted", shape="One note a bar", how="one held note in each bar",
         units=["G5:4 B5:4", "G5:4 E5:4", "D5:4 G5:4", "D6:4 A5:1 F#5:1 A5:1 D6:1"],
         last="D6:4 A5:1 F#5:1 A5:1 F#5:1", home="G5"),
    dict(mood="Falling", shape="The rise", how="two short steps up and a long note at the top, over the falling bass",
         units=["E5:2 G5:2 B5:4", "E5:2 G5:2 B5:4", "E5:2 G5:2 C6:4", "F#5:2 A5:2 B5:1 A5:1 F#5:1 D#5:1"],
         last="F#5:2 A5:2 B5:1 D#6:1 F#6:1 D#6:1", home="E6"),
]
# the LEAD's LFO ("lead lfo too"): the same falling eighth-note ramp on the lead's filter and level
LEAD_LFO = dict(lfo__shape="saw", lfo__sync='"1/8"', lfo__fade=0, lfo__cutoff=1.6, lfo__amp=0.55, lfo__width=0, lfo__pitch=0)
# the HEAVIER bass for --lfo ("the bass is need to be more heavier"): the loved 808 pushed harder, and over it a saw
# and a square through a driven ladder, saturated — both with every kick
HEAVY = dict(osc1__wave="saw", osc1__octave=-1, osc1__unison=1, osc1__level=0.8, osc2__wave="pulse", osc2__width=0.5,
             osc2__octave=-1, osc2__unison=1, osc2__level=0.55, sub__level=0, filter__mode='"lowpass 24"',
             filter__cutoff=460, filter__res=0.12, filter__drive=22, filter__env=2.0, filter__keytrack=0.3, fenv__attack=0,
             fenv__decay=320, fenv__sustain=0.4, fenv__release=80, amp__attack=1, amp__decay=0, amp__sustain=1,
             amp__release=40, velocity=0, voice__mode="mono", voice__glide=0, voice__phase="reset", voice__drift=0, volume=-8)
SAW_FX = []             # set by main() for --lfo: effects on the saw bus, before its EQ
SAW_EXTRA = {}          # set by main() for --lfo: the LFO patch laid over every saw layer
EASY = dict(mood="Longing", shape="Three notes", how="a long note, a step down, a long note — the same rhythm every two "
            "bars (a simple line, so the saw is what you judge)",
            units=["B5:3 A5:1 G5:4", "B5:3 A5:1 G5:4", "C6:3 B5:1 A5:4", "B5:3 A5:1 F#5:4"], last="B5:3 A5:1 B5:4", home="E6")


def melody_notes(mel):
    """[(beat, length, midi)] over the 16 bars: the four units, then three of them and the last, then home."""
    out, texts = [], mel["units"] + mel["units"][:3] + [mel["last"]]
    for u, text in enumerate(texts):
        t = 8.0 * u
        for tok in text.split():
            name, d = tok.split(":")
            if name != "r":
                out.append((t, float(d), midi(name)))
            t += float(d)
        assert abs(t - 8.0 * (u + 1)) < 1e-6, f"{mel['mood']} {mel['shape']} unit {u + 1}: {t - 8.0 * u} beats, not 8"
    return out + [(64.0, 2.0, midi(mel["home"]))]


def melody_fit(mel):
    """The share of the melody's strong notes (on a chord's hit, or a beat or longer) that are in the chord under them."""
    mood = next(m for m in MOODS if m["name"] == mel["mood"])
    seg, _ = mood_segments(mood)
    strong = ok = 0
    for t, d, p in melody_notes(mel):
        under = [c for a, ln, c in seg if a <= t + 1e-9][-1]
        pcs = {midi(n) % 12 for n in under[1].split()}
        if d >= 1 or any(abs(t - a) < 1e-9 for a, _, _ in seg):
            strong += 1
            ok += p % 12 in pcs
    return round(ok / max(1, strong), 2)


def mood_segments(mood):
    """(beat, length, chord (name, notes, bass)) for the 16 bars: four units, twice; and the snares."""
    seg, snares = [], []
    for u in range(8):
        t0 = 8 * u
        for i, k in enumerate(KICK8):
            nxt = min([x for x in KICK8 + SNARE8 if x > k] + [8])
            notes, bass = mood["units"][u % 4][i]
            seg.append((t0 + k, nxt - k, (mood["name"], notes, bass)))
        snares += [t0 + x for x in SNARE8]
    notes, bass = mood["units"][0][0]
    seg.append((64, 2.0, (mood["name"], notes, bass)))               # home once more, held to where a snare would be
    return seg, snares


# each element alone, LUFS over the 16 bars — the kick and the bass up front (they are what is judged, with the saw)
TARGETS = {"Kick": -10.0, "Snare": -16.5, "Saw": -14.5, "Bass": -16.0, "Lead": -14.5, "Pluck": -17.0}   # the kick at −13: its hit only 4.8 dB over the mix


def segments():
    """Every kick of the 16 bars and how long the chord it strikes lasts: to the next kick, to the snare (beat 3),
    or to the bar's end; a kick on the last eighth strikes the NEXT bar's chord (a push). → (beat, length, chord)."""
    out = []
    for b in range(BARS):
        ks = KICKS[b % 2]
        for i, k in enumerate(ks):
            nxt = ks[i + 1] if i + 1 < len(ks) else 4.0
            end = min(nxt, 2.0) if k < 2.0 else nxt
            out.append((4 * b + k, end - k, CHORDS[((b + 1) if k >= 3.5 else b) % 4]))
    out.append((4 * BARS, 2.0, CHORDS[0]))                           # the last hit, held to where a snare would be
    return out


def arrange(opt, kick_file, trim=None, seg=None, snares=None, kick_words=None, melody=None, lead=None, pluck=None):
    S, E, L = Script(), [], 4 * BARS + 8
    line = S.line
    seg = seg or segments()
    snares = snares if snares is not None else [4 * b + 2 for b in range(BARS)]
    line(f"# drop — {opt['name']}: {opt['about']} (arstro.solaris.sound.design drops.py) — E minor, {BPM} bpm half time")
    line(f'project new drop.slp --bpm {BPM} --name "Drop — {opt["name"]}"')
    # the kick: the user's sample, as recorded
    kick, kdv, kpt = S.new('ch'), S.new('dv'), S.new('pt')
    S.new('ac')
    line(f'clip add --instrument sampler --sample "{kick_file}" --at 0 --length {num(L)}')
    line(f'set {kick}.name="Kick" {kpt}.name="Kick" {kpt}.length={num(L)} {kdv}.mode=one-shot {kdv}.release=200 {kdv}.velocity=0')
    S.notes(kpt, [(60, t, 1.0, 127) for t, _, _ in seg])
    E.append({"name": "Kick", "strip": kick, "recipe": f"your sample {os.path.basename(kick_file)}, as recorded, on "
              + (kick_words or "1 · the and of 2 · the and of 4, then 1 · the a of 1 · the and of 2 · the a of 3 · the and of 4")})
    # the snare: tight and bright, a clap on it, a short room
    sn, sdv, spt = S.instrument("drums", "Snare", L)
    S.set(sdv, snare__tune=3, snare__decay=170, snare__tone=0.78, clap__decay=300, clap__tone=0.6, clap__level=-3, volume=3)
    S.notes(spt, [(p, t, 0.25, v) for t in snares for p, v in ((SNARE, 120), (CLAP, 112))])
    S.device(sn, "transient", attack=40, sustain=-10, output=0)
    S.device(sn, "reverb", decay=900, size=70, mix=0.14, lowcut=300, highcut=10000, width=0.8)
    E.append({"name": "Snare", "strip": sn, "recipe": "the Drum Machine's snare tuned up 3 st, 170 ms, bright, with a "
              "clap on it, a sharper attack, a short room — on beat 3"})
    # the saw: its layers on one bus
    bus = S.bus("Saw")
    for kind, part, shift, patch in opt["saw"]:
        ch, dv, pt = S.instrument(kind, f"Saw {part}", L)
        S.set(dv, volume=-10, **dict(dict(FLAT, **patch), **SAW_EXTRA))
        notes = []
        for t, d, (_, voicing, _) in seg:
            ps = [midi(n) for n in voicing.split()]
            ps = ps[:2] if part == "low" else (ps[-1:] if part == "top" else ps)
            notes += [(p + shift, t, max(0.1, d - 0.04), 110) for p in ps]
        S.notes(pt, notes)
        line(f"route {ch} --to {bus}")
    for fkind, fparams in SAW_FX:
        S.device(bus, fkind, **fparams)
    S.device(bus, "eq", lowcut__on="on", lowcut__freq=150, peak1__freq=350, peak1__gain=-2, peak1__q=1)
    S.device(bus, "imager", width=140, mono="on", frequency=200)
    S.device(bus, "reverb", decay=1200, size=90, mix=0.07, lowcut=400, highcut=10000, width=0.9)
    line(f"strip duck {bus} --under {kick} --amount 12")
    S.set(S.new('dv'), release=90)
    E.append({"name": "Saw", "strip": bus, "recipe": opt["saw_note"] + ". Straight: it speaks in 1 ms, holds flat, stops "
              "in 30 ms at the snare; no pitch move. Low cut 150 Hz, nearly dry, ducked 12 dB by the kick"})
    # the bass: the root, with the saw
    kind, shift, patch, fx = opt["bass"]
    bass, bdv, bpt = S.instrument(kind, "Bass", L)
    S.set(bdv, **(dict(MONO, **patch) if kind == "analog" else dict(patch, drive=1, punch=7, tone=2400) if SAW_EXTRA else patch))
    S.notes(bpt, [(midi(root) + shift, t, max(0.1, d - 0.04), 115) for t, d, (_, _, root) in seg])
    for fkind, fparams in fx:
        S.device(bass, fkind, **fparams)
    heavy = ""
    if SAW_EXTRA:                                                    # --lfo: the heavy layer, both on one Bass bus
        bbus = S.bus("Bass")
        line(f"route {bass} --to {bbus}")
        hv, hvd, hvp = S.instrument("analog", "Bass heavy", L)
        S.set(hvd, **HEAVY)
        S.notes(hvp, [(midi(root) + (12 if midi(root) < midi("C2") else 0) + 12, t, max(0.1, d - 0.04), 115)
                      for t, d, (_, _, root) in seg])
        S.device(hv, "saturator", drive=18, curve="tube", mix=1, output=-8)
        S.device(hv, "eq", lowcut__on="on", lowcut__freq=90)
        line(f"route {hv} --to {bbus}")
        S.device(bbus, "saturator", drive=6, curve="tube", mix=1, output=-3)
        S.device(bbus, "eq", lowshelf__freq=90, lowshelf__gain=2.5)
        bass = bbus
        heavy = (" HEAVIER AGAIN: the 808 at full drive with a bigger thump, and an octave over it — now as loud as "
                 "the 808 — a saw and a square through a ladder at 460 Hz driven 22 dB, then a tube; the two glued by "
                 "another tube with +2.5 dB under 90 Hz; 3.5 dB louder in the mix than the first drops")
    S.device(bass, "imager", width=0, mono="on", frequency=200)
    line(f"strip duck {bass} --under {kick} --amount 14")
    S.set(S.new('dv'), release=70)
    E.append({"name": "Bass", "strip": bass, "recipe": opt["bass_note"] + "." + heavy + " With every kick, the saw's "
              "lengths, no glide; mono; ducked 14 dB by the kick and back in ~150 ms"})
    if melody:   # the lead: its layers on one bus, then an echo and a room
        lead = lead or PLAIN
        lbus = S.bus("Lead")
        mn = melody_notes(melody)
        # a note that runs straight into the next overlaps it (so the voice slides); one before a rest ends early
        tied = [i + 1 < len(mn) and abs(mn[i + 1][0] - (t + d)) < 1e-6 for i, (t, d, p) in enumerate(mn)]
        for kind, part, shift, patch, how in lead["layers"]:
            ch, dv, pt = S.instrument(kind, f"Lead {part}", L)
            S.set(dv, **(dict(dict(dict(FLAT, volume=-10), **(patch or LEAD)), **(LEAD_LFO if SAW_EXTRA else {}))
                         if kind == "analog" else patch))
            if how == "slide":
                S.notes(pt, [(p + shift, t, d + 0.06 if tie else max(0.1, d - 0.06), 112) for (t, d, p), tie in zip(mn, tied)])
            else:
                S.notes(pt, [(p + shift, t, max(0.1, d - 0.05), 104) for t, d, p in mn])
            line(f"route {ch} --to {lbus}")
        for fkind, fparams in lead["fx"]:
            fdv = S.device(lbus, fkind, **fparams)
            if fkind == "vowel":                                     # held notes AH, quick ones EE and OH in turn
                au = S.held("Lead · Vowel", [{"t": t, "vowel": 0 if d >= 1.5 else (2 if i % 2 else 3)}
                                             for i, (t, d, p) in enumerate(mn)], L)
                line(f'set {fdv}.vowel="={au}"')
        S.device(lbus, "eq", lowcut__on="on", lowcut__freq=180, peak1__freq=2800, peak1__gain=2, peak1__q=0.8)
        S.device(lbus, "delay", time=num(lead.get("echo", 0.75) * 60000 / BPM), feedback=0.32, tone=4000, mix=0.17)
        S.device(lbus, "reverb", decay=2400, size=120, mix=0.17, lowcut=400, highcut=10000, width=0.9)
        line(f"strip duck {lbus} --under {kick} --amount 4")
        S.set(S.new('dv'), release=90)
        E.append({"name": "Lead", "strip": lbus, "recipe": f"the melody — {melody['shape']}: {melody['how']}. THE SOUND — "
                  f"{lead['name']}: {lead['recipe']}" + ("; ITS LFO: the same falling eighth-note ramp as the saw's on its "
                  "filter and level, so a held note pulses with the chords" if SAW_EXTRA else "") + f"; low cut 180 Hz, an echo, a 2.4 s room; "
                  f"{int(100 * melody_fit(melody))} % of its strong notes are in the chord under them"})
    if pluck:   # the pluck that answers: struck, short, its own echo bouncing side to side
        words, patch = PLUCKS[pluck["sound"]]
        pch, pdv, ppt = S.instrument("analog", "Pluck", L)
        S.set(pdv, **patch)
        pn = melody_notes(pluck)
        S.notes(ppt, [(p, t, max(0.1, d - 0.05), 118 if abs(t - round(t)) < 1e-6 else 100) for t, d, p in pn])
        S.device(pch, "eq", lowcut__on="on", lowcut__freq=160 if pluck["sound"] in THICK else 250, highshelf__freq=6000, highshelf__gain=0 if pluck["sound"] in THICK else 2)
        S.device(pch, "imager", width=130)
        S.device(pch, "delay", time=num(0.75 * 60000 / BPM), feedback=0.3 if pluck["sound"] in THICK else 0.38, tone=5000, mix=0.16 if pluck["sound"] in THICK else 0.24)
        S.device(pch, "reverb", decay=2000, size=110, mix=0.2, lowcut=500, highcut=11000, width=1)
        line(f"strip duck {pch} --under {kick} --amount 3")
        S.set(S.new('dv'), release=90)
        E.append({"name": "Pluck", "strip": pch, "recipe": f"the answer — {words}; a dotted-eighth echo, a room, wide; "
                  f"{int(100 * melody_fit(pluck))} % of its strong notes are in the chord under them"})
    for e in E:
        g = (trim or {}).get(e["name"], 0)
        if g:
            line(f"set {e['strip']}.gain={num(round(g, 1))}")
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
    ap.add_argument("--moods", action="store_true", help="five MOODS (chord sets) on one sound, the user's sparser pattern")
    ap.add_argument("--melody", action="store_true", help="the MELODIES: a lead over the moods the user loved")
    ap.add_argument("--leads", action="store_true", help="the LEADS: five lead SOUNDS on one melody and mood")
    ap.add_argument("--calls", action="store_true", help="CALLS: call and response between the loved lead and a pluck")
    ap.add_argument("--plucks", action="store_true", help="THICK: five clean, thick pluck sounds on the first of CALLS")
    ap.add_argument("--lfo", action="store_true", help="SAW_LFOS: the supersaw's filter or level moved by a beat-locked LFO")
    ap.add_argument("--design", default="808", help="with --moods / --melody: which of OPTIONS plays them")
    a = ap.parse_args()
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(out, exist_ok=True)
    kick = os.path.join(out, "kick.wav")
    if os.path.abspath(a.kick) != kick:
        shutil.copyfile(a.kick, kick)
    env = dict(os.environ, SOLARIS_SETTINGS=os.path.join(out, ".settings.txt"), SOLARIS_RECENTS=os.path.join(out, ".recents"))
    env.pop("DISPLAY", None)
    only = set(x for x in a.only.split(",") if x)
    path = os.path.join(out, "batch.json")
    old = {}
    if os.path.exists(path):
        with open(path) as f:
            old = {m["id"]: m for m in json.load(f).get("melodies", [])}
    items, failed = [], 0
    seconds = round((4 * BARS + 8) * 60 / BPM)
    design = next(o for o in OPTIONS if o["name"].lower() == a.design.lower())
    by = {m["name"]: m for m in MOODS}
    c0 = CALLS[0]
    runs = ([(by[m["mood"]], design, m, LEADS[0], None) for m in LFO_SONGS] if a.lfo else [(by[c0["mood"]], design, call_lines(c0)[0], LEADS[0], dict(call_lines(c0)[1], sound=x)) for x in THICK]
            if a.plucks else [(by[c["mood"]], design, call_lines(c)[0], LEADS[0], dict(call_lines(c)[1], sound=c["sound"])) for c in CALLS]
             if a.calls else [(by[MELODIES[0]["mood"]], design, MELODIES[0], ld, None) for ld in LEADS] if a.leads else
            [(by[x["mood"]], design, x, None, None) for x in MELODIES] if a.melody else
            [(m, design, None, None, None) for m in MOODS] if a.moods else [(None, o, None, None, None) for o in OPTIONS])
    for k, (mood, opt, mel, ld, pl) in enumerate(runs, 1):
        sid = f"sawlfo-{k}" if a.lfo else f"pluck-{k}" if a.plucks else f"call-{k}" if pl else f"lead-{k}" if ld else f"melody-{k}" if mel else (f"mood-{k}" if mood else f"drop-{k}")
        if only and str(k) not in only:
            if sid in old:
                items.append(old[sid])
            continue
        trim, ok, E = {}, True, []
        SAW_EXTRA.clear()
        del SAW_FX[:]
        if a.lfo:
            SAW_EXTRA.update(SAW_LFOS[k - 1]["patch"])
            SAW_FX.extend(SAW_LFOS[k - 1]["fx"])
            TARGETS["Bass"] = -12.5
        for attempt in ((1, 2) if not a.quick else (2,)):
            if mood:
                seg, snares = mood_segments(mood)
                text, E = arrange(opt, kick, trim, seg, snares, "the kick on 1, 4 and 6 of every two bars (the snare on 3 and 7)", mel, ld, pl)
            else:
                text, E = arrange(opt, kick, trim)
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
                print(f"{sid}: FAILED\n{r.stdout[-1500:]}{r.stderr[-600:]}", flush=True)
                break
            lufs = {e["name"]: loudness(os.path.join(out, f"{sid}.{e['strip']}.mp3")) for e in E}
            if attempt == 1:
                trim = {n: max(-18, min(12, round(TARGETS[n] - v, 1))) for n, v in lufs.items() if v is not None and v > -60}
        failed += not ok
        title = (f"LFO drop {k} — {mood['name']} ({mood['chords']}), '{mel['shape']}'; saw: {SAW_LFOS[k - 1]['name']} — {SAW_LFOS[k - 1]['words']}" if a.lfo else f"Pluck {k} — {PLUCKS[pl['sound']][0].split(' — ')[0]}" if a.plucks else f"Call {k} — {mel['shape']}, over {mood['name']}: {mel['how']}" if pl else f"Lead {k} — {ld['name']}: {ld['source']}" if ld else f"Melody {k} — {mel['shape']}, over {mood['name']} ({mood['chords']})" if mel else
                 f"Mood {k} — {mood['name']}: {mood['chords']}" if mood else f"Drop {k} — {opt['name']}: {opt['about']}")
        item = {"id": sid, "title": title, "sound": True, "section": "drop",
                "style": "future-bass", "style_label": "Future bass drop — kick, snare, saw, bass", "bpm": BPM,
                "key": "E minor", "form_label": "sound — drop", "seconds": seconds,
                "devices": ([f"mood: {mood['about']}", f"sound: the {opt['name']} design — {opt['about']}"] if mood else []) +
                           [f"{e['name']}: {e['recipe']}" for e in E], "elements": E,
                "audio": [{"label": "full", "file": f"{sid}.mp3"}] +
                         [{"label": e["name"].lower(), "file": f"{sid}.{e['strip']}.mp3"} for e in E],
                "metrics": {}}
        if ok:
            lufs["mix"] = loudness(os.path.join(out, sid + ".mp3"))
            item["metrics"] = {"lufs": lufs, "trim": trim}
            print(f"{sid} {mood['name'] if mood else opt['name']}: ok — " + " · ".join(f"{n} {v}" for n, v in lufs.items()) +
                  " · trim " + " ".join(f"{n} {v:+g}" for n, v in trim.items()), flush=True)
        items.append(item)
        with open(path, "w") as f:                                   # after every option: the lab shows what is done
            json.dump({"batch": os.path.basename(out), "kind": "sound", "melodies": items}, f, indent=1, ensure_ascii=False)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
