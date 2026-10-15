#!/usr/bin/env python3
"""Measures the Prelude made in the window (Solaris R-SVC-10) against its score.

    measure.py <dir> [score.txt]

<dir> holds notes.txt (the song's pattern as `solaris-cc` prints it: `C4@0:2:100  # 60`) and prelude.wav
(the song rendered). The score is J. S. Bach's Prelude in C, BWV 846 (public domain), as score.py writes it.

  1. the notes: every note of the score at its pitch, start and length — 99 % at least, the goal all of them —
     and nothing the score does not have;
  2. the render: not silent, under 0 dBFS, as long as 140 beats at 66 bpm (and its tail); each bar's pitch
     classes are its chord's (the chord's share of the bar's chroma, and no other bar's chord fits it
     better); and its pulse is the figure's sixteenth at 66 bpm.
"""
import os, re, sys, wave
import numpy as np

d = sys.argv[1] if len(sys.argv) > 1 else "."
score_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "score.txt")
BPM, BEATS = 66.0, 140
ok = True


def check(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + what)
    ok &= bool(cond)


# ── 1. the notes ────────────────────────────────────────────────────────────────────────────
want = set()
for line in open(score_path):
    if line.startswith("#") or not line.strip():
        continue
    p, a, l = line.split()
    want.add((int(p), round(float(a) * 960), round(float(l) * 960)))
got = set()
for line in open(os.path.join(d, "notes.txt")):
    m = re.match(r"\s*\S+@([0-9.]+):([0-9.]+):(\d+)\s*#\s*(\d+)", line)
    if m:
        got.add((int(m.group(4)), round(float(m.group(1)) * 960), round(float(m.group(2)) * 960)))
same = len(want & got)
missing, extra = sorted(want - got), sorted(got - want)
share = 100.0 * same / len(want)
check(share >= 99.0, f"the notes: {same} of the score's {len(want)} at their pitch, start and length — {share:.2f} % (R-SVC-10: 99 % at least)")
check(not extra, f"nothing the score does not have ({len(extra)} extra)")
for p, a, l in (missing + extra)[:8]:
    print(f"     {'missing' if (p, a, l) in want else 'extra  '} {p}@{a / 960:g}:{l / 960:g}")
check(same == len(want) and not extra, "every note — the goal")

# ── 2. the render ───────────────────────────────────────────────────────────────────────────
w = wave.open(os.path.join(d, "prelude.wav"))
sr, n, ch, sw = w.getframerate(), w.getnframes(), w.getnchannels(), w.getsampwidth()
raw = np.frombuffer(w.readframes(n), np.uint8)
if sw == 3:
    b = raw.reshape(-1, 3)
    x = b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16
    x = np.where(x >= 1 << 23, x - (1 << 24), x) / 2.0 ** 23
elif sw == 4:
    x = np.frombuffer(raw.tobytes(), np.float32).astype(np.float64)
else:
    x = np.frombuffer(raw.tobytes(), np.int16) / 32768.0
x = x.reshape(-1, ch).mean(axis=1)
db = lambda v: 20 * np.log10(max(v, 1e-12))
spb = 60.0 / BPM
dur = len(x) / sr
check(BEATS * spb - 0.5 <= dur <= BEATS * spb + 12.0, f"length {dur:.2f} s (140 beats at 66 bpm = {BEATS * spb:.2f} s, then the tail)")
peak = float(np.max(np.abs(x)))
check(peak < 1.0, f"peak {db(peak):.2f} dBFS (under 0)")
rms = float(np.sqrt(np.mean(x[: int(BEATS * spb * sr)] ** 2)))
check(db(rms) > -40.0, f"RMS {db(rms):.1f} dBFS (not silence)")

# the replica, heard: the same song made straight from the score table (score.py --reference: the same piano,
# reverb and tempo) — render is pure, so a bar whose notes are the score's is the same SAMPLES, and a bar that
# differs says how much, in dB under the bar's own level
ref_path = os.path.join(d, "reference.wav")
if os.path.exists(ref_path):
    rw = wave.open(ref_path)
    rr = np.frombuffer(rw.readframes(rw.getnframes()), np.uint8)
    if rw.getsampwidth() == 3:
        b3 = rr.reshape(-1, 3)
        y = b3[:, 0].astype(np.int32) | b3[:, 1].astype(np.int32) << 8 | b3[:, 2].astype(np.int32) << 16
        y = np.where(y >= 1 << 23, y - (1 << 24), y) / 2.0 ** 23
    elif rw.getsampwidth() == 4:
        y = np.frombuffer(rr.tobytes(), np.float32).astype(np.float64)
    else:
        y = np.frombuffer(rr.tobytes(), np.int16) / 32768.0
    y = y.reshape(-1, rw.getnchannels()).mean(axis=1)
    m = min(len(x), len(y))
    errs = []
    for b in range(35):
        s0, s1 = int(b * 4 * spb * sr), min(m, int((b + 1) * 4 * spb * sr))
        e = float(np.sum((x[s0:s1] - y[s0:s1]) ** 2)) / max(float(np.sum(y[s0:s1] ** 2)), 1e-30)
        errs.append(10 * np.log10(max(e, 1e-30)))
    same_bars = sum(1 for e in errs if e <= -60.0)
    check(same_bars == 35, f"the replica sounds as the score: {same_bars} of 35 bars the same samples as the score's own render (≤ −60 dB apart)")
    for b, e in enumerate(errs):
        if e > -60.0:
            print(f"     bar {b + 1}: {e:.1f} dB from the score's render")
else:
    print("SKIP the replica against the score's render: no reference.wav (score.py --reference)")

# the pulse: the figure's sixteenths, at 66 bpm — the onset envelope's strongest period
hop = 256
frames = np.lib.stride_tricks.sliding_window_view(x[: int(BEATS * spb * sr)], 1024)[::hop]
spec = np.abs(np.fft.rfft(frames * np.hanning(1024), axis=1))
flux = np.maximum(np.diff(np.log1p(spec), axis=0), 0).sum(axis=1)
flux -= flux.mean()
ac = np.correlate(flux, flux, "full")[len(flux) - 1 :]
lags = np.arange(len(ac)) * hop / sr
lo, hi = np.searchsorted(lags, 0.12), np.searchsorted(lags, 0.40)
period = lags[lo + int(np.argmax(ac[lo:hi]))]
check(abs(period - spb / 4) / (spb / 4) < 0.03, f"pulse every {period * 1000:.0f} ms (a sixteenth at 66 bpm: {spb / 4 * 1000:.0f} ms)")

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
