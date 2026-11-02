#!/usr/bin/env python3
"""exam.py — the skill's EXAM: one edit per photo, decided by the skill alone, shown beside the original
with the reasoning under it; the approver marks each good or not. 90 % good = ready for real shoots.

    python3 exam.py build --project ~/photo/grade-lab/exam/exam.cmp --plan ~/photo/grade-lab/exam/plan.json
                    [--names a.RAF,b.RAF] [--out ~/photo/grade-lab/exam]
    # then open http://127.0.0.1:8789/exam (lab.py serve) — verdicts go to data/judgments.jsonl, kind "exam"
    python3 exam.py score                                  # the result so far

The approver (2026-11-02): *"i will test your knowledge by each photo, you generate me 1 style only that is
cinematic, clean, have good depth and have suitable mood, i will check if the edit is good or not, if i can
see over 90% of the photos are good, that mean the skill is ready to edit in real life, write the summary of
your thinking under photo so that i can check where you get the idea for grading and i need to see if you
judge the photo right, put original photo too"*.

The edit is `edit.py run` with the default taste — exactly what a real shoot gets; nothing is tuned for the
exam. The reasoning is built from what the edit RECORDED (edit.json: the camera data, the measurement as shot,
the scene and why, every correction step, the look and the verdicts behind it, the local masks, the matching,
the depth and clipping checks) plus the reading the agent wrote LOOKING at the photo (plan.json `reading`).
"""
import argparse, json, os, re, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

DEFAULT_OUT = os.path.expanduser("~/photo/grade-lab/exam")
JUDGMENTS = HERE.parent / "data" / "judgments.jsonl"
REMOVED = ["_DSF6778.RAF", "_DSF7014.RAF"]


def _f(x, n=2):
    return ("%+." + str(n) + "f") % x


def light_words(row):
    m, b = row.get("meta") or {}, row.get("before") or {}
    parts = []
    taken = (m.get("Taken") or "")[11:16]
    cam = ", ".join(x for x in (taken and "taken %s" % taken, m.get("ISO"), m.get("Shutter"), m.get("Aperture"),
                                m.get("Focal length")) if x)
    if cam:
        parts.append(cam)
    p50 = b.get("lumPct", [0, 0, 0, 0])[3] if b else 0
    key = "very dark" if p50 < 0.08 else "dark" if p50 < 0.2 else "mid-key" if p50 < 0.55 else "bright"
    w, t = b.get("castWarmth", 0), b.get("castTint", 0)
    cast = []
    if abs(w) >= 0.1:
        cast.append("warm" if w > 0 else "cool")
    if abs(t) >= 0.1:
        cast.append("green" if t > 0 else "magenta")
    parts.append("as shot: %s (median %.2f), %s cast" % (key, p50, " and ".join(cast) if cast else "no strong"))
    hints = [n for n, k in (("sky", "skyFraction"), ("foliage", "foliageFraction"), ("skin-tone", "skinFraction"),
                            ("neon / point lights", "neonFraction")) if b.get(k, 0) >= 0.05 or (k == "neonFraction" and b.get(k, 0) >= 0.005)]
    if hints:
        parts.append("contains " + ", ".join(hints))
    return "; ".join(parts) + "."


def explain(row, pe):
    """Every step the edit took, in words a photographer reads — from the edit's own log, not re-imagined."""
    out = []
    why = "; ".join(row.get("why") or [])
    out.append("**Scene: %s** — %s" % (row["scene"], why or "classified from the light"))
    for l in row.get("log", []):
        m = re.match(r"wb temp=(\d+) tint=(-?[\d.]+) \(trust ([\d.]+)\) -> warmth ([-+][\d.]+) tint ([-+][\d.]+)", l)
        if m:
            trust = float(m.group(3))
            src = {1.0: "a white / grey sky or bright neutral surface, trusted fully",
                   0.5: "the near-neutral pixels, trusted half-way",
                   0.3: "a pale BLUE sky — trusted only a third, so the sky stays blue",
                   0.25: "a frame full of foliage — trusted only a quarter, leaves are not a grey card"}.get(trust, "trust %.2f" % trust)
            out.append("**White balance**: judged from %s → %s K, tint %s; the light now reads warmth %s / tint %s "
                       "(target: slightly warm, a hair green — a dead-grey sky looks lilac)." % (
                           src, m.group(1), _f(float(m.group(2)), 0), m.group(4), m.group(5)))
            continue
        m = re.match(r"exposure ([-+][\d.]+) highlights (-?\d+) shadows (-?\d+)( \(held by the highlights\))? -> p50 ([\d.]+) clip ([\d.]+)", l)
        if m:
            held = " — held back by the highlights (the brightest part would have clipped)" if m.group(4) else ""
            out.append("**Exposure**: %s EV, highlights %s%s%s → median %s." % (
                m.group(1), m.group(2), (", shadows +" + m.group(3)) if m.group(3) != "0" else "", held, m.group(5)))
            continue
        m = re.match(r"look (\S+) (\d+)%(.*?) — (.*)", l)
        if m:
            extras = m.group(3).replace("+skin", "skin line protected").replace("+", "").strip()
            out.append("**Look: %s at %s%% colour (full depth)%s** — %s." % (
                m.group(1).replace("-", " ").title(), m.group(2), (", " + extras) if extras else "", m.group(4)))
            continue
        m = re.match(r"(shadows|highlights) (toe|shoulder) ([-+][\d.]+)/([-+][\d.]+) -> w ([-+][\d.]+) t ([-+][\d.]+) \(set ([-+][\d.]+) ([-+][\d.]+)\)", l)
        if m:
            out.append("**Matched to the set** (%s): the %s colour nudged toward the set's (warmth %s, tint %s) → now %s / %s." % (
                m.group(1), m.group(1), m.group(7), m.group(8), m.group(5), m.group(6)))
            continue
        m = re.match(r"saturation ([-+]\d+) -> ([\d.]+) \(set ([\d.]+)\)", l)
        if m:
            out.append("**Saturation matched**: %s → %s (the set sits at %s)." % (m.group(1), m.group(2), m.group(3)))
            continue
        m = re.match(r"depth toe ([-+][\d.]+) shoulder ([-+][\d.]+) -> p1 ([\d.]+) p99 ([\d.]+)", l)
        if m:
            out.append("**Depth check** (cinematic = full range): blacks pulled %s, highlights lifted %s on the curve → "
                       "darkest 1 %% at %s, brightest 1 %% at %s." % (m.group(1), m.group(2), m.group(3), m.group(4)))
            continue
        m = re.match(r"highlights -> (-?[\d.]+)", l)
        if m:
            out.append("**Clipping guard**: highlights brought to %s so no light source burns." % m.group(1))
    masks = pe.get("masks", [])
    if masks or pe.get("intent"):
        desc = []
        for mk in masks:
            adj = ", ".join("%s %s" % (k, ("%+g" % v) if isinstance(v, (int, float)) else v) for k, v in mk.items()
                            if k in ("exposure", "contrast", "highlights", "shadows", "saturation", "texture", "clarity", "dehaze", "temp", "tint"))
            if mk.get("type") == "linear":
                desc.append("gradient from (%.2f, %.2f) to (%.2f, %.2f): %s" % (mk["x0"], mk["y0"], mk["x1"], mk["y1"], adj))
            else:
                desc.append("radial at (%.2f, %.2f), %.2f × %.2f: %s" % (mk["cx"], mk["cy"], mk["rx"], mk["ry"], adj))
        out.append("**Local**: %s%s" % (pe.get("intent", ""), (" — " + "; ".join(desc)) if desc else ""))
    a = row.get("after") or {}
    if a:
        out.append("**Result**: darkest 1 %% %.3f · median %.2f · brightest 1 %% %.3f · clipped %.1f %% · saturation %.2f." % (
            a["lumPct"][0], a["lumPct"][3], a["lumPct"][6], 100 * a["clipHighlights"], a["meanSaturation"]))
    return out


def build(args):
    import edit as E
    out = Path(args.out).expanduser()
    names = args.names or ",".join(n for n in json.loads(Path(args.plan).expanduser().read_text())["photos"]
                                   if n not in REMOVED)
    ns = argparse.Namespace(project=str(Path(args.project).expanduser()), plan=str(Path(args.plan).expanduser()),
                            out=str(out), mood="", names="", strength=1.0, match=0.7, save=True, save_as="")
    E.cmd_run(ns)
    ed = json.loads((out / "edit.json").read_text())
    plan = json.loads(Path(args.plan).expanduser().read_text())["photos"]
    keep = set(names.split(","))
    prev = json.loads((out / "exam.json").read_text()) if (out / "exam.json").exists() else {"photos": []}
    verdicts = {p["name"]: p.get("verdict") for p in prev.get("photos", []) if p.get("verdict")}
    photos = []
    for row in ed["photos"]:
        if row["name"] not in keep:
            continue
        pe = plan.get(row["name"], {})
        stem = Path(row["name"]).stem
        photos.append({"name": row["name"], "stem": stem, "before": "before/%s.jpg" % stem, "after": "after/%s.jpg" % stem,
                       "reading": row.get("reading") or pe.get("reading", ""), "light": light_words(row),
                       "steps": explain(row, pe), "verdict": None})
    order = list(plan)
    photos.sort(key=lambda p: order.index(p["name"]) if p["name"] in order else 999)
    (out / "exam.json").write_text(json.dumps({"built": __import__("time").strftime("%Y-%m-%dT%H:%M:%S"),
                                               "photos": photos, "previous_verdicts": verdicts}, indent=1))
    print("exam ->", out / "exam.json", "(%d photos) — open http://127.0.0.1:8789/exam" % len(photos))


def score(args):
    ex = json.loads((Path(args.out).expanduser() / "exam.json").read_text())
    js = [p for p in ex["photos"] if p.get("verdict")]
    good = sum(1 for p in js if p["verdict"]["good"])
    print("%d of %d judged, %d good = %.0f %% (target 90 %%)" % (len(js), len(ex["photos"]), good,
                                                                100.0 * good / max(1, len(js))))
    for p in js:
        print("  %-18s %-8s %s" % (p["name"], "good" if p["verdict"]["good"] else "NOT GOOD", p["verdict"].get("note", "")))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("build", "score"))
    ap.add_argument("--project", default=os.path.join(DEFAULT_OUT, "exam.cmp"))
    ap.add_argument("--plan", default=os.path.join(DEFAULT_OUT, "plan.json"))
    ap.add_argument("--names", default="")
    ap.add_argument("--out", default=DEFAULT_OUT)
    args = ap.parse_args()
    {"build": build, "score": score}[args.cmd](args)


if __name__ == "__main__":
    main()
