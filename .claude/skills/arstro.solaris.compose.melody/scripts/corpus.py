#!/usr/bin/env python3
"""corpus.py — learn from professional melodies: the lead lines of POP909 (909 professionally written pop songs, MIT;
Wang et al., ISMIR 2020 — https://github.com/music-x-lab/POP909-Dataset), measured against their own beats, chords
and keys. Only AGGREGATE statistics are kept (data/corpus_stats.json) — how real melodies move, never a melody.

    git clone --depth 1 https://github.com/music-x-lab/POP909-Dataset.git <dir>      # once, into a scratch folder
    python3 -I corpus.py <dir>/POP909 [--out ../data/corpus_stats.json]

What it measures (references/research.md says why each matters):
  intervals        the semitone steps between successive notes inside a phrase, and what follows each kind
                   (after a skip, how often the line turns back — post-skip reversal); a smoothed interval bigram for
                   the generator's "expectedness" score (statistical expectation: Pearce & Wiggins 2006)
  harmony          chord tones on strong beats and on weak ones; what a strong note is to its chord (root, 3rd, …)
  rhythm           notes per bar, note lengths in beats, the most common one-bar rhythms (16th grid) and where rests fall
  phrase           range in 8 bars, how often a 2-bar unit returns exactly, where the 4-bar phrase's high point is
"""
import argparse, collections, json, math, os, struct, sys

ROOTS = {'C': 0, 'C#': 1, 'Db': 1, 'D': 2, 'D#': 3, 'Eb': 3, 'E': 4, 'Fb': 4, 'E#': 5, 'F': 5, 'F#': 6, 'Gb': 6, 'G': 7,
         'G#': 8, 'Ab': 8, 'A': 9, 'A#': 10, 'Bb': 10, 'B': 11, 'Cb': 11, 'B#': 0}
QUAL = {'maj': (0, 4, 7), 'min': (0, 3, 7), 'dim': (0, 3, 6), 'aug': (0, 4, 8), 'sus4': (0, 5, 7), 'sus2': (0, 2, 7),
        '7': (0, 4, 7, 10), 'maj7': (0, 4, 7, 11), 'min7': (0, 3, 7, 10), 'hdim7': (0, 3, 6, 10), 'dim7': (0, 3, 6, 9),
        'maj6': (0, 4, 7, 9), 'min6': (0, 3, 7, 9), '9': (0, 4, 7, 10, 2), 'maj9': (0, 4, 7, 11, 2), 'min9': (0, 3, 7, 10, 2),
        'minmaj7': (0, 3, 7, 11), '11': (0, 4, 7, 10, 5), '13': (0, 4, 7, 10, 9), 'sus4(b7)': (0, 5, 7, 10), '7(b9)': (0, 4, 7, 10, 1)}


# ── a minimal standard MIDI file reader ─────────────────────────────────────────────────────────────────────────────
def varlen(b, i):
    v = 0
    while True:
        c = b[i]
        i += 1
        v = (v << 7) | (c & 0x7F)
        if c < 0x80:
            return v, i


def read_midi(path):
    """→ (division, tempo map [(tick, µs per beat)], {track name: [(start tick, end tick, pitch)]})."""
    b = open(path, "rb").read()
    if b[:4] != b"MThd":
        raise ValueError("not a MIDI file")
    _, ntrk, div = struct.unpack(">HHH", b[8:14])
    i = 8 + struct.unpack(">I", b[4:8])[0]
    tempos, tracks = [], {}
    for t in range(ntrk):
        if b[i:i + 4] != b"MTrk":
            break
        ln = struct.unpack(">I", b[i + 4:i + 8])[0]
        j, end = i + 8, i + 8 + ln
        tick, status, name, on, notes = 0, 0, f"track{t}", {}, []
        while j < end:
            d, j = varlen(b, j)
            tick += d
            c = b[j]
            if c == 0xFF:
                typ = b[j + 1]
                n, j = varlen(b, j + 2)
                data = b[j:j + n]
                j += n
                if typ == 0x03:
                    name = data.decode("latin-1").strip()
                elif typ == 0x51 and n == 3:
                    tempos.append((tick, (data[0] << 16) | (data[1] << 8) | data[2]))
                continue
            if c in (0xF0, 0xF7):
                n, j = varlen(b, j + 1)
                j += n
                continue
            if c & 0x80:
                status = c
                j += 1
            kind = status & 0xF0
            if kind in (0x80, 0x90):
                p, v = b[j], b[j + 1]
                j += 2
                if kind == 0x90 and v > 0:
                    on.setdefault(p, []).append(tick)
                elif on.get(p):
                    notes.append((on[p].pop(0), tick, p))
            elif kind in (0xA0, 0xB0, 0xE0):
                j += 2
            elif kind in (0xC0, 0xD0):
                j += 1
        tracks[name] = sorted(notes)
        i = end
    return div, sorted(tempos) or [(0, 500000)], tracks


def tick_seconds(div, tempos):
    """A function ticks → seconds through the tempo map."""
    pts = []
    sec, last_tick, last_tempo = 0.0, 0, tempos[0][1] if tempos[0][0] == 0 else 500000
    for tk, us in tempos:
        sec += (tk - last_tick) * last_tempo / 1e6 / div
        pts.append((tk, sec, us))
        last_tick, last_tempo = tk, us

    def f(t):
        k = max((p for p in pts if p[0] <= t), key=lambda p: p[0], default=(0, 0.0, 500000))
        return k[1] + (t - k[0]) * k[2] / 1e6 / div
    return f


# ── per song ────────────────────────────────────────────────────────────────────────────────────────────────────────
def song(folder):
    name = os.path.basename(folder)
    div, tempos, tracks = read_midi(os.path.join(folder, f"{name}.mid"))
    mel = tracks.get("MELODY")
    if not mel:
        return None
    sec = tick_seconds(div, tempos)
    beats = []
    for line in open(os.path.join(folder, "beat_midi.txt")):
        p = line.split()
        if len(p) >= 3:
            beats.append((float(p[0]), float(p[1]), float(p[2])))
    if len(beats) < 16:
        return None
    times = [b[0] for b in beats]
    down = [k for k, b in enumerate(beats) if b[2] == 1.0]
    gaps = collections.Counter(b - a for a, b in zip(down, down[1:]))
    if not gaps or gaps.most_common(1)[0][0] != 4:
        return None                                              # 4/4 only, so a bar is 4 beats

    def beat_of(s):
        """Seconds → a beat count (fractional), by the song's own beats."""
        if s <= times[0]:
            return (s - times[0]) / (times[1] - times[0])
        lo, hi = 0, len(times) - 1
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if times[mid] <= s:
                lo = mid
            else:
                hi = mid
        span = times[hi] - times[lo] or 1e-9
        return lo + (s - times[lo]) / span

    first_down = down[0]
    chords = []
    for line in open(os.path.join(folder, "chord_midi.txt")):
        p = line.split()
        if len(p) >= 3 and p[2] != "N" and ":" in p[2]:
            root, q = p[2].split(":", 1)
            q = q.split("/")[0]
            if root in ROOTS and q in QUAL:
                r = ROOTS[root]
                chords.append((beat_of(float(p[0])), beat_of(float(p[1])), r, {(r + x) % 12 for x in QUAL[q]}))
    key = None
    kpath = os.path.join(folder, "key_audio.txt")
    if os.path.exists(kpath):
        p = open(kpath).readline().split()
        if len(p) >= 3 and ":" in p[2]:
            root, mode = p[2].split(":")
            if root in ROOTS:
                key = (ROOTS[root], "major" if mode.startswith("maj") else "minor")
    notes = []
    for a, b_, p in mel:
        s, e = beat_of(sec(a)) - first_down, beat_of(sec(b_)) - first_down
        s = round(s * 4) / 4                                     # the 16th grid
        e = max(s + 0.25, round(e * 4) / 4)
        notes.append((s, e - s, p))
    return {"notes": sorted(notes), "chords": [(a - first_down, b - first_down, r, pcs) for a, b, r, pcs in chords], "key": key}


def chord_at(chords, t):
    for a, b, r, pcs in chords:
        if a - 1e-6 <= t < b:
            return r, pcs
    return None, None


# ── the measures ────────────────────────────────────────────────────────────────────────────────────────────────────
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pop909")
    ap.add_argument("--out", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "corpus_stats.json"))
    a = ap.parse_args()
    folders = sorted(os.path.join(a.pop909, d) for d in os.listdir(a.pop909) if d.isdigit())
    iv = collections.Counter()
    after = {"step": collections.Counter(), "third": collections.Counter(), "skip": collections.Counter()}
    bigram = collections.defaultdict(collections.Counter)
    strong_ct = strong_n = weak_ct = weak_n = 0
    roles = collections.Counter()
    per_bar, lengths, rhythms, rest_at = [], collections.Counter(), collections.Counter(), collections.Counter()
    ranges, returns, units, peak_pos = [], 0, 0, collections.Counter()
    two_bar = collections.Counter()                              # whole two-bar phrase rhythms, as real songs have them
    seqs = []                                                    # interval sequences, for the bigram's own surprise
    songs = 0
    for f in folders:
        try:
            s = song(f)
        except Exception:
            continue
        if not s or len(s["notes"]) < 32:
            continue
        songs += 1
        ns = s["notes"]
        # intervals inside phrases (a gap of over 1.5 beats ends one)
        prev_iv = None
        for x, y in zip(ns, ns[1:]):
            if y[0] - (x[0] + x[1]) > 1.5:
                prev_iv = None
                continue
            d = y[2] - x[2]
            iv[max(-12, min(12, d))] += 1
            if prev_iv is not None:
                cls = "step" if 1 <= abs(prev_iv) <= 2 else ("third" if 3 <= abs(prev_iv) <= 4 else ("skip" if abs(prev_iv) >= 5 else None))
                if cls:
                    after[cls]["reverse" if d * prev_iv < 0 else ("same" if d * prev_iv > 0 else "repeat")] += 1
                bigram[max(-12, min(12, prev_iv))][max(-12, min(12, d))] += 1
            prev_iv = d
        # harmony
        for t, l, p in ns:
            r, pcs = chord_at(s["chords"], t + 0.01)
            if pcs is None:
                continue
            strong = (t % 4) in (0.0, 2.0) or l >= 1
            if strong:
                strong_n += 1
                if p % 12 in pcs:
                    strong_ct += 1
                    iv_ = (p - r) % 12
                    roles[{0: "root", 3: "third", 4: "third", 7: "fifth", 6: "fifth", 8: "fifth", 10: "seventh", 11: "seventh",
                           9: "sixth", 2: "ninth", 5: "fourth"}.get(iv_, "other")] += 1
                else:
                    roles["non-chord"] += 1
            else:
                weak_n += 1
                weak_ct += p % 12 in pcs
        # rhythm: per bar
        bars = collections.defaultdict(list)
        for t, l, p in ns:
            if t >= 0:
                bars[int(t // 4)].append((round(t % 4, 2), min(l, 4 - t % 4), p))
        last = max(bars) if bars else 0
        for b in range(last + 1):
            cell = bars.get(b, [])
            if cell:
                per_bar.append(len(cell))
                rhythms[tuple((o, round(l, 2)) for o, l, _ in cell)] += 1
        for t, l, p in ns:
            lengths[min(4.0, round(l * 4) / 4)] += 1
        for x, y in zip(ns, ns[1:]):
            g = y[0] - (x[0] + x[1])
            if g >= 0.5:
                rest_at[round((x[0] + x[1]) % 8 * 2) / 2] += 1       # where in a 2-bar unit a rest begins
        # phrase: 8-bar ranges, 2-bar returns, the 4-bar high point
        for b0 in range(0, last - 7, 8):
            ps = [p for t, l, p in ns if 4 * b0 <= t < 4 * b0 + 32]
            if len(ps) >= 8:
                ranges.append(max(ps) - min(ps))
        for b0 in range(0, last, 2):
            u = tuple((round(t - 4 * b0, 2), round(min(l, 8 - (t - 4 * b0)), 2)) for t, l, p in ns if 4 * b0 <= t < 4 * b0 + 8)
            if 3 <= len(u) <= 16:
                two_bar[u] += 1
        seq, last_t = [], None
        for x, y in zip(ns, ns[1:]):
            if y[0] - (x[0] + x[1]) > 1.5:
                if len(seq) >= 8:
                    seqs.append(seq)
                seq = []
                continue
            seq.append(max(-12, min(12, y[2] - x[2])))
        if len(seq) >= 8:
            seqs.append(seq)
        unit = {}
        for b0 in range(0, last, 2):
            u = tuple((round(t - 4 * b0, 2), round(l, 2), p) for t, l, p in ns if 4 * b0 <= t < 4 * b0 + 8)
            if len(u) >= 3:
                units += 1
                if u in unit.values():
                    returns += 1
                unit[b0] = u
        for b0 in range(0, last - 3, 4):
            ph = [(t, p) for t, l, p in ns if 4 * b0 <= t < 4 * b0 + 16]
            if len(ph) >= 6:
                top = max(p for _, p in ph)
                t_top = next(t for t, p in ph if p == top)
                peak_pos[min(3, int((t_top - 4 * b0) // 4))] += 1
    tot_iv = sum(iv.values())

    def share(c, keys):
        n = sum(c.values()) or 1
        return round(sum(c[k] for k in keys) / n, 4)

    stats = {
        "source": "POP909 (Wang et al., ISMIR 2020; MIT) — the MELODY track of each song, its beat_midi.txt and chord_midi.txt",
        "songs": songs,
        "intervals": {
            "repeat": share(iv, [0]), "step": share(iv, [1, 2, -1, -2]), "third": share(iv, [3, 4, -3, -4]),
            "fourth_fifth": share(iv, [5, 6, 7, -5, -6, -7]), "larger": share(iv, [k for k in iv if abs(k) >= 8]),
            "descending_steps_over_ascending": round(share(iv, [-1, -2]) / max(1e-9, share(iv, [1, 2])), 3),
            "histogram": {str(k): round(v / tot_iv, 5) for k, v in sorted(iv.items())},
        },
        "after": {k: {kk: round(vv / max(1, sum(v.values())), 4) for kk, vv in v.items()} for k, v in after.items()},
        "bigram": {str(k): {str(kk): vv for kk, vv in v.items()} for k, v in sorted(bigram.items())},
        "harmony": {"strong_chord_tones": round(strong_ct / max(1, strong_n), 4), "weak_chord_tones": round(weak_ct / max(1, weak_n), 4),
                    "strong_roles": {k: round(v / max(1, sum(roles.values())), 4) for k, v in roles.most_common()}},
        "rhythm": {"notes_per_bar_median": sorted(per_bar)[len(per_bar) // 2] if per_bar else 0,
                   "notes_per_bar_quartiles": [sorted(per_bar)[len(per_bar) // 4], sorted(per_bar)[3 * len(per_bar) // 4]] if per_bar else [],
                   "lengths": {str(k): round(v / max(1, sum(lengths.values())), 4) for k, v in sorted(lengths.items())},
                   "rest_starts_in_2_bars": {str(k): round(v / max(1, sum(rest_at.values())), 4) for k, v in sorted(rest_at.items())},
                   "top_bars": [{"cells": [list(c) for c in k], "share": round(v / max(1, sum(rhythms.values())), 5)}
                                for k, v in rhythms.most_common(60)]},
        "phrase": {"range_8_bars_median": sorted(ranges)[len(ranges) // 2] if ranges else 0,
                   "range_8_bars_quartiles": [sorted(ranges)[len(ranges) // 4], sorted(ranges)[3 * len(ranges) // 4]] if ranges else [],
                   "two_bar_units_that_return_exactly": round(returns / max(1, units), 4),
                   "high_point_bar_of_4": {str(k + 1): round(v / max(1, sum(peak_pos.values())), 4) for k, v in sorted(peak_pos.items())}},
    }
    # the bigram's surprise on the corpus itself: the mean −log2 P(next interval | last), add-one smoothed — the
    # reference a generated melody's own is compared with (too low: predictable; too high: random)
    def surprise(seq):
        bits = []
        for x, y in zip(seq, seq[1:]):
            row = bigram.get(x, {})
            bits.append(-math.log2((row.get(y, 0) + 1) / (sum(row.values()) + 25)))
        return sum(bits) / max(1, len(bits))
    sur = sorted(surprise(q) for q in seqs)
    stats["expectancy"] = {"mean_bits": round(sum(sur) / len(sur), 4), "quartiles": [round(sur[len(sur) // 4], 4), round(sur[3 * len(sur) // 4], 4)],
                           "phrases": len(sur)}
    stats["rhythm"]["top_two_bars"] = [{"cells": [list(c) for c in k], "share": round(v / max(1, sum(two_bar.values())), 6), "count": v}
                                       for k, v in two_bar.most_common(300)]
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w") as f:
        json.dump(stats, f, indent=1)
    s = stats
    print(f"{songs} songs")
    print("intervals", {k: v for k, v in s["intervals"].items() if k != "histogram"})
    print("after a step / third / skip:", s["after"])
    print("harmony", s["harmony"])
    print("rhythm: notes per bar", s["rhythm"]["notes_per_bar_median"], s["rhythm"]["notes_per_bar_quartiles"], "| lengths", s["rhythm"]["lengths"])
    print("rests begin (beat in a 2-bar unit):", s["rhythm"]["rest_starts_in_2_bars"])
    print("top bars:", [(t["cells"], t["share"]) for t in s["rhythm"]["top_bars"][:8]])
    print("phrase", s["phrase"])
    print("expectancy (bits per interval)", s["expectancy"])
    print("two-bar rhythms:", len(s["rhythm"]["top_two_bars"]), "kept; the first:", [t["cells"] for t in s["rhythm"]["top_two_bars"][:3]])


if __name__ == "__main__":
    main()
