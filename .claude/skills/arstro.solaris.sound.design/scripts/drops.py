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
TARGETS = {"Kick": -10.0, "Snare": -16.5, "Saw": -14.5, "Bass": -16.0}   # the kick at −13: its hit only 4.8 dB over the mix


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


def arrange(opt, kick_file, trim=None, seg=None, snares=None, kick_words=None):
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
        S.set(dv, volume=-10, **dict(FLAT, **patch))
        notes = []
        for t, d, (_, voicing, _) in seg:
            ps = [midi(n) for n in voicing.split()]
            ps = ps[:2] if part == "low" else (ps[-1:] if part == "top" else ps)
            notes += [(p + shift, t, max(0.1, d - 0.04), 110) for p in ps]
        S.notes(pt, notes)
        line(f"route {ch} --to {bus}")
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
    S.set(bdv, **(dict(MONO, **patch) if kind == "analog" else patch))
    S.notes(bpt, [(midi(root) + shift, t, max(0.1, d - 0.04), 115) for t, d, (_, _, root) in seg])
    for fkind, fparams in fx:
        S.device(bass, fkind, **fparams)
    S.device(bass, "imager", width=0, mono="on", frequency=200)
    line(f"strip duck {bass} --under {kick} --amount 14")
    S.set(S.new('dv'), release=70)
    E.append({"name": "Bass", "strip": bass, "recipe": opt["bass_note"] + ". With every kick, the saw's lengths, no "
              "glide; mono; ducked 14 dB by the kick and back in ~150 ms"})
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
    ap.add_argument("--design", default="808", help="with --moods: which of OPTIONS plays them")
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
    runs = [(m, design) for m in MOODS] if a.moods else [(None, o) for o in OPTIONS]
    for k, (mood, opt) in enumerate(runs, 1):
        sid = f"mood-{k}" if mood else f"drop-{k}"
        if only and str(k) not in only:
            if sid in old:
                items.append(old[sid])
            continue
        trim, ok, E = {}, True, []
        for attempt in ((1, 2) if not a.quick else (2,)):
            if mood:
                seg, snares = mood_segments(mood)
                text, E = arrange(opt, kick, trim, seg, snares, "the kick on 1, 4 and 6 of every two bars (the snare on 3 and 7)")
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
                trim = {n: max(-18, min(18, round(TARGETS[n] - v, 1))) for n, v in lufs.items() if v is not None and v > -60}
        failed += not ok
        title = (f"Mood {k} — {mood['name']}: {mood['chords']}" if mood else f"Drop {k} — {opt['name']}: {opt['about']}")
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
