#!/usr/bin/env python3
"""mood.py — put a MOOD on a whole Cosmo project, or see every mood side by side first.

    python3 mood.py list                                          # the moods, what each is for
    python3 mood.py board <project.cmp> [--photos a.RAF,b.RAF] [--moods neon-noir,faded-film] [--strength 1]
                    [--out DIR]                                   # one sheet: moods (rows) x photos (columns)
    python3 mood.py apply <project.cmp> --mood <name|"a prompt"> [--night-mood <name>] [--strength 0.8]
                    [--out DIR] [--save | --save-as tokyo-faded.cmp]

A mood is the CREATIVE half of a grade and sits on the project's look groups (one for day scenes,
one for night); each photo keeps its own correction underneath. So `apply` on a project that was
never graded runs autograde's correction first (white balance, exposure, highlights per photo), and
on a graded one only swaps the groups' look — a whole set restyled in seconds, every photo still
editable in Cosmo, the look one decision a person can change once.

`--mood` takes a name or words: "make it moody", "neon cyberpunk", "soft pastel spring" (moods.find).
`--strength` 0..1.5 scales the mood (0.5 = half: curves halfway to straight, mixer and wheels halved).
After the look, each photo is checked for clipping only — a mood is ALLOWED to change the key
(moody-forest is darker on purpose, spring-haze brighter); it is not allowed to burn a sky.
"""
import argparse, json, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autograde as A  # noqa: E402
import looks as L  # noqa: E402
import moods as M  # noqa: E402

GROUP_RESET = A.GROUP_RESET   # everything a mood may set on a group, back to neutral
CLIP = {"day": 0.004, "night": 0.012}


def look_groups(state):
    """{family: node} for the groups a grade made, whatever look they carry now (`… - Day|Night`)."""
    out = {}
    for n in state["nodes"]:
        if n.get("kind") == "group":
            if n["name"].endswith(" - Day"):
                out["day"] = n["node"]
            elif n["name"].endswith(" - Night"):
                out["night"] = n["node"]
    return out


def members(state, group):
    return [n for n in state["nodes"] if n.get("kind") == "image" and n.get("parent") == group]


def set_mood(cs, groups, mood_day, mood_night, strength, rename=True):
    for fam, g in groups.items():
        key = mood_day if fam == "day" else mood_night
        m = M.MOODS[key]
        params = M.scale(m[fam], strength)
        cs.run("select %d\nset %s\nset %s" % (g, GROUP_RESET, params))
        if rename:
            cs.run('group rename %d "%s - %s"' % (g, m["title"], "Day" if fam == "day" else "Night"))


def own_params(cs, node):
    cs.run("select %d" % node)
    m = json.loads(cs.c.call("cosmo_state", params=True)[1])
    own = m.get("ownParams") or m.get("params") or ""
    return dict(l.split("=", 1) for l in own.splitlines() if "=" in l)


def guard_clipping(cs, node, family, log):
    """A mood may move the key; it may not burn the highlights. Pull this photo's highlights in
    until the graded render clips no more than its family allows."""
    a = cs.analyze(node)["analysis"]
    if a["clipHighlights"] <= CLIP[family]:
        return a
    hl = float(own_params(cs, node).get("highlights", 0))
    for _ in range(4):
        if a["clipHighlights"] <= CLIP[family] or hl <= -95:
            break
        hl = max(-95.0, hl - 15)
        cs.run("select %d\nset highlights=%g" % (node, hl))
        a = cs.analyze(node)["analysis"]
    log.append("highlights -> %d (clip %.3f)" % (hl, a["clipHighlights"]))
    return a


# ── commands ────────────────────────────────────────────────────────────────────────────────

def cmd_list(_args):
    for k, m in M.MOODS.items():
        print("%-16s %s\n%16s %s\n" % (k, m["about"], "", "prompt words: " + ", ".join(m["keywords"])))


def cmd_board(args):
    from PIL import Image, ImageDraw
    out = Path(args.out or str(Path(args.project).with_suffix("")) + ".moods")
    out.mkdir(parents=True, exist_ok=True)
    moods = [m for m in (args.moods.split(",") if args.moods else M.MOODS)]
    cs = A.Cosmo(str(out / "cosmo-mcp.log"))
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        st = cs.state()
        groups = look_groups(st)
        if not groups:
            sys.exit("board: %s has no look groups — grade it first: autograde.py grade %s" % (args.project, args.project))
        names = A.nodes_by_name(st)
        photos = args.photos.split(",") if args.photos else list(names)[:4]
        cell = 300
        rows = [("corrected", None)] + [(M.MOODS[k]["title"], k) for k in moods]
        S = Image.new("RGB", (150 + len(photos) * (cell + 6), len(rows) * (cell + 6)), (18, 18, 18))
        d = ImageDraw.Draw(S)
        for r, (title, key) in enumerate(rows):
            if key is None:
                for g in groups.values():
                    cs.run("select %d\nset %s" % (g, GROUP_RESET))
            else:
                set_mood(cs, groups, key, key, args.strength, rename=False)
            d.text((6, r * (cell + 6) + cell // 2 - 6), title, fill=(235, 235, 235))
            for c, name in enumerate(photos):
                tmp = out / "_cell.jpg"
                cs.preview(tmp, names[name], edge=cell * 2)
                im = Image.open(tmp)
                im.thumbnail((cell, cell))
                S.paste(im, (150 + c * (cell + 6) + (cell - im.width) // 2, r * (cell + 6) + (cell - im.height) // 2))
            print("rendered", title)
        sheet = out / ("board_%s.jpg" % time.strftime("%H%M%S"))
        S.save(sheet, quality=86)
        (out / "_cell.jpg").unlink(missing_ok=True)
        print("board ->", sheet, "(the project was not saved)")
    finally:
        cs.close()


def cmd_apply(args):
    day = M.find(args.mood) if args.mood not in M.MOODS else args.mood
    if not day:
        sys.exit("apply: no mood matches %r — `mood.py list` shows them" % args.mood)
    night = args.night_mood or day
    night = M.find(night) if night not in M.MOODS else night
    out = Path(args.out or str(Path(args.project).with_suffix("")) + "." + day)
    for sub in ("before", "after"):
        (out / sub).mkdir(parents=True, exist_ok=True)

    # A project nobody graded gets the correction first, with this mood as its look.
    cs = A.Cosmo(str(out / "cosmo-mcp.log"))
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        if not look_groups(cs.state()):
            cs.close()
            print("no look groups: correcting every photo first (autograde grade) ...")
            g = argparse.Namespace(project=args.project, out=str(out), look=day, nodes=[], save=True,
                                   resurvey=True, scene={}, strength=args.strength)
            A.grade(g)
            cs = A.Cosmo(str(out / "cosmo-mcp.log"))
            cs.run("project open %s" % Path(args.project).resolve())
        st = cs.state()
        groups = look_groups(st)
        set_mood(cs, groups, day, night, args.strength)
        report = []
        for fam, g in groups.items():
            for n in members(cs.state(), g):
                log = []
                a = guard_clipping(cs, n["node"], fam, log)
                cs.preview(out / "after" / ("%s.jpg" % Path(n["name"]).stem), n["node"])
                if not (out / "before" / ("%s.jpg" % Path(n["name"]).stem)).exists():
                    cs.preview(out / "before" / ("%s.jpg" % Path(n["name"]).stem), n["node"], before=True)
                report.append({"name": n["name"], "family": fam, "mood": day if fam == "day" else night,
                               "p50": a["lumPct"][3], "clip": a["clipHighlights"], "warmth": a["castWarmth"],
                               "tint": a["castTint"], "sat": a["meanSaturation"], "log": log})
                print("%-18s %-5s %-15s p50 %.3f clip %.3f sat %.2f %s" % (
                    n["name"], fam, report[-1]["mood"], a["lumPct"][3], a["clipHighlights"], a["meanSaturation"],
                    " ".join(log)))
        if args.save_as:
            cs.run("project save %s" % Path(args.save_as).resolve())
            print("saved as", args.save_as)
        elif args.save:
            cs.run("project save")
            print("saved", args.project)
        (out / "mood.json").write_text(json.dumps({"day": day, "night": night, "strength": args.strength,
                                                   "photos": report}, indent=1))
    finally:
        cs.close()
    A.sheets(out)
    overview(out, "%s%s" % (M.MOODS[day]["title"], "" if night == day else " / " + M.MOODS[night]["title"]))
    print("sheets ->", out)


def overview(out, title, cols=6, cell=330):
    """Every graded photo of the set on one sheet — how a mood reads as a SET, which is how it is judged."""
    from PIL import Image, ImageDraw
    files = sorted((Path(out) / "after").glob("*.jpg"))
    if not files:
        return
    rows = (len(files) + cols - 1) // cols
    S = Image.new("RGB", (cols * cell, rows * cell + 28), (18, 18, 18))
    d = ImageDraw.Draw(S)
    d.text((8, 8), title, fill=(235, 235, 235))
    for i, f in enumerate(files):
        im = Image.open(f)
        im.thumbnail((cell - 8, cell - 8))
        S.paste(im, ((i % cols) * cell + (cell - im.width) // 2, 28 + (i // cols) * cell + (cell - im.height) // 2))
    S.save(Path(out) / "overview.jpg", quality=85)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("list", "board", "apply"))
    ap.add_argument("project", nargs="?")
    ap.add_argument("--mood", default="")
    ap.add_argument("--night-mood", default="")
    ap.add_argument("--moods", default="", help="board: which moods (default all)")
    ap.add_argument("--photos", default="", help="board: photo names (default the first four)")
    ap.add_argument("--strength", type=float, default=1.0)
    ap.add_argument("--out", default=None)
    ap.add_argument("--save", action="store_true")
    ap.add_argument("--save-as", default="")
    args = ap.parse_args()
    if args.cmd != "list" and not args.project:
        ap.error("a project .cmp is required")
    {"list": cmd_list, "board": cmd_board, "apply": cmd_apply}[args.cmd](args)


if __name__ == "__main__":
    main()
