#!/usr/bin/env python3
"""song.py — a songbook song heard as a SONG through Solaris: every section arranged the way the style builds it, the
melody sung (a voice through the vowel filter) or played on a piano. Rendered to MP3 for the Melody Lab.

    python3 song.py <dir with batch.json from songbook.py --out> [--cc <solaris-cc>] [--only id,id] [--leads sung,piano]
                    [--measure]

The arrangement follows each section's KIND:
- intro: the hook teased (chops through a filter opening; on lo-fi, the keys);
- verse: thin — keys and a bass, a little rhythm — so the words are heard; verse 2 grooves from its first bar;
- pre: building — the pad opening, the drums to quarters, a roll — then a breath;
- chorus: full — wide chords pumping, the style's bass and drums, the voice doubled, a harmony from chorus 2 on;
- drop: the hook as vocal chops over the full band (the EDM styles);
- bridge: bare, then building to the last chorus;
- tag: quiet, the last line sung again, home.

The voice is not a sample of a singer; it is made to SING:
- a mono synth (each syllable strikes again) through the vowel filter, its vowel automated per syllable;
- a vibrato that arrives late on held notes — the mod wheel drawn per note (the synth's vibrato sounds only with the
  wheel up, R-MIDI-3);
- a scoop up into each phrase — the pitch wheel drawn per note;
- soft consonants: a noise burst, high-passed by key, at every s / sh / f / t / k / p … onset.

The lead is what is judged — `--measure` renders the stems and prints every part's loudness per section.

Writes <id>-sung.txt / <id>-piano.txt (re-runnable solaris-cc scripts) and their .mp3 beside batch.json. Runs solaris-cc
with scratch settings in that folder — never the user's own settings or recents.
"""
import argparse, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from melody import MODES  # noqa: E402

REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", ".."))
DEFAULT_CC = os.path.join(REPO, "build", "apps", "solaris", "cli", "solaris-cc")
KICK, RIM, SNARE, CLAP, CHAT, OHAT, CRASH = 36, 37, 38, 39, 42, 46, 49
VOICE = dict(osc1__wave="saw", osc1__voices=1, osc1__level=0.8, osc2__wave="square", osc2__octave=0, osc2__voices=1,
             osc2__level=0.22, osc2__fine=0, noise=0.025, filter__cutoff=4200, filter__res=0.05, filter__env=0.3,
             filter__keytrack=0.5, fenv__decay=260, fenv__sustain=0.6, amp__attack=24, amp__decay=300, amp__sustain=0.9,
             amp__release=150, voice__mode="mono", voice__glide=45, voice__bend=2, vibrato__depth=0.6,
             vibrato__rate=5.3, velocity=0.3)   # osc2 in tune with osc1: detuned, the vowel filter rang 5.5 LU louder (measured)


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

    def expr(self, pt, lane, points):
        pts = sorted({round(t, 4): v for t, v in points}.items())
        for i in range(0, len(pts), 48):
            self.line(f'expr set {pt} "' + " ".join(f"{num(t)}:{num(v)}" for t, v in pts[i:i + 48]) + f'" --lane {lane}')

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

    def held(self, name, notes, total):
        """An automation of held vowels (AH EH EE OH OO) under `notes`, placed as one clip; returns its id."""
        au = self.new('au')
        self.line(f'auto add --name "{name}" --min 0 --max 4 --length {num(total)} --rest {notes[0]["vowel"] if notes else 0}')
        for n in notes:
            self.line(f"auto point add {au} --at {num(n['t'])} --value {n['vowel']} --shape hold")
        self.new('ac')
        self.line(f"clip add --auto {au} --at 0 --length {num(total)}")
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


def where(m):
    """For every bar: (kind, its position in the section, the section's length, which time this kind comes)."""
    out, seen = [], {}
    for sc in m["sections"]:
        seen[sc["kind"]] = seen.get(sc["kind"], 0) + 1
        out += [(sc["kind"], k, sc["bars"], seen[sc["kind"]], sc) for k in range(sc["bars"])]
    return out + [("end", 0, 1, 1, None)]


def consonant(syl):
    """The noise a syllable starts with: (pitch for the key-tracked high-pass, length s, velocity, lead s) or None."""
    s = syl.lower().lstrip("'")
    if not s or s == "~":
        return None
    if s.startswith(("sh", "ch")):
        return 86, 0.08, 64, 0.05
    if s[0] in "sz" or s.startswith(("th", "ce", "ci")) or s[0] in "fx":
        return 96, 0.07, 60 if s[0] != "f" else 48, 0.05
    if s[0] in "tkpcq":
        return 88, 0.03, 72, 0.015
    if s[0] in "dbg":
        return 76, 0.02, 60, 0.01
    if s[0] == "h" or s.startswith("wh"):
        return 70, 0.06, 40, 0.03
    return None


def harmony_note(n, pcs, scale):
    """The harmony under the line: on a strong or held note the nearest chord tone a third to a sixth below, else the
    diatonic third below — close harmony, as a backing singer sings it."""
    p = n["p"]
    if n["t"] % 4 in (0, 2) or n["d"] >= 1:
        cands = [q for q in range(p - 9, p - 2) if q % 12 in pcs]
        if cands:
            return max(cands)
    for q in (p - 3, p - 4):
        if q % 12 in scale:
            return q
    return p - 3


def arrange(m, lead):
    S = Script()
    line = S.line
    bpm, nb, style = m["bpm"], len(m["chords"]), m["style"]
    edm = style != "lofi"
    total = 4 * (nb + 1)                                           # the song, and a bar for the last chord to ring
    beats = lambda sec: sec * bpm / 60                             # seconds → beats
    W = where(m)
    pcs = m["chord_pcs"] + [m["chord_pcs"][-1]]
    roots = m["chord_roots"] + [m["chord_roots"][-1]]
    secs = m["sections"]
    sec_of = lambda t: W[min(int(t // 4), nb)]
    scale = [(m["tonic"] + iv) % 12 for iv in MODES[m["mode"]]]
    line(f"# {m['title']} — {m['style_label']}, {m['key']}, {bpm} bpm; the {lead} lead "
         f"(arstro.solaris.compose.melody song.py)")
    line(f"# {m['metrics']['form']}")
    line(f'project new {m["id"]}-{lead}.slp --bpm {bpm} --name "{m["title"]}"')
    line()

    # ── the keys ──
    keys, kdv, kpt = S.instrument("piano", "Keys", total)
    S.set(kdv, strings__brightness=0.8 if not edm else 0.9, hammer__hardness=0.8, body__mix=0.55,
          pedal__sustain="on" if style != "prog-house" else "off", volume=3 if not edm else 0)
    line(f"set {keys}.gain=4")                                     # measured: a struck chord reads ~15 LU under a held pad
    kn = []
    for b, (kind, pos, nbars, nth, _) in enumerate(W):
        v, t = voicing(pcs[b], 55), 4 * b
        whole = lambda vel: [(p, t, 4, vel) for p in v]
        if kind in ("tag", "end"):
            kn += whole(56)
        elif kind == "bridge" and pos < 4:
            kn += whole(50)
        elif not edm:                                               # lazy: on the one, then late on the and of 3
            kn += [(p, t, 1.5, 58 + 6 * (kind == "chorus")) for p in v] + [(p, t + 2.5, 1.25, 50) for p in v[1:]]
        elif kind == "intro" or (kind == "verse" and nth == 1 and pos < 4):
            kn += whole(56)
        elif kind == "verse":
            kn += [(p, t + k, 2, 58) for k in (0, 2) for p in v]
        elif kind in ("pre", "bridge"):                              # quarters, swelling
            kn += [(p, t + k, 1, 52 + 18 * (pos + k / 4) / nbars) for k in range(4) for p in v]
        elif style == "prog-house":                                 # house piano: the offbeats
            kn += [(p, t + k + 0.5, 0.45, 60) for k in range(4) for p in v]
        else:
            kn += whole(62)
    S.notes(kpt, kn)
    line()

    # ── the chords: wide saws, from the pre-chorus up; pumping in the chorus and the drop ──
    pad, pdv, ppt = S.instrument("synth", "Chords", total)
    S.set(pdv, osc1__wave="saw", osc1__voices=5, osc1__detune=22, osc1__width=36, osc1__level=0.6, osc2__wave="saw",
          osc2__octave=1, osc2__voices=3, osc2__detune=14, osc2__level=0.25, filter__cutoff=900 if edm else 1100,
          filter__res=0.1, filter__env=0.4, filter__keytrack=0.3, fenv__decay=400, amp__attack=40 if edm else 400,
          amp__decay=500, amp__sustain=0.9, amp__release=600, velocity=0.6, volume=-21 if edm else -30)
    vel = {"pre": 70, "chorus": 100, "drop": 112, "tag": 64, "end": 60} if edm else {"chorus": 90, "tag": 60, "end": 56}
    pn = []
    for b, (kind, pos, nbars, nth, _) in enumerate(W):
        vv = vel.get(kind) or (72 if kind == "bridge" and pos >= 4 and edm else 0) or (60 if kind == "intro" and pos >= 2
                                                                                       and edm else 0)
        if vv:
            pn += [(p, 4 * b, 4, vv) for p in voicing(pcs[b], 60) + [bass_note(roots[b]) + 12]]
    S.notes(ppt, pn)
    if edm:
        cut = [(0, 400, "hold")]
        for sc in secs:
            t0, t1 = 4 * sc["bar"], 4 * (sc["bar"] + sc["bars"])
            cut += {"intro": [(t0, 400, "smooth"), (t1 - 0.5, 1200, "hold")], "verse": [(t0, 500, "hold")],
                    "pre": [(t0, 500, "smooth"), (t1 - 0.5, 4200, "hold")], "chorus": [(t0, 5200, "hold")],
                    "drop": [(t0, 7000, "hold")], "tag": [(t0, 1800, "hold")],
                    "bridge": [(t0, 600, "hold"), (t0 + 16, 600, "smooth"), (t1 - 0.5, 4200, "hold")]}[sc["kind"]]
        S.automate(f"{pdv}.filter.cutoff", cut)
        sh = S.device(pad, "shaper", rate='"1/4"', shape='"pump"', depth=0, smooth=4)
        S.automate(f"{sh}.depth", [(0, 0, "hold")] + [(4 * sc["bar"], {"chorus": 0.55, "drop": 0.75}.get(sc["kind"], 0),
                                                         "hold") for sc in secs])
    line()

    # ── the bass ──
    bass, bdv, bpt = S.instrument("synth", "Bass", total)
    S.set(bdv, osc1__wave="sine", osc1__level=0.9, osc2__wave="saw", osc2__octave=0, osc2__level=0.12, filter__cutoff=650,
          filter__res=0.05, filter__env=0.5, fenv__decay=180, amp__attack=4, amp__decay=300, amp__sustain=0.85,
          amp__release=120, voice__mode="mono", voice__glide=0, velocity=0.5, volume=-17)
    bn = []
    for b, (kind, pos, nbars, nth, _) in enumerate(W):
        r, t, last = bass_note(roots[b]), 4 * b, pos == nbars - 1
        lazy = [(r, t, 1.75, 92), (r, t + 2.5, 1.25, 80)]
        if kind == "end":
            continue
        if kind == "tag":
            bn += [(r, t, 3.75, 84)] if pos < nbars - 2 else ([(r, t, 7.5, 90)] if pos == nbars - 2 else [])
        elif kind == "intro":
            bn += lazy if not edm and pos >= 2 else []
        elif kind == "bridge" and pos < 4:
            bn += [(r, t, 3.75, 70)] if not edm else []
        elif not edm:
            bn += lazy
        elif kind == "verse":
            bn += [(r, t, 3.75, 84)] if nth == 1 else ([(r, t, 2.5, 92), (r, t + 2.75, 1, 82)] if style == "future-bass"
                                                       else [(r, t + k + 0.5, 0.45, 88) for k in range(4)])
        elif kind in ("pre", "bridge"):
            bn += [(r, t + k * 0.5, 0.45, 76 + 4 * k) for k in range(8)] if style == "prog-house" else \
                  [(r, t + k, 0.9, 86) for k in range(4)]
        elif style == "prog-house":                                 # the rolling offbeat bass
            bn += [(r, t + k + 0.5, 0.45, 110) for k in range(4)]
        else:                                                       # future bass: the half-time 808 rhythm
            bn += [(r, t, 2.5, 112), (r, t + 2.75, 1.0, 100)]
        if kind in ("pre", "bridge") and last:
            bn = [n for n in bn if n[1] < t + 2]                    # the breath before the chorus
    S.notes(bpt, bn)
    line()

    # ── the drums ──
    drm, ddv, dpt = S.instrument("drums", "Drums", total)
    S.set(ddv, volume=-4, kick__decay=520 if style == "future-bass" else 380)
    d = []
    for b, (kind, pos, nbars, nth, _) in enumerate(W):
        t, last = 4 * b, pos == nbars - 1
        if kind == "end":
            d += [(CRASH, t, 1, 70)] if edm else []
            continue
        if kind == "tag":
            d += [(CRASH, t, 1, 72)] if pos == 0 and edm else []
            continue
        if not edm:
            if (kind == "intro" and pos < 2) or (kind == "bridge" and pos < 4):
                d += [(RIM, t + 1, .25, 46), (RIM, t + 3, .25, 46)] if kind == "bridge" else []
                continue
            swing = lambda k: 0.5 * k + (0.09 if k % 2 else 0)
            d += [(KICK, t, .25, 92), (KICK, t + 2.5, .25, 74), (SNARE, t + 1, .25, 70), (SNARE, t + 3, .25, 72)]
            d += [(CHAT, t + swing(k), .25, 54 if k % 2 else 64) for k in range(8)]
            if kind == "chorus":
                d += [(OHAT, t + 3.59, .25, 48)]
            if kind in ("pre", "bridge") and last:
                d = [x for x in d if x[1] < t + 2] + [(RIM, t + 3.5, .25, 70)]
            continue
        if kind == "intro":
            d += [(OHAT, t + k + 0.5, .25, 48) for k in range(4)] if style == "prog-house" and pos >= 2 else []
        elif kind == "verse":
            full = nth > 1 or pos >= 4
            if style == "prog-house":
                d += [(OHAT, t + k + 0.5, .25, 52) for k in range(4)] + ([(KICK, t + k, .25, 84) for k in range(4)] if full else [])
            else:
                d += [(KICK, t, .25, 80), (CLAP, t + 2, .25, 66)] + ([(CHAT, t + 0.5 * k, .25, 52 if k % 2 else 42)
                                                                      for k in range(8)] if full else [])
        elif kind in ("pre", "bridge"):
            if kind == "bridge" and pos < 4:
                continue
            if not last:
                d += [(KICK, t + k, .25, 80 + 12 * pos / nbars) for k in range(4)] + [(CHAT, t + 0.5 * k, .25, 56) for k in range(8)]
                d += [(CLAP, t + k, .25, 84) for k in ((1, 3) if style == "prog-house" else (2,))]
            else:                                                   # the roll, rising — then nothing: the breath
                end = 3 if style == "prog-house" else 2
                n = int(end / 0.25)
                d += [(SNARE, t + 0.25 * k, .2, 60 + int(55 * k / n)) for k in range(n)]
        else:                                                       # the chorus and the drop
            if pos in (0, 4):
                d += [(CRASH, t, 1, 96)]
            big = kind == "drop"
            if style == "prog-house":
                d += [(KICK, t + k, .25, 120) for k in range(4)] + [(CLAP, t + k, .25, 100) for k in (1, 3)]
                d += [(OHAT, t + k + 0.5, .25, 74) for k in range(4)]
                d += [(CHAT, t + 0.25 * k, .2, 46 if big else 38) for k in range(16) if k % 2]
            else:
                d += [(KICK, t, .25, 118), (KICK, t + 2.75, .25, 100), (SNARE, t + 2, .25, 110), (CLAP, t + 2, .25, 96)]
                d += ([(CHAT, t + 0.25 * k, .2, 64 if k % 2 == 0 else 44) for k in range(16)] if big else
                      [(CHAT, t + 0.5 * k, .25, 72 if k % 2 == 0 else 54) for k in range(8)]) + [(OHAT, t + 3.5, .25, 62)]
    S.notes(dpt, d)
    line()

    # ── the hook: chops in the intro and the drops (lo-fi: the keys play it in the intro) ──
    hook = m.get("hook", [])
    chop = None
    if hook:
        hvel = lambda n: (78 if sec_of(n["t"])[0] == "intro" else 108) + 8 * n.get("stress", False)
        if edm:
            line("# the chops: a bright legato synth through the vowel filter — the chorus melody, each note its vowel")
            chop, cdv, cpt = S.instrument("synth", "Chops", total)
            S.set(cdv, osc1__wave="saw", osc1__voices=1, osc1__level=0.8, osc2__wave="square", osc2__octave=0,
                  osc2__level=0.3, osc2__fine=0, noise=0.03, filter__cutoff=8000, filter__res=0.05, filter__env=0.3,
                  filter__keytrack=0, fenv__decay=200, amp__attack=3, amp__decay=160, amp__sustain=0.85,
                  amp__release=80, voice__mode="legato", voice__glide=35, velocity=0.3, volume=-3)  # measured: the drop's lead
            S.notes(cpt, [(n["p"], n["t"], max(0.1, n["d"] - 0.05), hvel(n)) for n in hook])
            cvw = S.device(chop, "vowel", resonance=0.6, glide=8, mix=1, output=9)
            line(f'set {cvw}.vowel="={S.held("Chops · Vowel", hook, total)}"')
            intro = next(sc for sc in secs if sc["kind"] == "intro")
            S.automate(f"{cdv}.filter.cutoff", [(0, 500, "smooth"), (4 * intro["bars"] - 0.5, 7000, "hold")])
        else:
            chop, cdv, cpt = S.instrument("piano", "Hook keys", total)
            S.set(cdv, strings__brightness=1.0, hammer__hardness=0.9, volume=3)
            S.notes(cpt, [(n["p"], n["t"], max(0.1, n["d"] - 0.05), hvel(n) - 10) for n in hook])
        line()

    # ── the voice ──
    notes = [dict(n) for n in m["notes"]]
    for a, b in zip(notes, notes[1:]):                             # one voice: a note ends before the next begins
        a["d"] = min(a["d"], b["t"] - a["t"])
    vel = lambda n: 104 if n.get("stress") else 90
    voices = []
    if lead == "sung":
        line("# the voice: a mono synth through the vowel filter, every note its syllable's vowel; a late vibrato on held")
        line("# notes (the mod wheel), a scoop up into each phrase (the pitch wheel), its consonants on their own strip")
        voc, vdv, vpt = S.instrument("synth", "Voice", total)
        S.set(vdv, volume=-6, **VOICE)
        S.notes(vpt, [(n["p"], n["t"], max(0.1, n["d"] - 0.03), vel(n)) for n in notes])
        au = S.held("Voice · Vowel", notes, total)
        mod, bend = [(0, 0)], [(0, 0)]
        prev_end = -10
        for i, n in enumerate(notes):
            t, dsec = n["t"], n["d"] * 60 / bpm
            if dsec >= 0.55:                                        # the vibrato arrives once the note is held
                onset = t + beats(min(0.32, 0.4 * dsec))
                mod += [(t, 0), (onset, 0), (t + n["d"] - beats(0.06), 0.8 if dsec > 1.2 else 0.55), (t + n["d"], 0)]
            leap_up = i > 0 and n["p"] - notes[i - 1]["p"] >= 4
            if t - prev_end >= 0.5 or (leap_up and n.get("stress")):  # a phrase's first note, a reach: scooped
                bend += [(t - 0.01, 0), (t, -0.22 if t - prev_end >= 0.5 else -0.15), (t + beats(0.09), 0)]
            prev_end = t + n["d"]
        S.expr(vpt, "mod", mod)
        S.expr(vpt, "bend", bend)
        S.device(voc, "vowel", resonance=1.1, glide=35, mix=1, output=8)
        line(f'set dv_{S.n["dv"]}.vowel="={au}"')
        line(f"set {voc}.gain={ {'prog-house': 6, 'lofi': 5.5}.get(style, 4)}")   # measured per section: 3–7 LU over the rest
        voices.append(voc)
        # the doubles (left and right, a few cents apart) in every chorus and the tag; the harmony from chorus 2 on
        sung_in = lambda n, want: want(sec_of(n["t"]))
        dbl = [n for n in notes if sung_in(n, lambda w: w[0] in ("chorus", "tag"))]
        har = [n for n in notes if sung_in(n, lambda w: w[4] is not None and w[4].get("harmony"))]
        for name, fine, pan, part, vol in (("Double L", 7, -0.55, dbl, -15), ("Double R", -7, 0.55, dbl, -15),
                                           ("Harmony", 0, 0.3, har, -12)):
            if not part:
                continue
            ch, dv, pt = S.instrument("synth", name, total)
            S.set(dv, volume=vol, osc1__fine=fine, osc2__fine=fine, **{k: v for k, v in VOICE.items()
                                                                      if k not in ("osc1__fine", "osc2__fine")})
            hp = (lambda n: harmony_note(n, pcs[int(n["t"] // 4)], scale)) if name == "Harmony" else (lambda n: n["p"])
            S.notes(pt, [(hp(n), n["t"], max(0.1, n["d"] - 0.03), vel(n) - 8) for n in part])
            S.expr(pt, "mod", mod)
            S.device(ch, "vowel", resonance=1.1, glide=35, mix=1, output=8)
            line(f'set dv_{S.n["dv"]}.vowel="={au}" {ch}.pan={pan}')
            voices.append(ch)
        # the consonants: noise through a high-pass that follows the key, a burst at each onset, just before the vowel
        cons, cdv2, cnpt = S.instrument("synth", "Consonants", total)
        S.set(cdv2, osc1__level=0, osc2__level=0, noise=1, filter__mode="highpass", filter__cutoff=1500,
              filter__res=0.1, filter__env=0, filter__keytrack=1, amp__attack=2, amp__decay=40, amp__sustain=0.4,
              amp__release=25, velocity=0.6, volume=-22)
        cn = []
        for n in notes:
            c = consonant(n["syl"])
            if c:
                pitch, length, v, ahead = c
                cn.append((pitch, max(0, n["t"] - beats(ahead)), beats(length), v))
        S.notes(cnpt, cn)
        voices.append(cons)
    else:
        voc, vdv, vpt = S.instrument("piano", "Lead piano", total)
        S.set(vdv, strings__brightness=1.1, hammer__hardness=1.0, volume=6)
        S.notes(vpt, [(n["p"], n["t"], max(0.1, n["d"] - 0.03), vel(n)) for n in notes])
        line(f"set {voc}.gain={3 if edm else 2}")
        voices.append(voc)
    line()

    # ── space: a room for everything, an echo for the voices; a limiter on the master ──
    room = S.bus("Room")
    S.device(room, "reverb", decay=2400 if edm else 1600, size=120, mix=1, lowcut=300)
    echo = S.bus("Echo")
    S.device(echo, "delay", time=num(0.75 * 60000 / bpm), feedback=0.3, tone=3200, mix=1)
    for ch in voices:
        line(f"send add {ch} --to {room} --gain -10")
    line(f"send add {voc} --to {echo} --gain {-18 if edm else -24}")
    for ch, g in ((keys, -12), (pad, -14), (chop, -12)):
        if ch:
            line(f"send add {ch} --to {room} --gain {g}")
    line("device add master --type limiter")
    S.set(S.new('dv'), gain=3, ceiling=-1.2, truePeak="on")
    line("project save")
    line(f"render --out {m['id']}-{lead}.mp3 --format mp3 --kbps 192")
    strips = {"lead": voc, "keys": keys, "chords": pad, "bass": bass, "drums": drm}
    if chop:
        strips["hook"] = chop
    return "\n".join(S.L) + "\n", strips


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
            print(f"{stem}: {'ok' if ok else 'FAILED'}" + ("" if ok else f"\n{r.stdout[-900:]}{r.stderr[-600:]}"),
                  flush=True)
            if ok and a.measure:
                spb = 60 / m["bpm"] * 4                               # seconds a bar
                for sc in m["sections"]:
                    w = (sc["bar"] * spb, sc["bars"] * spb)
                    parts = [f"mix {loudness(os.path.join(bdir, stem + '.wav'), *w)}"]
                    parts += [f"{name} {loudness(os.path.join(bdir, f'{stem}.{ch}.wav'), *w)}" for name, ch in strips.items()]
                    print(f"   {sc['name']:13} LUFS: " + " · ".join(parts), flush=True)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
