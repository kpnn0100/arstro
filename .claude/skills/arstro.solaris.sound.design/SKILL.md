---
name: arstro.solaris.sound.design
description: Sound design in Solaris — the SOUNDS themselves (the patch of every element, its layers, its space, how the kick, the bass and the chords make room for each other), approved by ear section by section in the Sound Lab. Future bass first (Trap Nation style — Illenium, Said The Sky, Dabin, San Holo) — a verse (reese bass, airy bell, main synth), a build-up (rolls, risers, the breath before the drop), a drop (a supersaw carrying the melody, a sub and a mid bass struck with every kick and ducked by it, percussion filling between). Use it whenever a sound is about to be designed or judged — an instrument patch, a bass, a lead, a pad, drums and percussion, risers and impacts, a drop — before writing any demo track's sounds (`apps/solaris/demo/*/make_script.py`), when the user asks for better sound, "sound design", "make it sound professional / like Trap Nation / like Illenium", a better drop, a fatter supersaw, a punchier kick, and on `/arstro.solaris.sound.design` — even when the sound is a small part of a bigger task. It holds the research (references/research.md), the recipes with Solaris's own numbers (references/future-bass.md), the generator (scripts/futurebass.py), the Sound Lab launcher (scripts/lab.py), and the approver's ledger (taste.md).
---

# arstro.solaris.sound.design

> **The user is the approver of the sound** — their verdicts outrank every recipe in this file. **Read `taste.md`
> first, every time:** it is what they have loved and rejected in a sound, and what that taught.

The melody skill (`arstro.solaris.compose.melody`) owns the notes. This skill owns how they SOUND: which instrument,
which patch, which layers, which space, and how the elements share the frequencies and the time. The user's words that
started it (2026-10-22): *"now come to the sound design — focus on future bass only — give me verse, build up, drop
separately, i will judge the sound design … for verse need to have reese bass, airy bell and main synth; for drop need
super saw, bass, kick and bassline sync, a lot of percussion to fill between — learn from professional from trap
nation first"*.

**The sound lives in the DSP library** (`arstro.solaris.implement` law 1). This skill COMPOSES the devices Solaris
already has (`apps/solaris/docs/API.md`): the Basic Synth, Wavetable, FM, 808, Drum Machine; filter, saturator,
phaser, chorus, imager, shaper, compressor, transient, reverb, delay, limiter. If a sound truly needs a device that
does not exist, that is a DSP task (`arstro.dsp.implement`) — never a sample hidden in a script.

---

## 0. Before a single sound

1. **Read `taste.md`.**
2. **Know the section's JOB.** A verse makes room for the voice, a build-up makes the listener wait, a drop delivers.
   The same supersaw is wrong in a verse and right in a drop.
3. **Give every element ONE job and ONE place.** For each one, know:
   - its register (which octave, where its energy sits in Hz);
   - its width (mono, or wide);
   - its time (where in the bar it speaks);
   - its space (dry, a room, a hall, an echo).

   Two elements with the same job and the same place fight, and the mix turns to mud.

## 1. The elements of future bass (references/future-bass.md has every number)

| element | job | register | width | the recipe in one line |
|---|---|---|---|---|
| **kick** | the pulse, the punch | 55 Hz body + a click | mono | the Drum Machine's kick tuned to the key (it settles on 48·2^(tune/12) Hz — A1 = +2.36 for D major), 300–380 ms, a transient boost |
| **snare + clap** | the backbeat on 3 (half time) | 200 Hz – 5 kHz | mono-ish | a snare a semitone up and a clap together, one shared room |
| **percussion** | the motion between the hits | 5–12 kHz | spread L/R | 16th hats accented on the 8ths, 32nd and triplet rolls into each phrase, rim snaps, a tom fill after every second snare, a shaker, a ride, crashes on the phrase |
| **sub** | the floor | 40–90 Hz | MONO | a sine, struck WITH every kick on the chord's root, ducked by the kick |
| **mid bass** | the bass's voice | 150 Hz – 2 kHz | a little width | saws and a square an octave above the sub, low-passed, tube-driven, **high-passed at 160 Hz** so the sub owns the floor |
| **reese bass** (verse) | a dark, moving bed | 70–1300 Hz | mono under 140 Hz | two saws ±12 cents beating, a sine an octave under, low-pass opening through the verse, a slow phaser, soft saturation |
| **supersaw** (drop) | the hook: the melody on top of every chord | 180 Hz – 12 kHz | WIDE (160 %), mono under 180 Hz | two stacks of 5 saws (±16 ct, one an octave up), a pitch rise into each stab, a 1/16 tremolo on held chords (the "wub"), pumped by the kick |
| **airy bell** (verse) | sparkle above everything | D6–D7, 1–2.5 kHz and its partials | wide | FM: a 3.5-ratio modulator's clang over the fundamental, an octave pair 7 cents sharp; high-passed 600 Hz, a dotted-8th echo, a 4.5 s room |
| **main synth** (verse) | carries the melody | an octave above the voice's range | some width | a pluck (the attack) layered with a soft saw lead (the sustain, a late vibrato), chorused, an echo, a room |
| **risers, impacts** | tension, then the landing | everywhere | wide | noise through an opening low-pass, a saw bent up two octaves, a snare roll pitching up, an 808 boom + a crash on the downbeat |

## 2. The relations — what makes it sound professional

1. **The kick and the bass are ONE event.** The bassline plays the kick's rhythm: every kick strikes a bass note on
   the chord's root. Then the kick ducks the bass with a side-chain, `strip duck <bass> --under <kick>`, so the kick's
   punch comes first and the bass swells in after it. The duck needs:
   - a SHORT release (70 ms), so the bass is back within ~200 ms. Measured: at the default half-beat release, the
     kick's tail kept the sub down ~600 ms; it never recovered and the duck read 2.6 dB, not 9.4;
   - the kick tuned to the key, so the two do not beat against each other.
2. **One owner of the low end at a time.** The sub is mono. The mid bass is high-passed at 160 Hz. The supersaw and
   the reese are mono below 140–180 Hz (the imager). The kick's body (55 Hz) and the sub take turns, through the duck.
3. **The chords pump.** The supersaw is ducked 12 dB by the kick (release 120 ms) and 5 dB by the snare: the
   breathing that IS the future bass drop.
4. **Width is a choice.** Bass and kick are in the middle. The chords, the bell, the hats and the percussion are spread.
5. **Each element carries its own space**, its reverb and echo in its own rack. Its solo then sounds as designed, and
   two elements can sit in different rooms (a dry kick, a big bell).
6. **Measure, don't assume.** `futurebass.py` prints every element's loudness and the duck. The Drum Machine's pads
   read 10–20 LU under the synths at the same volume — measured, and set. The order of loudness in a drop: supersaw ≥ kick > snare
   > sub ≈ mid bass > percussion > shaker.

## 3. The sections

- **Verse** — reese bass, airy bell, main synth (the user's list), a soft kick on 1 and a snap on 3, hats from the
  second half. The main synth leads, the reese under it, the bell above both.
- **Build-up** — in two halves. The first is melodic (the main synth, the bell). The second hypes: the filtered
  supersaw's stabs quickening (halves → sixteenths) as its low-pass opens, the kick rolling, a snare roll (quarters →
  32nds) climbing an octave, a noise riser and a pitch riser. The reese leaves so the floor drops away. Then a BREATH:
  everything stops for a beat and a half, one big clap, and the drop's first bar (an impact) so the landing is heard.
- **Drop** — the supersaw carrying the melody, the sub and the mid bass with every kick, the snare and clap on 3, and
  the percussion filling between: hats, rolls, snaps, toms, a shaker, a ride, crashes, a reversed cymbal into the
  second phrase. An 808 boom and a crash on the downbeat.

## 4. The tools — `scripts/`

| script | what it does |
|---|---|
| `futurebass.py --out ~/Music/Solaris/sound-lab/fb-NNN [--only verse,build,drop]` | writes and renders the three sections (Paper Lanterns' material from the melody skill's songbook, D major, 150 bpm half time). Each is a clip plus every element's solo as MP3; each element's loudness and the kick's duck on the sub are measured into `batch.json` |
| `lab.py [--port 8788]` | **the Sound Lab**: the melody skill's lab server on `~/Music/Solaris/sound-lab` with THIS skill's ledger (`data/judgments.jsonl`). Every section is a card: play the full clip or any element alone, read its recipe and its measurements, judge it (Love it · Good · Okay · Weak · No) and say why |

A new genre becomes a new script beside `futurebass.py`, with its recipes in `references/<genre>.md`.

## 5. The audition loop — the user approves

1. Make a batch (`fb-NNN`, the next number), steered by `taste.md`.
2. Serve the lab (`lab.py`, in the background) and give the user the URL.
3. Wait for the verdicts, on the page or in chat. Record chat verdicts through `POST /api/verdict`.
4. Learn. Write the batch's entry in `taste.md`: the counts, what was loved and rejected and which element, the user's
   own words, and the LESSON in general terms ("the reese too dark: open its filter"). A lesson that is a rule becomes
   a recipe change in `futurebass.py` and `references/future-bass.md`.
5. Next batch: lean into what was loved; change what was named.
6. Commit the ledger, the data and the recipes (named files only, the `Co-Authored-By` trailer).

## 6. Failure modes already met

- **A vibrato that never sounds**: the Basic Synth's vibrato plays only while the mod wheel is up. Draw the `mod` lane.
- **A duck that never lets go**: the default half-beat release plus the kick's tail held the sub down ~600 ms (2.6 dB
  measured). Set the duck's release short (70 ms bass, 120 ms chords) and the kick's decay to ~300 ms.
- **Quiet drums**: the Drum Machine's pads read 10–20 LU under the synths at the same volume. Measure each strip and set it.
- **Solos without their space**: a send-only reverb is missing from a solo. Put the space in the element's rack.
- **A detuned pair into a resonant filter rings** (the melody skill's voice: +5.5 LU). Keep oscillators in tune
  unless the detune IS the sound (the reese, the supersaw), and measure.
- **A bus name with a space** must be quoted in the script (`strip add --kind bus --name "Main synth"`).

## References

- `references/research.md` — what professional future bass producers and their teachers say, with sources, and what
  each recipe took from them; and what the search could NOT find (Trap Nation's own material).
- `references/future-bass.md` — every element's recipe with Solaris's parameters, and why.
- `taste.md` — the approver's verdicts on sounds and the lessons; read first.
- `data/judgments.jsonl` — every verdict with the section it judged.
