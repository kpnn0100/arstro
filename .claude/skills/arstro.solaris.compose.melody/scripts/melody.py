#!/usr/bin/env python3
"""melody.py — the melody engine of arstro.solaris.compose.melody: the theory in references/theory.md, as code.

A melody here is MADE, not sampled: a MOTIF (a one-bar rhythm and a shape in scale steps) is DEVELOPED through a phrase
FORM (repetition, sequence, variation, fragmentation, inversion, contrast, cadence), FITTED to the harmony (chord tones
on the strong beats; a non-chord tone there only as an appoggiatura that resolves by step), then judged by a RUBRIC
(chord fit, steps vs leaps and their recovery, range, one climax in the right place, a cadence, unity vs variety,
syncopation for the style) plus the user's TASTE (data/weights.json, learned by taste.py from their verdicts). Many
candidates are made per style; the best, kept diverse, are what the user hears.

    python3 melody.py generate --out batch.json [--per-style 2] [--styles a,b] [--seed N] [--tries 600]
    python3 melody.py analyze melody.json          # the rubric for one melody (the JSON a batch holds)
    python3 melody.py styles                       # the style presets

Library: from melody import STYLES, chord, generate_style, analyze
"""
import argparse, json, math, os, random, sys

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, "..", "data"))

# ── keys and chords ─────────────────────────────────────────────────────────────────────────────────────────────────
MODES = {"major": [0, 2, 4, 5, 7, 9, 11], "minor": [0, 2, 3, 5, 7, 8, 10]}
PC = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'F': 5, 'F#': 6, 'Gb': 6, 'G': 7, 'G#': 8, 'Ab': 8,
      'A': 9, 'A#': 10, 'Bb': 10, 'B': 11}
NAMES = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'F♯', 'G', 'A♭', 'A', 'B♭', 'B']
SHARP_NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B']
FLAT_NAMES = ['C', 'D♭', 'D', 'E♭', 'E', 'F', 'G♭', 'G', 'A♭', 'A', 'B♭', 'C♭']
FLAT_KEYS = {('F', 'major'), ('Bb', 'major'), ('Eb', 'major'), ('Ab', 'major'), ('Db', 'major'), ('Gb', 'major'),
             ('D', 'minor'), ('G', 'minor'), ('C', 'minor'), ('F', 'minor'), ('Bb', 'minor'), ('Eb', 'minor')}
ROMAN = {'I': 1, 'II': 2, 'III': 3, 'IV': 4, 'V': 5, 'VI': 6, 'VII': 7}


class Key:
    def __init__(self, tonic, mode):
        self.tonic, self.mode = PC[tonic], mode
        self.scale = MODES[mode]
        self.names = FLAT_NAMES if (tonic, mode) in FLAT_KEYS else SHARP_NAMES
        if (tonic, mode) in FLAT_KEYS and tonic not in ('Gb',):
            self.names = self.names[:11] + ['B']                       # C♭ only where the key has it
        if mode == "minor":                                      # the leading tone of a minor key is a sharp (C♯ in D minor)
            lt = (self.tonic + 11) % 12
            self.names = list(self.names)
            self.names[lt] = SHARP_NAMES[lt] if (tonic, mode) not in (('F', 'minor'), ('Bb', 'minor'), ('Eb', 'minor')) else self.names[lt]
        self.name = f"{self.names[self.tonic]} {mode}"

    def spell(self, midi):
        return f"{self.names[midi % 12]}{midi // 12 - 1}"

    def midi(self, d, octave=4):
        """Scale index d (0 = the tonic in `octave`) → MIDI."""
        return 12 * (octave + 1) + self.tonic + 12 * (d // 7) + self.scale[d % 7]

    def degree_of(self, midi, octave=4):
        """MIDI → the nearest scale index at or below (and whether it is in the scale)."""
        base = 12 * (octave + 1) + self.tonic
        rel = midi - base
        o, r = divmod(rel, 12)
        best = max(k for k in range(7) if self.scale[k] <= r)
        return o * 7 + best, self.scale[best] == r


def chord(roman, key):
    """A Roman numeral in the KEY's own scale → {name, root, pcs (ordered root-first), roles}.
    Case is quality (I major, ii minor), '°' diminished, a leading b/# alters the root a semitone (borrowed: bVI, iv);
    suffixes 7, maj7, sus2, sus4, add9, 6."""
    s = roman
    acc = 0
    while s and s[0] in 'b#♭♯':
        acc += -1 if s[0] in 'b♭' else 1
        s = s[1:]
    i = 0
    while i < len(s) and s[i].upper() in 'IV':
        i += 1
    num, suf = s[:i], s[i:]
    deg = ROMAN[num.upper()]
    root = (key.tonic + key.scale[deg - 1] + acc) % 12
    if suf.startswith('°') or suf.startswith('dim'):
        tri, suf = (0, 3, 6), suf.lstrip('°').replace('dim', '', 1)
    elif num.isupper():
        tri = (0, 4, 7)
    else:
        tri = (0, 3, 7)
    if 'sus4' in suf:
        tri = (0, 5, 7)
    if 'sus2' in suf:
        tri = (0, 2, 7)
    ext = []
    if 'maj7' in suf:
        ext.append(11)
    elif '7' in suf:
        ext.append(10 if tri != (0, 3, 6) else 9)
    if '6' in suf and 'maj' not in suf:
        ext.append(9)
    if 'add9' in suf or '9' in suf.replace('add9', ''):
        ext.append(2)
    roles = {}
    for name, iv in zip(('root', 'third' if tri[1] in (3, 4) else 'sus', 'fifth'), tri):
        roles[(root + iv) % 12] = name
    for iv in ext:
        roles.setdefault((root + iv) % 12, {11: 'seventh', 10: 'seventh', 9: 'sixth', 2: 'ninth'}[iv])
    q = '°' if tri == (0, 3, 6) else ('m' if tri == (0, 3, 7) else ('sus4' if tri == (0, 5, 7) else ('sus2' if tri == (0, 2, 7) else '')))
    ext_name = ''.join({11: 'maj7', 10: '7', 9: '6', 2: 'add9'}[iv] for iv in ext)
    return {"roman": roman, "name": key.names[root] + q + ext_name, "root": root, "pcs": list(roles.keys()), "roles": roles}


# ── rhythm: one-bar cells (onset, length) in beats ───────────────────────────────────────────────────────────────────
RHYTHMS = {
    "lyrical":   [(0, 1.5), (1.5, 0.5), (2, 1), (3, 1)],                  # dotted quarter, eighth, quarter, quarter
    "dotted":    [(0, 0.75), (0.75, 0.75), (1.5, 0.5), (2, 2)],           # 3-3-2 then a held note
    "tresillo":  [(0, 0.75), (0.75, 0.75), (1.5, 0.5), (2, 0.75), (2.75, 0.75), (3.5, 0.5)],
    "hook8":     [(0, 0.5), (0.5, 0.5), (1, 0.5), (1.5, 0.5), (2, 0.75), (2.75, 0.75), (3.5, 0.5)],
    "long":      [(0, 2), (2, 2)],
    "breath":    [(0.5, 0.5), (1, 1), (2, 1.5), (3.5, 0.5)],              # a rest on the downbeat, a pickup feel
    "offbeat":   [(0.5, 1), (1.5, 0.5), (2, 0.5), (2.5, 1.5)],            # syncopated: across beat 2 and beat 3
    "anticip":   [(0, 1), (1, 0.5), (1.5, 1), (2.5, 1), (3.5, 0.5)],      # pushes across the beats
    "pluck16":   [(0, 0.25), (0.25, 0.25), (0.5, 0.5), (1, 0.25), (1.25, 0.25), (1.5, 0.5), (2, 0.5), (2.5, 0.5), (3, 1)],
    "ballad":    [(0, 1), (1, 1), (2, 1.5), (3.5, 0.5)],
    "sparse":    [(0, 1.5), (1.5, 1), (2.5, 1.5)],
}


def syncopation(cells):
    """Notes starting off the beat and crossing the next beat — what makes a line push."""
    n = sum(1 for a, l in cells if a % 1 and a + l > math.ceil(a) + 1e-9)
    return n / max(1, len(cells))


# ── style presets ────────────────────────────────────────────────────────────────────────────────────────────────────
STYLES = {
    "future-bass": dict(label="Future bass, emotional (Illenium / San Holo)", bpm=150, half_time=True,
                        keys=[('Db', 'major'), ('F', 'major')], progs={"major": [["vi", "IV", "I", "V"], ["IV", "V", "iii", "vi"]]},
                        rhythms=["dotted", "tresillo", "lyrical"], forms=["period", "call", "climb"], color=True,
                        appogg=0.35, lo=63, hi=82, voice="lead", drums="half", sync=(0.15, 0.45)),
    "prog-house": dict(label="Progressive house anthem", bpm=126, half_time=False, keys=[('D', 'major'), ('A', 'major')],
                       progs={"major": [["IV", "I", "V", "vi"], ["vi", "IV", "I", "V"]]}, rhythms=["lyrical", "anticip", "dotted"],
                       forms=["period", "aaba", "call"], color=True, appogg=0.3, lo=62, hi=81, voice="lead", drums="four",
                       sync=(0.1, 0.4)),
    "ncs-hook": dict(label="NCS / future house hook", bpm=128, half_time=False, keys=[('G', 'major'), ('E', 'major')],
                     progs={"major": [["vi", "IV", "I", "V"], ["I", "V", "vi", "IV"]]}, rhythms=["hook8", "tresillo", "anticip"],
                     forms=["call", "period"], color=False, appogg=0.15, lo=64, hi=83, voice="lead", drums="four",
                     sync=(0.2, 0.5)),
    "pop-ballad": dict(label="Pop ballad", bpm=80, half_time=False, keys=[('C', 'major'), ('Eb', 'major')],
                       progs={"major": [["I", "V", "vi", "IV"], ["I", "vi", "IV", "V"]]}, rhythms=["ballad", "lyrical", "breath"],
                       forms=["period", "call", "aaba"], color=False, appogg=0.4, lo=60, hi=77, voice="piano",
                       drums=None, sync=(0.0, 0.3)),
    "melodic-trap": dict(label="Melodic trap (Trap Nation)", bpm=140, half_time=True, keys=[('C#', 'minor'), ('F', 'minor')],
                         progs={"minor": [["i", "VI", "III", "VII"], ["VI", "III", "VII", "i"]]}, rhythms=["offbeat", "breath", "dotted"],
                         forms=["call", "period"], color=False, appogg=0.25, lo=61, hi=80, voice="lead",
                         drums="half", sync=(0.2, 0.55)),
    "cinematic": dict(label="Cinematic, borrowed and suspended", bpm=88, half_time=False, keys=[('D', 'minor'), ('Bb', 'major')],
                      progs={"minor": [["i", "VI", "III", "VII"], ["i", "iv", "VI", "V"]], "major": [["I", "iii", "IV", "iv"]]}, rhythms=["long", "lyrical", "sparse"],
                      forms=["period", "climb"], color=False, appogg=0.5, lo=60, hi=79, voice="piano", drums=None,
                      sync=(0.0, 0.25)),
    "kawaii": dict(label="Kawaii / bright future bass plucks", bpm=150, half_time=True, keys=[('E', 'major'), ('Ab', 'major')],
                   progs={"major": [["IVmaj7", "V", "iii7", "vi"], ["I", "V", "vi", "IVadd9"]]}, rhythms=["pluck16", "hook8"],
                   forms=["call", "period"], color=True, appogg=0.1, lo=66, hi=86, voice="pluck", drums="half",
                   sync=(0.1, 0.45)),
    "lofi": dict(label="Lo-fi / jazzy sevenths", bpm=84, half_time=False, keys=[('F', 'major'), ('A', 'major')],
                 progs={"major": [["ii7", "V7", "Imaj7", "vi7"], ["IVmaj7", "iii7", "ii7", "V7"]]}, rhythms=["breath", "offbeat", "sparse"],
                 forms=["call", "aaba", "period"], color=True, appogg=0.4, lo=60, hi=79, voice="piano", drums="lofi",
                 sync=(0.15, 0.5)),
}

# ── phrases ─────────────────────────────────────────────────────────────────────────────────────────────────────────
# batch-001's lesson (taste.md): a melody built bar by bar from a tiny motif, each bar moved on its own, sounds RANDOM.
# People sing in PHRASES: a two-bar IDEA — an active bar, then a bar that LANDS on a long note — repeated where the
# harmony repeats, every note heading for the next strong note, a breath only after a long note or before a pickup.

LANDINGS = {          # the second bar of an idea: it lands (onset, length) — the long note is where a singer breathes
    "land2":     [(0, 1), (1, 1), (2, 2)],                      # quarter, quarter, half
    "land_dot":  [(0, 1.5), (1.5, 0.5), (2, 2)],                # dotted quarter, eighth, half
    "land_long": [(0, 0.5), (0.5, 0.5), (1, 3)],                # two eighths, then held
    "land_pick": [(0, 1), (1, 0.5), (1.5, 2), (3.5, 0.5)],      # held, then a PICKUP into the next phrase
    "land_hold": [(0, 4)],                                      # a whole note
    "land_hook": [(0, 0.5), (0.5, 0.5), (1, 1), (2, 1.5), (3.5, 0.5)],
}
for _k, _v in {"future-bass": ["land_dot", "land_long", "land_pick"], "prog-house": ["land2", "land_dot", "land_pick"],
               "ncs-hook": ["land_hook", "land_pick"], "pop-ballad": ["land2", "land_hold", "land_dot"],
               "melodic-trap": ["land_long", "land_pick"], "cinematic": ["land_hold", "land2"],
               "kawaii": ["land_hook", "land_long"], "lofi": ["land_long", "land_pick", "land2"]}.items():
    STYLES[_k]["landings"] = _v

# ── what professional melodies teach (scripts/corpus.py over POP909 → data/corpus_stats.json) ───────────────────────
_CORPUS = None


def corpus():
    """The aggregate statistics of 880 professional pop melodies — or None before corpus.py has run."""
    global _CORPUS
    if _CORPUS is None:
        path = os.path.join(DATA, "corpus_stats.json")
        _CORPUS = json.load(open(path)) if os.path.exists(path) else {}
        if _CORPUS:
            _CORPUS["_bigram"] = {int(k): {int(kk): vv for kk, vv in v.items()} for k, v in _CORPUS["bigram"].items()}
    return _CORPUS or None


# notes in a two-bar idea, by style: a hook is busier, a film theme sparser (real pop vocals: 4–7 a bar)
DENSITY = {"future-bass": (5, 10), "prog-house": (5, 10), "ncs-hook": (7, 12), "pop-ballad": (4, 9),
           "melodic-trap": (4, 9), "cinematic": (3, 7), "kawaii": (8, 14), "lofi": (4, 9)}


def corpus_rhythm(style, rng, exclude=None):
    """A two-bar phrase rhythm as real songs have it — chosen by how common it is among those that can carry an idea:
    it starts in its first beat or so, it lands (a note of a beat or more in its second bar), and nothing in it is a
    hole (a rest over 1.5 beats) except before a pickup at its very end."""
    c = corpus()
    if not c:
        return None
    lo_n, hi_n = DENSITY.get(style, (4, 10))
    pool = []
    for t in c["rhythm"]["top_two_bars"]:
        cells = [tuple(x) for x in t["cells"]]
        if not lo_n <= len(cells) <= hi_n or cells[0][0] > 1.0 or cells == exclude:
            continue
        if not any(a >= 4 and l >= 1.0 for a, l in cells) or not any(a < 4 for a, _ in cells[1:]):
            continue
        holes = [b[0] - (a[0] + a[1]) for a, b in zip(cells, cells[1:]) if b[0] < 7.0]
        if any(h > 1.5 for h in holes) or cells[-1][0] + cells[-1][1] > 8.01:
            continue
        pool.append((cells, t["count"] ** 0.7))
    if not pool:
        return None
    return rng.choices([p for p, _ in pool], weights=[w for _, w in pool])[0]


def surprise(ps):
    """The melody's mean surprise, bits an interval, by the corpus's interval bigram — real pop: 2.94 (2.70–3.17)."""
    c = corpus()
    if not c or len(ps) < 3:
        return None
    bg = c["_bigram"]
    ivs = [max(-12, min(12, b - a)) for a, b in zip(ps, ps[1:])]
    bits = [-math.log2((bg.get(x, {}).get(y, 0) + 1) / (sum(bg.get(x, {}).values()) + 25)) for x, y in zip(ivs, ivs[1:])]
    return sum(bits) / len(bits)


CONTOURS = {          # the idea's strong notes, in scale steps from its first: the gesture the ear follows
    "arch": [0, 2, 1, 0], "rise": [0, 1, 2, 3], "fall": [3, 2, 1, 0], "leap-fall": [0, 4, 3, 2], "wave": [0, 2, 0, 1],
}
FORM_NAMES = {
    "period": "period — the idea, its question; the idea again, its answer",
    "sentence": "sentence — the idea, the idea a third up, fragments climbing, the cadence",
    "aaba": "AABA — the idea, again, a contrast at the top, the idea home",
    "hook": "hook loop — one two-bar hook, again, again, lifted at the end",
    "call": "call and response — the idea, its mirror; again; the answer home",
    "climb": "climbing — the idea a step higher each time, the top, the fall home",
}


AFTER = {"step": {"same": 0.43, "reverse": 0.39, "repeat": 0.17},     # what follows what in real pop melodies
         "third": {"same": 0.35, "reverse": 0.45, "repeat": 0.19},    # (data/corpus_stats.json "after"; these are
         "skip": {"same": 0.16, "reverse": 0.59, "repeat": 0.26}}     # its values, used when the file is missing)


def path(d0, d1, m, rng, prev=0):
    """m scale degrees from d0 toward d1 (both outside the path), moving as real melodies move: after a step it goes
    on or turns about equally, or stays; after a leap it mostly turns back (post-skip reversal) — the corpus's own
    rates — weighted toward d1 (every note heading somewhere), steps preferred to thirds, never three of a note in a
    row, the last within a third of d1."""
    if m == 0:
        return []
    after = (corpus() or {}).get("after", AFTER)
    for _ in range(300):
        out, cur, last = [], d0, prev
        ok = True
        for k in range(m):
            left = m - k - 1
            opts = []
            for st_ in (-2, -1, 0, 1, 2):
                nxt = cur + st_
                if st_ == 0 and (last == 0 or (len(out) >= 1 and out[-1] == cur and k >= 1)):
                    continue                                     # never a third of the same note
                gap = abs(d1 - nxt)
                if left == 0 and gap > 2:
                    continue
                if gap > 2 * left + 2:
                    continue
                cls = "skip" if abs(last) >= 3 else ("third" if abs(last) == 2 else "step")
                kind = "repeat" if st_ == 0 else ("same" if st_ * last > 0 or last == 0 else "reverse")
                w = after[cls][kind] * (1.0 if abs(st_) <= 1 else 0.45)
                if (d1 - cur) * st_ > 0 and abs(d1 - cur) > left:
                    w *= 2.5                                     # far from the goal: head for it
                opts.append((nxt, w, st_))
            if not opts:
                ok = False
                break
            nxt, _, st_ = rng.choices(opts, weights=[o[1] for o in opts])[0]
            out.append(nxt)
            cur, last = nxt, st_ if st_ else last * 0
        if ok:
            return out
    return [d0 + (1 if d1 > d0 else -1) * ((k % 2) + 1) for k in range(m)]


def build(style, rng, taste=None):
    """One candidate, PHRASE by phrase: a two-bar idea (an active bar and a landing), developed by a form over a
    four-chord loop played twice, adapted to the chords, with the emotion placed: long notes at phrase ends, leaning
    notes into them, one climax reached by a leap and left by steps."""
    st = STYLES[style]
    tonic, mode = rng.choice(st["keys"])
    key = Key(tonic, mode)
    form = rng.choice(st["forms"])
    progs = st["progs"][mode]
    homeward = [p for p in progs if key.tonic in chord(p[-1], key)["pcs"]]
    if form != "hook" and homeward and rng.random() < 0.8:
        progs = homeward                                         # an answer needs a chord it can come home on
    prog4 = rng.choice(progs)
    prog = [chord(r, key) for r in prog4 * 2]
    rname = rng.choice(st["rhythms"])
    lname = rng.choice(st["landings"])
    cname = rng.choice(list(CONTOURS))
    lo, hi = st["lo"], st["hi"]
    hook = form == "hook"
    active = RHYTHMS[rname]
    landing = active if (hook and rng.random() < 0.5) else LANDINGS[lname]
    if hook and landing is active:
        landing = [(a, l) for a, l in active if a < 2] + [(2, 1.5), (3.5, 0.5)]     # a hook's second bar still lands
    cells = [(a, l) for a, l in active] + [(4 + a, l) for a, l in landing]
    real = corpus_rhythm(style, rng) if rng.random() < 0.85 else None
    if real:                                                     # a phrase rhythm professional songs actually use
        cells, rname, lname = list(real), "corpus", "corpus"

    def is_strong(a, l):
        return (a % 4) in (0, 2) or l >= 1

    def ct_near(midi, ch, avoid_root=False, stable=False):
        c = [m for m in range(lo, hi + 1) if m % 12 in ch["pcs"]]
        if stable:
            c = [m for m in c if ch["roles"][m % 12] in ("root", "third", "fifth")] or c
        if avoid_root:
            c = [m for m in c if m % 12 != ch["root"]] or c
        return min(c, key=lambda m: (abs(m - midi), m))

    def idea(chA, chB, d0, contour):
        """The two-bar idea: its strong notes on the contour (chord tones, the last stable and long), the rest a path."""
        strong_ix = [k for k, (a, l) in enumerate(cells) if is_strong(a, l)]
        heights = [contour[min(3, round(j * 3 / max(1, len(strong_ix) - 1)))] for j in range(len(strong_ix))]
        pitch = {}
        for j, k in enumerate(strong_ix):
            a, l = cells[k]
            ch = chA if a < 4 else chB
            last = j == len(strong_ix) - 1
            pitch[k] = ct_near(key.midi(d0 + heights[j]), ch, avoid_root=(a % 4 == 0 and not last), stable=last)
        return fill(cells, pitch)

    def fill(cs, pitch):
        """Every note not fixed walks from the fixed note before toward the fixed note after."""
        fixed = sorted(pitch)
        out = [None] * len(cs)
        for k in fixed:
            out[k] = pitch[k]
        first = fixed[0]
        for k in range(first - 1, -1, -1):                       # before the first strong note: approach it by step
            out[k] = key.midi(key.degree_of(out[k + 1])[0] - 1)
        for a, b in zip(fixed, fixed[1:] + [None]):
            gap = list(range(a + 1, b if b is not None else len(cs)))
            if not gap:
                continue
            da = key.degree_of(out[a])[0]
            db = key.degree_of(out[b])[0] if b is not None else da - 1
            for k, d in zip(gap, path(da, db, len(gap), rng)):
                out[k] = key.midi(d)
        return [[cs[k][0], cs[k][1], out[k]] for k in range(len(cs))]

    def norepeat(seq):
        """A note the same as the one before, unmeant: the weaker of the two steps toward what follows it."""
        for i in range(2, len(seq)):
            if seq[i][2] == seq[i - 1][2] == seq[i - 2][2]:      # a pair dwells (real melodies do, a quarter of the
                # time); a third in a row is a stall
                k = i if not is_strong(seq[i][0] % 4, seq[i][1]) else i - 1
                if k == 0:
                    k = i
                nxt = seq[k + 1][2] if k + 1 < len(seq) else seq[k][2] - 1
                d = key.degree_of(seq[k][2])[0]
                cand = key.midi(d + (1 if nxt > seq[k][2] else -1))
                if (k > 0 and cand == seq[k - 1][2]) or cand == nxt:
                    cand = key.midi(d + (-1 if nxt > seq[k][2] else 1))
                seq[k][2] = cand
        return seq

    def adapt(unit, chA, chB, shift, t_from, t_to):
        """The same idea — its rhythm and its shape — moved `shift` steps (a tonal sequence) to sit on chA, chB: the
        transposition (within a step of the plan) with the most strong notes on the chords, the rest moved to one."""
        best, cost0 = None, 1e9
        for k in (shift - 1, shift, shift + 1):
            u = [[a - t_from + t_to, l, key.midi(key.degree_of(p)[0] + k)] for a, l, p in unit]
            if any(not lo <= n[2] <= hi for n in u):
                continue
            miss = sum(1 for n in u if is_strong(n[0] - t_to, n[1]) and n[2] % 12 not in (chA if n[0] - t_to < 4 else chB)["pcs"])
            cost = 3 * miss + abs(k - shift)
            if cost < cost0:
                best, cost0 = u, cost
        if best is None:
            best = [[a - t_from + t_to, l, p] for a, l, p in unit]
        for n in best:
            ch = chA if n[0] - t_to < 4 else chB
            if is_strong(n[0] - t_to, n[1]) and n[2] % 12 not in ch["pcs"]:
                n[2] = ct_near(n[2], ch)
        return best

    def ending(unit, ch, kind, t0):
        """The unit's last long note becomes a cadence: 'half' on 2, 5 or 7; 'home' on the tonic (or the chord's most
        stable tone when the chord lacks it); 'open' on its 3rd or 5th. The notes before it step into it."""
        held = [i for i, n in enumerate(unit) if n[1] >= 1.5]
        k = held[-1] if held else len(unit) - 1                 # the held note, not a pickup after it
        prev = unit[k - 1][2] if k else unit[k][2]
        if kind == "half":
            pcs = [p for p in ((key.tonic + key.scale[x]) % 12 for x in (1, 4, 6)) if p in ch["pcs"]] or \
                  [p for p in ch["pcs"] if ch["roles"][p] != "root"]
        elif kind == "home":
            pcs = [key.tonic] if key.tonic in ch["pcs"] else [p for p in ch["pcs"] if ch["roles"][p] in ("third", "root")]
        else:
            pcs = [p for p in ch["pcs"] if ch["roles"][p] in ("third", "fifth")] or ch["pcs"]
        cand = [m for m in range(lo, hi + 1) if m % 12 in pcs]
        unit[k][2] = min(cand, key=lambda m: (abs(m - prev), m))
        rel = (unit[k][2] - key.tonic) % 12
        deg = key.scale.index(rel) + 1 if rel in key.scale else {11: 7, 9: 6, 4: 3}.get(rel, 0)
        ends.append((kind, deg, ch["name"]))
        if k >= 2:                                               # the notes before step into it
            seg = path(key.degree_of(unit[k - 2][2])[0], key.degree_of(unit[k][2])[0], 1, rng)
            unit[k - 1][2] = key.midi(seg[0])
        return unit

    ends = []
    # the idea: its first strong note the 3rd or 5th of the first chord, low-middle, so the phrase can rise
    start = ct_near((lo + hi) // 2 - 4, prog[0], avoid_root=True)
    d0 = key.degree_of(start)[0]
    contour = CONTOURS[cname]
    u1 = norepeat(idea(prog[0], prog[1], d0, contour))
    devices = [f"the idea: a {cname} over two bars, on a phrase rhythm real pop songs use" if rname == "corpus"
               else f"the idea: a {cname} over two bars — a {rname} bar, then it lands ({lname})"]
    if form == "period":
        u2 = ending(adapt(u1, prog[2], prog[3], rng.choice([1, -1]), 0, 8), prog[3], "half", 8)
        u3 = [[a + 16, l, p] for a, l, p in u1]
        u4 = adapt(u1, prog[6], prog[7], 2, 0, 24)
        u4 = ending(u4, prog[7], "home", 24)
        climax_unit = 3
        devices += ["the idea returns exactly where the chords do (bars 5–6)"]
    elif form == "sentence":
        u2 = adapt(u1, prog[2], prog[3], 2, 0, 8)
        frag = [n for n in u1 if n[0] < 4]
        u3 = adapt(frag, prog[4], prog[4], 1, 0, 16) + adapt(frag, prog[5], prog[5], 3, 0, 20)
        u4 = ending(adapt(u1, prog[6], prog[7], 2, 0, 24), prog[7], "home", 24)
        climax_unit = 2
        devices += ["the idea a third higher", "its first bar, climbing (fragmentation)"]
    elif form == "aaba":
        u2 = adapt(u1, prog[2], prog[3], 0, 0, 8)
        b_cells = [(a, l) for a, l in RHYTHMS[rng.choice([r for r in st["rhythms"] if r != rname] or [rname])]] + \
                  [(4 + a, l) for a, l in LANDINGS["land_long"]]
        hi_d = key.degree_of(max(n[2] for n in u1 + u2))[0] + 2
        strong_ix = [k for k, (a, l) in enumerate(b_cells) if is_strong(a, l)]
        pitch = {k: ct_near(key.midi(hi_d - (1 if j else 0) - j), prog[4] if b_cells[k][0] < 4 else prog[5],
                            stable=j == len(strong_ix) - 1) for j, k in enumerate(strong_ix)}
        u3 = [[a + 16, l, p] for a, l, p in fill(b_cells, pitch)]
        u4 = ending(adapt(u1, prog[6], prog[7], 0, 0, 24), prog[7], "home", 24)
        climax_unit = 2
        devices += ["a contrast (B) at the top", "the idea home"]
    elif form == "hook":
        u2 = adapt(u1, prog[2], prog[3], 0, 0, 8)
        u3 = [[a + 16, l, p] for a, l, p in u1]
        u4 = ending(adapt(u1, prog[6], prog[7], 1, 0, 24), prog[7], "open", 24)
        climax_unit = 3
        devices += ["one hook, four times — exactly where the chords repeat", "lifted the last time, open to loop"]
    elif form == "call":
        mirror = [[a, l, key.midi(2 * key.degree_of(u1[0][2])[0] - key.degree_of(p)[0])] for a, l, p in u1]
        u2 = ending(adapt(mirror, prog[2], prog[3], 0, 0, 8), prog[3], "half", 8)
        u3 = [[a + 16, l, p] for a, l, p in u1]
        u4 = ending(adapt(mirror, prog[6], prog[7], 1, 0, 24), prog[7], "home", 24)
        climax_unit = 2
        devices += ["the response mirrors the call (inversion)", "the call again exactly, then the answer home"]
    else:  # climb
        u2 = adapt(u1, prog[2], prog[3], 1, 0, 8)
        u3 = adapt(u1, prog[4], prog[5], 3, 0, 16)
        u4 = ending(adapt([n for n in u1], prog[6], prog[7], 0, 0, 24), prog[7], "home", 24)
        climax_unit = 2
        devices += ["the idea a step higher, then higher", "down home"]
    for kind, deg, chn in ends:
        devices.append({"half": f"a question: it stops on {deg or '?'} over {chn}",
                        "home": (f"the answer: home on 1 over {chn}" if deg == 1 else f"the answer: on {deg or '?'} over {chn} — the loop resolves it"),
                        "open": f"open: on {deg or '?'} over {chn}, to loop"}[kind])
    units = [u1, u2, u3, u4]
    notes = [n for u in units for n in u]

    # a PICKUP leads into what follows it: a step below (or above) the next note
    for i in range(len(notes) - 1):
        a, l, p = notes[i]
        if (a % 4) == 3.5 and l <= 0.5 and notes[i + 1][0] == a + 0.5:
            nxt = notes[i + 1][2]
            notes[i][2] = key.midi(key.degree_of(nxt)[0] - 1)
    # THE CLIMAX: the highest note once, in its unit, reached by a leap up and left by steps
    cu = units[climax_unit]
    peak_i = max(range(len(cu)), key=lambda k: (is_strong(cu[k][0] % 4, cu[k][1]), cu[k][2]))
    others_top = max(n[2] for u_i, u in enumerate(units) for n in u if not (u_i == climax_unit and n is cu[peak_i]))
    ch_peak = prog[min(int(cu[peak_i][0] // 4), 7)]
    want = [m for m in range(others_top + 1, hi + 1) if m % 12 in ch_peak["pcs"]]
    if want:
        cu[peak_i][2] = want[0]
        devices.append(f"the climax, once, in bar {int(cu[peak_i][0] // 4) + 1}")
        if peak_i + 1 < len(cu):                                 # left by a step down
            cu[peak_i + 1][2] = key.midi(key.degree_of(cu[peak_i][2])[0] - 1)
    for n in notes:                                              # nothing else reaches it
        if n is not cu[peak_i] and n[2] >= cu[peak_i][2]:
            n[2] = key.midi(key.degree_of(cu[peak_i][2])[0] - 1)
    # THE LEAN: a long phrase end (or the climax) entered by an APPOGGIATURA — a step above, on the beat, falling
    leaned = 0
    spots = [n for n in notes if n[1] >= 2 and (n[0] % 4) in (0, 2) and (8 <= n[0] < 16 or n[0] >= 24)]
    for n in spots:
        if leaned >= 2 or rng.random() >= st["appogg"] + 0.25:
            continue
        if True:
            ch = prog[min(int(n[0] // 4), 7)]
            up = key.midi(key.degree_of(n[2])[0] + 1)
            if up % 12 not in ch["pcs"] and up <= hi and up != cu[peak_i][2]:
                j = notes.index(n)
                notes.insert(j, [n[0], 1.0, up])
                n[0], n[1] = n[0] + 1.0, n[1] - 1.0
                leaned += 1
    if leaned:
        devices.append(f"{leaned} leaning note{'s' if leaned > 1 else ''} (appoggiatura) into the long notes")
    notes.sort(key=lambda n: n[0])
    peak = cu[peak_i]
    for i in range(2, len(notes)):                               # last: no stalls, the climax untouched
        if notes[i][2] == notes[i - 1][2] == notes[i - 2][2] and notes[i] is not peak and notes[i - 1] is not peak:
            norepeat(notes[i - 1:i + 2] if i + 1 < len(notes) else notes[i - 1:i + 1])
    mel = {
        "style": style, "style_label": st["label"], "bpm": st["bpm"], "key": key.name, "tonic": key.tonic, "mode": mode,
        "key_names": key.names, "progression": prog4, "chords": [c["name"] for c in prog],
        "chord_pcs": [c["pcs"] for c in prog], "chord_roots": [c["root"] for c in prog],
        "chord_roles": [{str(k): v for k, v in c["roles"].items()} for c in prog],
        "rhythm": rname, "landing": lname, "form": form, "form_label": FORM_NAMES[form], "contour": cname,
        "motif_kind": cname, "engine": 2,
        "notes": [{"t": round(t, 4), "d": round(d, 4), "p": int(p), "name": key.spell(int(p))} for t, d, p in notes],
        "devices": devices, "voice": st["voice"], "drums": st["drums"], "half_time": st["half_time"],
    }
    mel["metrics"] = analyze(mel)
    mel["score"] = score(mel, st, taste)
    return mel


# ── the rubric ──────────────────────────────────────────────────────────────────────────────────────────────────────
def analyze(mel):
    """Theory metrics of a melody over its chords."""
    notes = mel["notes"]
    pcs, roots = mel["chord_pcs"], mel["chord_roots"]
    ps = [n["p"] for n in notes]
    ivs = [b - a for a, b in zip(ps, ps[1:])]
    total = 32.0
    strong = strong_ct = app = down = down_color = 0
    for i, n in enumerate(notes):
        b = min(int(n["t"] // 4), 7)
        pos = n["t"] - 4 * b
        if pos in (0, 2) or n["d"] >= 1:
            strong += 1
            if n["p"] % 12 in pcs[b]:
                strong_ct += 1
            elif i + 1 < len(notes) and abs(notes[i + 1]["p"] - n["p"]) <= 2 and \
                    notes[i + 1]["p"] % 12 in pcs[min(int(notes[i + 1]["t"] // 4), 7)]:
                app += 1
        if pos == 0:
            down += 1
            down_color += n["p"] % 12 != roots[b]                     # a 3rd, 5th, 7th or 9th on the downbeat
    leaps = [i for i, iv in enumerate(ivs) if abs(iv) >= 5]
    recovered = sum(1 for i in leaps if i + 1 < len(ivs) and ivs[i + 1] * ivs[i] < 0 and abs(ivs[i + 1]) <= 4)
    same_dir_leaps = sum(1 for a, b in zip(ivs, ivs[1:]) if abs(a) >= 5 and abs(b) >= 5 and a * b > 0)
    top = max(ps)
    tops = [n for n in notes if n["p"] == top]
    run, best = 1, 1
    for a, b in zip(ps, ps[1:]):
        run = run + 1 if a == b else 1
        best = max(best, run)
    lead = (mel["tonic"] + 11) % 12
    lt = [i for i, p in enumerate(ps[:-1]) if p % 12 == lead and (mel["mode"] == "major")]
    lt_ok = sum(1 for i in lt if ps[i + 1] % 12 == mel["tonic"] or abs(ps[i + 1] - ps[i]) <= 2)
    rhythms = {tuple((round(n["t"] - 4 * b, 3), n["d"]) for n in notes if int(n["t"] // 4) == b) for b in range(8)}
    sync = sum(1 for n in notes if n["t"] % 1 and n["t"] + n["d"] > math.ceil(n["t"]) + 1e-9) / max(1, len(notes))
    bar_mean = [sum(n["p"] for n in notes if int(n["t"] // 4) == b) / max(1, sum(1 for n in notes if int(n["t"] // 4) == b))
                for b in range(8)]
    turns = sum(1 for a, b in zip(ivs, ivs[1:]) if a * b < 0)
    last = notes[-1]
    # PHRASES (batch-001: "random", "the rest is not in natural place"): the idea's return, its rhythm kept, rests
    # where a singer breathes, phrase ends held
    unit = lambda u: [(round(n["t"] - 8 * u, 3), n["d"], n["p"]) for n in notes if 8 * u <= n["t"] < 8 * u + 8]
    units = [unit(u) for u in range(4)]
    exact_return = units[0] == units[2]
    rhythm_kept = sum(1 for u in units[1:] if [x[:2] for x in u] == [x[:2] for x in units[0]]) / 3
    misplaced = 0
    for a, b in zip(notes, notes[1:]):
        gap = b["t"] - (a["t"] + a["d"])
        if gap >= 0.49:
            breath = a["d"] >= 1.5 or (b["t"] % 8 >= 7.5 or b["t"] % 8 == 0)
            misplaced += not breath
    ends = 0
    for u in range(4):
        body = [n for n in notes if 8 * u <= n["t"] < 8 * u + 8 and not (n["t"] % 8 >= 7.5 and n["d"] <= 0.5)]
        ends += bool(body) and body[-1]["d"] >= 1.5
    ci = next(i for i, n in enumerate(notes) if n["p"] == top)
    leap_in = ci > 0 and notes[ci]["p"] - notes[ci - 1]["p"] >= 3
    step_out = ci + 1 < len(notes) and 1 <= notes[ci]["p"] - notes[ci + 1]["p"] <= 2
    return {
        "notes": len(notes), "range": top - min(ps), "pitches": len(set(ps)),
        "steps": round(sum(1 for iv in ivs if 1 <= abs(iv) <= 2) / max(1, len(ivs)), 3),
        "repeats": round(sum(1 for iv in ivs if iv == 0) / max(1, len(ivs)), 3), "max_repeat": best,
        "leaps": len(leaps), "leaps_recovered": round(recovered / len(leaps), 3) if leaps else 1.0,
        "same_direction_leaps": same_dir_leaps, "big_leaps": sum(1 for iv in ivs if abs(iv) > 9),
        "tritones": sum(1 for iv in ivs if abs(iv) == 6),
        "strong_chord_tones": round(strong_ct / max(1, strong), 3), "appoggiaturas": app,
        "strong_unresolved": strong - strong_ct - app,
        "downbeat_colour": round(down_color / max(1, down), 3),
        "climax_single": len(tops) == 1, "climax_at": round(tops[0]["t"] / total, 3),
        "rise_to_climax": round(max(bar_mean[4:7]) - min(bar_mean[0:3]), 2),
        "leading_tones_resolved": round(lt_ok / len(lt), 3) if lt else 1.0,
        "ends_on_tonic": last["p"] % 12 == mel["tonic"], "end_length": last["d"],
        "bar_rhythms": len(rhythms), "syncopation": round(sync, 3), "turns": round(turns / max(1, len(ivs) - 1), 3),
        "density": round(len(notes) / 8, 2),
        "surprise_bits": round(surprise(ps), 3) if surprise(ps) is not None else None,
        "exact_return": exact_return, "rhythm_kept": round(rhythm_kept, 3), "misplaced_rests": misplaced,
        "phrase_ends_held": ends, "climax_leap_in": leap_in, "climax_step_out": step_out,
    }


def bell(x, centre, width):
    """1 at the centre, e^(−½) a width away: a preference, not a gate — so candidates can be ranked."""
    return math.exp(-0.5 * ((x - centre) / width) ** 2)


def score(mel, st, taste=None):
    """The rubric, each part 0 … 1 (references/theory.md says why each one matters)."""
    m = mel["metrics"]
    hook = mel["form"] == "hook"
    parts = {
        # targets from 880 professional pop melodies (data/corpus_stats.json) where it has them
        "chord_fit": bell(m["strong_chord_tones"] + 0.5 * m["appoggiaturas"] / 8, 0.9, 0.15),
        "unresolved": 0.6 ** m["strong_unresolved"],
        "steps": bell(m["steps"], 0.5, 0.14),
        "repeats": bell(m["repeats"], 0.20, 0.08),               # taste.md lesson 5 (liked 0.20, disliked 0.08)
        "leaps": m["leaps_recovered"] * 0.7 ** m["same_direction_leaps"] * 0.6 ** m["big_leaps"] * 0.75 ** m["tritones"],
        "range": bell(m["range"], 14, 4),
        "variety": bell(m["pitches"], 9, 3),
        "colour": bell(m["downbeat_colour"], 0.65, 0.25),
        "climax": (1.0 if m["climax_single"] else 0.6) * bell(m["climax_at"], 0.62, 0.22),
        "arc": bell(m["rise_to_climax"], 5, 3) if mel["form"] != "hook" else 1.0,
        "tendency": m["leading_tones_resolved"],
        "cadence": bell(m["end_length"], 2.5, 1.2),
        "rhythm": bell(m["bar_rhythms"], 3, 1.5),                # lesson 6: fewer bar rhythms — simple
        "syncopation": bell(m["syncopation"], sum(st["sync"]) / 2, (st["sync"][1] - st["sync"][0]) / 2 + 0.05),
        "motion": bell(m["turns"], 0.36, 0.15),                  # lesson 6: liked lines turn less (0.32 vs 0.45)
        # EXPECTEDNESS (batch-001 averaged 3.38 bits and was heard as random; real pop: 2.94, 2.70–3.17)
        "expected": bell(m["surprise_bits"], 2.85, 0.22) if m.get("surprise_bits") is not None else 1.0,   # lesson 5
        # the phrase (batch-001's lesson): what makes a line sound meant rather than random
        "return": 1.0 if m.get("exact_return") else (0.5 if mel["form"] in ("climb", "sentence") else 0.2),
        "rhythm_kept": bell(m.get("rhythm_kept", 0), 0.67, 0.35),
        "rests": 0.4 ** m.get("misplaced_rests", 0),
        "phrase_ends": bell(m.get("phrase_ends_held", 0), 4, 1.2),
        "climax_gesture": 0.5 + 0.25 * bool(m.get("climax_leap_in")) + 0.25 * bool(m.get("climax_step_out")),
    }
    # what the research and the verdicts validated weighs more: expectedness (why "random" was heard), the idea's
    # return and rests in place (the approver's words, batch-001)
    weight = {"expected": 2.0, "return": 1.5, "rests": 1.5}
    total = sum(v * weight.get(k, 1.0) for k, v in parts.items()) / sum(weight.get(k, 1.0) for k in parts)
    if taste:
        total += taste_bonus(mel, taste)
    mel["rubric"] = {k: round(v, 3) for k, v in parts.items()}
    return round(total, 4)


def taste_bonus(mel, taste):
    """What the user's verdicts taught (data/weights.json, written by taste.py): a bonus for liked styles, forms and
    rhythms, and for metrics inside the range their liked melodies had."""
    b = 0.0
    for field in ("style", "form", "rhythm", "motif_kind", "voice"):
        b += taste.get(field, {}).get(mel.get(field), 0.0)
    conf = min(1.0, taste.get("liked_count", 0) / 8)               # four liked melodies pull half as hard as eight
    for metric, rng_ in taste.get("metric_ranges", {}).items():
        v = mel["metrics"].get(metric)
        if v is not None:
            b += (0.02 if rng_[0] <= v <= rng_[1] else -0.02) * conf
    return b


def load_taste():
    path = os.path.join(DATA, "weights.json")
    if os.path.exists(path):
        with open(path) as f:
            return json.load(f)
    return None


def generate_style(style, n, rng, tries=600, taste=None):
    """`tries` candidates for a style; the best n, each with its own motif and form where it can."""
    cands = sorted((build(style, rng, taste) for _ in range(tries)), key=lambda m: -m["score"])
    out, seen = [], set()
    for c in cands:
        sig = (c["form"], c["contour"], c["rhythm"], c["landing"])
        if sig in seen or any(c["form"] == o["form"] and c["rhythm"] == o["rhythm"] for o in out):
            continue
        seen.add(sig)
        out.append(c)
        if len(out) == n:
            break
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    g = sub.add_parser("generate")
    g.add_argument("--out", required=True)
    g.add_argument("--per-style", type=int, default=2)
    g.add_argument("--styles", default=",".join(STYLES))
    g.add_argument("--seed", type=int, default=1)
    g.add_argument("--tries", type=int, default=600)
    g.add_argument("--batch", default=None, help="the batch's name (default: the out file's folder)")
    a = sub.add_parser("analyze")
    a.add_argument("file")
    sub.add_parser("styles")
    args = ap.parse_args()
    if args.cmd == "styles":
        for k, v in STYLES.items():
            print(f"{k:14} {v['label']} — {v['bpm']} bpm, forms {', '.join(v['forms'])}")
        return
    if args.cmd == "analyze":
        with open(args.file) as f:
            mel = json.load(f)
        print(json.dumps(analyze(mel), indent=2))
        return
    rng = random.Random(args.seed)
    taste = load_taste()
    batch = args.batch or os.path.basename(os.path.dirname(os.path.abspath(args.out)))
    mels = []
    for style in args.styles.split(","):
        mels += generate_style(style, args.per_style, rng, args.tries, taste)
    for i, m in enumerate(mels):
        m["id"] = f"{batch}-{i + 1:02d}"
    with open(args.out, "w") as f:
        json.dump({"batch": batch, "seed": args.seed, "taste_used": bool(taste), "melodies": mels}, f, indent=1)
    for m in mels:
        print(f"{m['id']}  {m['style']:13} {m['key']:9} {m['form']:8} {m['rhythm']:9} score {m['score']:.3f}  "
              f"{' '.join(c for c in m['chords'][:4])}")


if __name__ == "__main__":
    main()
