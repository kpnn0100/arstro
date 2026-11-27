#!/usr/bin/env python3
"""dubstep.py — the MELODIC DUBSTEP drop. (The user, 2026-11-27, after the future bass drops: "try melodic dubstep".)

    python3 dubstep.py --out ~/Music/Solaris/sound-lab/dubstep-NNN --kick <a kick .wav> [--only 1,3] [--quick]

What the genre is (references/research.md ## Melodic dubstep): half time at 140–150 bpm, the kick on 1 and a BIG snare
on 3; an emotional chord progression on a wide supersaw that is HELD and pumps under the kick (not chopped, as the
future bass drop's is); a lead that sings over it; and a GROWL bass — a low saw and pulse through a driven filter that
an LFO opens and closes on the beat — that talks back to the melody. A clean sub carries the root under everything.

Everything the user has approved is kept: their kick sample, the 808 as the sub, the wide supersaw, the mood chords
(drops.py MOODS), the Glass stack lead (drops.py LEADS[0]) and its melodies. What is new is the growl, the held and
pumping chords, the bigger snare, and DROPS: five ways the melody and the growl share the two bars.

Each drop is rendered twice — the second time with every element's fader at its target (TARGETS) — and every element
alone for the lab. Runs solaris-cc with scratch settings in the output folder; the kick is never committed.
"""
import argparse, json, os, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import drops as D  # noqa: E402
from drops import Script, num, loudness, DEFAULT_CC, midi, BPM, BARS  # noqa: E402

CHAT, OHAT = 42, 46
KICKS, SNARES = [0, 4, 5.5], [2, 6]                # in a two-bar unit: 1 · (3) · 1 · the and of 2 · (3)
CHORD_HITS = [(0, 4), (4, 1.5), (5.5, 2.5)]        # the mood's three voicings: bar one, bar two, the push
TARGETS = {"Kick": -10.0, "Snare": -15.0, "Hats": -27.0, "Saw": -14.5, "Sub": -16.0, "Growl": -14.5, "Lead": -14.5}

_G = dict(osc1__wave="saw", osc1__octave=-1, osc1__unison=3, osc1__detune=0.18, osc1__blend=0.5, osc1__spread=0.3,
          osc1__level=0.8, osc2__wave="pulse", osc2__width=0.3, osc2__octave=-1, osc2__level=0.6, sub__level=0,
          filter__keytrack=0.3, amp__attack=2, amp__decay=0, amp__sustain=1, amp__release=40, velocity=0,
          voice__mode="mono", voice__glide=0, voice__phase="reset", voice__drift=0, volume=-8)
GROWLS = {
    "wub": dict(words="the wobble: three saws and a narrow pulse an octave down through a driven 24 dB ladder at 420 Hz "
                "that an LFO opens 2.4 octaves and closes again every eighth note; tube saturation after it",
                patch=dict(_G, filter__mode='"lowpass 24"', filter__cutoff=420, filter__res=0.35, filter__drive=12,
                           lfo__shape="sine", lfo__sync='"1/8"', lfo__cutoff=2.4),
                fx=[("saturator", dict(drive=12, curve="tube", mix=1, output=-6))]),
    "triplet": dict(words="the growl: the same low saw and pulse through a driven band-pass at 650 Hz swept by a "
                    "triangle LFO in eighth-note TRIPLETS — it rolls against the straight drums; clipped hard",
                    patch=dict(_G, filter__mode='"bandpass 12"', filter__cutoff=650, filter__res=0.5, filter__drive=14,
                               lfo__shape="triangle", lfo__sync='"1/8T"', lfo__cutoff=2.0, lfo__width=0.15),
                    fx=[("saturator", dict(drive=16, curve="hard", mix=1, output=-8))]),
    "yoi": dict(words="the talking bass: the low saw and pulse through a band-pass that a rising saw LFO sweeps up once "
                "every quarter note, clipped, then the vowel filter moving OH → EE on each note: it says 'yoi'",
                patch=dict(_G, filter__mode='"bandpass 12"', filter__cutoff=500, filter__res=0.45, filter__drive=10,
                           lfo__shape="saw", lfo__sync='"1/4"', lfo__cutoff=2.2),
                fx=[("saturator", dict(drive=14, curve="hard", mix=1, output=-8)),
                    ("vowel", dict(resonance=1.2, glide=150, mix=0.7, output=6))], vowel=True),
    "slow": dict(words="the swell: the low saw and pulse through the driven ladder, opened and closed by a slow sine LFO "
                 "once every half bar — a long 'wow' under the chords; tube saturation",
                 patch=dict(_G, filter__mode='"lowpass 24"', filter__cutoff=380, filter__res=0.3, filter__drive=10,
                            lfo__shape="sine", lfo__sync='"1/2"', lfo__cutoff=2.6),
                 fx=[("saturator", dict(drive=10, curve="tube", mix=1, output=-6))]),
}

CALLS = {c["name"]: c for c in D.CALLS}
MELS = {m["shape"]: m for m in D.MELODIES}
# Five drops. `chords` and `growl` are the windows (start, end, in beats of the two-bar unit) where each plays;
# `lead` is a melody in drops.py's shape (four units and a last one).
DROPS = [
    dict(name="Soaring", mood="Longing", growl_kind="wub", lead=MELS["the cry"],
         about="the chords held and pumping all the way, the lead singing over them; the wobble only fills the last "
               "beat and a half of every two bars — the most melodic of the five",
         chords=[(0, 8)], growl=[(6.5, 8)]),
    dict(name="Melody asks, bass answers", mood="Longing", growl_kind="wub",
         lead=D.call_lines(CALLS["Question and answer"])[0],
         about="bar one: the chords and the lead's question; then the chords cut out and the wobble answers alone for "
               "three beats — the classic melodic dubstep trade",
         chords=[(0, 5)], growl=[(5, 8)]),
    dict(name="Bass asks, melody answers", mood="Falling", growl_kind="triplet",
         lead=D.call_lines(CALLS["The pluck calls"])[0],
         about="the other way round: bar one is the triplet growl alone over the drums, dark; bar two the chords "
               "open and the lead answers with one long sliding note",
         chords=[(4, 8)], growl=[(0, 4)]),
    dict(name="Talking bass", mood="Longing", growl_kind="yoi", lead=D.call_lines(CALLS["Echo"])[0],
         about="the chords held throughout; the lead sings its three-note rise and the bass says 'yoi' back in each "
               "gap — a short one after the first phrase, a longer one after the second",
         chords=[(0, 8)], growl=[(3, 4), (6, 8)]),
    dict(name="Wall", mood="Dreamy", growl_kind="slow", lead=MELS["the ache"],
         about="everything at once: the chords held, the lead over them, and the slow swelling bass under the whole "
               "two bars — the heaviest and fullest",
         chords=[(0, 8)], growl=[(0, 8)]),
]


def arrange(drop, kick_file, trim=None):
    S, E, L = Script(), [], 4 * BARS + 8
    line = S.line
    mood = next(m for m in D.MOODS if m["name"] == drop["mood"])
    design = next(o for o in D.OPTIONS if o["name"] == "808")
    line(f"# melodic dubstep drop — {drop['name']} (arstro.solaris.sound.design dubstep.py) — {BPM} bpm half time")
    line(f'project new drop.slp --bpm {BPM} --name "Dubstep — {drop["name"]}"')
    units = range(BARS // 2)
    # the kick: the user's sample
    kick, kdv, kpt = S.new('ch'), S.new('dv'), S.new('pt')
    S.new('ac')
    line(f'clip add --instrument sampler --sample "{kick_file}" --at 0 --length {num(L)}')
    line(f'set {kick}.name="Kick" {kpt}.name="Kick" {kpt}.length={num(L)} {kdv}.mode=one-shot {kdv}.release=200 {kdv}.velocity=0')
    S.notes(kpt, [(60, 8 * u + k, 1.0, 127) for u in units for k in KICKS] + [(60, 64, 1.0, 127)])
    E.append({"name": "Kick", "strip": kick, "recipe": f"your sample {os.path.basename(kick_file)} on 1 of every bar, "
              "and once more on the and of 2 in the second bar"})
    # the snare: big — lower, longer, a clap inside it, a long bright room
    sn, sdv, spt = S.instrument("drums", "Snare", L)
    S.set(sdv, snare__tune=0, snare__decay=300, snare__tone=0.7, clap__decay=420, clap__tone=0.55, clap__level=-4, volume=3)
    S.notes(spt, [(p, 8 * u + t, 0.5, v) for u in units for t in SNARES for p, v in ((D.SNARE, 124), (D.CLAP, 110))])
    S.device(sn, "transient", attack=35, sustain=0, output=0)
    S.device(sn, "reverb", decay=1700, size=120, mix=0.24, lowcut=250, highcut=9000, width=1)
    E.append({"name": "Snare", "strip": sn, "recipe": "the Drum Machine's snare at its own pitch, 300 ms, a clap inside "
              "it, a sharper attack and a 1.7 s room — on beat 3 of every bar, the genre's big backbeat"})
    # the hats: quiet eighths, an open one before each snare
    ht, hdv, hpt = S.instrument("drums", "Hats", L)
    S.set(hdv, chat__decay=45, chat__tone=0.7, ohat__decay=160, ohat__tone=0.65, volume=-6)
    S.notes(hpt, [(CHAT, 8 * u + 0.5 * k, 0.2, 84 if k % 2 == 0 else 58) for u in units for k in range(16)] +
                 [(OHAT, 8 * u + t - 0.5, 0.3, 70) for u in units for t in SNARES])
    S.device(ht, "eq", lowcut__on="on", lowcut__freq=600)
    E.append({"name": "Hats", "strip": ht, "recipe": "closed hats in eighths, quiet, an open hat leading into each snare"})

    def inside(windows, t, d):
        """The part of (t, t+d) that lies in the first window containing t, or None."""
        for a, b in windows:
            if a - 1e-9 <= t < b - 1e-9:
                return min(d, b - t)
        return None

    # the saw: the loved supersaw, HELD and pumping
    bus = S.bus("Saw")
    for kind, part, shift, patch in design["saw"]:
        ch, dv, pt = S.instrument(kind, f"Saw {part}", L)
        S.set(dv, volume=-10, **dict(D.FLAT, amp__attack=4, amp__release=140, **patch))
        notes = []
        for u in units:
            hits = [(w[0], w[0] + w[1], i) for i, w in enumerate(CHORD_HITS)]
            for a, b in drop["chords"]:                              # a window starting between hits strikes there too
                if not any(abs(a - h[0]) < 1e-9 for h in hits):
                    hits.append((a, min([h[0] for h in hits if h[0] > a] + [8]), max(i for s, e, i in hits if s <= a)))
            for s, e, i in sorted(hits):
                d = inside(drop["chords"], s, e - s)
                if d:
                    notes += [(midi(n) + shift, 8 * u + s, max(0.1, d - 0.03), 110) for n in mood["units"][u % 4][i][0].split()]
        notes += [(midi(n) + shift, 64, 2.0, 110) for n in mood["units"][0][0][0].split()]
        S.notes(pt, notes)
        line(f"route {ch} --to {bus}")
    S.device(bus, "eq", lowcut__on="on", lowcut__freq=150, peak1__freq=350, peak1__gain=-2, peak1__q=1)
    S.device(bus, "imager", width=140, mono="on", frequency=200)
    S.device(bus, "reverb", decay=2200, size=120, mix=0.14, lowcut=400, highcut=10000, width=1)
    line(f"strip duck {bus} --under {kick} --amount 12")
    S.set(S.new('dv'), release=160)
    line(f"strip duck {bus} --under {sn} --amount 4")
    S.set(S.new('dv'), release=120)
    E.append({"name": "Saw", "strip": bus, "recipe": design["saw_note"] + f". HELD ({mood['chords']}), a 2.2 s room; it "
              "ducks 12 dB under every kick and swells back in 160 ms (the pump), 4 dB under the snare"})
    # the sub: the 808, the root under everything
    kind, shift, patch, fx = design["bass"]
    sub, bdv, bpt = S.instrument(kind, "Sub", L)
    S.set(bdv, **patch)
    S.notes(bpt, [(midi(mood["units"][u % 4][i][1]) + shift, 8 * u + s, d - 0.04, 115)
                  for u in units for i, (s, d) in enumerate(CHORD_HITS)] + [(midi(mood["units"][0][0][1]) + shift, 64, 2.0, 115)])
    S.device(sub, "imager", width=0, mono="on", frequency=200)
    line(f"strip duck {sub} --under {kick} --amount 14")
    S.set(S.new('dv'), release=70)
    E.append({"name": "Sub", "strip": sub, "recipe": design["bass_note"] + ". The root, held under chords and growl alike; "
              "mono; ducked 14 dB by the kick"})
    # the growl: the mid bass that talks; its lows cut so the sub stays clean
    g = GROWLS[drop["growl_kind"]]
    gr, gdv, gpt = S.instrument("analog", "Growl", L)
    S.set(gdv, **g["patch"])
    gn, steps = [], [0, 0, 12, 0, 7, 0, 12, 10]
    for u in units:
        k = 0
        for a, b in drop["growl"]:
            t = a
            while t < b - 1e-9:
                d = min(1.0 if (b - a) <= 4 else 2.0, b - t)
                hit = max(i for i, (s, _) in enumerate(CHORD_HITS) if s <= t + 1e-9)
                root = midi(mood["units"][u % 4][hit][1])
                root += 12 if root < midi("C2") else 0                # the growl an octave over the sub: C2 … B2
                gn.append((root + 12 + steps[k % len(steps)], 8 * u + t, d - 0.05, 118))
                t, k = t + d, k + 1
    S.notes(gpt, gn)
    for fkind, fparams in g["fx"]:
        fdv = S.device(gr, fkind, **fparams)
        if fkind == "vowel":                                         # every note starts on OH and moves to EE: "yoi"
            au = S.new('au')
            line(f'auto add --name "Growl · Vowel" --min 0 --max 4 --length {num(L)} --rest 3')
            for p, t, d, v in gn:
                line(f"auto point add {au} --at {num(t)} --value 3 --shape hold")
                line(f"auto point add {au} --at {num(t + 0.3)} --value 2 --shape hold")
            S.new('ac')
            line(f"clip add --auto {au} --at 0 --length {num(L)}")
            line(f'set {fdv}.vowel="={au}"')
    S.device(gr, "eq", lowcut__on="on", lowcut__freq=120, peak1__freq=2500, peak1__gain=2, peak1__q=0.8)
    S.device(gr, "imager", width=120, mono="on", frequency=250)
    S.device(gr, "reverb", decay=700, size=60, mix=0.07, lowcut=500, highcut=9000, width=0.9)
    line(f"strip duck {gr} --under {kick} --amount 8")
    S.set(S.new('dv'), release=70)
    wins = " and ".join(f"beats {num(a + 1)}–{num(b + 1)}" for a, b in drop["growl"])
    E.append({"name": "Growl", "strip": gr, "recipe": g["words"] + f". It plays {wins} of every two bars, on the chord's "
              "root with jumps to the octave and the fifth; low cut 120 Hz (the sub is the 808's); ducked 8 dB by the kick"})
    # the lead: the Glass stack
    lead, mel = D.LEADS[0], drop["lead"]
    lbus = S.bus("Lead")
    mn = D.melody_notes(mel)
    tied = [i + 1 < len(mn) and abs(mn[i + 1][0] - (t + d)) < 1e-6 for i, (t, d, p) in enumerate(mn)]
    for kind, part, shift, patch, how in lead["layers"]:
        ch, dv, pt = S.instrument(kind, f"Lead {part}", L)
        S.set(dv, **dict(dict(D.FLAT, volume=-10), **patch))
        S.notes(pt, [(p + shift, t, d + 0.06 if tie else max(0.1, d - 0.06), 112) for (t, d, p), tie in zip(mn, tied)])
        line(f"route {ch} --to {lbus}")
    S.device(lbus, "eq", lowcut__on="on", lowcut__freq=180, peak1__freq=2800, peak1__gain=2, peak1__q=0.8)
    S.device(lbus, "delay", time=num(0.75 * 60000 / BPM), feedback=0.34, tone=4000, mix=0.2)
    S.device(lbus, "reverb", decay=3000, size=130, mix=0.22, lowcut=400, highcut=10000, width=1)
    line(f"strip duck {lbus} --under {kick} --amount 5")
    S.set(S.new('dv'), release=110)
    E.append({"name": "Lead", "strip": lbus, "recipe": f"the lead you loved (the Glass stack: {lead['recipe']}); "
              f"the line — {mel['shape']}: {mel['how']}; a longer echo and a 3 s room"})
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
                "section": "drop", "style": "melodic-dubstep", "style_label": "Melodic dubstep drop", "bpm": BPM,
                "key": "E minor", "form_label": "sound — drop", "seconds": round((4 * BARS + 8) * 60 / BPM),
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
