---
name: arstro.solaris.sound.design
description: Sound design in Solaris — how a track SOUNDS — built on the analog synth (registry `analog`, a virtual-analog polysynth with the JP-8000's supersaw unison, a ladder filter, analog envelopes) and judged by ear in the Sound Lab. Use it whenever a sound is about to be designed, chosen or judged — a synth patch (supersaw, bass, reese, pluck, lead, pad, vocal chop, riser), drums and percussion, a mix balance, a drop — before writing any demo track's sounds (`apps/solaris/demo/*/make_script.py`), when the user asks for better sound, "sound design", "make it sound professional / clean / energetic / like Illenium, San Holo, Martin Garrix", a future bass song to rate, or how to make a patch on the analog synth, and on `/arstro.solaris.sound.design` — even when sound is a small part of a bigger task. It holds the method, the patch guide for the analog synth (references/analog-patches.md), the research (references/research.md), the song generator with its levelling pass and measures (scripts/futurebass.py), the Sound Lab launcher (scripts/lab.py), and the approver's ledger (taste.md).
---

# arstro.solaris.sound.design

> **The user is the approver of the sound.** Their verdicts outrank everything here. **Read `taste.md` first, every
> time**: what they loved and rejected, in their words, and what it taught.

This skill was removed and remade on 2026-11-27, at the user's word: *"remove the skill, remake the skill for me — i'm
starting to think the problem is the synth, the synth limited you so you can not make good music … create me a synth
that replicate an analog synth, learn future bass online and make me a song that is good, energetic"*. The synth came
first (the DSP library's `AnalogSynth`, Solaris R-INST-13); this skill is how to use it.

The melody skill (`arstro.solaris.compose.melody`) owns the notes. This skill owns the sound: which instrument, which
patch, which register, which space, and how loud. **The sound lives in the DSP library** (`arstro.solaris.implement`
law 1): a sound the devices cannot make is a DSP task (`arstro.dsp.implement`), never a sample hidden in a script. The
first version of this skill spent a month turning knobs on a synth that could not do the job. If three honest tries at
a patch do not get there, say the INSTRUMENT is the limit, and fix the instrument.

## Inside Solaris — when the app itself started you

If the environment variable `SOLARIS_CONTROL` is set, you are the user's agent inside the Solaris window:
- work on the song that is open, through the `solaris_*` MCP tools; every line is one step of their undo. Do not
  `project new`, write into the skill folder, or render a batch;
- the user judges in their own window. Make ONE proposal, say what you made and where, let them play it; `undo` if
  they dislike it;
- the method and the patch guide below still apply. You cannot hear: check with `show <id>` and
  `render --out <file.wav> --measure`, and say what you measured.

## 1. The method

1. **Read `taste.md`.**
2. **Know the section's job.** A verse makes room for the line that sings; a build makes the listener wait; a drop
   delivers. Energy is CONTRAST: the drop hits hard because the bar before it is nearly empty and the verse is thin.
3. **Choose the register first.** It decides more than any knob. The verse line B4–B5; the drop's top line C5–E6;
   chords filling down to C4; the mid bass an octave over the sub; the sub E1–D♯2; a bell E5–G6.
4. **One job and one place for every element**: its register, its width, its moment in the bar, its space.
5. **Start every patch from the analog synth** and from `references/analog-patches.md`. Reach for another
   instrument only for what an analog synth is not: drums (the Drum Machine), a bell (FM), keys (the piano, the
   guitar), an 808.
6. **Clean is subtraction**: no low end in anything but the kick and the sub; one room for the verse, a drier drop;
   drive only where a sound should growl.
7. **Measure, then let the user hear.** Levels by the levelling pass (§3); sections by loudness; the kick by its hit
   over the mix. A measure is trusted only after it has agreed with a verdict.

## 2. What makes it sound professional

- **The supersaw is free-running unison.** Seven voices an oscillator, random phases, the JP-8000's detune law,
  centre and sides blended, a second stack an octave up, a breath of noise. The old synth started its voices in
  phase: a click and a comb. Detune 0.35–0.5 for chords (wider smears the pitch), blend 0.7–0.8.
- **The kick and the bass are one event.** Every drop kick strikes a bass note; the kick ducks the sub and the mid
  bass (`strip duck`, release 70 ms) so the punch comes first. The sub is a sine from phase zero (`voice.phase=reset`),
  so every note has the same weight.
- **The chords pump**: ducked 10 dB by the kick (release 130 ms), 3 dB by the snare.
- **Movement is in the synth**: a pitch envelope for the rise into a stab, an LFO with a fade-in for a late vibrato,
  an LFO on the beat for a sway, the filter envelope for a pluck. Not a gate after it, not a lane drawn by hand.
- **The mix is set by faders, measured**; the limiter is a ceiling. The drop's loudness stands 4–7 LU over the verse's.

## 3. The tools — `scripts/`

| script | what it does |
|---|---|
| `futurebass.py --check` | the writing, checked before rendering: chord tones on strong notes, registers, surprise, hook/counter collisions |
| `futurebass.py --out ~/Music/Solaris/sound-lab/songs-NNN [--songs a,b] [--stems] [--quick]` | each song in `SONGS` (intro, verse, build, drop, break, build, bigger drop, outro): writes the solaris-cc script, renders, LEVELS (every element measured alone, its fader set to `TARGETS`, rendered again), and measures into `batch.json`. The song and every element alone as MP3. Slow: the analog synth runs hundreds of oscillators at twice the rate — allow minutes a song |
| `lab.py [--port 8788]` | the Sound Lab: the melody skill's lab server on `~/Music/Solaris/sound-lab`, verdicts into THIS skill's `data/judgments.jsonl`. A card a song: sections on a timeline (click to jump), the song or any element alone, the recipes, the measures, five verdicts and a note |

A new song is a `SONGS` entry (key, tempo, chords, melodies written in order, a small design). A new genre is a new
script beside `futurebass.py`.

## 4. The loop — the user approves

1. Make a batch (the next `songs-NNN`), steered by `taste.md`.
2. Serve the lab (`lab.py`, in the background); give the user the URL.
3. Take their verdicts (page, or chat → `POST /api/verdict`).
4. Learn: the batch's entry in `taste.md` — counts, the user's words, the lesson. A lesson that is a rule changes the
   patch in `futurebass.py` AND `references/analog-patches.md`.
5. **Before changing a number, ask what the complaint is about.** "The bell is not good", twice, was its design (an
   inharmonic ratio, a detuned partial, an octave too high), not its EQ. "The sound design is nowhere near
   acceptable" was the instrument.
6. Commit the ledger, the data and the patches (named files only).

## 5. Failure modes already paid for

- **Polishing a weak instrument** (a month, three batches, verdicts stuck at "okay").
- **An octave too high**: thin and piercing; no patch fixes a wrong register.
- **Answering a word with more of everything**: "sharp" → boosts, "hard" → 18 dB of drive, "punchy" → saturation;
  the sum was harsh, and the user then asked for clean. Make the smallest change that moves the measure.
- **A limiter doing the mix**; **30-second clips** that never show a drop lifting off its verse.
- **A duck that never lets go**: `strip duck`'s default half-beat release holds the bass down 600 ms. Set 70 ms.
- **Quiet drums, quiet guitar** at the same volume as a synth (10–20 LU): the levelling pass exists for this.
- **A measure that does not track the ear**: check it against a verdict before trusting it.
- **A scratch path too long for a unix socket** fails `solaris_control`, not your change.

## References

- `references/analog-patches.md` — the analog synth's controls in a sound designer's terms, and every patch.
- `references/research.md` — what the synth and the genre were learned from, with sources.
- `taste.md` — the approver's verdicts and the lessons; read first.
- The synth's math: `core/DigitalSignalProcessing/src/instrument/README.md` ## AnalogSynth.
