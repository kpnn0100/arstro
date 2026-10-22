#!/usr/bin/env python3
"""futurebass.py — future bass sound design for the approver to judge in the Sound Lab: for each TRACK, a verse and a
build-up running into its drop.

    python3 futurebass.py --out ~/Music/Solaris/sound-lab/fb-NNN [--tracks afterlight,glass,embers,neon]
                          [--only verse,drop] [--cc <solaris-cc>] [--no-render]

A track is its own music and its own sound — written here, original (TRACKS):
- the key, the tempo (half time: the kick on 1, the snare on 3);
- the chords of the verse, the build and the drop;
- the melodies: the verse's four lines, the build's, and the drop's call, response and last response;
- its DESIGN: patch overrides for every element, the main synth's kind, the chords' tremolo and pitch move, the kick's
  rhythm, the percussion's style.

  verse — a reese bass, an airy bell, the main synth, light drums (the user's brief);
  build → drop, ONE clip — the main synth and the bell, then everything rising: the supersaw's stabs quickening as its
          filter opens, kick and snare rolls (the snare climbing an octave), a noise and a pitch riser, the breath, a
          clap; then the drop: the supersaw carrying the melody, a sub and a mid bass struck WITH every kick and ducked
          by it, the snare and clap on 3, percussion filling between.

Every element is its own strip with its space INSIDE its rack, so each solo is the element as designed. The clip and
every element's solo are rendered to MP3 (the lab's switch plays them). Every element's loudness, and the duck the kick
puts on the sub, are measured into batch.json. The recipes and their reasons are in references/future-bass.md; the
sources are in references/research.md.

Runs solaris-cc with scratch settings in the output folder — never the user's own settings or recents.
"""
import argparse, json, math, os, subprocess, sys, wave

HERE = os.path.dirname(os.path.abspath(__file__))
MEL = os.path.normpath(os.path.join(HERE, "..", "..", "arstro.solaris.compose.melody", "scripts"))
sys.path.insert(0, MEL)
from melody import Key, chord, PC  # noqa: E402
from song import Script, num, loudness, DEFAULT_CC  # noqa: E402

KICK, RIM, SNARE, CLAP, LTOM, CHAT, MTOM, OHAT, HTOM, CRASH, RIDE, COWBELL = 36, 37, 38, 39, 41, 42, 45, 46, 48, 49, 51, 56
NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def midi(name):
    i, acc = 1, 0
    while name[i] in '#b':
        acc += 1 if name[i] == '#' else -1
        i += 1
    return 12 * (int(name[i:]) + 1) + NOTE[name[0]] + acc


def seq(text, start=0):
    """A melody written in order: 'A5:1.5 G5:.5 r:1 …' — each note (or r, a rest) and its length in beats; '|' is a bar
    line for the eye. Returns [(beat, length, midi)]."""
    out, t = [], start
    for tok in text.replace("|", " ").split():
        name, d = tok.split(":")
        d = float(d)
        if name != "r":
            out.append((t, d, midi(name)))
        t += d
    return out, t - start


# ── the tracks: music and design, each its own ─────────────────────────────────────────────────────────────────────
TRACKS = {
    "afterlight": dict(
        title="Afterlight", style="bright and emotional (Illenium)", tonic="F", mode="major", bpm=150,
        verse_chords=["vi7", "IVmaj7", "Iadd9", "V"] * 2, build_chords=["IVmaj7", "V", "vi7", "V"],
        drop_chords=["IVmaj7", "V", "iii7", "vi7", "IVmaj7", "V", "Iadd9", "Iadd9"],
        verse=["A5:1.5 G5:.5 F5:1 G5:1 A5:3 r:.5 C6:.5 | D6:1.5 C6:.5 A5:1 G5:1 F5:3.5 r:.5",
               "G5:1 A5:.5 C6:1 A5:.5 G5:1 F5:1 G5:3 | E5:1 F5:.5 G5:1.5 A5:1 G5:3.5 r:.5",
               "A5:1.5 G5:.5 F5:1 G5:1 A5:3 r:.5 C6:.5 | D6:1.5 C6:.5 A5:1 C6:1 D6:3.5 r:.5",
               "C6:1 A5:.5 G5:1 F5:.5 G5:1 A5:3.5 r:.5 | A5:1 G5:1 E5:1 F5:1 G5:4"],
        call="D6:.75 C6:.75 A5:1.5 r:.5 A5:.5 | F6:1.5 D6:.5 r:2 | C6:.75 Bb5:.75 G5:1.5 r:.5 G5:.5 | E6:1.5 C6:.5 r:2",
        response="C6:.75 Bb5:.75 A5:1.5 r:.5 A5:.5 | C6:1.5 A5:.5 r:2 | F5:.75 G5:.75 A5:1.5 r:.5 C6:.5 | D6:2 r:2",
        last="A5:.75 C6:.75 F6:1.5 r:.5 F6:.5 | G6:1.5 F6:.5 r:2 | D6:.75 C6:.75 A5:1.5 r:.5 G5:.5 | F5:4",
        counter_call="r:6 A5:.5 C6:.5 D6:1 r:6 G5:.5 Bb5:.5 C6:1",
        counter_response="r:6 E6:.5 C6:.5 A5:1 r:6 F6:.5 E6:.5 D6:1",
        counter_last="r:6 E6:.5 D6:.5 C6:1 r:8",
        build_rise="D6:2 C6:1 D6:1 F6:4 | E6:2 C6:1 E6:1 E6:2 r:2",
        design=dict(main="layers", bend=(-1, 0.07), trem=("1/16", "square", 0.45), kicks=([0, 1.5, 2.75], [0, 0.75, 2.5, 3.5]),
                    perc="full", patch={}),
        sound=["the main synth: a wavetable pluck for the attack + a soft saw lead with a late vibrato",
               "the supersaw bright (low-pass 6.5 kHz), a 1-semitone rise into each stab, a 1/16 square tremolo",
               "percussion full: 16th hats, rolls into each phrase, snaps, tom fills, a shaker, a ride"]),
    "glass": dict(
        title="Glass Garden", style="dreamy and wavy (San Holo)", tonic="E", mode="major", bpm=150,
        verse_chords=["Imaj7", "iii7", "vi7", "IVmaj7"] * 2, build_chords=["IVmaj7", "V", "vi7", "V"],
        drop_chords=["IVmaj7", "V", "iii7", "vi7", "IVmaj7", "V", "Iadd9", "Iadd9"],
        verse=["G#5:1 B5:1 D#6:1.5 B5:.5 G#5:3 r:1 | F#5:1 G#5:1 B5:1.5 G#5:.5 D#5:3 r:1",
               "E5:1 G#5:1 C#6:1.5 B5:.5 G#5:2 E5:1 r:1 | F#5:1 A5:1 C#6:1.5 B5:.5 B5:3 r:1",
               "G#5:1 B5:1 D#6:1.5 B5:.5 G#5:3 r:1 | F#5:1 G#5:1 B5:1.5 D#6:.5 F#6:3 r:1",
               "E6:1.5 D#6:.5 C#6:1 B5:1 G#5:3 r:1 | A5:1 B5:1 C#6:1 B5:1 G#5:4"],
        call="C#6:1.5 B5:1.5 G#5:1 | E6:2 r:2 | D#6:1.5 C#6:1.5 B5:1 | C#6:1 D#6:1 r:2",
        response="B5:1.5 G#5:1.5 F#5:1 | G#5:2 r:2 | G#5:1.5 F#5:1.5 E5:1 | E5:2 r:2",
        last="B5:1.5 E6:1.5 G#6:1 | F#6:2 r:2 | E6:1.5 C#6:1.5 D#6:1 | E6:4",
        counter_call="r:6 G#5:.5 B5:.5 E6:1 r:6 F#5:.5 B5:.5 D#6:1",
        counter_response="r:6 D#6:.5 B5:.5 G#5:1 r:6 C#6:.5 B5:.5 G#5:1",
        counter_last="r:6 G#5:.5 B5:.5 C#6:1 r:8",
        build_rise="E5:2 G#5:2 B5:2 C#6:2 | D#6:2 F#6:2 D#6:2 r:2",
        design=dict(main="chop", bend=(-2, 0.15), trem=("1/8", "sine", 0.6), kicks=([0, 2.5], [0, 1.5, 2.75]),
                    perc="light",
                    patch=dict(bell=dict(op2__ratio=2, op2__level=0.3, op4__ratio=4, op1__decay=3200),
                               saw=dict(osc1__detune=26, volume=-13.3))),
        sound=["the main synth: a vocal chop — one saw through the vowel filter, its vowel moving 'ah → oo' with the line",
               "a glassy bell (FM ratio 2: harmonic, a toy piano's glass) with a longer echo",
               "the supersaw softer (low-pass 4.2 kHz), each stab scooped up 2 semitones over 150 ms, a 1/8 sine sway",
               "percussion light: eighth hats, snaps, a tom fill every four bars, the shaker forward; a sparser kick"]),
    "embers": dict(
        title="Embers", style="dark and heavy (Said The Sky)", tonic="A", mode="minor", bpm=150,
        verse_chords=["i7", "VImaj7", "IIIadd9", "VII"] * 2, build_chords=["VImaj7", "VII", "i7", "VII"],
        drop_chords=["VImaj7", "VII", "IIIadd9", "i7", "VImaj7", "VII", "i7", "i7"],
        verse=["E5:1 A5:1 C6:1.5 B5:.5 A5:3 r:1 | G5:1 A5:1 C6:1.5 A5:.5 E5:3 r:1",
               "G5:1 E5:1 G5:1.5 C6:.5 B5:2 G5:1 r:1 | D5:1 G5:1 B5:1.5 A5:.5 G5:3 r:1",
               "E5:1 A5:1 C6:1.5 B5:.5 A5:3 r:1 | G5:1 A5:1 C6:1.5 D6:.5 E6:3 r:1",
               "G5:1.5 E5:.5 G5:1 C6:1 B5:3 r:1 | A5:1 G5:1 E5:1 G5:1 D5:4"],
        call="E6:1.5 D6:1.5 C6:1 | A5:1 C6:1 r:2 | D6:1.5 C6:1.5 B5:1 | D6:2 r:2",
        response="E6:1.5 D6:1.5 C6:1 | B5:1 C6:1 r:2 | C6:1.5 B5:1.5 A5:1 | A5:2 r:2",
        last="C6:1.5 G6:1.5 A6:1 | G6:2 r:2 | E6:1.5 D6:1.5 B5:1 | A5:4",
        counter_call="r:6 A5:.5 C6:.5 E6:1 r:6 G5:.5 B5:.5 D6:1",
        counter_response="r:6 E5:.5 G5:.5 C6:1 r:6 A5:.5 C6:.5 E6:1",
        counter_last="r:6 E6:.5 D6:.5 C6:1 r:8",
        build_rise="A5:2 C6:2 D6:2 E6:2 | D6:2 B5:2 D6:2 r:2",
        design=dict(main="layers", bend=(-1, 0.05), trem=("1/16", "square", 0.35),
                    kicks=([0, 0.75, 1.5, 2.75], [0, 1, 2.5, 3.25, 3.5]), perc="trap",
                    patch=dict(reese=dict(osc1__detune=20, filter__env=1.0, volume=-10.5),
                               reese_drive=8,
                               bell=dict(op2__ratio=4, op2__level=0.25, op4__level=0.1, op1__decay=2000),
                               saw=dict(osc1__detune=36, volume=-14.3), saw_drive=10,
                               mid=dict(osc1__detune=24, filter__res=0.35, volume=-15.5), mid_curve="fold")),
        sound=["the reese heavier: ±15 ct, more filter movement, driven 14 dB",
               "a soft celesta bell (FM ratio 4, a gentle clang)",
               "the supersaw darker (low-pass 5 kHz) and driven 10 dB; a growling mid bass (wavefolded)",
               "percussion trap: 16th hats throughout, 32nd and triplet rolls every two bars, a busier kick"]),
    "neon": dict(
        title="Neon Tide", style="quirky and punchy (Flume)", tonic="G", mode="major", bpm=145,
        verse_chords=["IVmaj7", "iii7", "ii7", "Vsus4"] * 2, build_chords=["ii7", "iii7", "IVmaj7", "V"],
        drop_chords=["IVmaj7", "iii7", "vi7", "V", "IVmaj7", "V", "Iadd9", "Iadd9"],
        verse=["G5:.75 E5:.75 G5:.5 B5:1 A5:1 | G5:.5 E5:1.5 r:2 | F#5:.75 D5:.75 F#5:.5 A5:1 G5:1 | F#5:.5 D5:1.5 r:2",
               "E5:.75 C5:.75 E5:.5 A5:1 G5:1 | E5:.5 C5:1.5 r:2 | D5:.75 G5:.75 A5:.5 D6:1 C6:1 | A5:4",
               "G5:.75 E5:.75 G5:.5 B5:1 A5:1 | G5:.5 E5:1.5 r:2 | F#5:.75 D5:.75 F#5:.5 A5:1 B5:1 | D6:4",
               "C6:.75 B5:.75 A5:.5 G5:1 E5:1 | G5:.5 A5:1.5 r:2 | D5:.75 G5:.75 A5:.5 B5:1 A5:1 | G5:.5 A5:3.5"],
        call="E6:.5 D6:.5 B5:.75 G5:.75 B5:.5 D6:1 | G6:.75 E6:.75 D6:.5 r:2 | D6:.5 B5:.5 A5:.75 F#5:.75 A5:.5 B5:1 | F#6:.75 D6:.75 A5:.5 r:2",
        response="B5:.5 A5:.5 G5:.75 E5:.75 G5:.5 B5:1 | E6:.75 D6:.75 B5:.5 r:2 | A5:.5 F#5:.5 D5:.75 F#5:.75 A5:.5 D6:1 | F#6:2 r:2",
        last="D6:.5 E6:.5 G6:.75 A6:.75 G6:.5 E6:1 | D6:.75 B5:.75 G5:.5 r:2 | A5:.5 B5:.5 D6:.75 E6:.75 D6:.5 B5:1 | G5:4",
        counter_call="r:6 B5:.5 D6:.5 E6:1 r:6 A5:.5 B5:.5 D6:1",
        counter_response="r:6 G5:.5 B5:.5 E6:1 r:6 F#5:.5 A5:.5 D6:1",
        counter_last="r:6 G5:.5 B5:.5 D6:1 r:8",
        build_first="E5:.75 G5:.75 A5:.5 C6:1 B5:1 | A5:.5 E5:1.5 r:2 | F#5:.75 A5:.75 B5:.5 D6:1 A5:1 | B5:.5 F#5:1.5 r:2",
        build_rise="G5:1 B5:1 C6:1 E6:1 | D6:2 E6:2 | F#5:1 A5:1 D6:1 F#6:1 | E6:2 r:2",
        design=dict(main="square", bend=(1.5, 0.09), trem=("1/16", "square", 0.75), kicks=([0, 1.75, 2.5], [0, 0.75, 2.25, 3.5]),
                    perc="swing",
                    patch=dict(reese=dict(osc1__detune=20, filter__cutoff=900, filter__res=0.35), reese_phaser=0.4,
                               bell=dict(op2__ratio=1.41, op2__level=0.5, feedback=0.2, op1__decay=1800),
                               saw=dict(amp__decay=300, amp__sustain=0.4, filter__env=1.6, volume=-10.3),
                               mid=dict(fenv__decay=120, fenv__sustain=0.1, filter__env=2.0))),
        sound=["the main synth: a square pluck (pulse wavetable) and a detuned saw lead",
               "a metallic bell (FM ratio 1.41, feedback): inharmonic, glassy-metal",
               "the supersaw as gated stabs (1/16 square tremolo, deep), each stab dropping 1.5 semitones INTO the note",
               "percussion swung: hats pushed late on the off-beats, a cowbell, doubled claps, rim on the a's; a plucky bass"]),
}

# ── the base patches (each track overrides some) ───────────────────────────────────────────────────────────────────
REESE = dict(osc1__wave="saw", osc1__voices=2, osc1__detune=16, osc1__width=0, osc1__level=0.7, osc2__wave="sine",
             osc2__octave=-1, osc2__voices=1, osc2__fine=0, osc2__level=0.85, filter__cutoff=620, filter__res=0.2,
             filter__env=0.6, filter__keytrack=0.3, fenv__attack=60, fenv__decay=900, fenv__sustain=0.4,
             amp__attack=25, amp__decay=500, amp__sustain=0.9, amp__release=250, voice__mode="legato",
             voice__glide=70, velocity=0.3, volume=-8)   # measured: deeper and darker reads ~4.5 LU quieter
BELL = dict(algorithm=5, feedback=0, op1__ratio=1, op1__level=1, op1__attack=1, op1__decay=2600, op1__sustain=0,
            op1__release=1600, op2__ratio=3.5, op2__level=0.42, op2__attack=1, op2__decay=650, op2__sustain=0,
            op2__release=400, op3__ratio=2, op3__fine=7, op3__level=0.5, op3__attack=1, op3__decay=1900, op3__sustain=0,
            op3__release=1200, op4__ratio=5, op4__level=0.18, op4__attack=1, op4__decay=380, op4__sustain=0,
            op4__release=300, volume=-7.5)
PLUCK = dict(osc1__table="harmonics", osc1__position=0.55, osc1__posenv=-0.3, osc1__voices=2, osc1__detune=10,
             osc2__level=0, filter__cutoff=2600, filter__env=0.9, fenv__decay=260, fenv__sustain=0, amp__attack=1,
             amp__decay=520, amp__sustain=0.15, amp__release=220, volume=-7)
SQUARE = dict(PLUCK, osc1__table="pulse", osc1__position=0.35, osc1__posenv=0, filter__cutoff=2000, filter__env=1.4,
              fenv__decay=180, amp__decay=340, amp__sustain=0.05)
LEAD = dict(osc1__wave="saw", osc1__voices=3, osc1__detune=14, osc1__width=20, osc1__level=0.7, osc2__wave="triangle",
            osc2__octave=0, osc2__fine=0, osc2__level=0.3, filter__cutoff=1700, filter__res=0, filter__env=0.4,
            fenv__decay=400, fenv__sustain=0.6, amp__attack=35, amp__decay=300, amp__sustain=0.8, amp__release=260,
            voice__mode="legato", voice__glide=40, vibrato__depth=0.35, vibrato__rate=5.4, velocity=0.4, volume=-15)
CHOP = dict(osc1__wave="saw", osc1__voices=1, osc1__level=0.8, osc2__wave="square", osc2__octave=0, osc2__fine=0,
            osc2__level=0.3, noise=0.03, filter__cutoff=8000, filter__res=0.05, filter__env=0.3, filter__keytrack=0,
            fenv__decay=200, amp__attack=3, amp__decay=160, amp__sustain=0.85, amp__release=80, voice__mode="legato",
            voice__glide=35, vibrato__depth=0.25, vibrato__rate=5.6, velocity=0.3, volume=-1)   # measured: the vowel filter takes ~11 dB
SAW = dict(osc1__wave="saw", osc1__voices=5, osc1__detune=32, osc1__width=45, osc1__level=0.7, osc2__wave="saw",
           osc2__octave=1, osc2__voices=5, osc2__detune=24, osc2__width=40, osc2__fine=0, osc2__level=0.55, noise=0.04,
           filter__cutoff=14000, filter__res=0.12, filter__env=0.6, filter__keytrack=0.2, fenv__attack=2,
           fenv__decay=260, fenv__sustain=0.55, amp__attack=2, amp__decay=700, amp__sustain=0.8, amp__release=220,
           voice__bend=2, velocity=0.4, volume=-12.3)   # measured: sharper reads ~4 LU louder
SUB = dict(osc1__wave="sine", osc1__level=0.9, osc2__level=0, filter__cutoff=300, filter__res=0, filter__env=0,
           amp__attack=2, amp__decay=300, amp__sustain=0.9, amp__release=60, voice__mode="mono", voice__glide=0,
           velocity=0.3, volume=-11)
MID = dict(osc1__wave="saw", osc1__voices=3, osc1__detune=18, osc1__level=0.8, osc2__wave="square", osc2__octave=0,
           osc2__fine=0, osc2__level=0.4, filter__cutoff=2600, filter__res=0.3, filter__env=1.0, fenv__decay=180,
           fenv__sustain=0.35, amp__attack=3, amp__decay=300, amp__sustain=0.8, amp__release=80, voice__mode="mono",
           velocity=0.4, volume=-14)
NOISE = dict(osc1__level=0, osc2__level=0, noise=1, filter__cutoff=300, filter__res=0.2, filter__env=0,
             filter__keytrack=0, amp__attack=20, amp__decay=100, amp__sustain=1, amp__release=40, velocity=0, volume=-12)


class Track:
    """A track's music, read once: its key, its chords per section, its melodies as notes."""
    def __init__(self, name):
        T = TRACKS[name]
        self.name, self.T, self.D = name, T, T["design"]
        self.key = Key(T["tonic"], T["mode"])
        self.bpm = T["bpm"]
        self.spb = 60 / self.bpm
        self.chords = {s: [chord(r, self.key) for r in T[s + "_chords"]] for s in ("verse", "build", "drop")}
        self.patch = lambda k, base: dict(base, **self.D["patch"].get(k, {}))
        # the kick in the key: its root or its fifth, whichever lands nearer 52 Hz (the kick settles on 48·2^(tune/12))
        hz = lambda pc: min((440 * 2 ** ((m - 69) / 12) for m in range(24, 48) if m % 12 == pc), key=lambda f: abs(f - 52))
        f = min((hz(self.key.tonic), hz((self.key.tonic + 7) % 12)), key=lambda f: abs(f - 52))
        self.kick_hz, self.kick_tune = round(f, 1), round(12 * math.log2(f / 48), 2)

    def sec(self, beats):
        return beats * self.spb

    def beats(self, seconds):
        return seconds / self.spb

    def melody(self, texts, start):
        out, t = [], start
        for text in texts:
            notes, length = seq(text, t)
            assert abs(length - 16) < 1e-6, f"{self.name}: a line of {length} beats, not 16: {text}"
            out += notes
            t += length
        return out


def chord_at(chords, t, start=0):
    return chords[max(0, min(len(chords) - 1, int((t - start) // 8)))]


def place(pcs, lo, hi):
    return sorted(m for m in range(lo, hi + 1) if m % 12 in pcs)


def root_in(ch, lo):
    return lo + (ch["root"] - lo) % 12


def vel(t, base=100):
    """Accents by where a note falls: the beat strongest, the off-beat eighth next, the sixteenths softest."""
    return base + 8 if t % 1 == 0 else (base if t % 0.5 == 0 else base - 10)


class Clip:
    """A section's script: its elements (name → strip, recipe) for the solos and the page."""
    def __init__(self, tr, title, length):
        self.tr, self.S, self.length, self.elements = tr, Script(), length, []
        self.S.line(f"# {title} (arstro.solaris.sound.design futurebass.py) — {tr.key.name}, {tr.bpm} bpm, half time")
        self.S.line(f'project new clip.slp --bpm {tr.bpm} --name "{title}"')

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
            self.S.device(ch, "delay", time=num(0.75 * self.tr.spb * 1000), feedback=delay[1], tone=3500, mix=delay[0])
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


def mod_lane(tr, notes, depth=0.8):
    """A late vibrato on held notes: the mod wheel 0 at the onset, rising after ~0.3 s (the synth's vibrato sounds only
    with the wheel up)."""
    pts = [(0, 0)]
    for t, d, *_ in notes:
        if tr.sec(d) >= 0.5:
            pts += [(t, 0), (t + tr.beats(min(0.3, 0.4 * tr.sec(d))), 0), (t + d - tr.beats(0.05), depth), (t + d, 0)]
    return pts


def main_synth(C, mel, recipe_tail):
    """The main synth, by the track's design: 'layers' (a pluck + a saw lead), 'chop' (a saw through the vowel filter),
    'square' (a pulse pluck + a saw lead). One bus; it leads its section."""
    tr, S = C.tr, C.S
    kind = tr.D["main"]
    mbus = S.bus("Main synth")
    if kind == "chop":
        ch, dv, pt = S.instrument("synth", "Main chop", C.length)
        S.set(dv, **CHOP)
        S.notes(pt, [(p, t, max(0.1, d - 0.05), vel(t, 100)) for t, d, p in mel])
        S.expr(pt, "mod", mod_lane(tr, mel, 0.7))
        vw = S.device(ch, "vowel", resonance=0.7, glide=25, mix=1, output=9)
        au = S.new('au')
        S.line(f'auto add --name "Chop · Vowel" --min 0 --max 4 --length {num(C.length)} --rest 0')
        for i, (t, d, p) in enumerate(mel):                          # 'ah' on the long notes, 'oo' / 'eh' between
            S.line(f"auto point add {au} --at {num(t)} --value {0 if d >= 1.5 else (4 if i % 2 else 1)} --shape hold")
        S.line(f'set {vw}.vowel="={au}"')
        S.new('ac')
        S.line(f"clip add --auto {au} --at 0 --length {num(C.length)}")
        S.line(f"route {ch} --to {mbus}")
        recipe = "a vocal chop: one saw and a square through the vowel filter — 'ah' on held notes, 'oo'/'eh' between"
    else:
        first = (("Main square", "wavetable", SQUARE) if kind == "square" else ("Main pluck", "wavetable", PLUCK))
        for name, inst, params in (first, ("Main lead", "synth", LEAD)):
            ch, dv, pt = S.instrument(inst, name, C.length)
            S.set(dv, **params)
            S.notes(pt, [(p, t, max(0.1, d - 0.05), vel(t, 98)) for t, d, p in mel])
            if name == "Main lead":
                S.expr(pt, "mod", mod_lane(tr, mel))
            S.line(f"route {ch} --to {mbus}")
        recipe = ("a square pluck (pulse table)" if kind == "square" else "a pluck (harmonics table)") + \
                 " for the attack + a soft saw lead with a late vibrato for the sustain"
    S.device(mbus, "chorus", rate=0.6, depth=3, delay=12, mix=0.3)
    C.space(mbus, reverb=(0.25, 2400, 130), delay=(0.16, 0.3))
    S.line(f"set {mbus}.gain=6")                                     # measured: it must lead its section
    C.elements.append({"name": "Main synth", "strip": mbus, "recipe": recipe + "; chorused, an echo, a room — " + recipe_tail})


def reese_strip(C, recipe):
    tr, S = C.tr, C.S
    p = tr.patch("reese", REESE)
    ch, dv, pt = C.element("synth", "Reese bass", recipe, p)
    S.device(ch, "phaser", rate=tr.D["patch"].get("reese_phaser", 0.12), center=600, depth=1.5, stages=4, feedback=0.4,
             stereo=60, mix=0.4)
    S.device(ch, "saturator", drive=tr.D["patch"].get("reese_drive", 8), curve='"soft"', mix=0.5, output=-3)
    S.device(ch, "chorus", rate=0.3, depth=4, delay=14, mix=0.25)      # fb-002: "it need more ambient" — width on top,
    S.device(ch, "imager", width=150, mono="on", frequency=140)        # the floor kept in the middle
    C.space(ch, reverb=(0.25, 3800, 160))                              # a wash above 300 Hz; the sub stays dry
    return ch, dv, pt


def bell_strip(C, recipe):
    tr, S = C.tr, C.S
    ch, dv, pt = C.element("fm", "Airy bell", recipe, tr.patch("bell", BELL))
    S.device(ch, "filter", mode='"highpass"', cutoff=600, res=0.1, mix=1)
    S.device(ch, "eq", highcut__on="on", highcut__freq=6500)          # fb-002: "bell need to high cut a little bit
    C.space(ch, reverb=(0.55, 6500, 200), delay=(0.36 if tr.D["main"] == "chop" else 0.32, 0.5))   # and more depth"
    return ch, dv, pt


def bell_arps(chords, start, dense_from, lo=86, hi=100):
    out = []
    for i, ch in enumerate(chords):
        tones = place(ch["pcs"], lo, hi)
        pattern = [(0, 0), (1.5, 1), (3, 2), (4.5, 3), (6, 2)] if i >= dense_from else [(0, 0), (1.5, 1), (3, 2)]
        out += [(tones[min(k, len(tones) - 1)], start + 8 * i + a, 1.5, 70 if a == 0 else 58) for a, k in pattern]
    return out


# ── the verse ───────────────────────────────────────────────────────────────────────────────────────────────────────
def verse(tr):
    lead_in = 4
    chords = tr.chords["verse"]
    n = len(chords) * 8
    C = Clip(tr, f"{tr.T['title']} — verse", lead_in + n + 8)
    S = C.S
    C.drums("Kick", f"the kick: tuned to the key ({tr.kick_hz} Hz: tune {tr.kick_tune}), 380 ms, on 1 — the verse breathes",
            dict(kick__tune=tr.kick_tune, kick__decay=380, kick__tone=0.6, volume=-4),
            [(KICK, lead_in + 4 * b, .25, 96) for b in range(n // 4)])
    snap, _ = C.drums("Snap", "a rim snap with a soft clap on 3 (half time), a short room",
                      dict(rim__tune=5, rim__decay=70, rim__tone=0.8, clap__decay=260, clap__tone=0.6, volume=3),
                      [(RIM, lead_in + 4 * b + 2, .25, 92) for b in range(n // 4)] +
                      [(CLAP, lead_in + 4 * b + 2, .25, 58) for b in range(n // 4)])
    C.space(snap, reverb=(0.22, 1300, 80))
    hats = [(CHAT, lead_in + 32 + 0.5 * k, .2, 54 if k % 2 == 0 else 40) for k in range(64)]
    hats += [(CHAT, lead_in + n - 1 + 0.125 * k, .1, 40 + 6 * k) for k in range(8)]
    hh, _ = C.drums("Hats", "closed hats on eighths from bar 9, accents on the beat, a 32nd roll at the end",
                    dict(chat__tune=3, chat__decay=45, chat__tone=0.75, chat__pan=-0.15, volume=1), hats)
    C.space(hh, reverb=(0.12, 900, 60))
    p = tr.patch("reese", REESE)
    reese, rdv, rpt = reese_strip(C, f"two saws ±{p['osc1__detune'] / 2:g} ct beating (the reese), a sine an octave "
                                     f"under, low-pass {p['filter__cutoff']} Hz opening through the verse, a phaser "
                                     f"({tr.D['patch'].get('reese_phaser', 0.12)} Hz), saturation "
                                     f"{tr.D['patch'].get('reese_drive', 8)} dB, mono under 140 Hz")
    rn = [(root_in(chords[0], 34), 0, lead_in - 0.25, 80)]           # A♯1 … A2: deeper (fb-002 "not deep enough")
    for i, ch in enumerate(chords):
        t, r = lead_in + 8 * i, root_in(ch, 34)
        nxt = root_in(chords[(i + 1) % len(chords)], 34)
        rn += [(r, t, 6, 92), (r + 7 if abs(nxt - r) > 2 else r + 12, t + 6, 1.5, 84)]
    S.notes(rpt, rn)
    S.automate(f"{rdv}.filter.cutoff", [(0, p["filter__cutoff"] - 100, "smooth"), (lead_in + n, 1300, "hold")])
    C.duck(reese, "ch_2", 4, release=120)
    b = tr.patch("bell", BELL)
    bell, bdv, bpt = bell_strip(C, f"FM bell (algorithm 5): a {b['op2__ratio']}-ratio modulator's clang over the "
                                   f"fundamental, an octave pair 7 cents sharp for shimmer; high-passed at 600 Hz, a "
                                   f"dotted-8th echo, a 4.5 s room — arpeggiating the chord above everything")
    S.notes(bpt, bell_arps(chords, lead_in, 4))
    main_synth(C, tr.melody(tr.T["verse"], lead_in), "the verse's melody")
    return C, {"kick": "ch_2", "bass": reese}


# ── the build-up and its drop, one clip ────────────────────────────────────────────────────────────────────────────
def drop(tr):
    lead_in = 4
    bchords, dchords = tr.chords["build"], tr.chords["drop"]
    land = lead_in + 32                                            # the drop's first beat
    n = len(dchords) * 8                                           # the drop: 64 beats, 16 bars
    end = land + n
    C = Clip(tr, f"{tr.T['title']} — build-up → drop", end + 4)
    S, D = C.S, tr.D
    # ── the kick: on 1, quarters, eighths, a roll — the breath — then the drop's rhythm (the bassline's too)
    kA, kB = D["kicks"]
    dkicks = [land + 4 * b + a for b in range(n // 4) for a in (kA if b % 2 == 0 else kB)]
    kicks = [(KICK, lead_in + 4 * b, .25, 92) for b in range(4)] + [(KICK, lead_in + 16 + k, .25, 96) for k in range(8)]
    kicks += [(KICK, lead_in + 24 + 0.5 * k, .25, 100) for k in range(8)]
    kicks += [(KICK, lead_in + 28 + 0.25 * k, .2, 104 + 2 * k) for k in range(8)]
    kicks += [(KICK, t, .25, 124 if (t - land) % 4 == 0 else 112) for t in dkicks]
    C.drums("Kick", f"tuned to the key ({tr.kick_hz} Hz), 260 ms, tone 0.85, a hard transient (+70 %) and tube "
            f"density: in the build on 1, quarters, eighths, a roll; in the drop {kA} / {kB} in the bar — the rhythm "
            f"the bass plays too", dict(kick__tune=tr.kick_tune, kick__decay=260, kick__tone=0.85, volume=-3), kicks)
    S.device("ch_2", "transient", attack=70, sustain=-20, output=0)   # fb-002: "kick is not punchy enough"
    S.device("ch_2", "saturator", drive=6, curve='"tube"', mix=0.4, output=-2)
    C.drums("Kick top", "the click: the kick an octave up, 70 ms, all tone — layered on every hit, the punch the ear "
            "hears in a full drop", dict(kick__tune=min(12, tr.kick_tune + 12), kick__decay=70, kick__tone=1.0, volume=-6),
            [(k, t, .1, v) for k, t, d, v in kicks])
    # ── the snare: the roll climbing an octave, the breath, one clap — then snare + clap on 3
    roll = [(SNARE, lead_in + 4 * b + 2, .25, 90) for b in range(2)]
    roll += [(SNARE, lead_in + 8 + k, .25, 70 + 3 * k) for k in range(8)]
    roll += [(SNARE, lead_in + 16 + 0.5 * k, .2, 80 + k) for k in range(16)]
    roll += [(SNARE, lead_in + 24 + 0.25 * k, .15, 96 + k) for k in range(16)]
    roll += [(SNARE, lead_in + 28 + 0.125 * k, .1, 112 + k // 2) for k in range(16)]
    roll += [(CLAP, lead_in + 4 * b + 2, .25, 70) for b in range(6)] + [(CLAP, land - 0.5, .25, 127)]
    roll += [(SNARE, land + 4 * b + 2, .25, 118) for b in range(n // 4)] + [(CLAP, land + 4 * b + 2, .25, 110) for b in range(n // 4)]
    if D["perc"] == "swing":                                       # doubled claps: a ghost on the a of 4
        roll += [(CLAP, land + 4 * b + 3.75, .2, 64) for b in range(n // 4) if b % 2]
    sn, sdv = C.drums("Snare + clap", "the build's roll (quarters → 32nds, rising, climbing an octave), the breath, one "
                      "big clap; in the drop a snare a semitone up and a clap together on 3, one room",
                      dict(snare__tune=1, snare__decay=240, snare__tone=0.62, clap__decay=360, clap__tone=0.55,
                           volume=3), roll)
    S.automate(f"{sdv}.snare.tune", [(0, 1, "hold"), (lead_in + 16, 1, "linear"), (lead_in + 30, 12, "hold"),
                                     (land - 0.5, 1, "hold")])
    C.space(sn, reverb=(0.24, 1800, 110))
    # ── the percussion between: by the track's style
    perc = [(CHAT, lead_in + 16 + 0.25 * k, .15, 40 + k // 2) for k in range(56)]   # the build's rising hats
    style = D["perc"]
    for b in range(n // 4):
        t, last4 = land + 4 * b, b % 4 == 3
        if style == "light":
            perc += [(CHAT, t + 0.5 * k, .15, 66 if k % 2 == 0 else 50) for k in range(8)]
        elif style == "trap":
            perc += [(CHAT, t + 0.25 * k, .12, 72 if k % 4 == 0 else (58 if k % 2 == 0 else 44)) for k in range(16)
                     if not (b % 2 == 1 and k >= 12)]
        elif style == "swing":
            perc += [(CHAT, t + 0.5 * k + (0.08 if k % 2 else 0), .15, 70 if k % 2 == 0 else 56) for k in range(8)]
            perc += [(CHAT, t + 0.25 * k, .1, 36) for k in (3, 7, 11, 15)]
        elif b < 8:
            perc += [(CHAT, t + 0.25 * k, .15, 74 if k % 2 == 0 else 46) for k in range(16) if not (last4 and k >= 12)]
        else:
            perc += [(CHAT, t + 0.5 * k, .15, 72) for k in range(8) if not (last4 and k >= 6)]
            perc += [(OHAT, t + 0.5 * k + 0.25, .2, 56) for k in range(8) if k % 2 == 1 and not (last4 and k >= 6)]
            perc += [(RIDE, t + k, .3, 50) for k in range(4)]
        roll_every = 2 if style == "trap" else 4
        if b % roll_every == roll_every - 1 and style != "light":    # a roll into the next phrase
            perc += ([(CHAT, t + 3 + k / 6, .1, 50 + 8 * k) for k in range(6)] if b % 8 == 7 or (style == "trap" and b % 4 == 1)
                     else [(CHAT, t + 3 + 0.125 * k, .1, 46 + 7 * k) for k in range(8)])
        if style == "swing":
            perc += [(RIM, t + 0.75, .2, 70), (RIM, t + 2.75, .2, 62)]
            if b % 2:
                perc += [(COWBELL, t + 3.5, .2, 72)]
        else:
            perc += [(RIM, t + 1.25, .2, 74), (RIM, t + 3.25, .2, 66)]
            if style == "light":
                perc += [(RIM, t + 3.75, .2, 58)]
        if (b % 2 == 1 and style in ("full", "trap")) or (b % 4 == 3 and style in ("light", "swing")):
            perc += [(HTOM, t + 3, .25, 96), (MTOM, t + 3.25, .25, 92), (MTOM, t + 3.5, .25, 98), (LTOM, t + 3.75, .25, 104)]
        if b % 8 == 0:
            perc += [(CRASH, t, 1, 112)]
    pc, _ = C.drums("Percussion", {"full": "16th hats accented on the 8ths (8ths with open hats and a ride in the second "
                                           "half), rolls into each phrase, snaps on the e of 2 and 4, a tom fill after "
                                           "every second snare, crashes on the phrase",
                                   "light": "eighth hats, snaps on the e of 2 and 4 and the a of 4, a tom fill every four "
                                            "bars, crashes — space to breathe",
                                   "trap": "16th hats throughout, 32nd and triplet rolls every two bars, snaps, tom "
                                           "fills after every second snare, crashes",
                                   "swing": "hats swung (the off-beats 80 ms late) with ghost 16ths, rim on the a's, a "
                                            "cowbell every second bar, a tom fill every four, crashes"}[style],
                    dict(chat__tune=3, chat__decay=45, chat__tone=0.75, chat__pan=-0.2, ohat__decay=260, ohat__pan=0.25,
                         rim__tune=6, rim__decay=60, rim__pan=0.35, htom__pan=-0.4, mtom__pan=0, ltom__pan=0.4,
                         ride__tune=4, ride__pan=0.45, crash__decay=2200, cowbell__tune=-2, cowbell__pan=-0.3,
                         volume=-4 + 2 * (style == "swing")), perc)
    C.space(pc, reverb=(0.16, 1200, 80))
    C.drums("Shaker", "a shaker: a hat tuned up, 35 ms, on the off-sixteenths, right of centre",
            dict(chat__tune=9, chat__decay=35, chat__tone=0.95, chat__pan=0.5, volume=-2 + 3 * (style == "light")),
            [(CHAT, land + 4 * b + 0.25 * k, .1, 60 if k % 4 == 2 else 44) for b in range(n // 4) for k in range(16)
             if k % 2 == 1 or k % 4 == 2])
    # ── the reese under the build's first six bars, then out: the floor drops away
    reese, rdv, rpt = reese_strip(C, "the verse's reese under the build's first six bars, then gone")
    S.notes(rpt, [(root_in(bchords[0], 34), 0, lead_in - 0.25, 80)] +
            [(root_in(ch, 34), lead_in + 8 * i, 7.75, 92) for i, ch in enumerate(bchords[:3])])
    S.automate(f"{rdv}.filter.cutoff", [(0, 900, "smooth"), (lead_in + 20, 2400, "hold")])
    C.duck(reese, "ch_2", 4, release=120)
    # ── the bass: a sub and a mid bass struck WITH every drop kick, on the chord's root, ducked by the kick
    bass = []
    for i, t in enumerate(dkicks):
        nxt = dkicks[i + 1] if i + 1 < len(dkicks) else end
        r = root_in(chord_at(dchords, t, land), 28)               # E1 … D#2 (41–78 Hz)
        if (t - land) % 16 >= 15:                                  # into each phrase: the octave, a lift
            r += 12
        bass.append((r, t, min(nxt - t, 2.0) - 0.05))
    sub, _, spt = C.element("synth", "Sub", "a sine sub, mono, on the chord's root WITH every drop kick (the kick's rhythm "
                            "is the bassline's), ducked 14 dB by the kick and back within ~200 ms", SUB)
    S.notes(spt, [(p, t, d, 110) for p, t, d in bass])
    C.duck(sub, "ch_2", 14, release=70)
    mp = tr.patch("mid", MID)
    mid, _, mpt = C.element("synth", "Mid bass", f"the bass's voice an octave up: saws and a square, low-passed "
                            f"{mp['filter__cutoff'] / 1000:g} kHz, {tr.D['patch'].get('mid_curve', 'tube')}-driven, "
                            f"high-passed at 160 Hz so the sub owns the low end; driven HARD (18 dB) — fb-002: 'need some hard bass'; ducked 14 dB by the kick", mp)
    S.notes(mpt, [(p + 12, t, d, 104) for p, t, d in bass])
    S.device(mid, "saturator", drive=18, curve=f'"{tr.D["patch"].get("mid_curve", "tube")}"', mix=0.7, output=-7)
    S.device(mid, "filter", mode='"highpass"', cutoff=160, res=0.1, mix=1)
    S.device(mid, "chorus", rate=0.5, depth=2, delay=10, mix=0.2)
    C.duck(mid, "ch_2", 14, release=70)
    # ── the supersaw: the build's stabs quickening as its filter opens; the drop's melody on top of every chord
    sp = tr.patch("saw", SAW)
    rate, shape, depth = D["trem"]
    semis, move = D["bend"]
    saw, wdv, wpt = C.element("synth", "Supersaw",
                              f"two saw stacks (5 voices ±{sp['osc1__detune'] / 2:g} ct; 5 an octave up): in the build "
                              f"stabs quickening (halves → sixteenths) as the low-pass opens 350 Hz → 7 kHz; in the drop the "
                              f"melody on top of every chord, each stab {'rising' if semis < 0 else 'dropping'} "
                              f"{abs(semis):g} st into the note over {int(move * 1000)} ms, a {rate} {shape} tremolo "
                              f"(depth {depth}), driven {tr.D['patch'].get('saw_drive', 5)} dB, 160 % wide, mono under "
                              f"180 Hz, pumped 12 dB by the kick", sp)
    stabs = []
    for t0, t1, step in ((0, 8, 2), (8, 16, 1), (16, 24, 0.5), (24, 30, 0.25)):
        t = t0
        while t < t1:
            ch = chord_at(bchords, lead_in + t, lead_in)
            stabs += [(p, lead_in + t, step * 0.8, 70 + int(40 * t / 30)) for p in place(ch["pcs"], 60, 72)]
            t += step
    mel = tr.melody([tr.T["call"], tr.T["response"], tr.T["call"], tr.T["last"]], land)
    bend = [(0, 0), (land - 0.05, 0)]
    for t, d, p in mel:
        ch = chord_at(dchords, t, land)
        under = [q for q in range(p - 11, p) if q % 12 in ch["pcs"] and q % 12 != p % 12]
        stabs += [(q, t, max(0.2, d - 0.1), vel(t, 106)) for q in [p] + sorted(under, reverse=True)[:3]]
        bend += [(t - 0.02, 0), (t, semis / 2), (t + tr.beats(move), 0)]            # the bend range is ±2 st
    S.notes(wpt, stabs)
    S.expr(wpt, "bend", bend)
    # the counter-melody (fb-002 drop 1: "need some rest note and counter melody"): a vocal chop answering the
    # supersaw in the rests it now leaves at the end of every second bar
    cmel = tr.melody([tr.T["counter_call"], tr.T["counter_response"], tr.T["counter_call"], tr.T["counter_last"]], land)
    chop, _, cpt = C.element("synth", "Counter chop", "a vocal chop answering the supersaw in its rests — the counter-"
                             "melody: one saw and a square through the vowel filter, 'ah' and 'oh' in turn, an echo, a "
                             "room", dict(CHOP, volume=2))
    S.notes(cpt, [(p, t, max(0.1, d - 0.05), vel(t, 100)) for t, d, p in cmel])
    cvw = S.device(chop, "vowel", resonance=0.7, glide=25, mix=1, output=9)
    cau = S.new('au')
    S.line(f'auto add --name "Counter · Vowel" --min 0 --max 4 --length {num(C.length)} --rest 0')
    for i, (t, d, p) in enumerate(cmel):
        S.line(f"auto point add {cau} --at {num(t)} --value {0 if i % 2 == 0 else 3} --shape hold")
    S.line(f'set {cvw}.vowel="={cau}"')
    S.new('ac')
    S.line(f"clip add --auto {cau} --at 0 --length {num(C.length)}")
    S.device(chop, "imager", width=130)
    C.space(chop, reverb=(0.25, 2400, 130), delay=(0.22, 0.35))
    S.automate(f"{wdv}.filter.cutoff", [(0, 350, "smooth"), (lead_in + 30, 7000, "hold"),
                                        (land, sp["filter__cutoff"], "hold")])
    sh = S.device(saw, "shaper", rate=f'"{rate}"', shape=f'"{shape}"', depth=0, smooth=6)
    S.automate(f"{sh}.depth", [(0, 0, "hold"), (land, depth, "hold")])
    S.device(saw, "saturator", drive=tr.D["patch"].get("saw_drive", 5), curve='"soft"', mix=0.5, output=-2)
    S.device(saw, "eq", lowshelf__freq=300, lowshelf__gain=-4, peak3__freq=3500, peak3__gain=5, peak3__q=0.7,
             highshelf__freq=8000, highshelf__gain=4)             # less body, more bite and air: measured sharper
    S.device(saw, "imager", width=160, mono="on", frequency=180)       # fb-002: "super saw need to be more sharp" —
    C.space(saw, reverb=(0.12, 2000, 110))                             # open to 14 kHz, presence, air, a drier room
    C.duck(saw, "ch_2", 12, release=120)
    C.duck(saw, sn, 5, release=80)
    # ── the risers
    nz, ndv, npt = C.element("synth", "Noise riser", "white noise through a low-pass opening 300 Hz → 12 kHz over the "
                             "build's last four bars, swelling in, cut for the breath", NOISE)
    S.notes(npt, [(60, lead_in + 16, 14, 100)])
    S.automate(f"{ndv}.filter.cutoff", [(0, 300, "hold"), (lead_in + 16, 300, "smooth"), (lead_in + 30, 12000, "hold")])
    S.automate(f"{ndv}.volume", [(0, -40, "hold"), (lead_in + 16, -40, "smooth"), (lead_in + 30, -8, "hold")])
    C.space(nz, reverb=(0.3, 2500, 150))
    pr, _, ppt = C.element("synth", "Pitch riser", "one supersaw note bent up two octaves over the build's last four bars",
                           dict(SAW, voice__bend=24, volume=-15, filter__cutoff=3000))
    S.notes(ppt, [(midi(f"{tr.T['tonic']}4") if len(tr.T['tonic']) == 1 else 62, lead_in + 16, 14, 90)])
    S.expr(ppt, "bend", [(0, 0), (lead_in + 16, 0), (lead_in + 30, 1)])
    C.space(pr, reverb=(0.3, 2500, 150))
    # ── the main synth carries the build; the bell answers its first half
    bmel = tr.melody([tr.T.get("build_first", tr.T["call"]), tr.T["build_rise"]], lead_in)
    bmel = [(t, min(d, land - 2 - t), p) for t, d, p in bmel if t < land - 2]
    main_synth(C, bmel, "the build's melody (the drop's call teased first)")
    bell, _, bpt = bell_strip(C, "the verse's bell, answering the build's first half")
    S.notes(bpt, [x for x in bell_arps(bchords[:2], lead_in, 0)])
    # ── the landing and the drop's marks
    boom, _, bopt = C.element("808", "Impact", "the drop's downbeat: an 808 boom punched down two octaves (2.5 s) + the "
                              "crash; a reversed-cymbal swell (noise, 1.2 s attack) into the drop's second phrase",
                              dict(punch=24, amp__decay=2500, click=0.4, drive=0.45, tone=2500, volume=-11))
    S.notes(bopt, [(root_in(dchords[0], 36), land, 4, 120)])
    rev, _, rvpt = C.element("synth", "Reverse cymbal", "noise, a 1.2 s attack, high-passed: a cymbal backwards into the "
                             "drop's ninth bar", dict(NOISE, filter__mode="highpass", filter__cutoff=2500, amp__attack=1200,
                                                      amp__release=20, volume=-16))
    S.notes(rvpt, [(60, land + 32 - tr.beats(1.2), tr.beats(1.2), 110)])
    C.space(rev, reverb=(0.3, 2000, 140))
    return C, {"kick": "ch_2", "bass": sub, "kicks": dkicks}


def read_wav(path):
    import array
    w = wave.open(path)
    ch, sw, n = w.getnchannels(), w.getsampwidth(), w.getnframes()
    raw = w.readframes(n)
    if sw == 3:
        b = memoryview(raw)
        vals = [int.from_bytes(b[i:i + 3], "little", signed=True) for i in range(0, len(raw), 3)]
    else:
        vals = array.array("h" if sw == 2 else "i", raw).tolist()
    return w.getframerate(), [sum(vals[i:i + ch]) / ch for i in range(0, len(vals), ch)]


def duck_depth(tr, path, kick_beats):
    """How far the kick ducks the bass and how soon it is back: its level 5–35 ms after each kick against 200–280 ms
    after, in dB (negative: ducked at the kick, back up before the next)."""
    sr, x = read_wav(path)
    rms = lambda a, b: math.sqrt(sum(v * v for v in x[a:b]) / max(1, b - a)) + 1e-9
    ds = []
    for kb in kick_beats:
        k = int(tr.sec(kb) * sr)
        if k + int(0.28 * sr) < len(x):
            ds.append(20 * math.log10(rms(k + int(0.005 * sr), k + int(0.035 * sr)) / rms(k + int(0.2 * sr), k + int(0.28 * sr))))
    return round(sum(ds) / len(ds), 1) if ds else None


def stem(path):
    """A WAV (or anything ffmpeg reads) as mono floats at its rate — numpy, for the spectral measures."""
    import numpy as np
    if not path.endswith(".wav"):
        r = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f32le", "-ac", "1", "-"], capture_output=True)
        rate = int(subprocess.run(["ffprobe", "-v", "error", "-select_streams", "a:0", "-show_entries", "stream=sample_rate",
                                   "-of", "csv=p=0", path], capture_output=True, text=True).stdout.strip())
        return rate, np.frombuffer(r.stdout, dtype=np.float32).astype(float)
    sr, x = read_wav(path)
    return sr, np.array(x) / 2 ** 23


def sharpness(path, t0, t1):
    """How SHARP a sound is over [t0, t1) seconds: its spectral centroid (Hz) and its energy above 4 kHz (dB against
    the whole) — "the supersaw needs to be more sharp" (fb-002), measured."""
    import numpy as np
    sr, x = stem(path)
    x = x[int(t0 * sr):int(t1 * sr)]
    n, cs, hs = 4096, [], []
    f = np.fft.rfftfreq(n, 1 / sr)
    for i in range(0, len(x) - n, n):
        X = np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n))) ** 2
        if X.sum() > 1e-9:
            cs.append((f * X).sum() / X.sum())
            hs.append(X[f > 4000].sum() / X.sum())
    return round(float(np.mean(cs))), round(10 * math.log10(max(1e-9, float(np.mean(hs)))), 1)


def punch(paths, times, mix, t0, t1):
    """How PUNCHY a kick is in its mix: the RMS of its layers over the first 50 ms of each hit, against the RMS of the
    whole mix over [t0, t1) seconds, in dB — how far the hit stands out (fb-002: "kick is not punchy enough"). (A
    kick's peak against its own tail did not track the verdict: the kick called weak scored highest.)"""
    import numpy as np
    sr, x = stem(paths[0])
    for p in paths[1:]:
        _, y = stem(p)
        n = min(len(x), len(y))
        x = x[:n] + y[:n]
    _, m = stem(mix)
    mix_rms = np.sqrt(np.mean(m[int(t0 * sr):int(t1 * sr)] ** 2))
    hits = [np.sqrt(np.mean(x[int(t * sr):int((t + 0.05) * sr)] ** 2)) for t in times if int((t + 0.05) * sr) < len(x)]
    return round(20 * math.log10(float(np.mean(hits)) / mix_rms), 1) if hits else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True)
    ap.add_argument("--tracks", default=",".join(TRACKS))
    ap.add_argument("--only", default="verse,drop")
    ap.add_argument("--cc", default=DEFAULT_CC)
    ap.add_argument("--no-render", action="store_true")
    a = ap.parse_args()
    out = os.path.abspath(os.path.expanduser(a.out))
    os.makedirs(out, exist_ok=True)
    env = dict(os.environ, SOLARIS_SETTINGS=os.path.join(out, ".settings.txt"), SOLARIS_RECENTS=os.path.join(out, ".recents"))
    env.pop("DISPLAY", None)
    path = os.path.join(out, "batch.json")
    old = {}
    if os.path.exists(path):
        with open(path) as f:
            old = {m["id"]: m for m in json.load(f).get("melodies", [])}
    names = [t for t in TRACKS if t in a.tracks.split(",")]
    items, failed = [], 0
    for section in ("verse", "drop"):                              # the verses together, then the build-ups and drops
        for k, name in enumerate(names):
            tr = Track(name)
            sid = f"{section}-{k + 1}-{name}"
            if section not in a.only.split(","):
                if sid in old:
                    items.append(old[sid])
                continue
            C, keys = (verse if section == "verse" else drop)(tr)
            text = C.finish(sid).replace("project new clip.slp", f"project new {sid}.slp")
            with open(os.path.join(out, sid + ".txt"), "w") as f:
                f.write(text)
            label = "Verse" if section == "verse" else "Build-up → drop"
            item = {"id": sid, "title": f"{label} {k + 1} — {tr.T['title']}", "sound": True, "section": section,
                    "track": name, "style": "future-bass", "style_label": f"Future bass — {tr.T['style']}",
                    "bpm": tr.bpm, "key": tr.key.name, "form_label": f"sound — {section}",
                    "devices": [f"design: {d}" for d in tr.T["sound"]] + [f"{e['name']}: {e['recipe']}" for e in C.elements],
                    "design": tr.T["sound"],
                    "elements": C.elements, "seconds": round(tr.sec(C.length)),
                    "audio": [{"label": "full", "file": f"{sid}.mp3"}] +
                             [{"label": e["name"].lower(), "file": f"{sid}.{e['strip']}.mp3"} for e in C.elements],
                    "metrics": {}}
            if not a.no_render:
                if os.path.exists(os.path.join(out, sid + ".slp")):
                    os.remove(os.path.join(out, sid + ".slp"))
                r = subprocess.run([a.cc, "--script", sid + ".txt"], cwd=out, env=env, capture_output=True, text=True)
                ok = r.returncode == 0 and os.path.exists(os.path.join(out, sid + ".mp3"))
                failed += not ok
                print(f"{sid}: {'ok' if ok else 'FAILED'}" + ("" if ok else f"\n{r.stdout[-1500:]}{r.stderr[-600:]}"),
                      flush=True)
                if ok:
                    lufs = {"mix": loudness(os.path.join(out, sid + ".wav"))}
                    for e in C.elements:
                        lufs[e["name"]] = loudness(os.path.join(out, f"{sid}.{e['strip']}.wav"))
                    item["metrics"]["lufs"] = lufs
                    if section == "drop":
                        w = lambda name: os.path.join(out, f"{sid}.{next(e['strip'] for e in C.elements if e['name'] == name)}.wav")
                        land = keys["kicks"][0]
                        item["metrics"]["sub_duck_db"] = duck_depth(tr, os.path.join(out, f"{sid}.{keys['bass']}.wav"),
                                                                    keys["kicks"])
                        c, h = sharpness(w("Supersaw"), tr.sec(land), tr.sec(land + 64))
                        item["metrics"]["supersaw_centroid_hz"], item["metrics"]["supersaw_above_4k_db"] = c, h
                        item["metrics"]["kick_hit_db"] = punch([w("Kick"), w("Kick top")], [tr.sec(k) for k in keys["kicks"]],
                                                               os.path.join(out, sid + ".wav"), tr.sec(land), tr.sec(land + 64))
                    print("   LUFS " + " · ".join(f"{k} {v}" for k, v in lufs.items()) +
                          (f" · the kick ducks the sub {item['metrics'].get('sub_duck_db')} dB · the supersaw's centre "
                           f"{item['metrics'].get('supersaw_centroid_hz')} Hz · the kick hits {item['metrics'].get('kick_hit_db')} "
                           f"dB over the mix" if section == "drop" else ""),
                          flush=True)
                    for f in os.listdir(out):                      # the WAVs were for measuring only
                        if f.startswith(sid + ".") and f.endswith(".wav"):
                            os.remove(os.path.join(out, f))
            items.append(item)
    with open(path, "w") as f:
        json.dump({"batch": os.path.basename(out), "kind": "sound", "melodies": items}, f, indent=1, ensure_ascii=False)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
