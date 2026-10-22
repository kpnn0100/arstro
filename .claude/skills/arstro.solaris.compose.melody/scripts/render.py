#!/usr/bin/env python3
"""render.py — every melody of a batch heard through Solaris: a short solaris-cc song each, rendered to MP3.

    python3 render.py <batch dir with batch.json> [--cc <solaris-cc>] [--only id,id]

Each song: one bar of the chords alone (so the ear has the key), the eight bars of melody over a soft pad and a bass,
light drums where the style has them, and the last chord held. The melody leads the mix — it is what is judged. The
same arrangement for every melody of a style, so two melodies differ only in themselves.

Writes <id>.txt (the solaris-cc script, re-runnable) and <id>.mp3 beside batch.json. Runs solaris-cc with scratch
settings in the batch folder — never the user's own settings or recents.
"""
import argparse, json, os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
DEFAULT_CC = os.path.join(REPO, "build", "apps", "solaris", "cli", "solaris-cc")
INTRO = 4.0                      # beats of chords before the melody
KICK, SNARE, CLAP, CHAT, OHAT = 36, 38, 39, 42, 46


def num(x):
    return f"{x:.6g}"


def tokens(notes):
    return " ".join(f"{p}@{num(a)}:{num(l)}:{v}" for p, a, l, v in notes)


def voicing(pcs, root):
    """The chord around E3–A4, root first; the bass gets the root an octave or two down."""
    out = []
    for pc in pcs:
        m = 52 + (pc - 52) % 12
        out.append(m)
    return sorted(set(out))


def script(mel, out_mp3):
    bpm = mel["bpm"]
    L = []
    line = L.append
    total = INTRO + 32 + 4
    line(f"# {mel['id']} — {mel['style_label']}, {mel['key']}, {mel['form']} / {mel['rhythm']} "
         f"(arstro.solaris.compose.melody render.py)")
    line(f'project new {mel["id"]}.slp --bpm {bpm} --name "{mel["id"]}"')
    # the pad: the chords, soft and slow
    line(f"clip add --instrument synth --at 0 --length {num(total)}")
    line('set ch_2.name="Chords"')
    line("set dv_1.osc1.wave=saw dv_1.osc1.voices=3 dv_1.osc1.detune=14 dv_1.osc1.width=20 dv_1.osc2.wave=triangle "
         "dv_1.osc2.octave=1 dv_1.osc2.level=0.25 dv_1.filter.cutoff=1600 dv_1.filter.res=0.1 dv_1.filter.env=0.2 "
         "dv_1.amp.attack=60 dv_1.amp.decay=600 dv_1.amp.sustain=0.85 dv_1.amp.release=700 dv_1.volume=-15")
    pad = []
    bars = [(0.0, mel["chord_pcs"][0], mel["chord_roots"][0])]                 # the lead-in bar: the first chord
    bars += [(INTRO + 4 * k, mel["chord_pcs"][k], mel["chord_roots"][k]) for k in range(8)]
    bars += [(INTRO + 32, mel["chord_pcs"][7], mel["chord_roots"][7])]       # the last chord rings on: home, after a
    # cadence (batch-003: the loop's FIRST chord after the end made "the ending is not good")
    for at, pcs, root in bars:
        pad += [(m, at, 4, 70) for m in voicing(pcs, root)]
    line(f'notes add pt_1 "{tokens(pad)}"')
    # the bass: the root, held
    line(f"clip add --instrument synth --at 0 --length {num(total)}")
    line('set ch_3.name="Bass"')
    line("set dv_2.osc1.wave=sine dv_2.osc2.level=0 dv_2.filter.cutoff=900 dv_2.amp.attack=5 dv_2.amp.sustain=0.9 "
         "dv_2.amp.release=200 dv_2.volume=-13")
    bass = [(36 + (root - 36) % 12, at, 3.75, 90) for at, _, root in bars]
    line(f'notes add pt_2 "{tokens(bass)}"')
    # the melody — what is judged
    voice = mel.get("voice", "lead")
    line(f"clip add --instrument {'piano' if voice == 'piano' else ('wavetable' if voice == 'pluck' else 'synth')} "
         f"--at 0 --length {num(total)}")
    line('set ch_4.name="Melody"')
    if voice == "piano":
        line("set dv_3.strings.brightness=1.1 dv_3.hammer.hardness=1.0 dv_3.volume=6 ch_4.gain=8")   # a struck string reads
        # far quieter than a held pad at the same setting: measured, +12 puts it over the chords as the lead synth is
    elif voice == "pluck":
        line("set dv_3.osc1.table=harmonics dv_3.osc1.position=0.6 dv_3.osc1.posenv=-0.3 dv_3.osc1.voices=1 dv_3.osc2.level=0 "
             "dv_3.filter.cutoff=4200 dv_3.filter.env=1.5 dv_3.fenv.decay=220 dv_3.fenv.sustain=0 dv_3.amp.attack=1 "
             "dv_3.amp.decay=420 dv_3.amp.sustain=0.15 dv_3.amp.release=200 dv_3.volume=-3")
    else:
        # batch-001: the bright saw lead rated lowest (1.9 against the piano's 2.8): rounder now — a square under the
        # saw instead of a saw an octave up, the filter lower, a softer attack, a little vibrato, a longer release
        line("set dv_3.osc1.wave=saw dv_3.osc1.voices=3 dv_3.osc1.detune=10 dv_3.osc1.width=12 dv_3.osc2.wave=square "
             "dv_3.osc2.octave=0 dv_3.osc2.voices=1 dv_3.osc2.level=0.3 dv_3.filter.cutoff=3200 dv_3.filter.res=0.1 "
             "dv_3.filter.env=0.6 dv_3.fenv.decay=320 dv_3.fenv.sustain=0.6 dv_3.amp.attack=10 dv_3.amp.decay=220 "
             "dv_3.amp.sustain=0.85 dv_3.amp.release=220 dv_3.vibrato.depth=0.08 dv_3.vibrato.rate=5 "
             "dv_3.voice.mode=legato dv_3.voice.glide=0 dv_3.volume=-4")
    mnotes = [(n["p"], INTRO + n["t"], max(0.1, n["d"] - 0.02), 104 if n["t"] % 1 == 0 else 92) for n in mel["notes"]]
    line(f'notes add pt_3 "{tokens(mnotes)}"')
    nxt_ch, nxt_dv = 5, 4
    # light drums, so the rhythm is felt — never louder than the melody
    drums = mel.get("drums")
    if drums:
        line(f"clip add --instrument drums --at {num(INTRO)} --length 32")
        line(f'set ch_{nxt_ch}.name="Drums" dv_{nxt_dv}.volume=-12')
        d = []
        for b in range(8):
            t = 4 * b
            if drums == "four":
                d += [(KICK, t + k, 0.25, 110) for k in range(4)] + [(CLAP, t + k, 0.25, 92) for k in (1, 3)]
                d += [(OHAT, t + k + 0.5, 0.25, 70) for k in range(4)]
            elif drums == "half":
                d += [(KICK, t, 0.25, 112), (KICK, t + 2.75, 0.25, 96), (SNARE, t + 2, 0.25, 104), (CLAP, t + 2, 0.25, 90)]
                d += [(CHAT, t + 0.5 * k, 0.25, 64 if k % 2 else 78) for k in range(8)]
            else:  # lofi
                d += [(KICK, t, 0.25, 96), (KICK, t + 2.5, 0.25, 84), (SNARE, t + 1, 0.25, 80), (SNARE, t + 3, 0.25, 80)]
                d += [(CHAT, t + 0.5 * k + (0.08 if k % 2 else 0), 0.25, 56 if k % 2 else 66) for k in range(8)]
        line(f'notes add pt_4 "{tokens(d)}"')
        nxt_ch, nxt_dv = 6, 5
    # a room, and a gentle limiter
    line("strip add --kind bus --name Room")
    line(f"device add ch_{nxt_ch} --type reverb")
    line(f"set dv_{nxt_dv}.decay=1800 dv_{nxt_dv}.size=110 dv_{nxt_dv}.mix=1 dv_{nxt_dv}.lowcut=300")
    line(f"send add ch_4 --to ch_{nxt_ch} --gain -12")
    line(f"send add ch_2 --to ch_{nxt_ch} --gain -10")
    line('device add master --type limiter')
    line(f"set dv_{nxt_dv + 1}.gain=3 dv_{nxt_dv + 1}.ceiling=-1.5 dv_{nxt_dv + 1}.truePeak=on")
    line("project save")
    line(f"render --out {os.path.basename(out_mp3)} --format mp3 --kbps 192")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("batch_dir")
    ap.add_argument("--cc", default=DEFAULT_CC)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    bdir = os.path.abspath(a.batch_dir)
    with open(os.path.join(bdir, "batch.json")) as f:
        batch = json.load(f)
    only = set(x for x in a.only.split(",") if x)
    env = dict(os.environ, SOLARIS_SETTINGS=os.path.join(bdir, ".settings.txt"), SOLARIS_RECENTS=os.path.join(bdir, ".recents"))
    env.pop("DISPLAY", None)
    failed = 0
    for mel in batch["melodies"]:
        if only and mel["id"] not in only:
            continue
        mp3 = os.path.join(bdir, mel["id"] + ".mp3")
        txt = os.path.join(bdir, mel["id"] + ".txt")
        for stale in (mp3, os.path.join(bdir, mel["id"] + ".slp")):
            if os.path.exists(stale):
                os.remove(stale)
        with open(txt, "w") as f:
            f.write(script(mel, mp3))
        r = subprocess.run([a.cc, "--script", os.path.basename(txt)], cwd=bdir, env=env, capture_output=True, text=True)
        ok = r.returncode == 0 and os.path.exists(mp3)
        failed += not ok
        print(f"{mel['id']}: {'ok' if ok else 'FAILED'}" + ("" if ok else f"\n{r.stdout[-600:]}{r.stderr[-600:]}"))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
