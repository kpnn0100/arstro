#!/usr/bin/env python3
"""edit.py — edit EVERY photo on its own: look, plan, correct, mood, local, match. The photo editor.

    python3 edit.py review <project.cmp> --out DIR [--per-sheet 6]
            # as-shot previews + review sheets with a 0.25 grid, for the agent to LOOK at every photo
    python3 edit.py run <project.cmp> --mood spring-haze --plan DIR/plan.json --out DIR [--names a,b] [--save | --save-as P]
            # the whole edit, per photo, then the set matched

The approver (2026-10-30): *"i see you apply color for whole group — that not what i want — i need you to
see the actual image and apply edit for each image so that it become cinematic, look good and have
consistent mood … photographer take 1000 photo, prompt agent to edit, 1000 photo got edit nicely"*.

So no look group: each photo carries its WHOLE edit, and consistency comes from MATCHING, not sharing.
Per photo, in order (SKILL.md §2c):

 1. correct   — autograde.normalise: white balance against this photo's witness, exposure with the
                highlights first, noise by ISO (the scene from the plan if the agent named it).
 2. mood      — the mood's day or night variant (moods.py), written onto the photo itself.
 3. plan      — what the agent decided LOOKING at this photo (plan.json): `set` tweaks (k=v, k+=v,
                k-=v), local masks (a subject lifted, a sky graded down, a light given glow).
 4. match     — after every photo has 1-3, the set's median shadow cast, highlight cast and saturation
                (per day / night family) become the targets, and each photo's OWN R/G/B curve toe and
                shoulder and saturation are nudged toward them — a colourist matching shots to a hero.
                Same mood, different photos: identical settings never give identical results.
 5. guard     — nothing that matters clips.

plan.json (written by the agent; every key optional, a photo with no entry is edited automatically):
    {"mood": "spring-haze",
     "photos": {"_DSF6686.RAF": {
        "scene": "daylight", "subject": "deer grazing, lower right", "intent": "lift the deer, deepen the sky",
        "set": "vibrance+=4 lensVignette-=6",
        "masks": [{"type": "radial", "cx": 0.6, "cy": 0.66, "rx": 0.22, "ry": 0.2, "feather": 0.7,
                   "exposure": 0.2, "clarity": 8},
                  {"type": "linear", "x0": 0.5, "y0": 0.45, "x1": 0.5, "y1": 0.0, "exposure": -0.3,
                   "saturation": 10}],
        "match": true}}}
Mask geometry is 0..1 of the framed photo, y down. A linear mask is 0 at (x0,y0) and full at (x1,y1).
Mask adjustments: exposure (EV), contrast, highlights, shadows, whites, blacks, temp, tint, saturation,
texture, clarity, dehaze (-100..100, temp/tint relative).
"""
import argparse, json, math, statistics, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autograde as A  # noqa: E402
import looks as L  # noqa: E402
import moods as M  # noqa: E402

PHOTO_RESET = A.GROUP_RESET          # every key a correction, a mood or a plan may set
CURVE_X = [0.0, 0.15, 0.3, 0.5, 0.7, 0.85, 1.0]
MASK_KEYS = ("exposure", "contrast", "highlights", "shadows", "whites", "blacks", "temp", "tint",
             "saturation", "texture", "clarity", "dehaze")
CLIP = {"day": 0.004, "night": 0.012}


# ── params as a dict ────────────────────────────────────────────────────────────────────────

def parse(line):
    out = {}
    for tok in (line or "").split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def render(d):
    return " ".join("%s=%s" % (k, v) for k, v in d.items())


def curve_points(v):
    pts = []
    for p in (v or "0,0;1,1").split(";"):
        x, y = (float(a) for a in p.split(","))
        pts.append((x, y))
    return sorted(pts)


def sample(pts, x):
    if x <= pts[0][0]:
        return pts[0][1]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / max(1e-9, x1 - x0)
    return pts[-1][1]


def curve_grid(v):
    """A curve resampled on CURVE_X, so offsets at the toe and the shoulder have points to land on."""
    pts = curve_points(v)
    xs = sorted(set(CURVE_X) | {p[0] for p in pts})
    return [(x, sample(pts, x)) for x in xs]


def curve_str(pts):
    return ";".join("%g,%g" % (x, max(0.0, min(1.0, y))) for x, y in pts)


def toe_w(x):
    return max(0.0, 1.0 - x / 0.35)


def shoulder_w(x):
    return max(0.0, (x - 0.6) / 0.4)


def offset_curve(v, toe=0.0, shoulder=0.0):
    return curve_str([(x, y + toe * toe_w(x) + shoulder * shoulder_w(x)) for x, y in curve_grid(v)])


def apply_ops(d, ops):
    """`k=v` sets, `k+=v` / `k-=v` adjust (temp too). The agent's per-photo tweaks."""
    for tok in (ops or "").split():
        if "+=" in tok or "-=" in tok:
            sign = 1.0 if "+=" in tok else -1.0
            k, v = tok.split("+=" if sign > 0 else "-=", 1)
            base = float(d.get(k, "6500" if k == "temp" else "0") or 0)
            d[k] = "%g" % (base + sign * float(v))
        elif "=" in tok:
            k, v = tok.split("=", 1)
            d[k] = v
    return d


def mask_blob(m):
    t = {"radial": 0, "linear": 1}[m.get("type", "radial")]
    g = [t, 1 if m.get("inverted") else 0, m.get("feather", 0.5),
         m.get("cx", 0.5), m.get("cy", 0.5), m.get("rx", 0.3), m.get("ry", 0.3),
         m.get("x0", 0.5), m.get("y0", 0.35), m.get("x1", 0.5), m.get("y1", 0.65)]
    a = [m.get(k, 0) for k in MASK_KEYS]
    return ",".join("%g" % v for v in g) + "|" + ",".join("%g" % v for v in a) + "|"


# ── one photo ───────────────────────────────────────────────────────────────────────────────

def clear_masks(cs, node):
    own = own_params(cs, node)
    n = sum(1 for k in own if k == "mask")
    lines = own.get("_masks", 0)
    for _ in range(max(n, lines)):
        cs.run("select %d\nmask delete 0" % node)


def own_params(cs, node):
    cs.run("select %d" % node)
    m = json.loads(cs.c.call("cosmo_state", params=True)[1])
    text = m.get("ownParams") or m.get("params") or ""
    d = {}
    masks = 0
    for l in text.splitlines():
        if "=" in l:
            k, v = l.split("=", 1)
            if k == "mask":
                masks += 1
            d[k] = v
    d["_masks"] = masks
    return d


def compose(correction, mood_line, plan):
    """correction + the mood variant + the plan's tweaks, as one dict for the photo. A mood's temp is
    an OFFSET from 6500 on top of the photo's own white balance, as a group's would have been."""
    d = {k: ("%g" % v if isinstance(v, float) else str(v)) for k, v in correction.items()}
    for k, v in parse(mood_line).items():
        if k == "temp":
            d["temp"] = "%g" % (float(d.get("temp", 6500)) + float(v) - 6500)
        elif k in d and k in ("exposure", "highlights", "shadows", "whites", "blacks", "nrLuminance", "nrColor"):
            d[k] = "%g" % (float(d[k]) + float(v))
        else:
            d[k] = v
    return apply_ops(d, plan.get("set", ""))


def set_photo(cs, node, d, masks):
    clear_masks(cs, node)
    cs.run("select %d\nset %s\nset %s" % (node, PHOTO_RESET, render(d)))
    for m in masks:
        cs.run("select %d\nset mask=%s" % (node, mask_blob(m)))


def zone(a, z):
    s = a["zones"][z]
    return s if s["fraction"] >= 0.06 else None


def match(cs, node, d, target, fam, strength, log):
    """Nudge this photo's own R/G/B curves (toe for the shadows, shoulder for the highlights) and its
    saturation toward the set's medians. Two secant steps per axis; partial (`strength`) on purpose —
    a photo keeps its own character, only the outliers are pulled in."""
    for z, where in (("shadows", "toe"), ("highlights", "shoulder")):
        t = target.get(z)
        a = cs.analyze(node)["analysis"]
        cur = zone(a, z)
        if not t or not cur:
            continue
        gw, gt = 0.0, 0.0                    # applied offsets: warmth axis (R-/B+), tint axis (G-)
        kw, kt = -6.0, -10.0                 # starting slopes: d(zone warmth)/d(offset) etc.
        for _ in range(2):
            ew = cur["warmth"] - t["warmth"]
            et = cur["tint"] - t["tint"]
            if abs(ew) < 0.04 and abs(et) < 0.03:
                break
            # Move the zone by -strength * error along each axis, through the measured slope.
            dw = max(-0.05, min(0.05, -strength * ew / kw)) if abs(ew) >= 0.04 else 0.0
            dt = max(-0.04, min(0.04, -strength * et / kt)) if abs(et) >= 0.03 else 0.0
            # dw > 0 means "cooler": B up, R down. dt > 0 means "less green": G down.
            gw2, gt2 = max(-0.07, min(0.07, gw + dw)), max(-0.05, min(0.05, gt + dt))
            for ch, val in (("curveR", -gw2 / 2), ("curveB", gw2 / 2), ("curveG", -gt2)):
                key = d.get(ch, "0,0;1,1")
                base = d.setdefault("_base_" + ch, key)
                d[ch] = offset_curve(base, toe=val if where == "toe" else 0.0,
                                     shoulder=val if where == "shoulder" else 0.0) \
                    if not d.get("_other_" + ch) else offset_curve(d["_other_" + ch],
                                                                    toe=val if where == "toe" else 0.0,
                                                                    shoulder=val if where == "shoulder" else 0.0)
            cs.run("select %d\nset curveR=%s curveG=%s curveB=%s" % (node, d["curveR"], d["curveG"], d["curveB"]))
            a2 = cs.analyze(node)["analysis"]
            new = zone(a2, z)
            if not new:
                break
            if abs(gw2 - gw) > 1e-4 and abs(new["warmth"] - cur["warmth"]) > 1e-3:
                kw = max(-40.0, min(-1.0, (new["warmth"] - cur["warmth"]) / (gw2 - gw)))
            if abs(gt2 - gt) > 1e-4 and abs(new["tint"] - cur["tint"]) > 1e-3:
                kt = max(-40.0, min(-1.0, (new["tint"] - cur["tint"]) / (gt2 - gt)))
            gw, gt, cur = gw2, gt2, new
        for ch in ("curveR", "curveG", "curveB"):
            d["_other_" + ch] = d[ch]       # the shoulder pass builds on the toe pass
        log.append("%s %s %+.3f/%+.3f -> w %+.2f t %+.2f (set %+.2f %+.2f)" % (
            z, where, gw, gt, cur["warmth"], cur["tint"], t["warmth"], t["tint"]))
    a = cs.analyze(node)["analysis"]
    if target.get("sat") and a["meanSaturation"] > 0.02:
        ratio = target["sat"] / a["meanSaturation"]
        dp = max(-15.0, min(15.0, 100.0 * (ratio - 1.0) * 0.5 * strength))
        if abs(dp) >= 3:
            d["saturation"] = "%g" % (float(d.get("saturation", 0)) + dp)
            cs.run("select %d\nset saturation=%s" % (node, d["saturation"]))
            a = cs.analyze(node)["analysis"]
            log.append("saturation %+.0f -> %.2f (set %.2f)" % (dp, a["meanSaturation"], target["sat"]))
    return a


def guard(cs, node, d, fam, log):
    a = cs.analyze(node)["analysis"]
    hl = float(d.get("highlights", 0))
    while a["clipHighlights"] > CLIP[fam] and hl > -95:
        hl = max(-95.0, hl - 15)
        cs.run("select %d\nset highlights=%g" % (node, hl))
        a = cs.analyze(node)["analysis"]
    if hl != float(d.get("highlights", 0)):
        d["highlights"] = "%g" % hl
        log.append("highlights -> %g" % hl)
    return a


# ── commands ────────────────────────────────────────────────────────────────────────────────

def ungroup_looks(cs):
    """Per-photo editing: a look group would apply a SECOND, shared edit on top. Dissolve them —
    the photos keep their own params (the group's are gone, which is the point)."""
    for _ in range(8):
        st = cs.state()
        g = [n for n in st["nodes"] if n.get("kind") == "group" and
             (n["name"].endswith(" - Day") or n["name"].endswith(" - Night"))]
        if not g:
            return
        cs.run("group ungroup %d" % g[0]["node"])


def cmd_review(args):
    from PIL import Image, ImageDraw
    out = Path(args.out)
    (out / "review").mkdir(parents=True, exist_ok=True)
    cs = A.Cosmo(str(out / "cosmo-mcp.log"))
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        names = A.nodes_by_name(cs.state())
        files = []
        for name, node in names.items():
            p = out / "review" / (Path(name).stem + ".jpg")
            cs.preview(p, node, before=True, edge=900)
            files.append((name, p))
        per = args.per_sheet
        cell = 620
        for k in range(0, len(files), per):
            chunk = files[k:k + per]
            cols = 3
            rows = (len(chunk) + cols - 1) // cols
            S = Image.new("RGB", (cols * cell, rows * (cell + 24)), (18, 18, 18))
            dr = ImageDraw.Draw(S)
            for i, (name, p) in enumerate(chunk):
                im = Image.open(p)
                im.thumbnail((cell - 10, cell - 10))
                g = ImageDraw.Draw(im)
                for f in (0.25, 0.5, 0.75):      # the grid the plan's mask coordinates are read from
                    g.line([(f * im.width, 0), (f * im.width, im.height)], fill=(255, 255, 0), width=1)
                    g.line([(0, f * im.height), (im.width, f * im.height)], fill=(255, 255, 0), width=1)
                x = (i % cols) * cell + (cell - im.width) // 2
                y = (i // cols) * (cell + 24)
                S.paste(im, (x, y))
                dr.text(((i % cols) * cell + 6, y + cell - 6), name, fill=(235, 235, 235))
            S.save(out / ("review_%02d.jpg" % (k // per + 1)), quality=84)
        print("review sheets ->", out, "(%d photos; grid lines at 0.25 / 0.5 / 0.75)" % len(files))
    finally:
        cs.close()


def cmd_run(args):
    out = Path(args.out)
    for sub in ("before", "after"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    plan = json.loads(Path(args.plan).read_text()) if args.plan and Path(args.plan).exists() else {"photos": {}}
    mood = args.mood or plan.get("mood") or "spring-haze"
    mood = mood if mood in M.MOODS else M.find(mood)
    if not mood:
        sys.exit("run: no mood matches %r" % args.mood)
    only = set(n for n in args.names.split(",") if n) if args.names else None
    # A partial re-run (fixing the few photos that missed) keeps the WHOLE set's targets and merges
    # into its record — two photos' medians would match them to each other, not to the set.
    prev = json.loads((out / "edit.json").read_text()) if (out / "edit.json").exists() else None
    cs = A.Cosmo(str(out / "cosmo-mcp.log"))
    rows = []
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        ungroup_looks(cs)
        names = A.nodes_by_name(cs.state())
        # ── pass 1: read, correct, mood, plan — every photo on its own ──
        for name, node in names.items():
            if only and name not in only:
                continue
            pp = plan["photos"].get(name, {})
            r = cs.analyze(node, before=True)
            md = A.meta_dict(r["metadata"])
            scene, fam, why = A.classify(r["analysis"], md)
            if pp.get("scene"):
                scene, why = pp["scene"], ["set by the reviewer"]
                fam = L.FAMILY[scene]
            log = []
            clear_masks(cs, node)
            cs.run("select %d\nset %s" % (node, PHOTO_RESET))
            corr, _ = A.normalise(cs, node, scene, int(A.number(md.get("ISO"), 100)), log)
            d = compose(corr, M.scale(M.MOODS[mood][fam], args.strength * pp.get("strength", 1.0)), pp)
            set_photo(cs, node, d, pp.get("masks", []))
            a = cs.analyze(node)["analysis"]
            rows.append({"name": name, "node": node, "scene": scene, "family": fam, "why": why,
                         "subject": pp.get("subject", ""), "intent": pp.get("intent", ""),
                         "d": d, "masks": pp.get("masks", []), "match": pp.get("match", True),
                         "pass1": a, "log": log})
            print("%-18s %-14s pass1 p50 %.3f sat %.2f  %s" % (name, scene, a["lumPct"][3], a["meanSaturation"],
                                                               pp.get("subject", "")))
        # ── the set's medians, per family: what "consistent" means for THIS set in THIS mood ──
        targets = {}
        for fam in (() if (only and prev and prev.get("targets")) else ("day", "night")):
            rs = [r for r in rows if r["family"] == fam]
            if not rs:
                continue
            t = {}
            for z in ("shadows", "highlights"):
                zs = [zone(r["pass1"], z) for r in rs if zone(r["pass1"], z)]
                if len(zs) >= 2:
                    t[z] = {"warmth": statistics.median(x["warmth"] for x in zs),
                            "tint": statistics.median(x["tint"] for x in zs)}
            t["sat"] = statistics.median(r["pass1"]["meanSaturation"] for r in rs)
            targets[fam] = t
        if only and prev and prev.get("targets"):
            targets = prev["targets"]
        # ── pass 2: match each photo to its family, guard, look ──
        for r in rows:
            node, fam, d = r["node"], r["family"], r["d"]
            if r["match"] and targets.get(fam):
                a = match(cs, node, d, targets[fam], fam, args.match, r["log"])
            a = guard(cs, node, d, fam, r["log"])
            stem = Path(r["name"]).stem
            cs.preview(out / "after" / (stem + ".jpg"), node)
            if not (out / "before" / (stem + ".jpg")).exists():
                cs.preview(out / "before" / (stem + ".jpg"), node, before=True)
            r["after"] = a
            print("%-18s %-5s p50 %.3f sat %.2f clip %.3f | %s" % (r["name"], fam, a["lumPct"][3], a["meanSaturation"],
                                                                  a["clipHighlights"], " | ".join(r["log"][-3:])))
        if args.save_as:
            cs.run("project save %s" % Path(args.save_as).resolve())
        elif args.save:
            cs.run("project save")
        for r in rows:
            r["params"] = {k: v for k, v in r.pop("d").items() if not k.startswith("_")}
            r.pop("pass1", None)
        if only and prev:
            keep = [p for p in prev.get("photos", []) if p["name"] not in only]
            rows = keep + rows
        (out / "edit.json").write_text(json.dumps({"mood": mood, "strength": args.strength, "match": args.match,
                                                   "targets": targets, "photos": rows}, indent=1))
    finally:
        cs.close()
    A.sheets(out)
    import mood as MOODTOOL
    MOODTOOL.overview(out, "%s - edited per photo" % M.MOODS[mood]["title"])
    print("-> %s (overview.jpg, sheet_*.jpg, edit.json)" % out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("review", "run"))
    ap.add_argument("project")
    ap.add_argument("--out", required=True)
    ap.add_argument("--mood", default="")
    ap.add_argument("--plan", default="")
    ap.add_argument("--names", default="", help="only these photos (names, comma-separated)")
    ap.add_argument("--strength", type=float, default=1.0, help="the mood's strength")
    ap.add_argument("--match", type=float, default=0.7, help="how far each photo is pulled to the set (0..1)")
    ap.add_argument("--per-sheet", type=int, default=6)
    ap.add_argument("--save", action="store_true")
    ap.add_argument("--save-as", default="")
    args = ap.parse_args()
    {"review": cmd_review, "run": cmd_run}[args.cmd](args)


if __name__ == "__main__":
    main()
