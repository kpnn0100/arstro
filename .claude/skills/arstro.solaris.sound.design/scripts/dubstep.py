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
MELS = {m["shape"]: m for m in D.MELODIES}
DROPS = [
    dict(name="The cry", mood="Longing", lead=MELS["the cry"], arp="up", grid="half",
         about="a low start, a climb, a leap to a note held alone; the arp climbs the chord in sixteenths"),
    dict(name="Sighs", mood="Longing", lead=MELS["sighs"], arp="updown", grid="half",
         about="falling pairs that lean and resolve; the arp runs up and back down"),
    dict(name="Tears", mood="Falling", lead=MELS["tears"], arp="jump", grid="half",
         about="a held note, then a line falling with the bass as it steps down — the darkest; the arp leaps about"),
    dict(name="The ache", mood="Dreamy", lead=MELS["the ache"], arp="gallop", grid="half",
         about="a gentle line that reaches E♭ over the borrowed chord; the arp gallops (an eighth and two sixteenths)"),
    dict(name="The cry, counted fast", mood="Longing", lead=MELS["the cry"], arp="up", grid="fast",
         about="the SAME music as 1 with 'kick 1 3, snare 2 4' counted at 150 instead of 75 — twice as many drums; "
               "tell me which count you meant"),
]


def arrange(drop, kick_file, trim=None):
    S, E, L = Script(), [], 4 * BARS + 8
    line = S.line
    mood = next(m for m in D.MOODS if m["name"] == drop["mood"])
    design = next(o for o in D.OPTIONS if o["name"] == "808")
    kicks, snares, voic = GRIDS[drop["grid"]]
    units = range(BARS // 2)
    hits = []                                                        # (beat, length to the snare, voicing, bass note)
    for u in units:
        for k, v in zip(kicks, voic):
            notes, root = mood["units"][u % 4][v]
            hits.append((8 * u + k, min(x for x in snares if x > k) - k, notes, root))
    hits.append((64, 2.0) + tuple(mood["units"][0][0]))
    count = "1 and 3 … 2 and 4, counted slow" if drop["grid"] == "half" else "1 and 3 … 2 and 4 of every bar at 150"
    line(f"# melodic dubstep drop — {drop['name']} (arstro.solaris.sound.design dubstep.py) — {BPM} bpm")
    line(f'project new drop.slp --bpm {BPM} --name "Dubstep — {drop["name"]}"')
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
    S.notes(spt, [(p, 8 * u + t, 0.4, v) for u in units for t in snares for p, v in ((D.SNARE, 127), (D.CLAP, 112), (RIM, 120))])
    S.device(sn, "transient", attack=70, sustain=-20, output=0)
    S.device(sn, "saturator", drive=9, curve="hard", mix=1, output=-5)
    S.device(sn, "eq", lowcut__on="on", lowcut__freq=120, peak1__freq=200, peak1__gain=3, peak1__q=1.2, highshelf__freq=6000,
             highshelf__gain=3)
    S.device(sn, "reverb", decay=1100, size=90, mix=0.13, lowcut=300, highcut=9000, width=1)
    E.append({"name": "Snare", "strip": sn, "recipe": "HARD: the snare tuned up 2 st, 210 ms, bright, a rim shot and a clap "
              "inside it; the attack pushed (+70), the tail pulled back, clipped 9 dB, +3 dB of body at 200 Hz and of "
              f"crack over 6 kHz, a short room — on {count.split(' … ')[1]}; 1.5 dB louder than before"})
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
              "kick, holds flat and STOPS at the snare; nearly dry; ducked 12 dB by the kick"})
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
    S.device(ar, "delay", time=num(0.75 * 60000 / BPM), feedback=0.25, tone=5000, mix=0.12)
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
    S.device(lbus, "delay", time=num(0.75 * 60000 / BPM), feedback=0.34, tone=4000, mix=0.18)
    S.device(lbus, "reverb", decay=2800, size=130, mix=0.2, lowcut=400, highcut=10000, width=1)
    line(f"strip duck {lbus} --under {kick} --amount 4")
    S.set(S.new('dv'), release=100)
    E.append({"name": "Lead", "strip": lbus, "recipe": "HEAVY: the lead you loved (the Glass stack — saws, pulses, a filter "
              "that opens at each note, a 140 ms slide) with three saws and a pulse an OCTAVE BELOW through a driven "
              f"ladder, the whole lead through a tube; 1 dB louder than the chords. The melody — {mel['shape']}: "
              f"{mel['how']}; {int(100 * D.melody_fit(mel))} % of its strong notes are in the chord"})
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
