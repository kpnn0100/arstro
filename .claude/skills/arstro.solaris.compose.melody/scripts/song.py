#!/usr/bin/env python3
"""song.py — a songbook song heard as a SONG through Solaris: its intro, verse, pre-chorus and chorus arranged the way
the style builds them, the melody sung (a voice through the vowel filter, each note its syllable's vowel) or played on
a piano. Rendered to MP3 for the Melody Lab.

    python3 song.py <dir with batch.json from songbook.py --out> [--cc <solaris-cc>] [--only id,id] [--measure]

The arrangement follows the sections: the verse thin (keys, a bass, a little rhythm) so the words are heard; the
pre-chorus building (a pad opening, the drums filling, a roll) and stopping for a breath; the chorus full (wide
chords pumping, the bass and drums driving); the last chord held. The lead is what is judged — `--measure` renders
the stems and prints each one's loudness, so "the melody leads" is measured, not assumed.

Writes <id>-sung.txt / <id>-piano.txt (re-runnable solaris-cc scripts) and their .mp3 beside batch.json. Runs
solaris-cc with scratch settings in that folder — never the user's own settings or recents.
"""
import argparse, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
DEFAULT_CC = os.path.join(REPO, "build", "apps", "solaris", "cli", "solaris-cc")
KICK, RIM, SNARE, CLAP, CHAT, OHAT, CRASH = 36, 37, 38, 39, 42, 46, 49


def num(x):
    return f"{x:.6g}"


class Script:
    """The lines, and the ids the service will give what they make (a fresh song: ch_1 is Main)."""
    def __init__(self):
        self.L, self.n = [], {'ch': 1, 'dv': 0, 'pt': 0, 'ac': 0, 'au': 0}

    def new(self, kind):
        self.n[kind] += 1
        return f"{kind}_{self.n[kind]}"

    def line(self, s=""):
        self.L.append(s)

    def notes(self, pt, notes):
        notes = sorted(notes, key=lambda n: (n[1], n[0]))
        for i in range(0, len(notes), 48):
            self.line(f'notes add {pt} "' + " ".join(f"{p}@{num(a)}:{num(l)}:{max(1, min(127, int(v)))}"
                                                     for p, a, l, v in notes[i:i + 48]) + '"')

    def instrument(self, kind, name, length):
        ch, dv, pt = self.new('ch'), self.new('dv'), self.new('pt')
        self.new('ac')
        self.line(f"clip add --instrument {kind} --at 0 --length {num(length)}")
        self.line(f'set {ch}.name="{name}" {pt}.name="{name}" {pt}.length={num(length)}')
        return ch, dv, pt

    def set(self, dv, **params):
        self.line("set " + " ".join(f"{dv}.{k.replace('__', '.')}={v}" for k, v in params.items()))

    def device(self, ch, kind, **params):
        dv = self.new('dv')
        self.line(f"device add {ch} --type {kind}")
        if params:
            self.set(dv, **params)
        return dv

    def automate(self, address, points):
        """(beat, value[, shape]) — a point's shape governs the segment after it."""
        au = self.new('au')
        self.new('ac')
        self.line(f"auto create {address}")
        for p in points:
            self.line(f"auto point add {au} --at {num(p[0])} --value {num(p[1])}" + (f" --shape {p[2]}" if len(p) > 2 else ""))
        return au

    def bus(self, name):
        ch = self.new('ch')
        self.line(f'strip add --kind bus --name {name}')
        return ch


def voicing(pcs, lo):
    """Every chord tone once, inside an octave from `lo` — successive chords stay close (voice leading for free)."""
    return sorted(lo + (pc - lo) % 12 for pc in pcs)


def bass_note(root):
    return 36 + (root - 36) % 12                                   # C2 … B2


def arrange(m, lead):
    S = Script()
    line = S.line
    bpm, nb = m["bpm"], len(m["chords"])
    total = 4 * (nb + 1)                                           # the chords, then the last one held a bar
    sec = {}
    for s in m["sections"]:
        for b in range(s["bar"], s["bar"] + s["bars"]):
            sec[b] = s["name"]
    sec[nb] = "end"
    first = lambda name: next(s["bar"] for s in m["sections"] if s["name"] == name)
    PRE, CHO = first("pre-chorus"), first("chorus")
    pcs = m["chord_pcs"] + [m["chord_pcs"][-1]]
    roots = m["chord_roots"] + [m["chord_roots"][-1]]
    style = m["style"]
    line(f"# {m['title']} — {m['style_label']}, {m['key']}, {bpm} bpm; the {lead} lead "
         f"(arstro.solaris.compose.melody song.py)")
    line(f'project new {m["id"]}-{lead}.slp --bpm {bpm} --name "{m["title"]}"')
    line()

    # ── the keys: the harmony from the first bar, the way the style comps ──
    keys, kdv, kpt = S.instrument("piano", "Keys", total)
    S.set(kdv, strings__brightness=0.8 if style == "lofi" else 0.9, hammer__hardness=0.8, body__mix=0.55,
          pedal__sustain="on" if style != "prog-house" else "off", volume=0 if style != "lofi" else 3)
    line(f"set {keys}.gain=4")                                     # measured: a struck chord reads ~15 LU under a held pad
    kn = []
    for b in range(nb + 1):
        v = voicing(pcs[b], 55)
        t, s = 4 * b, sec[b]
        if style == "lofi":                                         # lazy: on the one, then late on the and of 3
            kn += [(p, t, 1.5, 58 + 6 * (s == "chorus")) for p in v] + [(p, t + 2.5, 1.25, 50) for p in v[1:]]
        elif s in ("intro", "verse", "end"):
            kn += [(p, t, 4, 60) for p in v] if (s != "verse" or b < 5) else [(p, t + k, 2, 58) for k in (0, 2) for p in v]
        elif s == "pre-chorus":                                     # quarters, swelling
            kn += [(p, t + k, 1, 56 + 6 * (b - PRE) + 2 * k) for k in range(4) for p in v]
        elif style == "prog-house":                                 # house piano: the offbeats
            kn += [(p, t + k + 0.5, 0.45, 60) for k in range(4) for p in v]
        else:
            kn += [(p, t, 4, 66) for p in v]
    S.notes(kpt, kn)
    line()

    # ── the pad: from the pre-chorus, opening; in the chorus wide and pumping (lo-fi: a soft bed under the chorus) ──
    pad, pdv, ppt = S.instrument("synth", "Chords", total)
    S.set(pdv, osc1__wave="saw", osc1__voices=5, osc1__detune=22, osc1__width=36, osc1__level=0.6, osc2__wave="saw",
          osc2__octave=1, osc2__voices=3, osc2__detune=14, osc2__level=0.25, filter__cutoff=900, filter__res=0.1,
          filter__env=0.4, filter__keytrack=0.3, fenv__decay=400, amp__attack=40 if style != "lofi" else 400,
          amp__decay=500, amp__sustain=0.9, amp__release=600, velocity=0.6, volume=-21 if style != "lofi" else -30)
    pn = []
    for b in range(PRE if style != "lofi" else CHO, nb + 1):
        v = voicing(pcs[b], 60) + [bass_note(roots[b]) + 12]
        pn += [(p, 4 * b, 4, 70 if sec[b] == "pre-chorus" else 108) for p in v]
    S.notes(ppt, pn)
    if style != "lofi":
        S.automate(f"{pdv}.filter.cutoff", [(0, 500), (4 * PRE, 500, "smooth"), (4 * CHO - 0.5, 4200, "hold"),
                                             (4 * CHO, 5600, "hold")])
        sh = S.device(pad, "shaper", rate='"1/4"', shape='"pump"', depth=0, smooth=4)
        S.automate(f"{sh}.depth", [(0, 0, "hold"), (4 * CHO, 0.7, "hold")])
    else:
        S.set(pdv, filter__cutoff=1100)
    line()

    # ── the bass ──
    bass, bdv, bpt = S.instrument("synth", "Bass", total)
    S.set(bdv, osc1__wave="sine", osc1__level=0.9, osc2__wave="saw", osc2__octave=0, osc2__level=0.12, filter__cutoff=650,
          filter__res=0.05, filter__env=0.5, fenv__decay=180, amp__attack=4, amp__decay=300, amp__sustain=0.85,
          amp__release=120, voice__mode="mono", voice__glide=0, velocity=0.5, volume=-17)
    bn = []
    for b in range(1, nb + 1):
        r, t, s = bass_note(roots[b]), 4 * b, sec[b]
        if s == "end":
            bn += [(r, t, 4, 100)]
        elif style == "lofi":
            bn += [(r, t, 1.75, 92), (r, t + 2.5, 1.25, 80)]
        elif s == "verse":
            bn += [(r, t, 3.75, 84)]
        elif s == "pre-chorus":
            bn += [(r, t + k * 0.5, 0.45, 76 + 4 * k) for k in range(8)] if style == "prog-house" else \
                  [(r, t + k, 0.9, 86) for k in range(4)]
            if b == CHO - 1:
                bn = [n for n in bn if n[1] < t + 2]                 # the breath before the chorus
        elif style == "prog-house":                                 # the rolling offbeat bass
            bn += [(r, t + k + 0.5, 0.45, 110) for k in range(4)]
        else:                                                       # future bass: the half-time 808 rhythm
            bn += [(r, t, 2.5, 112), (r, t + 2.75, 1.0, 100)]
    S.notes(bpt, bn)
    line()

    # ── the drums ──
    drm, ddv, dpt = S.instrument("drums", "Drums", total)
    S.set(ddv, volume=-4, kick__decay=520 if style == "future-bass" else 380)
    d = []
    for b in range(1, nb + 1):
        t, s = 4 * b, sec[b]
        if s == "end":
            d += [(KICK, t, .25, 110), (CRASH, t, 1, 90)]
            continue
        if style == "lofi":
            swing = lambda k: 0.5 * k + (0.09 if k % 2 else 0)
            d += [(KICK, t, .25, 92), (KICK, t + 2.5, .25, 74), (SNARE, t + 1, .25, 70), (SNARE, t + 3, .25, 72)]
            d += [(CHAT, t + swing(k), .25, 54 if k % 2 else 64) for k in range(8)]
            if s == "chorus":
                d += [(OHAT, t + 3.5 + 0.09, .25, 48)]
            if b == CHO - 1:
                d = [x for x in d if x[1] < t + 2] + [(RIM, t + 3.5, .25, 70)]
            continue
        if s == "verse":
            if style == "prog-house":
                d += [(OHAT, t + k + 0.5, .25, 52) for k in range(4)]
                if b >= 5:
                    d += [(KICK, t + k, .25, 84) for k in range(4)]
            else:
                d += [(KICK, t, .25, 80), (CLAP, t + 2, .25, 66)]
                if b >= 5:
                    d += [(CHAT, t + 0.5 * k, .25, 52 if k % 2 else 42) for k in range(8)]
        elif s == "pre-chorus":
            if b < CHO - 1:
                d += [(KICK, t + k, .25, 92) for k in range(4)] + [(CHAT, t + 0.5 * k, .25, 56) for k in range(8)]
                d += [(CLAP, t + k, .25, 84) for k in ((1, 3) if style == "prog-house" else (2,))]
            else:                                                   # the roll, rising — then nothing: the breath
                end = 3 if style == "prog-house" else 2
                n = int(end / 0.25)
                d += [(SNARE, t + 0.25 * k, .2, 60 + int(55 * k / n)) for k in range(n)]
        else:                                                       # the chorus
            if b in (CHO, CHO + 4):
                d += [(CRASH, t, 1, 96)]
            if style == "prog-house":
                d += [(KICK, t + k, .25, 120) for k in range(4)] + [(CLAP, t + k, .25, 100) for k in (1, 3)]
                d += [(OHAT, t + k + 0.5, .25, 74) for k in range(4)] + [(CHAT, t + 0.25 * k, .2, 40) for k in range(16) if k % 2]
            else:
                d += [(KICK, t, .25, 118), (KICK, t + 2.75, .25, 100), (SNARE, t + 2, .25, 110), (CLAP, t + 2, .25, 96)]
                d += [(CHAT, t + 0.5 * k, .25, 72 if k % 2 == 0 else 54) for k in range(8)] + [(OHAT, t + 3.5, .25, 62)]
    S.notes(dpt, d)
    line()

    # ── the lead: what is judged ──
    notes = m["notes"]
    if lead == "sung":
        line("# the voice: one oscillator pair, mono (each syllable sounds again), a singer's slide between notes and a")
        line("# late vibrato's depth, through the vowel filter — every note its syllable's vowel (AH EH EE OH OO)")
        voc, vdv, vpt = S.instrument("synth", "Voice", total)
        S.set(vdv, osc1__wave="saw", osc1__voices=1, osc1__level=0.8, osc2__wave="square", osc2__octave=0,
              osc2__voices=1, osc2__level=0.22, osc2__fine=0, noise=0.025, filter__cutoff=4200, filter__res=0.05,
              filter__env=0.3, filter__keytrack=0.5, fenv__decay=260, fenv__sustain=0.6, amp__attack=24,
              amp__decay=300, amp__sustain=0.9, amp__release=150, voice__mode="mono", voice__glide=45,
              vibrato__depth=0.14, vibrato__rate=5.2, velocity=0.3, volume=-6)
        vw = S.device(voc, "vowel", resonance=1.1, glide=35, mix=1, output=8)
        au = S.new('au')
        line(f'auto add --name "Voice · Vowel" --min 0 --max 4 --length {num(total)} --rest {notes[0]["vowel"]}')
        for n in notes:
            line(f"auto point add {au} --at {num(n['t'])} --value {n['vowel']} --shape hold")
        line(f'set {vw}.vowel="={au}"')
        S.new('ac')
        line(f"clip add --auto {au} --at 0 --length {num(total)}")
        gain = 6 if style == "prog-house" else 4          # measured per section: ~5 LU over the loudest other part
    else:
        voc, vdv, vpt = S.instrument("piano", "Lead piano", total)
        S.set(vdv, strings__brightness=1.1, hammer__hardness=1.0, volume=6)
        gain = 3 if style != "lofi" else 2
    S.notes(vpt, [(n["p"], n["t"], max(0.1, n["d"] - 0.03), 104 if n.get("stress") else 90) for n in notes])
    line(f"set {voc}.gain={gain}")
    line()

    # ── space: a room for everything, an echo for the lead; a limiter on the master ──
    room = S.bus("Room")
    rv = S.device(room, "reverb", decay=2400 if style != "lofi" else 1600, size=120, mix=1, lowcut=300)
    echo = S.bus("Echo")
    S.device(echo, "delay", time=num(0.75 * 60000 / bpm), feedback=0.3, tone=3200, mix=1)
    line(f"send add {voc} --to {room} --gain -10")
    line(f"send add {voc} --to {echo} --gain {-18 if style != 'lofi' else -24}")
    line(f"send add {keys} --to {room} --gain -12")
    line(f"send add {pad} --to {room} --gain -14")
    line("device add master --type limiter")
    lim = S.new('dv')
    S.set(lim, gain=3, ceiling=-1.2, truePeak="on")
    line("project save")
    out = f"{m['id']}-{lead}.mp3"
    line(f"render --out {out} --format mp3 --kbps 192")
    return "\n".join(S.L) + "\n", {"lead": voc, "keys": keys, "chords": pad, "bass": bass, "drums": drm}


def loudness(path, start=None, length=None):
    """Integrated loudness (LUFS) of a file, or of a window of it."""
    win = ["-ss", num(start), "-t", num(length)] if start is not None else []
    r = subprocess.run(["ffmpeg", "-hide_banner", "-nostats"] + win + ["-i", path, "-filter_complex", "ebur128", "-f",
                        "null", "-"], capture_output=True, text=True)
    m = re.findall(r"I:\s+(-?[\d.]+) LUFS", r.stderr)
    return float(m[-1]) if m else None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir")
    ap.add_argument("--cc", default=DEFAULT_CC)
    ap.add_argument("--only", default="")
    ap.add_argument("--leads", default="sung,piano")
    ap.add_argument("--measure", action="store_true", help="render the stems too and print their loudness")
    a = ap.parse_args()
    bdir = os.path.abspath(a.dir)
    with open(os.path.join(bdir, "batch.json")) as f:
        batch = json.load(f)
    only = set(x for x in a.only.split(",") if x)
    env = dict(os.environ, SOLARIS_SETTINGS=os.path.join(bdir, ".settings.txt"), SOLARIS_RECENTS=os.path.join(bdir, ".recents"))
    env.pop("DISPLAY", None)
    failed = 0
    for m in batch["melodies"]:
        if only and m["id"] not in only:
            continue
        for lead in a.leads.split(","):
            stem = f"{m['id']}-{lead}"
            for stale in (stem + ".mp3", stem + ".slp"):
                if os.path.exists(os.path.join(bdir, stale)):
                    os.remove(os.path.join(bdir, stale))
            text, strips = arrange(m, lead)
            if a.measure:
                text = text.replace(f"render --out {stem}.mp3", f"render --out {stem}.wav --stems "
                                    + ",".join(strips.values()) + f"\nrender --out {stem}.mp3")
            with open(os.path.join(bdir, stem + ".txt"), "w") as f:
                f.write(text)
            r = subprocess.run([a.cc, "--script", stem + ".txt"], cwd=bdir, env=env, capture_output=True, text=True)
            ok = r.returncode == 0 and os.path.exists(os.path.join(bdir, stem + ".mp3"))
            failed += not ok
            print(f"{stem}: {'ok' if ok else 'FAILED'}" + ("" if ok else f"\n{r.stdout[-900:]}{r.stderr[-600:]}"))
            if ok and a.measure:
                spb = 60 / m["bpm"] * 4                               # seconds a bar
                for sc in m["sections"]:
                    if sc["name"] == "intro":
                        continue
                    w = (sc["bar"] * spb, sc["bars"] * spb)
                    parts = [f"mix {loudness(os.path.join(bdir, stem + '.wav'), *w)}"]
                    parts += [f"{name} {loudness(os.path.join(bdir, f'{stem}.{ch}.wav'), *w)}" for name, ch in strips.items()]
                    print(f"   {sc['name']:10} LUFS: " + " · ".join(parts))
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
