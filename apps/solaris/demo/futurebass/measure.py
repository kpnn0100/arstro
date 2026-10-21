#!/usr/bin/env python3
"""Measures Halation as a melodic trap / future bass master is judged (Solaris R-SVC-15) — the style, not only "not silent".

    measure.py <dir with halation.wav and its stems: .ch_2 kick · .ch_3 808 · .ch_4 snare · .ch_5 hats · .ch_9 chords · .ch_10 chops>

  loudness   ITU-R BS.1770-4 integrated (−10 … −6 LUFS) and the true peak under −1 dBTP
  the form   both drops at least 4 LU over the breakdown; each build rising and brightening; each drop landing
  the gap    the beat before each drop at least 12 dB under the drop's first bar
  half time  the snare on the third beat of every bar of the drops, within 2 ms — and never on the backbeat
  the 808    on each bar's root (A, E, B, C♯) past its punch; GLIDING through the pitches between at the riff's slides
  the hats   rolls: triplet and thirty-second gaps among the drops' hat onsets, not only eighths
  the chop   the chords' loudness, section by section, moving at the shaper's rate (its strongest modulation within 3 %)
             and folded on the SONG's grid matching the shaper's own curve — quietest where the shape is lowest, the gate
             in the builds a quarter → a thirty-second, the wobble in the drops triplet eighths, then sixteenths
  the voice  the chops saying their written vowels — an "ah" note's first-formant band (550–1250 Hz) over its
             second's (1900–3200 Hz), an "ee" note's the other way; and singing their written notes (YIN, 50 cents)
  the field  the low end (< 120 Hz) mono; the highs (> 1 kHz) wide
  the pump   the chords ducking under each kick, in dB
  the key    the drops' pitched peaks over 250 Hz in C♯ minor (E major's notes)
  the range  the 808's octave (40–80 Hz) within 6 dB of the loudest, the air (10–16 kHz) present
"""
import os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter, resample_poly, butter, sosfiltfilt
from scipy.ndimage import median_filter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_script import BPM, GLIDES, KICKS, CHOPS, CHOP_RATES, PROG, ROOTS, GATE, WOBBLE, SMOOTH, PIECE, AH, EE, m  # noqa: E402

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


mix, sr = load("halation.wav")
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


I = loudness(0, len(L))
check(-10.0 <= I <= -6.0, f"integrated loudness {I:.1f} LUFS (−10 … −6)")
tp = 20 * np.log10(max(float(np.max(np.abs(resample_poly(c, 4, 1)))) for c in mix))
check(tp <= -1.0, f"true peak {tp:.2f} dBTP (under −1)")
st = {k: short_term(*span(v)) for k, v in SECTIONS.items()}
print("     sections, short-term LUFS: " + " · ".join(f"{k} {v:.1f}" for k, v in st.items()))
check(min(st["drop 1"], st["drop 2"]) >= st["breakdown"] + 4.0,
      f"the drops ({st['drop 1']:.1f}, {st['drop 2']:.1f}) at least 4 LU over the breakdown ({st['breakdown']:.1f})")

check(min(st["drop 1"], st["drop 2"]) >= st["verse"] + 3.0,
      f"…and at least 3 LU over the verse ({st['verse']:.1f}) — the groove held back")
for name, drop, (a, b) in [("build 1", "drop 1", SECTIONS["build 1"]), ("build 2", "drop 2", SECTIONS["build 2"])]:
    first, last = short_term(*span((a, a + 12))), short_term(*span((b - 13, b - 1)))
    check(last >= first + 3.0, f"{name} rises: {first:.1f} → {last:.1f} LUFS (≥ +3 LU)")
    hi0 = rms_db(band(L[at(a):at(a + 4)], 5000, 14000))
    hi1 = rms_db(band(L[at(b - 5):at(b - 1)], 5000, 14000))
    check(hi1 - hi0 >= 6.0, f"{name} brightens: 5–14 kHz up {hi1 - hi0:.1f} dB (the riser's sweep)")
    lands = short_term(*span((b, b + 16)))
    check(lands >= last - 0.5, f"{drop} lands: its first bars {lands:.1f} LUFS, not under the build's end ({last:.1f})")

# ── the gap ─────────────────────────────────────────────────────────────────────────────────────────────────────
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


# ── half time ───────────────────────────────────────────────────────────────────────────────────────────────────
snare, _ = load("halation.ch_4.wav")
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
        for off in (1, 3):
            if bar + off >= b - 1 and (a, b) == DROPS[1]:
                continue                # the second drop's last bar closes on a fill
            back += np.max(np.abs(sm[at(bar + off):at(bar + off) + 2400])) > 0.25 * pk
check(miss == 0 and worst <= 2.0, f"half time: the snare on the third beat of all 32 drop bars, the worst {worst:.2f} ms off")
check(back == 0, f"…and never on the backbeat (beats two and four): {back} hits there")

# ── the 808 ─────────────────────────────────────────────────────────────────────────────────────────────────────
b8, _ = load("halation.ch_3.wav")
low = band(b8.mean(axis=0), 0, 180)


def cycles(a, b):
    """The fundamental's periods (s) from its rising zero crossings in samples [a, b)."""
    x = low[a:b]
    z = np.flatnonzero((x[1:] > 0) & (x[:-1] <= 0))
    frac = z + x[z] / (x[z] - x[z + 1])
    return np.diff(frac) / sr


def hz(n):
    return 440.0 * 2 ** ((m(n) - 69) / 12)


worst_c, roots_ok = 0.0, 0
for a, b in DROPS:
    for k, bar in enumerate(range(a, b, 4)):
        want = hz(ROOTS[PROG[k % 4]])
        p = cycles(at(bar + 0.25), at(bar + 0.7))
        if len(p) < 3:
            continue
        cents = 1200 * np.log2(1.0 / np.median(p) / want)
        worst_c = max(worst_c, abs(cents))
        roots_ok += abs(cents) <= 35
check(roots_ok == 32, f"the 808 on each drop bar's root (A1 E1 B1 C♯2): {roots_ok} of 32 within 35 cents (the worst {worst_c:.0f})")
glided = 0
for a, b in DROPS:
    for turn in range(a, b, 16):
        for beat, frm, to in GLIDES:
            if (a, b) == DROPS[1] and beat == 15.25:
                continue                 # the second drop's turn dives instead
            s = at(turn + beat)
            per = cycles(s - int(0.12 * sr), s + int(0.30 * sr))
            lo_f, hi_f = min(hz(frm), hz(to)), max(hz(frm), hz(to))
            between = np.sum((1 / per > lo_f * 2 ** (0.5 / 12)) & (1 / per < hi_f * 2 ** (-0.5 / 12)))
            glided += between >= 2
n_glides = 2 * 4 * len(GLIDES) - 4
check(glided >= n_glides - 2, f"the 808 glides: {glided} of {n_glides} slides pass through the pitches between their notes")

# ── the hats ────────────────────────────────────────────────────────────────────────────────────────────────────
hats, _ = load("halation.ch_5.wav")
gaps = []
for a, b in DROPS:
    t = onsets(hats.mean(axis=0)[at(a):at(b)], 6000, 16000, thr_db=-26.0, hold=0.02)
    gaps += list(np.diff(t) / SPB)
gaps = np.array(gaps)
n32 = int(np.sum(np.abs(gaps - 0.125) < 0.02))
ntri = int(np.sum((np.abs(gaps - 1 / 6) < 0.02) | (np.abs(gaps - 1 / 3) < 0.03)))
n8 = int(np.sum(np.abs(gaps - 0.5) < 0.03))
check(n8 >= 100 and ntri >= 40 and n32 >= 8, f"the hats roll: {n8} eighth gaps, {ntri} triplet, {n32} thirty-second")

# ── the chop: the chords on the song's grid ─────────────────────────────────────────────────────────────────────
chords, _ = load("halation.ch_9.wav")
cm = chords.mean(axis=0)
env = np.sqrt(np.convolve(cm ** 2, np.ones(96) / 96, mode="same") + 1e-12)   # the chords' loudness over 2 ms — a
# moving average never rings below zero, as a sharp filter would at a gate's edge


def shape_at(shape, phi):
    return 0.5 - 0.5 * np.cos(2 * np.pi * phi) if shape == "sine" else (phi >= 0.5).astype(float)


BEATS = {'1/4': 1.0, '1/8': 0.5, '1/16': 0.25, '1/32': 0.125, '1/8T': 1 / 3}


def chop(x, first_beat, rate, shape):
    """The chop in loudness x (its first sample at first_beat): the strongest modulation (Hz), and the loudness folded
    on the SONG's grid — its swing (dB) and its likeness (r) to the shaper's own curve (SH3) through its own smoothing
    (SH4) and this measure's 2 ms window, folded the same way."""
    n = 1 << int(np.ceil(np.log2(len(x) * 8)))
    spec = np.abs(np.fft.rfft((x - x.mean()) * np.hanning(len(x)), n))
    f = np.fft.rfftfreq(n, 1 / sr)
    sel = (f >= 1.5) & (f <= 30)
    got = f[sel][np.argmax(spec[sel])]
    beats = first_beat + np.arange(len(x)) / sr / SPB
    phi = np.mod(beats / BEATS[rate], 1.0)
    bins = np.minimum((phi * 48).astype(int), 47)
    db = 20 * np.log10(x)
    fold = np.array([np.mean(db[bins == k]) for k in range(48)])
    g = 1 - (GATE if shape == "square" else WOBBLE) * (1 - shape_at(shape, phi))
    alpha = 1 - np.exp(-1 / (sr * SMOOTH / 1000))
    g = lfilter([alpha], [1, alpha - 1], g, zi=[g[0] * (1 - alpha)])[0]
    gdb = 10 * np.log10(np.convolve(g ** 2, np.ones(96) / 96, mode="same") + 1e-12)
    ideal = np.array([np.mean(gdb[bins == k]) for k in range(48)])
    return got, float(fold.max() - fold.min()), float(np.corrcoef(fold, ideal)[0, 1])


stages = []
for rate, shape, spans in CHOP_RATES:
    for a, b in spans:
        if b - a > 32:                                         # a drop: its halves, so each is judged on its own
            stages += [(rate, shape, a, a + 32), (rate, shape, a + 32, b)]
        else:
            stages.append((rate, shape, a, b - 1 if b in (DROPS[0][0], DROPS[1][0]) else b))   # the gap is silent
stages.sort(key=lambda s: s[2])
rate_ok = fold_ok = 0
for rate, shape, a, b in stages:
    s, e = at(a + 0.05), at(b)
    got, swing, r = chop(env[s:e], s / sr / SPB, rate, shape)
    want = BPM / 60.0 / BEATS[rate]
    rate_ok += abs(got / want - 1) <= 0.03
    fold_ok += r >= 0.9 and swing >= 10.0
    print(f"     beats {a:>5.0f}–{b:<5.0f} {rate:>4} {shape:<6}: strongest at {got:5.2f} Hz (the rate {want:5.2f}); "
          f"folded on the grid {swing:4.1f} dB, matching the shape r = {r:.2f}")
check(rate_ok == len(stages), f"the chop: the chords moving at the shaper's rate in {rate_ok} of {len(stages)} stretches (within 3 %)")
check(fold_ok == len(stages), f"…and quietest where the shape is, on the SONG's grid, in {fold_ok} of {len(stages)} (r ≥ 0.9, ≥ 10 dB)")
# a render that starts mid-drop, off the beat: its chop is the song's, not counted from the render's first sample
piece, _ = load("halation-piece.ch_9.wav")
pe = np.sqrt(np.convolve(piece.mean(axis=0) ** 2, np.ones(96) / 96, mode="same") + 1e-12)
got, swing, r = chop(pe[at(0.05):], PIECE + 0.05, '1/8T', 'sine')
_, _, r0 = chop(pe[at(0.05):], 0.05, '1/8T', 'sine')
check(r >= 0.9 and r0 < 0.5, f"…and in a render from beat {PIECE:g}: folded on the song's grid r = {r:.2f}, on the render's own count r = {r0:.2f}")

# ── the voice: the chops' vowels and notes ──────────────────────────────────────────────────────────────────────
chops, _ = load("halation.ch_10.wav")
cx = chops.mean(axis=0)


def formant_ratio(a, l):
    s, e = at(a + 0.12), at(a + min(l, 0.5) - 0.02)
    seg = cx[s:e] * np.hanning(e - s)
    sp = np.abs(np.fft.rfft(seg, 16384)) ** 2
    f = np.fft.rfftfreq(16384, 1 / sr)
    lo = sp[(f >= 550) & (f < 1250)].sum()
    hi = sp[(f >= 1900) & (f < 3200)].sum()
    return 10 * np.log10(max(lo, 1e-30) / max(hi, 1e-30))


said = {AH: [], EE: []}
for base in (DROPS[0][0], DROPS[0][0] + 32, DROPS[1][0], DROPS[1][0] + 32):
    for a, l, n, v in CHOPS:
        if v in said and l >= 0.5:
            said[v].append(formant_ratio(base + a, l))
ah, ee = np.array(said[AH]), np.array(said[EE])
mid = (np.median(ah) + np.median(ee)) / 2
right = int(np.sum(ah > mid) + np.sum(ee < mid))
check(np.median(ah) - np.median(ee) >= 10.0 and right >= 0.9 * (len(ah) + len(ee)),
      f"the chops SAY their vowels: an \"ah\" note's first-formant band {np.median(ah):+.1f} dB over its second's, an \"ee\" "
      f"note's {np.median(ee):+.1f} — {right} of {len(ah) + len(ee)} drop notes on their written side")
# the same pitch sung both ways: the source is the same, so the difference is the vowel filter's alone
by_pitch = {}
for a, l, n, v in CHOPS:
    if v in (AH, EE) and l >= 0.5:
        by_pitch.setdefault(n, {AH: [], EE: []})[v].append((a, l))
both = [n for n, w in by_pitch.items() if w[AH] and w[EE]]
apart = []
for base in (DROPS[0][0], DROPS[0][0] + 32, DROPS[1][0], DROPS[1][0] + 32):
    for n in both:
        apart.append(np.median([formant_ratio(base + a, l) for a, l in by_pitch[n][AH]])
                     - np.median([formant_ratio(base + a, l) for a, l in by_pitch[n][EE]]))
check(min(apart) >= 5.0, f"…and at the same pitch ({', '.join(both)}), in all four drop passes: its \"ah\" over its \"ee\" by "
      f"{min(apart):.1f} … {max(apart):.1f} dB (≥ 5)")


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


good, cents = 0, []
for base in (DROPS[0][0], DROPS[1][0]):
    for a, l, n, _ in CHOPS:
        if l < 0.5:
            continue                     # a stutter is too short to read a pitch from cleanly
        s = at(base + a + 0.15)
        e = min(at(base + a + min(l, 0.95)), s + 4096 + int(sr / 150) + 1)
        f = f0(cx[s:e])
        c = 1200 * np.log2(f / hz(n)) if f > 0 else 9999.0
        cents.append(c)
        good += abs(c) <= 50
check(good >= 0.9 * len(cents), f"the chops SING their notes: {good} of {len(cents)} in the drops within 50 cents (median {np.median(np.abs(cents)):.0f})")

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
dips = []
for a, b in DROPS:
    for turn in range(a, b, 16):
        for k in KICKS:
            t = (turn + k) * SPB
            after = cm[int((t + 0.03) * sr):int((t + 0.07) * sr)]
            before = cm[int((t - 0.07) * sr):int((t - 0.03) * sr)]
            if len(after) and len(before):
                dips.append(rms_db(before) - rms_db(after))
check(np.median(dips) >= 3.0, f"the pump: the chords {np.median(dips):.1f} dB under themselves just after each kick (≥ 3 dB)")

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
check(best[1] == 4 and best[0] >= 0.75,
      f"the key: C♯ minor (E major's notes) holds {100 * best[0]:.0f} % of the drops' pitched peaks — the next set ({NAMES12[second[1]]} major's) {100 * second[0]:.0f} %")

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
