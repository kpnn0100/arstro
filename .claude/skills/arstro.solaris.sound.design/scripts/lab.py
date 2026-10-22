#!/usr/bin/env python3
"""lab.py — the Sound Lab: the melody skill's lab server (one page, one server for both skills) on the sound lab's
folder, its verdicts going to THIS skill's ledger.

    python3 lab.py [--port 8788] [--lab ~/Music/Solaris/sound-lab]

Binds 127.0.0.1 only. Every batch folder (fb-NNN, from futurebass.py) is a choice in the page's menu; every section a
card — play it whole or any element alone, read its recipe and measurements, judge it, say why.
"""
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
SERVE = os.path.normpath(os.path.join(HERE, "..", "..", "arstro.solaris.compose.melody", "scripts", "serve.py"))
DATA = os.path.normpath(os.path.join(HERE, "..", "data"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", default="8788")
    ap.add_argument("--lab", default=os.path.expanduser("~/Music/Solaris/sound-lab"))
    a = ap.parse_args()
    os.makedirs(a.lab, exist_ok=True)
    os.execv(sys.executable, [sys.executable, SERVE, "--lab", a.lab, "--data", DATA, "--port", str(a.port),
                              "--title", "Sound Lab", "--subtitle", "arstro.solaris.sound.design — you are the approver"])


if __name__ == "__main__":
    main()
