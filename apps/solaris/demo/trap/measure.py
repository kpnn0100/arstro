#!/usr/bin/env python3
"""Measures Magnetar as a trap master is judged (Solaris R-SVC-13) — the drop, not only "not silent".

    measure.py <dir with magnetar.wav and its stems: .ch_2 kick · .ch_3 808 · .ch_4 snare · .ch_5 hats · .ch_12 supersaw>

  loudness   ITU-R BS.1770-4 integrated (a trap master: −10 … −6 LUFS) and the true peak under −1 dBTP
  the form   both drops at least 4 LU over the breakdown; each build rising and brightening; each drop landing
  the gap    the beat before each drop at least 12 dB under the drop's first bar — the silence that makes it hit
  half time  the snare on the third beat of every bar of the drops, within 2 ms — and never on the backbeat
  the 808    on each bar's root (F, Db, Bb, C) a quarter-beat in; and GLIDING at the four slides of the riff: the
             fundamental's cycles pass through the pitches between, not a jump
  the hats   rolls: thirty-second and triplet gaps among the drops' hat onsets, not only eighths
  the field  the low end (< 120 Hz) mono; the highs (> 1 kHz) wide
  the pump   the supersaw ducking under each kick, in dB
  the key    the drops' pitched peaks in F minor (A♭ major's notes)
  the range  the 808's octave (40–80 Hz) within 6 dB of the loudest, the air (10–16 kHz) present
"""
import os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter, resample_poly, butter, sosfiltfilt
from scipy.ndimage import median_filter

d = sys.argv[1] if len(sys.argv) > 1 else "."
BPM, SR = 150.0, 48000
SPB = 60.0 / BPM
SECTIONS = {"intro": (0, 32), "build 1": (32, 64), "drop 1": (64, 128), "breakdown": (128, 192),
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


def span(beats):
    a, b = beats
    return int(a * SPB * SR), int(b * SPB * SR)


def at(beat):
    return int(beat * SPB * SR)


mix, sr = load("magnetar.wav")
check(sr == SR, f"rate {sr} Hz")
L, R = mix[0], mix[1]
dur = len(L) / sr
check(320 * SPB - 0.5 <= dur <= 320 * SPB + 10, f"length {dur:.1f} s (80 bars at 150 bpm = {320 * SPB:.1f} s, then the tail)")

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


I = loudness(0, len(L))
check(-10.0 <= I <= -6.0, f"integrated loudness {I:.1f} LUFS (a trap master: −10 … −6)")
tp = 20 * np.log10(max(float(np.max(np.abs(resample_poly(c, 4, 1)))) for c in mix))
check(tp <= -1.0, f"true peak {tp:.2f} dBTP (under −1)")
st = {k: short_term(*span(v)) for k, v in SECTIONS.items()}
print("     sections, short-term LUFS: " + " · ".join(f"{k} {v:.1f}" for k, v in st.items()))
check(min(st["drop 1"], st["drop 2"]) >= st["breakdown"] + 4.0,
      f"the drops ({st['drop 1']:.1f}, {st['drop 2']:.1f}) at least 4 LU over the breakdown ({st['breakdown']:.1f})")


def band(x, lo, hi):
    sos = butter(4, [lo, hi] if lo else hi, btype="bandpass" if lo else "lowpass", fs=sr, output="sos")
    return sosfiltfilt(sos, x)


for name, drop, (a, b) in [("build 1", "drop 1", SECTIONS["build 1"]), ("build 2", "drop 2", SECTIONS["build 2"])]:
    first, last = short_term(*span((a, a + 12))), short_term(*span((b - 13, b - 1)))
    check(last >= first + 3.0, f"{name} rises: {first:.1f} → {last:.1f} LUFS (≥ +3 LU)")
    hi0 = rms_db(band(L[at(a):at(a + 4)], 5000, 14000))
    hi1 = rms_db(band(L[at(b - 5):at(b - 1)], 5000, 14000))
    check(hi1 - hi0 >= 6.0, f"{name} brightens: 5–14 kHz up {hi1 - hi0:.1f} dB (the riser's sweep)")
    lands = short_term(*span((b, b + 16)))
    check(lands >= last - 0.5, f"{drop} lands: its first bars {lands:.1f} LUFS, not under the build's end ({last:.1f})")

# ── the gap: a beat of near-silence, then the drop ──────────────────────────────────────────────────────────────
for name, (a, _) in [("drop 1", DROPS[0]), ("drop 2", DROPS[1])]:
    gap = rms_db(mix[:, at(a - 0.85):at(a - 0.05)].mean(axis=0))
    hit = rms_db(mix[:, at(a):at(a + 4)].mean(axis=0))
    check(hit - gap >= 12.0, f"the gap before {name}: {gap:.1f} dBFS, the drop's first bar {hit:.1f} — {hit - gap:.1f} dB apart (≥ 12)")


def onsets(x, lo, hi, thr_db=-30.0, hold=0.03):
    """Onsets of a band's envelope: rises over a fraction of the local peak, one per `hold` seconds."""
    env = np.abs(band(x, lo, hi))
    sm = np.convolve(env, np.ones(48) / 48, mode="same")
    thr = np.max(sm) * 10 ** (thr_db / 20)
    rise = np.flatnonzero((sm[1:] > thr) & (sm[:-1] <= thr))
    out, lastt = [], -1.0
    for i in rise:
        t = i / sr
        if t - lastt >= hold:
            out.append(t)
            lastt = t
    return np.array(out)


# ── half time: the snare on beat three of every bar, never the backbeat ─────────────────────────────────────────
snare, _ = load("magnetar.ch_4.wav")
sm = snare.mean(axis=0)
worst, miss, back = 0.0, 0, 0
for a, b in DROPS:
    for bar in range(a, b, 4):
        s = at(bar + 2)
        seg = np.abs(sm[s - 960:s + 960])
        pk = np.max(np.abs(sm[s:s + 4800]))
        if pk < 1e-3:
            miss += 1
            continue
        onset = (np.argmax(seg > 0.25 * pk) - 960) / sr * 1000
        worst = max(worst, abs(onset))
        miss += abs(onset) > 2.0
        for off in (1, 3):              # the backbeat: beats two and four
            if bar + off >= b - 1 and (a, b) == DROPS[1]:
                continue                # the second drop's last bar closes on a fill
            back += np.max(np.abs(sm[at(bar + off):at(bar + off) + 2400])) > 0.25 * pk
check(miss == 0 and worst <= 2.0, f"half time: the snare on the third beat of all {len(range(*DROPS[0], 4)) * 2} drop bars, the worst {worst:.2f} ms off")
check(back == 0, f"…and never on the backbeat (beats two and four): {back} hits there")

# ── the 808: its roots, and its glides ──────────────────────────────────────────────────────────────────────────
b8, _ = load("magnetar.ch_3.wav")
b8m = b8.mean(axis=0)
low = band(b8m, 0, 180)                 # the fundamental (the drive's harmonics filtered off)


def cycles(a, b):
    """The fundamental's periods (s) from its rising zero crossings in samples [a, b)."""
    x = low[a:b]
    z = np.flatnonzero((x[1:] > 0) & (x[:-1] <= 0))
    frac = z + x[z] / (x[z] - x[z + 1])  # sub-sample: where the line crosses
    return np.diff(frac) / sr


def hz_of(note):
    names = {'C': 0, 'Db': 1, 'D': 2, 'Eb': 3, 'E': 4, 'F': 5, 'Gb': 6, 'G': 7, 'Ab': 8, 'A': 9, 'Bb': 10, 'B': 11}
    n = 12 * (int(note[-1]) + 1) + names[note[:-1]]
    return 440.0 * 2 ** ((n - 69) / 12)


ROOTS = ['F1', 'Db2', 'Bb1', 'C2']
worst_c, roots_ok = 0.0, 0
for a, b in DROPS:
    for k, bar in enumerate(range(a, b, 4)):
        want = hz_of(ROOTS[k % 4])
        p = cycles(at(bar + 0.25), at(bar + 0.7))  # past the punch (its first 32 ms)
        if len(p) < 3:
            continue
        got = 1.0 / np.median(p)
        cents = 1200 * np.log2(got / want)
        worst_c = max(worst_c, abs(cents))
        roots_ok += abs(cents) <= 35
check(roots_ok == 32, f"the 808 on each drop bar's root (F1 Db2 Bb1 C2): {roots_ok} of 32 within 35 cents (the worst {worst_c:.0f})")
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_script import GLIDES  # the riff's slides: (beat in the four bars, from, to)
glided = 0
for a, b in DROPS:
    for turn in range(a, b, 16):
        for beat, frm, to in GLIDES:
            if (a, b) == DROPS[1] and beat == 15.25:
                continue                 # the second drop's turn dives instead
            s = at(turn + beat)
            per = cycles(s - int(0.12 * sr), s + int(0.30 * sr))
            f0, f1 = hz_of(frm), hz_of(to)
            lo_f, hi_f = min(f0, f1), max(f0, f1)
            between = np.sum((1 / per > lo_f * 2 ** (0.5 / 12)) & (1 / per < hi_f * 2 ** (-0.5 / 12)))
            glided += between >= 2         # at least two cycles at pitches between the notes: a glide, not a jump
n_glides = 2 * 4 * len(GLIDES) - 4
check(glided >= n_glides - 2, f"the 808 glides: {glided} of {n_glides} slides pass through the pitches between their notes")

# ── the hats: rolls ─────────────────────────────────────────────────────────────────────────────────────────────
hats, _ = load("magnetar.ch_5.wav")
gaps = []
for a, b in DROPS:
    t = onsets(hats.mean(axis=0)[at(a):at(b)], 6000, 16000, thr_db=-26.0, hold=0.025)
    gaps += list(np.diff(t) / SPB)          # in beats
gaps = np.array(gaps)
n32 = int(np.sum(np.abs(gaps - 0.125) < 0.02))
ntri = int(np.sum((np.abs(gaps - 1 / 6) < 0.02) | (np.abs(gaps - 1 / 3) < 0.03)))
n8 = int(np.sum(np.abs(gaps - 0.5) < 0.03))
check(n8 >= 100 and n32 >= 20 and ntri >= 20, f"the hats roll: {n8} eighth gaps, {n32} thirty-second, {ntri} triplet")

# ── the field ───────────────────────────────────────────────────────────────────────────────────────────────────
corr, width = [], []
for a, b in [span(x) for x in DROPS]:
    lo_l, lo_r = band(L[a:b], 0, 120), band(R[a:b], 0, 120)
    corr.append(float(np.sum(lo_l * lo_r) / np.sqrt(np.sum(lo_l ** 2) * np.sum(lo_r ** 2))))
    hl, hr = band(L[a:b], 1000, 16000), band(R[a:b], 1000, 16000)
    width.append(float(np.sum(((hl - hr) / 2) ** 2) / np.sum(((hl + hr) / 2) ** 2)))
check(min(corr) >= 0.95, f"the low end mono: L/R correlation under 120 Hz {min(corr):.3f} (≥ 0.95)")
check(min(width) >= 0.10, f"the highs wide: side/mid energy over 1 kHz {10 * np.log10(min(width)):.1f} dB (≥ −10 dB)")

# ── the pump ────────────────────────────────────────────────────────────────────────────────────────────────────
sup, _ = load("magnetar.ch_12.wav")
spm = sup.mean(axis=0)
from make_script import KICKS
dips = []
for a, b in DROPS:
    for turn in range(a, b, 16):
        for k in KICKS:
            t = (turn + k) * SPB
            after = spm[int((t + 0.03) * sr):int((t + 0.07) * sr)]
            before = spm[int((t - 0.07) * sr):int((t - 0.03) * sr)]
            if len(after) and len(before):
                dips.append(rms_db(before) - rms_db(after))
check(np.median(dips) >= 3.0, f"the pump: the supersaw {np.median(dips):.1f} dB under itself just after each kick (≥ 3 dB)")

# ── the key ─────────────────────────────────────────────────────────────────────────────────────────────────────
nfft = 8192
freqs = np.fft.rfftfreq(nfft, 1 / sr)
sel = (freqs >= 60) & (freqs <= 2000)
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
check(best[1] == 8 and best[0] >= 0.75,
      f"the key: F minor (Ab major's notes) holds {100 * best[0]:.0f} % of the drops' pitched peaks — the next set ({NAMES12[second[1]]} major's) {100 * second[0]:.0f} %")

# ── the range ───────────────────────────────────────────────────────────────────────────────────────────────────
a, b = span(DROPS[0])
f = np.fft.rfftfreq(16384, 1 / sr)
spec = np.zeros(len(f))
for s in range(a, b - 16384, 16384):
    spec += np.abs(np.fft.rfft(mono[s:s + 16384] * np.hanning(16384))) ** 2
octs = [(40, 80), (80, 160), (160, 320), (320, 640), (640, 1280), (1280, 2560), (2560, 5120), (5120, 10240), (10240, 16000)]
lv = [10 * np.log10(spec[(f >= lo) & (f < hi)].sum() + 1e-30) for lo, hi in octs]
top = max(lv)
print("     the drop's octaves, dB under the loudest: " + " · ".join(f"{lo}: {l - top:.0f}" for (lo, _), l in zip(octs, lv)))
check(lv[0] - top >= -6 and lv[-1] - top >= -35, f"the range: the 808's octave {lv[0] - top:.0f} dB and the air (10–16 kHz) {lv[-1] - top:.0f} dB under the loudest")

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
