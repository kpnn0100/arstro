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
                        rhythms=["dotted", "tresillo", "lyrical"], forms=["period", "sentence", "climb"], color=True,
                        appogg=0.35, lo=63, hi=82, voice="lead", drums="half", sync=(0.15, 0.45)),
    "prog-house": dict(label="Progressive house anthem", bpm=126, half_time=False, keys=[('D', 'major'), ('A', 'major')],
                       progs={"major": [["IV", "I", "V", "vi"], ["vi", "IV", "I", "V"]]}, rhythms=["lyrical", "anticip", "dotted"],
                       forms=["period", "aaba", "climb"], color=True, appogg=0.3, lo=62, hi=81, voice="lead", drums="four",
                       sync=(0.1, 0.4)),
    "ncs-hook": dict(label="NCS / future house hook", bpm=128, half_time=False, keys=[('G', 'major'), ('E', 'major')],
                     progs={"major": [["vi", "IV", "I", "V"], ["I", "V", "vi", "IV"]]}, rhythms=["hook8", "tresillo", "anticip"],
                     forms=["hook", "call"], color=False, appogg=0.15, lo=64, hi=83, voice="lead", drums="four",
                     sync=(0.2, 0.5)),
    "pop-ballad": dict(label="Pop ballad", bpm=80, half_time=False, keys=[('C', 'major'), ('Eb', 'major')],
                       progs={"major": [["I", "V", "vi", "IV"], ["I", "vi", "IV", "V"]]}, rhythms=["ballad", "lyrical", "breath"],
                       forms=["period", "sentence", "aaba"], color=False, appogg=0.4, lo=60, hi=77, voice="piano",
                       drums=None, sync=(0.0, 0.3)),
    "melodic-trap": dict(label="Melodic trap (Trap Nation)", bpm=140, half_time=True, keys=[('C#', 'minor'), ('F', 'minor')],
                         progs={"minor": [["i", "VI", "III", "VII"], ["VI", "III", "VII", "i"]]}, rhythms=["offbeat", "breath", "dotted"],
                         forms=["call", "sentence", "period"], color=False, appogg=0.25, lo=61, hi=80, voice="lead",
                         drums="half", sync=(0.2, 0.55)),
    "cinematic": dict(label="Cinematic, borrowed and suspended", bpm=88, half_time=False, keys=[('D', 'minor'), ('Bb', 'major')],
                      progs={"minor": [["i", "VI", "III", "VII"], ["i", "iv", "VI", "V"]], "major": [["I", "iii", "IV", "iv"]]}, rhythms=["long", "lyrical", "sparse"],
                      forms=["period", "climb"], color=False, appogg=0.5, lo=60, hi=79, voice="piano", drums=None,
                      sync=(0.0, 0.25)),
    "kawaii": dict(label="Kawaii / bright future bass plucks", bpm=150, half_time=True, keys=[('E', 'major'), ('Ab', 'major')],
                   progs={"major": [["IVmaj7", "V", "iii7", "vi"], ["I", "V", "vi", "IVadd9"]]}, rhythms=["pluck16", "hook8"],
                   forms=["hook", "call"], color=True, appogg=0.1, lo=66, hi=86, voice="pluck", drums="half",
                   sync=(0.1, 0.45)),
    "lofi": dict(label="Lo-fi / jazzy sevenths", bpm=84, half_time=False, keys=[('F', 'major'), ('A', 'major')],
                 progs={"major": [["ii7", "V7", "Imaj7", "vi7"], ["IVmaj7", "iii7", "ii7", "V7"]]}, rhythms=["breath", "offbeat", "sparse"],
                 forms=["call", "aaba", "period"], color=True, appogg=0.4, lo=60, hi=79, voice="piano", drums="lofi",
                 sync=(0.15, 0.5)),
}

# ── phrase forms: eight bars of operations on the motif ─────────────────────────────────────────────────────────────
# M: the motif, its first note `s` scale steps from the anchor; V: varied (its end moved); F: its first half twice;
# I: inverted; C: a contrasting line to a target; K: a cadence ('half' ends on 2/5/7, 'full' on 1, 'open' on a chord tone)
FORMS = {
    "period":   [('M', 0), ('M', 1), ('V', 2), ('K', 'half'), ('M', 0), ('M', 1), ('C', 'peak'), ('K', 'full')],
    "sentence": [('M', 0), ('M', -1), ('F', 1), ('K', 'half'), ('M', 0), ('M', -1), ('F', 2), ('K', 'full')],
    "aaba":     [('M', 0), ('V', 0), ('M', 0), ('V', 1), ('C', 'peak'), ('C', 'down'), ('M', 0), ('K', 'full')],
    "hook":     [('M', 0), ('V', 0), ('M', 0), ('V', 0), ('M', 0), ('V', 0), ('M', 1), ('K', 'open')],
    "call":     [('M', 0), ('I', 0), ('M', 1), ('I', 1), ('M', 2), ('I', 2), ('C', 'peak'), ('K', 'full')],
    "climb":    [('M', 0), ('M', 1), ('M', 2), ('M', 3), ('V', 3), ('C', 'peak'), ('C', 'down'), ('K', 'full')],
}
FORM_NAMES = {"period": "period — a question (half cadence) and its answer", "sentence": "sentence — idea, idea moved, "
              "fragments, cadence", "aaba": "AABA — a line, again, a contrast, home", "hook": "hook loop — two bars "
              "four times, lifted at the end", "call": "call and response — the motif and its mirror", "climb":
              "climbing sequence — the motif a step higher each bar, then the fall"}


ARCHETYPES = {          # the shapes a motif can take, in scale steps (a motif is ONE recognisable gesture)
    "rise":  [[0, 1, 2, 4], [0, 1, 2, 3], [0, 2, 4, 3], [0, 1, 3, 4], [-2, 0, 1, 2]],
    "fall":  [[4, 3, 2, 0], [2, 1, 0, -1], [4, 2, 1, 0], [3, 2, 0, 1]],
    "arch":  [[0, 2, 3, 1], [0, 1, 3, 2], [0, 2, 4, 2], [0, 3, 2, 1]],
    "turn":  [[0, 1, 0, -1], [0, -1, 0, 2], [0, 1, -1, 0], [2, 1, 2, 0]],
    "leap":  [[0, 4, 3, 2], [0, -3, -2, -1], [0, 5, 4, 2], [0, 3, 2, 0]],
}


def motif_shape(n, rng, rising_bias=0.0, allow_repeat=False):
    """A motif for n notes: an ARCHETYPE (rise, fall, arch, turn, leap-and-fill) stretched or ornamented to n notes.
    No note repeats the one before it unless the style uses repeated notes as rhythm (a hook); a leap is filled by
    steps the other way; the span stays within a sixth; at least three different pitches."""
    for _ in range(200):
        kind = rng.choices(list(ARCHETYPES), weights=[3 + 4 * rising_bias, 2, 3, 2, 2])[0]
        base = list(rng.choice(ARCHETYPES[kind]))
        shape = base[:]
        while len(shape) < n:                                   # ornament: a neighbour, a passing tone, a step on
            k = rng.randrange(len(shape))
            here = shape[k]
            nxt = shape[k + 1] if k + 1 < len(shape) else here + rng.choice([-1, 1])
            if abs(nxt - here) >= 2:
                ins = here + (1 if nxt > here else -1)          # a passing tone fills the gap
            else:
                ins = here + rng.choice([-1, 1])                # a neighbour
            shape.insert(k + 1, ins)
        shape = shape[:n]
        reps = sum(1 for a, b in zip(shape, shape[1:]) if a == b)
        if reps and not (allow_repeat and reps <= 2):
            continue
        span = max(shape) - min(shape)
        if 2 <= span <= 5 and len(set(shape)) >= min(3, n):
            return [x - shape[0] for x in shape], kind
    return [0, 1, 2, 1][:n] + [0] * max(0, n - 4), "rise"


def build(style, rng, taste=None):
    """One candidate: a key, a progression, a motif DEVELOPED through a form and ADAPTED to each bar's chord."""
    st = STYLES[style]
    tonic, mode = rng.choice(st["keys"])
    key = Key(tonic, mode)
    prog4 = rng.choice(st["progs"][mode])                    # a progression belongs to its mode
    prog = [chord(r, key) for r in prog4 * 2]                  # eight bars, a chord a bar
    rname = rng.choice(st["rhythms"])
    cells = RHYTHMS[rname]
    form = rng.choice(st["forms"])
    hooky = form == "hook"
    shape, kind = motif_shape(len(cells), rng, rising_bias=0.3 if form in ("climb", "call") else 0.0, allow_repeat=hooky)
    lo, hi = st["lo"], st["hi"]
    mid = (lo + hi) // 2 - 3

    def strong(a, l):
        return a in (0, 2) or l >= 1

    def cts(ch, a=lo, b=hi):
        return [m for m in range(a, b + 1) if m % 12 in ch["pcs"]]

    def nearest_ct(midi, ch, direction=0):
        cands = cts(ch)
        if direction:
            cands = [m for m in cands if (m - midi) * direction > 0] or cands
        return min(cands, key=lambda m: (abs(m - midi), m)) if cands else midi

    def realise(cells_b, degs, t0):
        return [[t0 + a, l, key.midi(d)] for (a, l), d in zip(cells_b, degs)]

    # the anchor: the motif's first note on the 3rd or 5th of bar 1 (a root start is bland), mid-range
    first = [m for m in cts(prog[0], mid - 5, mid + 5) if m % 12 != prog[0]["root"]] or cts(prog[0], mid - 5, mid + 5)
    start = rng.choice(first)
    anchor_d, _ = key.degree_of(start)
    others = [r for r in st["rhythms"] if r != rname] or [r for r in RHYTHMS if r != rname]
    contrast_cells = RHYTHMS[rng.choice(others)]
    notes, devices, plan = [], [], FORMS[form]
    prev_last = None
    for b, (op, arg) in enumerate(plan):
        ch = prog[b]
        t0 = 4 * b
        if op in ('M', 'V', 'I', 'F'):
            sh, cells_b = shape, cells
            if op == 'I':
                sh = [-x for x in shape]
            if op == 'F':
                half = max(1, len(cells) // 2)
                cells_b = cells[:half] + [(a + 2, l) for a, l in cells[:half] if a + 2 < 4]
                sh = (shape[:half] + [x + 1 for x in shape[:half]])[:len(cells_b)]
            if op == 'V':
                sh = shape[:-1] + [shape[-1] + rng.choice([-2, -1, 1, 2])]
            # ADAPTATION: the transposition (within two steps of the plan) that puts the strong notes on this chord,
            # joins the last bar smoothly and keeps the phrase's register — a tonal sequence, as a composer bends one
            best, best_cost = None, 1e9
            for k in range(arg - 2, arg + 3):
                bar = realise(cells_b, [anchor_d + k + x for x in sh], t0)
                if any(not (lo <= n[2] <= hi) for n in bar):
                    continue
                miss = sum(1 for n in bar if strong(n[0] - t0, n[1]) and n[2] % 12 not in ch["pcs"])
                root_down = 1 if bar[0][2] % 12 == ch["root"] and b not in (0, 7) else 0
                join = abs(bar[0][2] - prev_last) if prev_last else 0
                cost = 3 * miss + 1.0 * abs(k - arg) + 0.35 * join + 0.5 * root_down
                if prev_last is not None and bar[0][2] == prev_last:
                    cost += 1.5                                     # do not restart on the note just held
                if cost < best_cost:
                    best, best_cost = bar, cost
            bar = best or realise(cells_b, [anchor_d + arg + x for x in sh], t0)
        elif op == 'C':
            # CONTRAST: another rhythm, a stepwise line toward a target, chord tones on the strong beats
            goal = hi - rng.choice([1, 2, 3]) if arg == 'peak' else mid - 3
            cur = prev_last if prev_last else start
            bar = []
            n = len(contrast_cells)
            for k, (a, l) in enumerate(contrast_cells):
                aim = cur + (goal - cur) / max(1, n - k)
                if strong(a, l):
                    m = nearest_ct(int(round(aim)), ch, 1 if goal > cur else -1)
                else:
                    d0, _ = key.degree_of(int(round(aim)))
                    m = key.midi(d0 + (1 if goal > cur else 0))
                if m == cur:
                    d0, _ = key.degree_of(m)
                    m = key.midi(d0 + (1 if goal >= cur else -1))
                bar.append([t0 + a, l, max(lo, min(hi, m))])
                cur = bar[-1][2]
        else:
            # CADENCE: the motif's opening, a step into the last note, held. 'full' ends home — on the tonic when
            # this chord holds it, else on its most stable tone; 'half' on 2, 5 or 7 (a question); 'open' loops
            head = [c for c in cells if c[0] < 2][:2] or [(0, 1)]
            prev = prev_last if prev_last else start
            if arg == 'full':
                pcs = [key.tonic] if key.tonic in ch["pcs"] else [p for p in ch["pcs"] if ch["roles"][p] in ("third", "root")]
                devices.append("ends home on the tonic" if key.tonic in ch["pcs"] else f"ends on {ch['name']}'s "
                               f"{ch['roles'][pcs[0]]} (the loop goes home)")
            elif arg == 'half':
                pcs = [(key.tonic + key.scale[k]) % 12 for k in (1, 4, 6)]
                pcs = [p for p in pcs if p in ch["pcs"]] or [p for p in ch["pcs"] if ch["roles"][p] != "root"]
                devices.append("half cadence — a question")
            else:
                pcs = [p for p in ch["pcs"] if ch["roles"][p] in ("third", "fifth")] or ch["pcs"]
                devices.append("open ending — it loops")
            ends = [m for m in range(lo, hi + 1) if m % 12 in pcs]
            end = min(ends, key=lambda m: (abs(m - prev), m)) if ends else nearest_ct(prev, ch)
            d_end, _ = key.degree_of(end)
            from_above = rng.random() < 0.6
            appr = key.midi(d_end + (1 if from_above else -1))
            bar = []
            if len(head) > 1:
                d0 = d_end + (2 if from_above else -2)
                bar.append([t0 + head[0][0], head[0][1], key.midi(d0)])
            h = head[-1]
            bar.append([t0 + h[0], h[1], appr])
            at_ = max(h[0] + h[1], 1.0)
            bar.append([t0 + at_, 4 - at_, end])
        notes += bar
        prev_last = notes[-1][2]

    # fit what the adaptation left: a strong non-chord tone moves to a chord tone — unless it is an APPOGGIATURA
    # (the next note a step away and a chord tone), kept at the style's rate; a LEADING TONE resolves up
    for i, n in enumerate(notes):
        b = min(int(n[0] // 4), 7)
        ch = prog[b]
        if strong(n[0] - 4 * b, n[1]) and n[2] % 12 not in ch["pcs"]:
            nxt = notes[i + 1] if i + 1 < len(notes) else None
            is_app = nxt and int(nxt[0] // 4) == b and abs(nxt[2] - n[2]) <= 2 and nxt[2] % 12 in ch["pcs"]
            if is_app and rng.random() < st["appogg"]:
                if "appoggiatura" not in devices:
                    devices.append("appoggiatura")
            else:
                n[2] = nearest_ct(n[2], ch)
    # one climax: equal tops after the longest come down a step
    top = max(n[2] for n in notes)
    tops = [i for i, n in enumerate(notes) if n[2] == top]
    keep = max(tops, key=lambda i: (notes[i][1], i))
    for i in tops:
        if i != keep:
            d, _ = key.degree_of(notes[i][2])
            notes[i][2] = key.midi(d - 1)
    devices.insert(0, f"motif: a {kind}, {rname} rhythm")
    devices += {"period": ["the antecedent repeated as the consequent's start"],
                "sentence": ["idea, idea moved, fragmentation"],
                "call": ["inversion — the response mirrors the call"],
                "climb": ["rising sequence to the climax"],
                "hook": ["two-bar hook, repeated — the last time lifted"],
                "aaba": ["contrast (B) before the return"]}[form]
    mel = {
        "style": style, "style_label": st["label"], "bpm": st["bpm"], "key": key.name, "tonic": key.tonic, "mode": mode,
        "key_names": key.names, "progression": prog4, "chords": [c["name"] for c in prog],
        "chord_pcs": [c["pcs"] for c in prog], "chord_roots": [c["root"] for c in prog],
        "chord_roles": [{str(k): v for k, v in c["roles"].items()} for c in prog],
        "rhythm": rname, "form": form, "form_label": FORM_NAMES[form], "motif_steps": shape, "motif_kind": kind,
        "notes": [{"t": round(t, 4), "d": round(d, 4), "p": int(p), "name": key.spell(int(p))} for t, d, p in sorted(notes)],
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
    }


def bell(x, centre, width):
    """1 at the centre, e^(−½) a width away: a preference, not a gate — so candidates can be ranked."""
    return math.exp(-0.5 * ((x - centre) / width) ** 2)


def score(mel, st, taste=None):
    """The rubric, each part 0 … 1 (references/theory.md says why each one matters)."""
    m = mel["metrics"]
    hook = mel["form"] == "hook"
    parts = {
        "chord_fit": bell(m["strong_chord_tones"] + 0.5 * m["appoggiaturas"] / 8, 1.0, 0.12),
        "unresolved": 0.6 ** m["strong_unresolved"],
        "steps": bell(m["steps"], 0.62, 0.15),
        "repeats": bell(m["repeats"], 0.12 if hook else 0.03, 0.12 if hook else 0.07),
        "leaps": m["leaps_recovered"] * 0.7 ** m["same_direction_leaps"] * 0.6 ** m["big_leaps"] * 0.75 ** m["tritones"],
        "range": bell(m["range"], 12, 3.5),
        "variety": bell(m["pitches"], 9, 3),
        "colour": bell(m["downbeat_colour"], 0.8, 0.25),
        "climax": (1.0 if m["climax_single"] else 0.6) * bell(m["climax_at"], 0.72, 0.14),
        "arc": bell(m["rise_to_climax"], 5, 3) if mel["form"] != "hook" else 1.0,
        "tendency": m["leading_tones_resolved"],
        "cadence": bell(m["end_length"], 2.5, 1.2),
        "rhythm": bell(m["bar_rhythms"], 3, 1.2),
        "syncopation": bell(m["syncopation"], sum(st["sync"]) / 2, (st["sync"][1] - st["sync"][0]) / 2 + 0.05),
        "motion": bell(m["turns"], 0.45, 0.2),
    }
    total = sum(parts.values()) / len(parts)
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
    for metric, rng_ in taste.get("metric_ranges", {}).items():
        v = mel["metrics"].get(metric)
        if v is not None:
            b += 0.05 if rng_[0] <= v <= rng_[1] else -0.05
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
        sig = (c["form"], tuple(c["motif_steps"]), c["rhythm"])
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
