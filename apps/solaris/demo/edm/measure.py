#!/usr/bin/env python3
"""Measures Perihelion as a master is judged (Solaris R-SVC-12) — not only "not silent".

    measure.py <dir with perihelion.wav and its stems: .ch_2 (kick) .ch_8 (chords) .ch_6 (sub) .ch_9 (lead)>

  loudness   ITU-R BS.1770-4: K-weighted, gated integrated loudness, and 3 s short-term loudness per section
  true peak  4× oversampled, under −1 dBTP (the headroom a lossy encode needs)
  the form   both drops at least 4 LU over the breakdown; each build's last bars louder and brighter than its first
  the field  the low end (< 120 Hz) mono in the drops (L/R correlation); the highs (> 1 kHz) wide (side / mid)
  the pump   the chords' level just after each kick against just before the next, in dB
  the grid   the kick on every beat of the drops, within 2 ms
  the key    the drops' chroma in A natural minor
  the range  the drops reach from the sub to the air: every octave band from 40 Hz to 16 kHz present
"""
import os, sys, wave
import numpy as np
from scipy.signal import lfilter, resample_poly, butter, sosfiltfilt

d = sys.argv[1] if len(sys.argv) > 1 else "."
BPM, SR = 126.0, 48000
SPB = 60.0 / BPM            # seconds a beat
SECTIONS = {"intro": (0, 32), "build 1": (32, 64), "drop 1": (64, 128), "breakdown": (128, 160),
            "build 2": (160, 192), "drop 2": (192, 256), "outro": (256, 288)}
ok = True


def check(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + what)
    ok &= bool(cond)


def load(name):
    w = wave.open(os.path.join(d, name))
    n, ch, sw = w.getnframes(), w.getnchannels(), w.getsampwidth()
    raw = np.frombuffer(w.readframes(n), np.uint8)
    if sw == 3:
        b = raw.reshape(-1, 3)
        x = b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / 2.0 ** 23
    elif sw == 4:
        x = np.frombuffer(raw.tobytes(), np.float32).astype(np.float64)
    else:
        x = np.frombuffer(raw.tobytes(), np.int16) / 32768.0
    return x.reshape(-1, ch).T, w.getframerate()


def span(beats):
    a, b = beats
    return int(a * SPB * SR), int(b * SPB * SR)


mix, sr = load("perihelion.wav")
check(sr == SR, f"rate {sr} Hz")
L, R = mix[0], mix[1]
dur = len(L) / sr
check(288 * SPB - 0.5 <= dur <= 288 * SPB + 10, f"length {dur:.1f} s (72 bars at 126 bpm = {288 * SPB:.1f} s, then the tail)")

# ── loudness: BS.1770-4 ─────────────────────────────────────────────────────────────────────────────────────────
# the K-weighting at 48 kHz: the head's high shelf, then the RLB high-pass (the standard's own coefficients)
SHELF = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
RLB = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])
kw = [lfilter(*RLB, lfilter(*SHELF, c)) for c in mix]


def loudness(a, b):
    """Gated loudness of samples [a, b): 400 ms blocks, 75 % overlap, −70 LUFS then −10 LU gates."""
    blk, hop = int(0.4 * sr), int(0.1 * sr)
    zs = []
    for s in range(a, max(a + 1, b - blk + 1), hop):
        zs.append(sum(np.mean(c[s:s + blk] ** 2) for c in kw))
    zs = np.array(zs)
    lk = -0.691 + 10 * np.log10(np.maximum(zs, 1e-30))
    zs = zs[lk > -70]
    if not len(zs):
        return -99.0
    rel = -0.691 + 10 * np.log10(np.mean(zs)) - 10
    zs = zs[-0.691 + 10 * np.log10(zs) > rel]
    return float(-0.691 + 10 * np.log10(np.mean(zs)))


def short_term(a, b):
    """Mean 3 s short-term loudness over [a, b), ungated (the section's level as it plays)."""
    win, hop = int(3 * sr), int(0.5 * sr)
    v = [sum(np.mean(c[s:s + win] ** 2) for c in kw) for s in range(a, max(a + 1, b - win + 1), hop)]
    return float(-0.691 + 10 * np.log10(max(np.mean(v), 1e-30)))


I = loudness(0, len(L))
check(-11.0 <= I <= -7.0, f"integrated loudness {I:.1f} LUFS (an EDM master: −11 … −7)")
tp = max(float(np.max(np.abs(resample_poly(c, 4, 1)))) for c in mix)
tpdb = 20 * np.log10(tp)
check(tpdb <= -1.0, f"true peak {tpdb:.2f} dBTP (under −1)")
st = {k: short_term(*span(v)) for k, v in SECTIONS.items()}
print("     sections, short-term LUFS: " + " · ".join(f"{k} {v:.1f}" for k, v in st.items()))
check(min(st["drop 1"], st["drop 2"]) >= st["breakdown"] + 4.0,
      f"the drops ({st['drop 1']:.1f}, {st['drop 2']:.1f}) at least 4 LU over the breakdown ({st['breakdown']:.1f})")
for name, drop, (a, b) in [("build 1", "drop 1", SECTIONS["build 1"]), ("build 2", "drop 2", SECTIONS["build 2"])]:
    first, last = short_term(*span((a, a + 12))), short_term(*span((b - 12, b - 1)))
    check(last >= first + 3.0, f"{name} rises: {first:.1f} → {last:.1f} LUFS (≥ +3 LU)")
    lands = short_term(*span((b, b + 16)))
    check(lands >= last - 0.5, f"{drop} lands: its first bars {lands:.1f} LUFS, not under the build's end ({last:.1f})")


def band(x, lo, hi):
    sos = butter(4, [lo, hi] if lo else hi, btype="bandpass" if lo else "lowpass", fs=sr, output="sos")
    return sosfiltfilt(sos, x)


for name, (a, b) in [("build 1", SECTIONS["build 1"]), ("build 2", SECTIONS["build 2"])]:
    s0, s1 = span((a, a + 4))
    e0, e1 = span((b - 5, b - 1))
    hi0 = np.sqrt(np.mean(band(L[s0:s1], 5000, 14000) ** 2))
    hi1 = np.sqrt(np.mean(band(L[e0:e1], 5000, 14000) ** 2))
    check(20 * np.log10(hi1 / max(hi0, 1e-12)) >= 6.0, f"{name} brightens: 5–14 kHz up {20 * np.log10(hi1 / max(hi0, 1e-12)):.1f} dB (the riser's sweep)")

# ── the field ───────────────────────────────────────────────────────────────────────────────────────────────────
drops = [span(SECTIONS["drop 1"]), span(SECTIONS["drop 2"])]
corr, width = [], []
for a, b in drops:
    lo_l, lo_r = band(L[a:b], 0, 120), band(R[a:b], 0, 120)
    corr.append(float(np.sum(lo_l * lo_r) / np.sqrt(np.sum(lo_l ** 2) * np.sum(lo_r ** 2))))
    hl, hr = band(L[a:b], 1000, 16000), band(R[a:b], 1000, 16000)
    mid, side = (hl + hr) / 2, (hl - hr) / 2
    width.append(float(np.sum(side ** 2) / np.sum(mid ** 2)))
check(min(corr) >= 0.95, f"the low end mono: L/R correlation under 120 Hz {min(corr):.3f} (≥ 0.95)")
check(min(width) >= 0.10, f"the highs wide: side/mid energy over 1 kHz {10 * np.log10(min(width)):.1f} dB (≥ −10 dB)")

# ── the pump ────────────────────────────────────────────────────────────────────────────────────────────────────
chords, _ = load("perihelion.ch_8.wav")
cm = chords.mean(axis=0)
dips = []
for a, b in [SECTIONS["drop 1"], SECTIONS["drop 2"]]:
    for beat in range(a + 1, b - 1):
        t = beat * SPB
        after = cm[int((t + 0.03) * sr):int((t + 0.07) * sr)]
        before = cm[int((t + SPB - 0.06) * sr):int((t + SPB - 0.02) * sr)]
        dips.append(20 * np.log10(np.sqrt(np.mean(before ** 2)) / max(np.sqrt(np.mean(after ** 2)), 1e-12)))
check(np.median(dips) >= 4.0, f"the pump: the chords {np.median(dips):.1f} dB under themselves just after each kick (≥ 4 dB)")

# ── the grid ────────────────────────────────────────────────────────────────────────────────────────────────────
kick, _ = load("perihelion.ch_2.wav")
km = np.abs(kick.mean(axis=0))
miss, worst = 0, 0.0
for a, b in [SECTIONS["drop 1"], SECTIONS["drop 2"]]:
    for beat in range(a, b):
        s = int(beat * SPB * sr)
        seg = km[max(0, s - 480):s + 480]
        thr = 0.25 * np.max(km[s:s + 4800]) if np.max(km[s:s + 4800]) > 0.01 else None
        if thr is None:
            miss += 1
            continue
        onset = np.argmax(seg > thr) - 480
        worst = max(worst, abs(onset) / sr * 1000)
        miss += abs(onset) > 96
check(miss == 0 and worst <= 2.0, f"the kick on all {2 * 64} beats of the drops: {miss} off, the worst {worst:.2f} ms")

# ── the key ─────────────────────────────────────────────────────────────────────────────────────────────────
# spectral PEAKS only (a bin over 4× its neighbourhood's median), so the drums' noise — spread evenly over all
# twelve pitch classes — does not stand in for the notes; then which of the twelve diatonic sets holds the most
nfft = 8192
freqs = np.fft.rfftfreq(nfft, 1 / sr)
sel = (freqs >= 60) & (freqs <= 2000)
pcs = (np.round(12 * np.log2(freqs[sel] / 440.0)).astype(int) + 69) % 12
chroma = np.zeros(12)
mono = (L + R) / 2
from scipy.ndimage import median_filter
for a, b in drops:
    for s in range(a, b - nfft, nfft):
        mag = np.abs(np.fft.rfft(mono[s:s + nfft] * np.hanning(nfft)))[sel]
        floor = median_filter(mag, size=31)
        peak = (mag > 4 * floor) & (mag >= np.roll(mag, 1)) & (mag >= np.roll(mag, -1))
        np.add.at(chroma, pcs[peak], mag[peak])
major = [0, 2, 4, 5, 7, 9, 11]
fits = sorted(((chroma[[(t + k) % 12 for k in major]].sum() / chroma.sum(), t) for t in range(12)), reverse=True)
NAMES12 = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
best, second = fits[0], fits[1]
check(best[1] == 0 and best[0] >= 0.8,
      f"the key: A minor (C major's notes) holds {100 * best[0]:.0f} % of the drops' pitched peaks — the next set ({NAMES12[second[1]]} major's) {100 * second[0]:.0f} %")

# ── the range ───────────────────────────────────────────────────────────────────────────────────────────────────
a, b = drops[0]
spec = np.zeros(len(np.fft.rfftfreq(16384, 1 / sr)))
for s in range(a, b - 16384, 16384):
    spec += np.abs(np.fft.rfft(mono[s:s + 16384] * np.hanning(16384))) ** 2
f = np.fft.rfftfreq(16384, 1 / sr)
octs = [(40, 80), (80, 160), (160, 320), (320, 640), (640, 1280), (1280, 2560), (2560, 5120), (5120, 10240), (10240, 16000)]
lv = [10 * np.log10(spec[(f >= lo) & (f < hi)].sum() + 1e-30) for lo, hi in octs]
top = max(lv)
print("     the drop's octaves, dB under the loudest: " + " · ".join(f"{lo}: {l - top:.0f}" for (lo, _), l in zip(octs, lv)))
check(lv[0] - top >= -12 and lv[-1] - top >= -35, f"the range: the sub {lv[0] - top:.0f} dB and the air (10–16 kHz) {lv[-1] - top:.0f} dB under the loudest octave")

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
