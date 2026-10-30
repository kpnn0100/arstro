#!/usr/bin/env python3
"""calibrate.py — measure what one Cosmo control actually does (references/cosmo-controls.md).

    python3 calibrate.py <project.cmp> <node> "temp=5500" "contrast=30" "curve=0,0;0.5,0.6;1,1" ...

For each setting: reset the photo to neutral, `set` it, `analyze`, print the numbers a grader reads.
It never saves — the project on disk is untouched.
Run it on the project's own photos when a control surprises you, and when the engine changes.
"""
import json, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
from autograde import Cosmo, NEUTRAL  # noqa: E402


def line(a):
    z = a["zones"]
    return ("p5=%.3f p50=%.3f p95=%.3f sd=%.3f warm=%+.3f tint=%+.3f sat=%.3f | sh w%+.2f t%+.2f | "
            "hi w%+.2f t%+.2f clipH=%.3f" % (a["lumPct"][1], a["lumPct"][3], a["lumPct"][5], a["lumStdDev"],
                                             a["castWarmth"], a["castTint"], a["meanSaturation"],
                                             z["shadows"]["warmth"], z["shadows"]["tint"],
                                             z["highlights"]["warmth"], z["highlights"]["tint"], a["clipHighlights"]))


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    project, node, tests = Path(sys.argv[1]).resolve(), int(sys.argv[2]), sys.argv[3:]
    cs = Cosmo("/dev/null")
    try:
        cs.run("project open %s" % project)
        for s in [""] + tests:
            cs.run("select %d\nset %s%s" % (node, NEUTRAL, ("\nset " + s) if s else ""))
            print("%-42s %s" % (s or "(neutral)", line(cs.analyze(node)["analysis"])))
    finally:
        cs.close()


if __name__ == "__main__":
    main()
