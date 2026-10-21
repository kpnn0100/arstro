#!/usr/bin/env python3
"""Measures Afterglow as a progressive house master is judged (Solaris R-SVC-14) — and its melody, note by note.

    measure.py <dir with afterglow.wav and its stems: .ch_2 kick · .ch_6 bass · .ch_11 piano melody · .ch_12 lead
                · .ch_14 supersaw>

  loudness   ITU-R BS.1770-4 integrated (−11 … −7 LUFS) and the true peak under −1 dBTP
  the form   both drops at least 4 LU over the breakdown's piano; each build rising, brightening and landing
  the grid   the kick on every beat of the drops, within 2 ms
  the pump   the chords ducking under each kick, in dB
  the field  the low end (< 120 Hz) mono; the highs (> 1 kHz) wide
  the melody as written: its range, mostly steps, its climax in the second half of each phrase, home on a chord tone
             as heard: the lead's pitch on each written note of the first drop within 50 cents; the piano's in the
             breakdown; the lead's in the last drop a whole tone higher (the lift)
  the key    the first drop in D major, the last in E major
  the range  the sub to the air
"""
import os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter, resample_poly, butter, sosfiltfilt
from scipy.ndimage import median_filter

d = sys.argv[1] if len(sys.argv) > 1 else "."
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_script import MELODY, PHRASE, ASK, LIFT, m, PROG, CHORD

BPM, SR = 126.0, 48000
SPB = 60.0 / BPM
SECTIONS = {"intro": (0, 64), "verse": (64, 128), "breakdown": (128, 160), "build 1": (160, 192), "drop 1": (192, 256),
            "break": (256, 288), "build 2": (288, 320), "drop 2": (320, 384), "outro": (384, 416)}
DROPS = [SECTIONS["drop 1"], SECTIONS["drop 2"]]
ok = True


def check(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + what)
    ok &= bool(cond)


def load(name):
    sr, x = wavfile.read(os.path.join(d, name))
    scale = {np.dtype(np.int16): 32768.0, np.dtype(np.int32): 2.0 ** 31}.get(x.dtype, 1.0)
    x = x.astype(np.float64) / scale
    return (x.T if x.ndim == 2 else np.stack([x, x])), sr


def at(beat):
    return int(beat * SPB * SR)


def span(beats):
    return at(beats[0]), at(beats[1])


mix, sr = load("afterglow.wav")
check(sr == SR, f"rate {sr} Hz")
L, R = mix[0], mix[1]
dur = len(L) / sr
check(416 * SPB - 0.5 <= dur <= 416 * SPB + 12, f"length {dur:.1f} s (104 bars at 126 bpm = {416 * SPB:.1f} s, then the tail)")

# ── loudness ────────────────────────────────────────────────────────────────────────────────────────────────────
SHELF = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
RLB = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])
kw = [lfilter(*RLB, lfilter(*SHELF, c)) for c in mix]


def loudness(a, b):
    blk, hop = int(0.4 * sr), int(0.1 * sr)
    zs = np.array([sum(np.mean(c[s:s + blk] ** 2) for c in kw) for s in range(a, max(a + 1, b - blk + 1), hop)])
    zs = zs[-0.691 + 10 * np.log10(np.maximum(zs, 1e-30)) > -70]
    if not len(zs):
        return -99.0
    rel = -0.691 + 10 * np.log10(np.mean(zs)) - 10
    zs = zs[-0.691 + 10 * np.log10(zs) > rel]
    return float(-0.691 + 10 * np.log10(np.mean(zs)))


def short_term(a, b):
    win, hop = int(3 * sr), int(0.5 * sr)
    v = [sum(np.mean(c[s:s + win] ** 2) for c in kw) for s in range(a, max(a + 1, b - win + 1), hop)]
    return float(-0.691 + 10 * np.log10(max(np.mean(v), 1e-30)))


def rms_db(x):
    return 20 * np.log10(max(np.sqrt(np.mean(x ** 2)), 1e-12))


def band(x, lo, hi):
    sos = butter(4, [lo, hi] if lo else hi, btype="bandpass" if lo else "lowpass", fs=sr, output="sos")
    return sosfiltfilt(sos, x)


I = loudness(0, len(L))
check(-11.0 <= I <= -7.0, f"integrated loudness {I:.1f} LUFS (a progressive house master: −11 … −7)")
tp = 20 * np.log10(max(float(np.max(np.abs(resample_poly(c, 4, 1)))) for c in mix))
check(tp <= -1.0, f"true peak {tp:.2f} dBTP (under −1)")
st = {k: short_term(*span(v)) for k, v in SECTIONS.items()}
print("     sections, short-term LUFS: " + " · ".join(f"{k} {v:.1f}" for k, v in st.items()))
check(min(st["drop 1"], st["drop 2"]) >= st["breakdown"] + 4.0,
      f"the drops ({st['drop 1']:.1f}, {st['drop 2']:.1f}) at least 4 LU over the breakdown's piano ({st['breakdown']:.1f})")
check(st["drop 2"] >= st["drop 1"] - 0.5, f"the last drop no smaller than the first ({st['drop 2']:.1f} vs {st['drop 1']:.1f})")
check(min(st["drop 1"], st["drop 2"]) >= st["verse"] + 1.5, f"the drops the peak: at least 1.5 LU over the verse's groove ({st['verse']:.1f})")
for name, drop, (a, b) in [("build 1", "drop 1", SECTIONS["build 1"]), ("build 2", "drop 2", SECTIONS["build 2"])]:
    first, last = short_term(*span((a, a + 12))), short_term(*span((b - 13, b - 1)))
    check(last >= first + 3.0, f"{name} rises: {first:.1f} → {last:.1f} LUFS (≥ +3 LU)")
    hi0, hi1 = rms_db(band(L[at(a):at(a + 4)], 5000, 14000)), rms_db(band(L[at(b - 5):at(b - 1)], 5000, 14000))
    check(hi1 - hi0 >= 6.0, f"{name} brightens: 5–14 kHz up {hi1 - hi0:.1f} dB (the riser's sweep)")
    lands = short_term(*span((b, b + 16)))
    check(lands >= last - 0.5, f"{drop} lands: its first bars {lands:.1f} LUFS, not under the build's end ({last:.1f})")

# ── the grid and the pump ───────────────────────────────────────────────────────────────────────────────────────
kick, _ = load("afterglow.ch_2.wav")
km = np.abs(kick.mean(axis=0))
miss, worst = 0, 0.0
for a, b in DROPS:
    for beat in range(a, b):
        s = at(beat)
        pk = np.max(km[s:s + 4800])
        if pk < 0.01:
            miss += 1
            continue
        onset = np.argmax(km[s - 480:s + 480] > 0.25 * pk) - 480
        worst = max(worst, abs(onset) / sr * 1000)
        miss += abs(onset) > 96
check(miss == 0 and worst <= 2.0, f"the kick on all {2 * 64} beats of the drops: {miss} off, the worst {worst:.2f} ms")
sup, _ = load("afterglow.ch_14.wav")
cm = sup.mean(axis=0)
dips = []
for a, b in DROPS:
    for beat in range(a + 1, b - 1):
        t = beat * SPB
        dips.append(rms_db(cm[int((t + SPB - 0.06) * sr):int((t + SPB - 0.02) * sr)]) - rms_db(cm[int((t + 0.03) * sr):int((t + 0.07) * sr)]))
check(np.median(dips) >= 4.0, f"the pump: the chords {np.median(dips):.1f} dB under themselves just after each kick (≥ 4 dB)")

# ── the field ───────────────────────────────────────────────────────────────────────────────────────────────────
corr, width = [], []
for a, b in [span(x) for x in DROPS]:
    lo_l, lo_r = band(L[a:b], 0, 120), band(R[a:b], 0, 120)
    corr.append(float(np.sum(lo_l * lo_r) / np.sqrt(np.sum(lo_l ** 2) * np.sum(lo_r ** 2))))
    hl, hr = band(L[a:b], 1000, 16000), band(R[a:b], 1000, 16000)
    width.append(float(np.sum(((hl - hr) / 2) ** 2) / np.sum(((hl + hr) / 2) ** 2)))
check(min(corr) >= 0.95, f"the low end mono: L/R correlation under 120 Hz {min(corr):.3f} (≥ 0.95)")
check(min(width) >= 0.10, f"the highs wide: side/mid energy over 1 kHz {10 * np.log10(min(width)):.1f} dB (≥ −10 dB)")

# ── the melody, as written ──────────────────────────────────────────────────────────────────────────────────────
pitches = [m(n) for _, _, n in MELODY]
steps = [abs(b - a) for a, b in zip(pitches, pitches[1:])]
span_st = max(pitches) - min(pitches)
check(span_st <= 16, f"the melody's range {span_st} semitones (a voice's: ≤ 16)")
stepwise = sum(s <= 2 for s in steps) / len(steps)
check(stepwise >= 0.6, f"the melody moves mostly by step: {100 * stepwise:.0f} % of its intervals a tone or less (≥ 60 %)")
top = max(pitches)
climaxes = [a for a, _, n in MELODY if m(n) == top]
check(all((a % 32) >= 16 for a in climaxes), f"its climax ({max(MELODY, key=lambda x: m(x[2]))[2]}) in the second half of each phrase: beats {climaxes}")
last_chord = CHORD[PROG[-1]]
check(pitches[-1] % 12 in {m(n) % 12 for n in last_chord}, f"it ends home: {MELODY[-1][2]}, a tone of the last chord ({PROG[-1]})")

# ── the melody, as heard ────────────────────────────────────────────────────────────────────────────────────────
def f0(x, fmin=150.0, fmax=1600.0, thresh=0.12):
    """YIN: the fundamental of a short frame — the cumulative-mean-normalised difference, its first dip under
    `thresh`, refined by a parabola."""
    tmax, tmin = int(sr / fmin), int(sr / fmax)
    w = len(x) - tmax
    if w < tmax:
        return 0.0
    dif = np.array([np.sum((x[:w] - x[t:t + w]) ** 2) for t in range(tmax + 1)])
    cm_ = np.ones_like(dif)
    cm_[1:] = dif[1:] * np.arange(1, len(dif)) / np.maximum(np.cumsum(dif[1:]), 1e-20)
    t = tmin
    while t < tmax and cm_[t] >= thresh:
        t += 1
    if t >= tmax:
        t = tmin + int(np.argmin(cm_[tmin:tmax]))
    while t + 1 < tmax and cm_[t + 1] < cm_[t]:
        t += 1
    a_, b_, c_ = cm_[t - 1], cm_[t], cm_[t + 1]
    den = a_ - 2 * b_ + c_
    tt = t + (0.5 * (a_ - c_) / den if abs(den) > 1e-12 else 0.0)
    return sr / tt


def follows(stem, base, notes, shift=0):
    """How many written notes the stem plays within 50 cents (each read 0.15 beat in, up to 0.8 beat)."""
    x = stem.mean(axis=0)
    good, cents = 0, []
    for a, l, n in notes:
        s = at(base + a + 0.15)
        e = min(at(base + a + min(l, 0.95)), s + 4096 + int(sr / 150) + 1)
        f = f0(x[s:e])
        want = 440.0 * 2 ** ((m(n) + shift - 69) / 12)
        c = 1200 * np.log2(f / want) if f > 0 else 9999.0
        cents.append(c)
        good += abs(c) <= 50
    return good, len(notes), cents


lead, _ = load("afterglow.ch_12.wav")
g1, n1, c1 = follows(lead, DROPS[0][0], MELODY)
check(g1 >= 0.9 * n1, f"the lead sings the melody in the first drop: {g1} of {n1} notes within 50 cents (median {np.median(np.abs(c1)):.0f})")
piano, _ = load("afterglow.ch_11.wav")
gp, npn, cp = follows(piano, SECTIONS["breakdown"][0], PHRASE + ASK)
check(gp >= 0.9 * npn, f"the piano sings it in the breakdown: {gp} of {npn} notes within 50 cents (median {np.median(np.abs(cp)):.0f})")
g2, n2, c2 = follows(lead, DROPS[1][0], MELODY, LIFT)
check(g2 >= 0.9 * n2, f"the last drop lifts it a whole tone: {g2} of {n2} notes within 50 cents of the written +{LIFT} (median {np.median(np.abs(c2)):.0f})")

# ── the key, and the lift ───────────────────────────────────────────────────────────────────────────────────────
nfft = 8192
freqs = np.fft.rfftfreq(nfft, 1 / sr)
sel = (freqs >= 250) & (freqs <= 2000)  # over the bass: a saw's seventh harmonic falls between the scale's notes
pcs = (np.round(12 * np.log2(freqs[sel] / 440.0)).astype(int) + 69) % 12
mono = (L + R) / 2
NAMES12 = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
major = [0, 2, 4, 5, 7, 9, 11]
keys = []
for a, b in [span(x) for x in DROPS]:
    chroma = np.zeros(12)
    for s in range(a, b - nfft, nfft):
        mag = np.abs(np.fft.rfft(mono[s:s + nfft] * np.hanning(nfft)))[sel]
        floor = median_filter(mag, size=31)
        peak = (mag > 4 * floor) & (mag >= np.roll(mag, 1)) & (mag >= np.roll(mag, -1))
        np.add.at(chroma, pcs[peak], mag[peak])
    fits = sorted(((chroma[[(t + k) % 12 for k in major]].sum() / chroma.sum(), t) for t in range(12)), reverse=True)
    keys.append(fits[0])
check(keys[0][1] == 2 and keys[0][0] >= 0.8, f"the first drop in D major: {100 * keys[0][0]:.0f} % of its pitched peaks ({NAMES12[keys[0][1]]} major's notes the best)")
check(keys[1][1] == 4 and keys[1][0] >= 0.8, f"the last drop in E major, a whole tone up: {100 * keys[1][0]:.0f} % ({NAMES12[keys[1][1]]} major's notes the best)")

# ── the range ───────────────────────────────────────────────────────────────────────────────────────────────────
a, b = span(DROPS[0])
f = np.fft.rfftfreq(16384, 1 / sr)
spec = np.zeros(len(f))
for s in range(a, b - 16384, 16384):
    spec += np.abs(np.fft.rfft(mono[s:s + 16384] * np.hanning(16384))) ** 2
octs = [(40, 80), (80, 160), (160, 320), (320, 640), (640, 1280), (1280, 2560), (2560, 5120), (5120, 10240), (10240, 16000)]
lv = [10 * np.log10(spec[(f >= lo) & (f < hi)].sum() + 1e-30) for lo, hi in octs]
loud = max(lv)
print("     the drop's octaves, dB under the loudest: " + " · ".join(f"{lo}: {l - loud:.0f}" for (lo, _), l in zip(octs, lv)))
check(lv[0] - loud >= -12 and lv[-1] - loud >= -35, f"the range: the sub {lv[0] - loud:.0f} dB and the air (10–16 kHz) {lv[-1] - loud:.0f} dB under the loudest")

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
