#!/usr/bin/env python3
"""taste.py — what the approver's verdicts teach: read data/judgments.jsonl, say what the liked melodies share, and
write data/weights.json, which melody.py adds to its rubric (taste_bonus). The verdicts outrank the rubric — this is
how.

    python3 taste.py            # the summary, and data/weights.json rewritten
    python3 taste.py --dry      # the summary only

What it learns, and how carefully:
  categories (style, form, rhythm, motif kind, voice): the mean rating of each against the mean of all, as a bonus of
    ±0.1 at most, shrunk while there are few verdicts (n / (n + 2)) — one rating is a hint, not a law;
  metrics (steps, range, climax position, syncopation, …): where the liked (4–5) and the disliked (1–2) differ by more
    than half their spread, the liked melodies' 10th–90th percentile range becomes a preferred range.
Only each melody's LATEST verdict counts (a re-judged melody replaces its old verdict).
"""
import argparse, json, os, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = os.path.normpath(os.path.join(HERE, "..", "data"))
METRICS = ("surprise_bits", "steps", "range", "pitches", "repeats", "leaps", "climax_at", "rise_to_climax", "syncopation", "density",
           "downbeat_colour", "strong_chord_tones", "appoggiaturas", "bar_rhythms", "turns")
CATEGORIES = ("style", "form", "rhythm", "motif_kind", "voice")


def load():
    rows = {}
    path = os.path.join(DATA, "judgments.jsonl")
    if not os.path.exists(path):
        return []
    with open(path) as f:
        for line in f:
            try:
                j = json.loads(line)
            except ValueError:
                continue
            rows[(j["batch"], j["id"])] = j
    return list(rows.values())


def pct(xs, q):
    xs = sorted(xs)
    k = (len(xs) - 1) * q
    lo, hi = int(k), min(int(k) + 1, len(xs) - 1)
    return xs[lo] + (xs[hi] - xs[lo]) * (k - lo)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry", action="store_true")
    a = ap.parse_args()
    rows = load()
    songs = [r for r in rows if r["melody"].get("song")]
    rows = [r for r in rows if not r["melody"].get("song")]   # a song's metrics are not an 8-bar line's: judged apart
    for r in songs:
        print(f"song {r['batch']}/{r['id']} {r['melody'].get('title', '')}: {r['verdict']}"
              + (f" — {r['note']}" if r.get("note") else ""))
    if not rows:
        print("no verdicts yet")
        return
    mean = statistics.mean(r["rating"] for r in rows)
    print(f"{len(rows)} verdicts, mean {mean:.2f} — love {sum(r['rating'] == 5 for r in rows)}, good "
          f"{sum(r['rating'] == 4 for r in rows)}, okay {sum(r['rating'] == 3 for r in rows)}, weak "
          f"{sum(r['rating'] == 2 for r in rows)}, no {sum(r['rating'] == 1 for r in rows)}")
    weights = {}
    for cat in CATEGORIES:
        groups = {}
        for r in rows:
            v = r["melody"].get(cat)
            if v is not None:
                groups.setdefault(v, []).append(r["rating"])
        if not groups:
            continue
        weights[cat] = {}
        line = []
        for v, rs in sorted(groups.items(), key=lambda kv: -statistics.mean(kv[1])):
            n = len(rs)
            w = max(-0.1, min(0.1, (statistics.mean(rs) - mean) / 4 * 0.4 * n / (n + 2)))
            weights[cat][v] = round(w, 4)
            line.append(f"{v} {statistics.mean(rs):.1f} (×{n})")
        print(f"  {cat:10} " + " · ".join(line))
    liked = [r for r in rows if r["rating"] >= 4]
    disliked = [r for r in rows if r["rating"] <= 2]
    ranges = {}
    if len(liked) >= 4 and len(disliked) >= 3:
        print("  metrics where the liked and the disliked differ:")
        for m in METRICS:
            lv = [float(r["melody"]["metrics"][m]) for r in liked if m in r["melody"].get("metrics", {})]
            dv = [float(r["melody"]["metrics"][m]) for r in disliked if m in r["melody"].get("metrics", {})]
            if len(lv) < 3 or len(dv) < 2:
                continue
            spread = statistics.pstdev(lv + dv) or 1e-9
            gap = statistics.mean(lv) - statistics.mean(dv)
            if abs(gap) > 0.5 * spread:
                # widened by half the liked melodies' own spread: a handful of verdicts is evidence, not a fence
                pad = 0.5 * (statistics.pstdev(lv) or 0.25 * spread)
                ranges[m] = [round(pct(lv, 0.1) - pad, 3), round(pct(lv, 0.9) + pad, 3)]
                print(f"    {m:18} liked {statistics.mean(lv):.2f} against {statistics.mean(dv):.2f} → prefer "
                      f"{ranges[m][0]} … {ranges[m][1]}")
    else:
        print("  (metric ranges need at least 4 liked and 3 disliked)")
    weights["metric_ranges"] = ranges
    weights["liked_count"] = len(liked)                          # melody.py scales the ranges' pull by it
    notes = [f"{r['id']} ({r['verdict']}): {r['note']}" for r in rows if r.get("note")]
    if notes:
        print("  the approver's words:")
        for n in notes:
            print(f"    {n}")
    if not a.dry:
        with open(os.path.join(DATA, "weights.json"), "w") as f:
            json.dump(weights, f, indent=1)
        print(f"wrote {os.path.join(DATA, 'weights.json')}")


if __name__ == "__main__":
    main()
