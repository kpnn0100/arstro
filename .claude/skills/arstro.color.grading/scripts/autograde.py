#!/usr/bin/env python3
"""autograde.py — grade a Cosmo project the way a colourist would, through Cosmo's MCP front end.

    python3 autograde.py survey  <project.cmp> [--out DIR]
    python3 autograde.py grade   <project.cmp> [--look cinematic] [--out DIR] [--nodes 1,2,5] [--save]
    python3 autograde.py sheet   <DIR>                        # before|after contact sheets again

It is a USER of Cosmo, never a second Cosmo: it starts `cosmo-cc mcp` and speaks only the app's own
grammar (`cosmo_run`), model (`cosmo_state`), measurement (`cosmo_analyze`) and pictures
(`cosmo_preview`). Everything it decides is written down in survey.json / grade.json, so the agent
reviewing the result can see WHY each photo got what it got, and the photographer only checks.

The method (SKILL.md §2, references/workflow.md):
  1. read the photo   — metadata (time, ISO, shutter, aperture) + as-shot measurement -> a scene
  2. normalise        — closed loop on the real render: white balance to the scene's target cast,
                        exposure to its target median, highlights until nothing important clips
  3. the look         — one group per scene family carries the look (curve, split tone, mixer,
                        grain, vignette), so the whole set is one decision a person can change once
  4. check            — measure the graded render; trim exposure / highlights / black point back
                        inside the family's limits; render previews and contact sheets
"""
import argparse, json, math, os, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from mcp_client import Client  # noqa: E402
import looks as L  # noqa: E402

REPO = HERE.parents[3]
COSMO_CC = os.environ.get("COSMO_CC", str(REPO / "build/apps/cosmo/cli/cosmo-cc"))

# Every image parameter the grader may set; a fresh grade puts these back to neutral first so a
# re-run never stacks on its own previous answer.
NEUTRAL = ("exposure=0 contrast=0 highlights=0 shadows=0 whites=0 blacks=0 temp=6500 tint=0 vibrance=0 "
           "saturation=0 texture=0 clarity=0 dehaze=0 grainAmount=0 grainSize=0 sharpenAmount=0 "
           "nrLuminance=0 nrColor=0 lensVignette=0 grade0=0,0,0 grade1=0,0,0 grade2=0,0,0 balance=0 "
           "curve=0,0;1,1")


class Cosmo:
    """The MCP client with the three calls the grader needs, failing loudly on any refusal."""

    def __init__(self, log):
        self.c = Client([COSMO_CC, "mcp"], log=log)

    def run(self, script):
        err, text = self.c.call("cosmo_run", script=script)
        if err:
            raise RuntimeError(text)
        return text

    def state(self):
        return json.loads(self.c.call("cosmo_state")[1])

    def analyze(self, node=None, before=False):
        args = {"before": before}
        if node is not None:
            args["node"] = node
        err, text = self.c.call("cosmo_analyze", **args)
        if err:
            raise RuntimeError(text)
        return json.loads(text)

    def preview(self, path, node=None, before=False, edge=900):
        args = {"before": before, "edge": edge, "quality": 82}
        if node is not None:
            args["node"] = node
        err, text = self.c.call("cosmo_preview", img=str(path), **args)
        if err:
            raise RuntimeError(text)

    def close(self):
        self.c.close()


# ── 1. read the photo ──────────────────────────────────────────────────────────────────────

def meta_dict(rows):
    return {r["label"]: r["value"] for r in rows or []}


def number(text, default=0.0):
    """'ISO 1600' -> 1600, '1/70 s' -> 0.0142, 'f/1.4' -> 1.4, '23 mm' -> 23."""
    t = (text or "").replace("ISO", "").replace("s", "").replace("f/", "").replace("mm", "").strip()
    try:
        if "/" in t:
            a, b = t.split("/", 1)
            return float(a) / float(b)
        return float(t)
    except ValueError:
        return default


def classify(a, md):
    """Scene from evidence. Returns (scene, family, reasons). Never from one number alone — the
    time of day, the camera's exposure and the measured light have to agree (references/light.md)."""
    s = a
    p50 = s["lumPct"][3]
    iso = number(md.get("ISO"), 0)
    taken = md.get("Taken", "")
    hour = int(taken[11:13]) if len(taken) >= 13 and taken[11:13].isdigit() else -1
    hues = s["hues"]
    blue = hues[6] + hues[7]                 # 180..240
    hi = s["zones"]["highlights"]
    reasons = []
    night_hour = hour >= 18 or (0 <= hour < 5)
    dark = p50 < 0.16
    if p50 < 0.2 and blue > 0.55 and s["neonFraction"] < 0.01 and iso >= 1600:
        reasons.append("dark, %.0f%% of the colour is blue-cyan, no point lights, high ISO" % (blue * 100))
        return "aquarium", "night", reasons
    if dark and (night_hour or iso >= 1600) and s["neonFraction"] >= 0.005 and s["skyFraction"] < 0.03 \
            and (hour >= 18 or hour < 5):
        reasons.append("taken %02d:00, median L %.2f, neon %.1f%%" % (hour, p50, s["neonFraction"] * 100))
        return "night-city", "night", reasons
    if dark and iso >= 800:
        reasons.append("median L %.2f at ISO %d — a dark interior lit by its own art light" % (p50, iso))
        return "dark-interior", "night", reasons
    if hi["fraction"] > 0.12 and hi["saturation"] < 0.12:
        reasons.append("%.0f%% bright and colourless (a white sky)" % (hi["fraction"] * 100))
        return "overcast-day", "day", reasons
    if 16 <= hour <= 18 and s["castWarmth"] > 0.6:
        reasons.append("late afternoon and warm (%.2f)" % s["castWarmth"])
        return "golden-hour", "day", reasons
    reasons.append("daylight, median L %.2f" % p50)
    return "daylight", "day", reasons


# ── 2. normalise (closed loop on the real render) ──────────────────────────────────────────

def witness(a, scene):
    """Which measured cast stands for the LIGHT, and how far to trust it (0..1).

    A bright, colourless highlight zone — an overcast sky, a white wall, cloud — is the best grey
    card a scene has. Foliage is not: grey-green leaves pass a "low saturation" test and drag a
    grey-world estimate toward green, which is how a forest got warmed to yellow (references/
    light.md). So daylight trusts the highlights when there are enough of them, and otherwise
    trusts the neutral estimate only half-way."""
    z = a["zones"]["highlights"]
    if scene in ("overcast-day", "daylight", "golden-hour"):
        if z["fraction"] >= 0.08 and z["saturation"] < 0.2:
            # ...unless it measures BLUE: then it is a pale blue sky, not a grey card, and making
            # it neutral warms the whole frame (the Kamakura temple's sky went white and the roofs
            # amber). Real overcast here measured warm; a blue witness is trusted a third.
            return z["warmth"], z["tint"], (1.0 if z["warmth"] > -0.05 else 0.3)
        return a["castWarmth"], a["castTint"], 0.5 if a["foliageFraction"] < 0.25 else 0.25
    return a["castWarmth"], a["castTint"], 1.0


def normalise(cs, node, scene, iso, log):
    t = L.TARGETS[scene]
    p = {"temp": 6500.0, "tint": 0.0, "exposure": 0.0, "highlights": 0.0, "shadows": 0.0, "whites": 0.0}
    p.update(L.noise_for_iso(iso))

    def apply():
        cs.run("set " + " ".join("%s=%s" % (k, round(v, 3) if isinstance(v, float) else v) for k, v in p.items()))
        return cs.analyze(node)["analysis"]

    a = apply()
    # White balance: a secant walk on temperature and tint toward the scene's target cast, scaled by
    # how far the witness can be trusted, inside the scene's limits. Slopes start at the calibrated
    # ones (references/cosmo-controls.md) and are re-measured on every step.
    if t["wb"]:
        kW, kT = 0.19 / 1000.0, -0.0029
        w0, ti0, trust = witness(a, scene)
        goalW = t["warmth"] if t["wb"] == "full" else w0
        goalW = w0 + (goalW - w0) * trust
        goalT = ti0 + (t["tint"] - ti0) * trust
        if t["wb"] == "tint-only":
            # Remove a decoder's GREEN only. A magenta-lit room (teamLab) measures "magenta" because
            # it IS magenta; correcting it toward green would undo the artist's light.
            goalT = min(ti0, t["tint"]) if ti0 > t["tint"] else ti0
        for _ in range(5):
            w, ti, _ = witness(a, scene)
            if abs(w - goalW) < 0.03 and abs(ti - goalT) < 0.02:
                break
            dT = 0.0 if t["wb"] != "full" else max(-2500, min(2500, (goalW - w) / kW)) * 0.85
            dTi = max(-40, min(40, (goalT - ti) / kT)) * 0.85
            p["temp"] = max(t["tempRange"][0], min(t["tempRange"][1], p["temp"] + dT))
            # A witness trusted fully (clouds, snow, a white wall) may correct a real tinted-glass
            # cast past the usual range — the Fuji frame was shot through a green train window.
            tintHi = t["tintRange"][1] if trust < 1.0 or not t["floor"] else max(t["tintRange"][1], 80)
            p["tint"] = max(t["tintRange"][0], min(tintHi, p["tint"] + dTi))
            a2 = apply()
            w2, ti2, _ = witness(a2, scene)
            if dT and abs(w2 - w) > 1e-3:
                kW = max(0.00005, min(0.0006, (w2 - w) / dT))
            if dTi and abs(ti2 - ti) > 1e-3:
                kT = max(-0.008, min(-0.001, (ti2 - ti) / dTi))
            a = a2
        w, ti, _ = witness(a, scene)
        log.append("wb temp=%d tint=%.0f (trust %.2f) -> warmth %+.2f tint %+.2f" % (p["temp"], p["tint"], trust, w, ti))
    # Exposure toward the scene's median — but HIGHLIGHTS FIRST: when the sky still clips with the
    # highlights slider pulled in, exposure comes back down and the median is left for the curve.
    capped = False
    for _ in range(8):
        p50 = max(a["lumPct"][3], 0.004)
        clip = a["clipHighlights"]
        if clip > t["clip"]:
            if p["highlights"] > -80:
                p["highlights"] = max(-80.0, p["highlights"] - 20)
            else:
                p["exposure"] -= 0.25
                capped = True
        elif (p50 < t["p50"] - 0.02 and not capped) or (not t["floor"] and p50 > t["p50"] + 0.02):
            # Daylight's target is a FLOOR: a frame that is mostly white sky has the sky for its
            # median, and pulling that down to mid-grey turned the Asakusa pagoda into a silhouette.
            p["exposure"] = max(-2.0, min(t["maxEv"], p["exposure"] + 0.8 * 2.2 * math.log2(t["p50"] / p50)))
        else:
            break
        a = apply()
    if t["openShadows"] and a["lumPct"][1] < 0.015:
        p["shadows"] = 8.0
        a = apply()
    log.append("exposure %+.2f highlights %d shadows %d%s -> p50 %.3f clip %.3f" % (
        p["exposure"], p["highlights"], p["shadows"], " (held by the highlights)" if capped else "",
        a["lumPct"][3], a["clipHighlights"]))
    return p, a


# ── 3/4. the look, then the check ──────────────────────────────────────────────────────────

def find_group(state, name):
    for n in state["nodes"]:
        if n.get("kind") == "group" and n.get("name") == name:
            return n["node"]
    return None


def ensure_group(cs, name, members):
    st = cs.state()
    g = find_group(st, name)
    if g is not None:
        return g
    lines = ["select %d" % members[0]] + ["select %d add" % m for m in members[1:]] + ['group new "%s"' % name]
    cs.run("\n".join(lines))
    return cs.state()["selectedNode"]


def check(cs, node, scene, p, log):
    """Measure the graded render and trim back inside the family's limits."""
    t = L.TARGETS[scene]
    a = cs.analyze(node)["analysis"]
    for _ in range(4):
        changed = False
        p50 = max(a["lumPct"][3], 0.004)
        lo, hi = t["final"]
        if (p50 < lo and a["clipHighlights"] < t["clip"] * 0.5) or p50 > hi:
            goal = min(max(p50, lo), hi)
            p["exposure"] = max(-2.0, min(t["maxEv"], p["exposure"] + 0.8 * 2.2 * math.log2(goal / p50)))
            changed = True
        if a["clipHighlights"] > t["clip"] and p["highlights"] > -95:
            p["highlights"] = max(-95.0, p["highlights"] - 12)
            changed = True
        if a["lumPct"][0] > t["blackMax"]:
            p["blacks"] = p.get("blacks", 0.0) - 3
            changed = True
        if not changed:
            break
        cs.run("set " + " ".join("%s=%s" % (k, round(v, 3)) for k, v in p.items() if k in
                                 ("exposure", "highlights", "blacks")))
        a = cs.analyze(node)["analysis"]
    log.append("final p1 %.3f p50 %.3f p99 %.3f clip %.3f warmth %+.2f tint %+.2f sat %.2f" % (
        a["lumPct"][0], a["lumPct"][3], a["lumPct"][6], a["clipHighlights"], a["castWarmth"], a["castTint"],
        a["meanSaturation"]))
    return a


# ── the commands ───────────────────────────────────────────────────────────────────────────

def image_nodes(state):
    return [n for n in state["nodes"] if n.get("kind") != "group"]


def nodes_by_name(state):
    """Photo name -> node id NOW. Node ids are tree positions and are renumbered when a project
    with groups is reopened, so a survey row is keyed by its photo's NAME and resolved again after
    every open — a node id carried across a reload once regraded the wrong photo."""
    return {n["name"]: n["node"] for n in image_nodes(state)}


def survey(args):
    out = Path(args.out)
    (out / "before").mkdir(parents=True, exist_ok=True)
    cs = Cosmo(str(out / "cosmo-mcp.log"))
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        rows = []
        for n in image_nodes(cs.state()):
            if args.nodes and n["node"] not in args.nodes:
                continue
            r = cs.analyze(n["node"], before=True)
            md = meta_dict(r["metadata"])
            scene, family, why = classify(r["analysis"], md)
            if n["node"] in args.scene:
                # The agent LOOKED and knows better than the heuristic (a teamLab room is not an
                # aquarium because both are dark and blue). Its call wins, and is recorded as such.
                why = ["set by the reviewer (heuristic said %s: %s)" % (scene, "; ".join(why))]
                scene = args.scene[n["node"]]
                family = L.FAMILY[scene]
            cs.preview(out / "before" / ("%02d.jpg" % n["node"]), n["node"], before=True)
            rows.append({"node": n["node"], "name": n["name"], "scene": scene, "family": family, "why": why,
                         "taken": md.get("Taken"), "iso": md.get("ISO"), "shutter": md.get("Shutter"),
                         "aperture": md.get("Aperture"), "focal": md.get("Focal length"),
                         "camera": "%s %s" % (md.get("Camera make", ""), md.get("Camera model", "")),
                         "lens": md.get("Lens"), "before": r["analysis"]})
            print("%2d %-18s %-14s %s" % (n["node"], n["name"], scene, "; ".join(why)))
        (out / "survey.json").write_text(json.dumps(rows, indent=1))
        return rows
    finally:
        cs.close()


def grade(args):
    out = Path(args.out)
    rows = survey(args) if not (out / "survey.json").exists() or args.resurvey else json.loads((out / "survey.json").read_text())
    look = L.LOOKS[args.look]
    (out / "after").mkdir(parents=True, exist_ok=True)
    cs = Cosmo(str(out / "cosmo-mcp.log"))
    report = []
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        now = nodes_by_name(cs.state())
        for r in rows:
            r["node"] = now[r["name"]]
        if args.nodes:
            rows = [r for r in rows if r["node"] in args.nodes]
        families = {}
        for r in rows:
            families.setdefault(r["family"], []).append(r["node"])
        groups = {}
        for fam, members in families.items():
            name = look["groups"][fam]["name"]
            g = ensure_group(cs, name, members)
            groups[fam] = g
            cs.run("select %d\nset %s\nset %s" % (g, NEUTRAL, look["groups"][fam]["params"]))
            cs.run("bypass %d on" % g)          # normalise each photo with the look OFF
        for r in rows:
            log = []
            node = r["node"]
            cs.run("select %d\nset %s" % (node, NEUTRAL))
            p, _ = normalise(cs, node, r["scene"], int(number(r["iso"], 100)), log)
            r["normalised"] = dict(p)
            report.append((r, log, p))
        for fam, g in groups.items():
            cs.run("bypass %d off" % g)
        for r, log, p in report:
            node = r["node"]
            extra = look["scene"].get(r["scene"], "")
            cs.run("select %d%s" % (node, ("\nset " + extra) if extra else ""))
            r["after"] = check(cs, node, r["scene"], p, log)
            r["params"] = dict(p)
            r["sceneLook"] = extra
            r["log"] = log
            cs.preview(out / "after" / ("%02d.jpg" % node), node)
            print("%2d %-14s %s" % (node, r["scene"], " | ".join(log)))
        if args.save:
            print(cs.run("project save").strip().splitlines()[-1])
        (out / "grade.json").write_text(json.dumps({"look": args.look, "groups": groups,
                                                    "photos": [r for r, _, _ in report]}, indent=1))
    finally:
        cs.close()
    sheets(out)


def sheets(out):
    from PIL import Image, ImageDraw
    out = Path(out)
    pairs = sorted(p.name for p in (out / "after").glob("*.jpg"))
    cell = 520
    for k in range(0, len(pairs), 4):
        chunk = pairs[k:k + 4]
        S = Image.new("RGB", (cell * 2 + 30, len(chunk) * (cell + 24)), (18, 18, 18))
        d = ImageDraw.Draw(S)
        for i, name in enumerate(chunk):
            for j, side in enumerate(("before", "after")):
                im = Image.open(out / side / name)
                im.thumbnail((cell, cell))
                S.paste(im, (j * (cell + 30) + (cell - im.width) // 2, i * (cell + 24)))
            d.text((4, i * (cell + 24) + cell + 4), "%s   before | after" % name[:-4], fill=(230, 230, 230))
        S.save(out / ("sheet_%02d.jpg" % (k // 4 + 1)), quality=84)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("survey", "grade", "sheet"))
    ap.add_argument("target", help="project .cmp (survey/grade) or output dir (sheet)")
    ap.add_argument("--out", default=None)
    ap.add_argument("--look", default="cinematic", choices=sorted(L.LOOKS))
    ap.add_argument("--nodes", default="", help="comma-separated node ids (default: every photo)")
    ap.add_argument("--save", action="store_true", help="save the project when graded")
    ap.add_argument("--resurvey", action="store_true")
    ap.add_argument("--scene", default="", help="reviewer overrides, e.g. 16=dark-interior,5=aquarium "
                    "(scenes: %s)" % ", ".join(sorted(L.TARGETS)))
    args = ap.parse_args()
    args.nodes = [int(x) for x in args.nodes.split(",") if x.strip()]
    args.scene = {int(k): v for k, v in (kv.split("=", 1) for kv in args.scene.split(",") if kv.strip())}
    for v in args.scene.values():
        if v not in L.TARGETS:
            ap.error("unknown scene %s (one of: %s)" % (v, ", ".join(sorted(L.TARGETS))))
    if args.cmd == "sheet":
        return sheets(args.target)
    args.project = args.target
    args.out = args.out or str(Path(args.project).with_suffix("")) + ".grade"
    t0 = time.time()
    survey(args) if args.cmd == "survey" else grade(args)
    print("done in %.0f s -> %s" % (time.time() - t0, args.out))


if __name__ == "__main__":
    main()
