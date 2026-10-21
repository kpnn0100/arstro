#!/usr/bin/env python3
"""Measures Starward as an electro / future house master in the NCS style is judged (Solaris R-SVC-16).

    measure.py <dir with starward.wav and its stems: .ch_2 kick · .ch_3 clap · .ch_8 bass · .ch_9 chords · .ch_11 lead>

  loudness   ITU-R BS.1770-4 integrated (a bright, loud master: −9 … −5 LUFS) and the true peak under −1 dBTP
  the form   both drops at least 4 LU over the breakdown and 3 over the verse; each build rising, brightening, landing
  the gap    the beat before each drop at least 12 dB under the drop's first bar
  the floor  the kick on every beat of the drops within 1 ms; the clap on two and four, never on one and three
  the bounce the bass on the off-beats: its loudness folded on the beat loudest after the half, quiet where the kick is
  the pump   the chords' loudness folded on the SONG's beat matching the shaper's pump curve (r ≥ 0.9, ≥ 10 dB)
  the hook   as written — one rhythm in every bar, its two-bar motif returning; as heard — the lead's pitch on its
             notes in both drops (YIN, within 50 cents)
  the field  the low end (< 120 Hz) mono; the highs (> 1 kHz) wide
  the key    the drops' pitched peaks over 250 Hz in G major
  the shine  the drop's air (10–16 kHz) within 20 dB of its loudest octave — a bright master; the low octave present
"""
import os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter, resample_poly, butter, sosfiltfilt
from scipy.ndimage import median_filter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_script import BPM, HOOK, RHYTHM, BARS, PUMP, SMOOTH, m  # noqa: E402

d = sys.argv[1] if len(sys.argv) > 1 else "."
SR = 48000
SPB = 60.0 / BPM
SECTIONS = {"intro": (0, 32), "verse": (32, 64), "build 1": (64, 96), "drop 1": (96, 160), "breakdown": (160, 192),
            "build 2": (192, 224), "drop 2": (224, 288), "outro": (288, 320)}
DROPS = [SECTIONS["drop 1"], SECTIONS["drop 2"]]
ok = True


def check(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + what)
    ok &= bool(cond)


def load(name):
    sr, x = wavfile.read(os.path.join(d, name))
    if x.dtype == np.int16:
        x = x / 32768.0
    elif x.dtype == np.int32:
        x = x / 2.0 ** 31
    x = x.astype(np.float64)
    return (x.T if x.ndim == 2 else np.stack([x, x])), sr


def at(beat):
    return int(round(beat * SPB * SR))


def span(beats):
    return at(beats[0]), at(beats[1])


mix, sr = load("starward.wav")
check(sr == SR, f"rate {sr} Hz")
L, R = mix[0], mix[1]
dur = len(L) / sr
check(320 * SPB - 0.5 <= dur <= 320 * SPB + 10, f"length {dur:.1f} s (80 bars at {BPM} bpm = {320 * SPB:.1f} s, then the tail)")

# ── loudness: BS.1770-4 ─────────────────────────────────────────────────────────────────────────────────────────
SHELF = ([1.53512485958697, -2.69169618940638, 1.19839281085285], [1.0, -1.69065929318241, 0.73248077421585])
RLB = ([1.0, -2.0, 1.0], [1.0, -1.99004745483398, 0.99007225036621])
kw = [lfilter(*RLB, lfilter(*SHELF, c)) for c in mix]


def loudness(a, b):
    blk, hop = int(0.4 * sr), int(0.1 * sr)
    zs = np.array([sum(np.mean(c[s:s + blk] ** 2) for c in kw) for s in range(a, max(a + 1, b - blk + 1), hop)])
    lk = -0.691 + 10 * np.log10(np.maximum(zs, 1e-30))
    zs = zs[lk > -70]
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


def loud2ms(x):
    """Loudness over a 2 ms window — a moving average never rings below zero."""
    return np.sqrt(np.convolve(x ** 2, np.ones(96) / 96, mode="same") + 1e-12)


I = loudness(0, len(L))
check(-9.0 <= I <= -5.0, f"integrated loudness {I:.1f} LUFS (a bright, loud master: −9 … −5)")
tp = 20 * np.log10(max(float(np.max(np.abs(resample_poly(c, 4, 1)))) for c in mix))
check(tp <= -1.0, f"true peak {tp:.2f} dBTP (under −1)")
st = {k: short_term(*span(v)) for k, v in SECTIONS.items()}
print("     sections, short-term LUFS: " + " · ".join(f"{k} {v:.1f}" for k, v in st.items()))
check(min(st["drop 1"], st["drop 2"]) >= st["breakdown"] + 4.0,
      f"the drops ({st['drop 1']:.1f}, {st['drop 2']:.1f}) at least 4 LU over the breakdown ({st['breakdown']:.1f})")
check(min(st["drop 1"], st["drop 2"]) >= st["verse"] + 3.0, f"…and at least 3 LU over the verse ({st['verse']:.1f})")
for name, drop, (a, b) in [("build 1", "drop 1", SECTIONS["build 1"]), ("build 2", "drop 2", SECTIONS["build 2"])]:
    first, last = short_term(*span((a, a + 12))), short_term(*span((b - 13, b - 1)))
    check(last >= first + 3.0, f"{name} rises: {first:.1f} → {last:.1f} LUFS (≥ +3 LU)")
    hi0 = rms_db(band(L[at(a):at(a + 4)], 5000, 14000))
    hi1 = rms_db(band(L[at(b - 5):at(b - 1)], 5000, 14000))
    check(hi1 - hi0 >= 6.0, f"{name} brightens: 5–14 kHz up {hi1 - hi0:.1f} dB")
    lands = short_term(*span((b, b + 16)))
    check(lands >= last - 0.5, f"{drop} lands: its first bars {lands:.1f} LUFS, not under the build's end ({last:.1f})")
for name, (a, _) in [("drop 1", DROPS[0]), ("drop 2", DROPS[1])]:
    gap = rms_db(mix[:, at(a - 0.85):at(a - 0.05)].mean(axis=0))
    hit = rms_db(mix[:, at(a):at(a + 4)].mean(axis=0))
    check(hit - gap >= 12.0, f"the gap before {name}: {gap:.1f} dBFS, the drop's first bar {hit:.1f} — {hit - gap:.1f} dB apart (≥ 12)")


def onset_ms(x, beat, before=0.02):
    """Where a hit near `beat` starts (ms from the beat): the first sample over a quarter of its peak."""
    s = at(beat)
    pk = np.max(np.abs(x[s:s + 2400]))
    if pk < 1e-3:
        return None
    w = int(before * sr)
    seg = np.abs(x[s - w:s + 2400])
    return (np.argmax(seg > 0.25 * pk) - w) / sr * 1000


# ── the floor: four on the floor, the clap on two and four ──────────────────────────────────────────────────────
kick = load("starward.ch_2.wav")[0].mean(axis=0)
offs = [onset_ms(kick, b) for a, e in DROPS for b in range(a, e)]
hit = [o for o in offs if o is not None and abs(o) <= 1.0]
check(len(hit) == len(offs), f"the kick on every drop beat: {len(hit)} of {len(offs)} within 1 ms "
      f"(the worst {max(abs(o) for o in offs if o is not None):.2f} ms)")
clap = load("starward.ch_3.wav")[0].mean(axis=0)
on, off = 0, 0
for a, e in DROPS:
    for bar in range(a, e, 4):
        pk = max(np.max(np.abs(clap[at(bar + k):at(bar + k) + 2400])) for k in (1, 3))
        on += sum(o is not None and abs(o) <= 2.0 for o in (onset_ms(clap, bar + k) for k in (1, 3)))
        off += sum(np.max(np.abs(clap[at(bar + k):at(bar + k) + 1200])) > 0.25 * pk for k in (0, 2))
check(on == 64 and off == 0, f"the clap on two and four of all 32 drop bars ({on} of 64 within 2 ms), {off} hits on one or three")

# ── the bounce: the bass between the kicks ──────────────────────────────────────────────────────────────────────
bass = loud2ms(load("starward.ch_8.wav")[0].mean(axis=0))
fold = np.zeros(32)
for a, e in DROPS:
    s, t = at(a), at(e)
    phi = np.mod(np.arange(s, t) / sr / SPB, 1.0)
    bins = np.minimum((phi * 32).astype(int), 31)
    db = 20 * np.log10(bass[s:t])
    fold += np.array([np.mean(db[bins == k]) for k in range(32)]) / 2
on_beat = float(np.mean(fold[0:4]))         # the first eighth of a beat: where the kick is
after_half = float(np.mean(fold[16:20]))    # the eighth after the half
peak_at = (int(np.argmax(fold)) + 0.5) / 32
check(after_half - on_beat >= 12.0 and peak_at >= 0.5,
      f"the bass bounces on the off-beats: folded on the beat, loudest {peak_at:.2f} of the way through, the half's "
      f"eighth {after_half - on_beat:.1f} dB over the kick's (≥ 12)")

# ── the pump: the chords on the song's beat ─────────────────────────────────────────────────────────────────────
chords = loud2ms(load("starward.ch_9.wav")[0].mean(axis=0))
pump_r, pump_swing = [], []
for a, e in DROPS:
    s, t = at(a + 0.05), at(e)
    phi = np.mod(np.arange(s, t) / sr / SPB, 1.0)
    bins = np.minimum((phi * 48).astype(int), 47)
    db = 20 * np.log10(chords[s:t])
    f_ = np.array([np.mean(db[bins == k]) for k in range(48)])
    # the shaper's own pump (SH2, SH3) through its smoothing (SH4) and the same 2 ms window
    shape = (1 - np.exp(-phi / 0.18)) / (1 - np.exp(-1 / 0.18))
    g = 1 - PUMP * (1 - shape)
    alpha = 1 - np.exp(-1 / (sr * SMOOTH / 1000))
    g = lfilter([alpha], [1, alpha - 1], g, zi=[g[0] * (1 - alpha)])[0]
    gdb = 10 * np.log10(np.convolve(g ** 2, np.ones(96) / 96, mode="same") + 1e-12)
    ideal = np.array([np.mean(gdb[bins == k]) for k in range(48)])
    pump_r.append(float(np.corrcoef(f_, ideal)[0, 1]))
    pump_swing.append(float(f_.max() - f_.min()))
check(min(pump_r) >= 0.9 and min(pump_swing) >= 10.0,
      f"the pump: the chords folded on the song's beat match the shaper's pump r = {min(pump_r):.2f} … {max(pump_r):.2f}, "
      f"swinging {min(pump_swing):.1f} dB — with no compressor")

# ── the hook ────────────────────────────────────────────────────────────────────────────────────────────────────
same_rhythm = sum(sorted(a - b * 4 for a, _, _ in HOOK if b * 4 <= a < b * 4 + 4) == [a for a, _ in RHYTHM] for b in range(8))
check(same_rhythm == 8, f"the hook as written: one rhythm in {same_rhythm} of 8 bars (four eighths, two dotted eighths, an eighth)")
check(BARS[0] == BARS[4] and BARS[1] == BARS[5] and BARS[2] == BARS[6] and BARS[3] != BARS[7],
      "…its two-bar motif returning at bars 5–6, bar 3 at 7; bar 4 asks (up to A5), bar 8 answers (down to B4)")


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


lead = load("starward.ch_11.wav")[0].mean(axis=0)
good, cents = 0, []
for a0, _ in DROPS:
    for base in (a0, a0 + 32):
        for a, l, n in HOOK:
            s = at(base + a + 0.15)
            e = min(at(base + a + l - 0.02), s + 4096 + int(sr / 150) + 1)
            f = f0(lead[s:e])
            c = 1200 * np.log2(f / (440.0 * 2 ** ((m(n) - 69) / 12))) if f > 0 else 9999.0
            cents.append(c)
            good += abs(c) <= 50
check(good >= 0.9 * len(cents), f"the hook as heard: the lead on {good} of {len(cents)} of the drops' notes within 50 cents "
      f"(median {np.median(np.abs(cents)):.0f})")

# ── the field ───────────────────────────────────────────────────────────────────────────────────────────────────
corr, width = [], []
for a, b in [span(x) for x in DROPS]:
    lo_l, lo_r = band(L[a:b], 0, 120), band(R[a:b], 0, 120)
    corr.append(float(np.sum(lo_l * lo_r) / np.sqrt(np.sum(lo_l ** 2) * np.sum(lo_r ** 2))))
    hl, hr = band(L[a:b], 1000, 16000), band(R[a:b], 1000, 16000)
    width.append(float(np.sum(((hl - hr) / 2) ** 2) / np.sum(((hl + hr) / 2) ** 2)))
check(min(corr) >= 0.95, f"the low end mono: L/R correlation under 120 Hz {min(corr):.3f} (≥ 0.95)")
check(min(width) >= 0.10, f"the highs wide: side/mid energy over 1 kHz {10 * np.log10(min(width)):.1f} dB (≥ −10 dB)")

# ── the key ─────────────────────────────────────────────────────────────────────────────────────────────────────
nfft = 8192
freqs = np.fft.rfftfreq(nfft, 1 / sr)
sel = (freqs >= 250) & (freqs <= 2000)
pcs = (np.round(12 * np.log2(freqs[sel] / 440.0)).astype(int) + 69) % 12
chroma = np.zeros(12)
mono = (L + R) / 2
for a, b in [span(x) for x in DROPS]:
    for s in range(a, b - nfft, nfft):
        mag = np.abs(np.fft.rfft(mono[s:s + nfft] * np.hanning(nfft)))[sel]
        floor = median_filter(mag, size=31)
        peak = (mag > 4 * floor) & (mag >= np.roll(mag, 1)) & (mag >= np.roll(mag, -1))
        np.add.at(chroma, pcs[peak], mag[peak])
major = [0, 2, 4, 5, 7, 9, 11]
fits = sorted(((chroma[[(t + k) % 12 for k in major]].sum() / chroma.sum(), t) for t in range(12)), reverse=True)
NAMES12 = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
best, second = fits[0], fits[1]
check(best[1] == 7 and best[0] >= 0.8,
      f"the key: G major holds {100 * best[0]:.0f} % of the drops' pitched peaks — the next set ({NAMES12[second[1]]} major's) {100 * second[0]:.0f} %")

# ── the shine ───────────────────────────────────────────────────────────────────────────────────────────────────
a, b = span(DROPS[0])
f = np.fft.rfftfreq(16384, 1 / sr)
spec = np.zeros(len(f))
for s in range(a, b - 16384, 16384):
    spec += np.abs(np.fft.rfft(mono[s:s + 16384] * np.hanning(16384))) ** 2
octs = [(40, 80), (80, 160), (160, 320), (320, 640), (640, 1280), (1280, 2560), (2560, 5120), (5120, 10240), (10240, 16000)]
lv = [10 * np.log10(spec[(f >= lo) & (f < hi)].sum() + 1e-30) for lo, hi in octs]
top = max(lv)
print("     the drop's octaves, dB under the loudest: " + " · ".join(f"{lo}: {l - top:.0f}" for (lo, _), l in zip(octs, lv)))
check(lv[-1] - top >= -20 and lv[0] - top >= -8, f"the shine: the air (10–16 kHz) {lv[-1] - top:.0f} dB under the loudest "
      f"octave (≥ −20), the low octave {lv[0] - top:.0f} (≥ −8)")

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
