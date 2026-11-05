#!/usr/bin/env python3
"""style.py — a style in the user's words -> the edit; and the board the approver judges the styles on.

    python3 style.py list                         # every style: what it means, its words, judged or not
    python3 style.py match "tông Hàn nhẹ nhàng"    # how a prompt is read, and the edit it becomes
    python3 style.py test                         # the real prompts in styles.EXAMPLES still read right
    python3 style.py board PROJECT.cmp [--styles warm,cool] [--plan plan.json] [--lab ~/photo/grade-lab]
                                                  # 3 candidates per style on its demo photo -> Grade Lab /styles

Applying a style to a whole shoot is the editor's job: `edit.py run P.cmp --out DIR --style "<the user's words>"`
(arstro.color.grading) — every photo corrected, given the style, matched to the set, its sky and subject checked.

The board renders from a COPY of the project (<lab>/styles/styles.cmp) and never saves anything the approver uses.
Each candidate goes through the editor's own steps — correct, look, depth (unless the style drops it), the subject
and sky checks, the clipping guard — so what the approver picks here is what `edit.py run --style` delivers.
"""
import argparse, json, random, shutil, sys, time
from pathlib import Path

HERE = Path(__file__).resolve().parent
GRADING = HERE.parent.parent / "arstro.color.grading" / "scripts"
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(GRADING))
import styles as ST  # noqa: E402
import autograde as A  # noqa: E402
import edit as E  # noqa: E402
import looks as L  # noqa: E402
import taste as T  # noqa: E402

DEFAULT_PLAN = GRADING.parent / "data" / "tokyo-plan-2026-10-30.json"


def resolve(v, scene, analysis, plan_entry, style=None):
    """A candidate made concrete for one photo: "taste" becomes the approver's look for THIS photo; skin and a
    white subject are protected (not in black and white: there is no colour to keep)."""
    v = dict(v)
    pe = plan_entry or {}
    if v["look"] == "taste":
        ch = T.choose(scene, analysis, pe)
        v.update(look=ch["look"], strength=v["strength"] * ch["strength"], tweaks=ch["tweaks"] + v["tweaks"])
    mono = style == "bw" or "saturation=-100" in (v.get("set") or "")
    v["skin"] = not mono and T.has_person(analysis, pe)
    v["white"] = not mono and T.is_white(pe.get("subject", ""))
    return v


# The sky check (edit.py) keeps a sky as saturated as the camera had it and its clouds as white as white balance
# left them — the approver's rule for THE TASTE ("the sky belongs to the photo"). A style the user ASKED for colours
# the whole frame, its sky included: on the first board the check took "warm" out of a white overcast sky (sky filter
# saturation -48) and the warm candidates read neutral. So for a named style the sky may carry the style's colour
# (saturation up to 1.6x + 0.04, clouds may take its warmth or cool) while everything that made skies FAKE stays
# enforced — sand (texture), one colour (spread), clouds apart from their sky (gap), contrast, structure, dull clouds.
# vibrant too: a user who asks for colour that pops still wants a white sky white (first board: a mint sky)
STRICT_SKY = {"natural", "cinematic", "vibrant"}
STYLE_SKY_SAT = (1.6, 0.04)


class sky_tolerance:
    def __init__(self, style):
        self.on = style not in STRICT_SKY

    def __enter__(self):
        if self.on:
            self.limits, self.faults = dict(E.SKY_LIMITS), E.sky_faults
            E.SKY_LIMITS["sat"] = STYLE_SKY_SAT
            E.sky_faults = lambda now, was: [f for f in self.faults(now, was) if f != "warm"]

    def __exit__(self, *exc):
        if self.on:
            E.SKY_LIMITS.clear()
            E.SKY_LIMITS.update(self.limits)
            E.sky_faults = self.faults


def edit_photo(cs, node, corr, fam, scene, analysis, pe, v, before_jpg, scratch, log, style=None):
    """One photo, one style candidate — the editor's pass without set matching (a board shows one photo)."""
    v = resolve(v, scene, analysis, pe, style)
    d = T.params(corr, fam, v, pe)
    masks = pe.get("masks", [])
    E.set_photo(cs, node, d, masks)
    if v.get("depth", True):
        E.depth(cs, node, d, fam, log)
    masks = E.subject_check(cs, node, d, masks, before_jpg, scratch, log, skin=v["skin"], white=v["white"], fam=fam,
                            corr=corr)
    if fam == "day" or scene == "blue-hour":
        with sky_tolerance(style):
            masks = E.sky_check(cs, node, d, masks, before_jpg, scratch, log, corr=corr)
    E.guard(cs, node, d, fam, log)
    return v, d


def cmd_list(args):
    for sid, s in ST.STYLES.items():
        i, judged = ST.chosen(sid)
        c = s["candidates"][i]
        print("%-13s %-16s %s" % (sid, s["title"], s["means"]))
        print("              words: %s" % ", ".join(s["words"]))
        print("              recipe: %s — %s" % ("ABC"[i], c["label"]) +
              ("  [the approver's pick]" if judged else "  [not judged yet: the grader's first reading]"))
        if (ST.verdicts().get(sid) or {}).get("note"):
            print("              note: %s" % ST.verdicts()[sid]["note"])


def cmd_match(args):
    m = ST.match(args.prompt)
    print(json.dumps(m, ensure_ascii=False, indent=1))
    v = ST.variant(m)
    if v:
        print("edit:", json.dumps({k: v[k] for k in ("style", "label", "look", "strength", "tweaks", "set", "depth")},
                                  ensure_ascii=False, indent=1))


def cmd_test(args):
    """every EXAMPLES prompt must read as written there — run after changing words, modifiers or the reader"""
    bad = 0
    for prompt, style, strength, steps in ST.EXAMPLES:
        m = ST.match(prompt)
        got = {n for n, _ in m["tweaks"]}
        ok = m["style"] == style and abs(m["strength"] - strength) < 1e-6 and set(steps) <= got
        bad += not ok
        print("%s %-48s -> %s %.1f %s%s" % ("ok  " if ok else "FAIL", prompt, m["style"], m["strength"],
                                         sorted(got), "" if ok else "   (want %s %.1f %s)" % (style, strength, steps)))
    print("%d of %d read right" % (len(ST.EXAMPLES) - bad, len(ST.EXAMPLES)))
    sys.exit(1 if bad else 0)


def cmd_board(args):
    lab = Path(args.lab).expanduser()
    out = lab / "styles"
    out.mkdir(parents=True, exist_ok=True)
    proj = out / "styles.cmp"
    shutil.copy(Path(args.project).expanduser(), proj)
    plan = json.loads(Path(args.plan).expanduser().read_text())["photos"] if args.plan else {}
    want = [s for s in (args.styles.split(",") if args.styles else ST.STYLES) if s]
    board_path = out / "board.json"
    board = json.loads(board_path.read_text()) if board_path.exists() and args.styles else {"styles": []}
    keep = [b for b in board["styles"] if b["id"] not in want]
    cs = A.Cosmo(str(out / "cosmo-mcp.log"))
    corr_cache, rows = {}, []
    try:
        cs.run("project open %s" % proj)
        names = A.nodes_by_name(cs.state())
        for sid in want:
            s = ST.STYLES[sid]
            photo = s["demo"] if s["demo"] in names else next(iter(names))
            node, pe = names[photo], plan.get(photo, {})
            sd = out / sid
            sd.mkdir(exist_ok=True)
            if photo not in corr_cache:
                r = cs.analyze(node, before=True)
                md = A.meta_dict(r["metadata"])
                scene, fam, _ = A.classify(r["analysis"], md)
                if pe.get("scene"):
                    scene, fam = pe["scene"], L.FAMILY[pe["scene"]]
                E.clear_masks(cs, node)
                cs.run("select %d\nset %s" % (node, E.PHOTO_RESET))
                corr, _ = A.normalise(cs, node, scene, int(A.number(md.get("ISO"), 100)), [])
                bj = out / (".before-%s.jpg" % Path(photo).stem)
                cs.preview(bj, node, before=True)
                corr_cache[photo] = (corr, fam, scene, r["analysis"], bj)
            corr, fam, scene, analysis, before_jpg = corr_cache[photo]
            cs.preview(sd / "original.jpg", node, before=True, edge=1400)
            order = list(range(len(s["candidates"])))
            random.Random(sid).shuffle(order)            # blind: the position says nothing about A, B or C
            cands = []
            for pos, i in enumerate(order):
                c = s["candidates"][i]
                log = []
                t0 = time.time()
                v, d = edit_photo(cs, node, corr, fam, scene, analysis, pe, c, before_jpg, sd / ".check.jpg", log, sid)
                f = "%d.jpg" % (pos + 1)
                cs.preview(sd / f, node, edge=1400)
                cands.append({"letter": "ABC"[i], "file": f, "label": c["label"], "recipe": c,
                              "params": {k: x for k, x in d.items() if not k.startswith("_")}, "log": log})
                print("%-13s %s %s  %4.1fs  %s" % (sid, "ABC"[i], photo, time.time() - t0, " | ".join(log[-2:])), flush=True)
            (sd / ".check.jpg").unlink(missing_ok=True)
            rows.append({"id": sid, "title": s["title"], "means": s["means"], "words": s["words"], "photo": photo,
                         "scene": scene, "subject": pe.get("subject", ""), "original": "original.jpg",
                         "candidates": cands})
    finally:
        cs.close()
    order = list(ST.STYLES)
    board = {"built": time.strftime("%Y-%m-%dT%H:%M:%S"), "project": str(Path(args.project).expanduser()),
             "styles": sorted(keep + rows, key=lambda b: order.index(b["id"]) if b["id"] in order else 99)}
    board_path.write_text(json.dumps(board, ensure_ascii=False, indent=1))
    print("-> %s (%d styles) — judge at http://127.0.0.1:8789/styles" % (board_path, len(board["styles"])))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("list")
    sub.add_parser("test")
    m = sub.add_parser("match")
    m.add_argument("prompt")
    b = sub.add_parser("board")
    b.add_argument("project")
    b.add_argument("--styles", default="", help="only these styles (ids, comma-separated)")
    b.add_argument("--plan", default=str(DEFAULT_PLAN))
    b.add_argument("--lab", default="~/photo/grade-lab")
    args = ap.parse_args()
    {"list": cmd_list, "match": cmd_match, "board": cmd_board, "test": cmd_test}[args.cmd](args)


if __name__ == "__main__":
    main()
