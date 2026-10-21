#!/usr/bin/env python3
"""Measures Porch Light — a future bass track that tells a story (Solaris R-SVC-17): the master, the arc, and every device
of harmony and melody the story is told with, from the score and from the sound.

    measure.py <dir with porchlight.wav and its stems: .ch_2 guitar · .ch_4 piano (her theme) · .ch_5 chords · .ch_6 lead ·
                .ch_8 growl · .ch_9 808 · .ch_11 snare>

  the master  BS.1770 integrated (−10 … −6 LUFS), true peak under −1 dBTP
  the arc     the drops over the breakdown (4 LU) and the verse (3 LU); the builds rising, brightening, landing; the
              gap; the second drop brighter than the first (light after grief); half time
  the theme   AS WRITTEN — the motif and its sequence, the appoggiatura, the ♭6 → 5 sigh, the climax once over ♭VI, the
              4–3 suspension and the half cadence; the augmentation, the diatonic inversion, the transposition a major
              third up with its sigh gone and its climax a third higher
              AS HEARD — its pitch read (YIN) from the guitar (the verse), the lead (the first drop), the piano (augmented),
              the guitar (inverted), the guitar (in G): each within 50 cents for 90 % of its notes
  the harmony the borrowed tones (C♭, G♭) sounding in their bars of the first drop and nowhere else in it; none outside G
              major in the second; the deceptive cadence's root (C♭, not E♭); the keys a major third apart; the minor
              plagal close (E♭ under the iv, then a major third and no E♭)
  the wonk    the chords sagging two semitones at the end of every fourth bar
  the growl   its formants moving on the sixteenths (10 Hz)
  the field   the low end mono; the highs wide
"""
import os, sys
import numpy as np
from scipy.io import wavfile
from scipy.signal import lfilter, resample_poly, butter, sosfiltfilt
from scipy.ndimage import median_filter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_script as S  # noqa: E402

d = sys.argv[1] if len(sys.argv) > 1 else "."
SR = 48000
SPB = 60.0 / S.BPM
SECTIONS = {"intro": (S.INTRO, S.VERSE), "verse": (S.VERSE, S.BUILD1), "build 1": (S.BUILD1, S.DROP1),
            "drop 1": (S.DROP1, S.BREAK), "breakdown": (S.BREAK, S.BUILD2), "build 2": (S.BUILD2, S.DROP2),
            "drop 2": (S.DROP2, S.OUTRO), "outro": (S.OUTRO, S.END)}
DROPS = [SECTIONS["drop 1"], SECTIONS["drop 2"]]
NAMES12 = ["C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B"]
EB_MAJOR = {3, 5, 7, 8, 10, 0, 2}
G_MAJOR = {7, 9, 11, 0, 2, 4, 6}
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


def hz(n):
    return 440.0 * 2 ** ((S.m(n) - 69) / 12)


mix, sr = load("porchlight.wav")
check(sr == SR, f"rate {sr} Hz")
L, R = mix[0], mix[1]
mono = (L + R) / 2
dur = len(L) / sr
check(S.END * SPB - 0.5 <= dur <= S.END * SPB + 10, f"length {dur:.1f} s (96 bars at {S.BPM} bpm = {S.END * SPB:.1f} s, then the tail)")

# ── the master ──────────────────────────────────────────────────────────────────────────────────────────────────────
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

# ── the arc ─────────────────────────────────────────────────────────────────────────────────────────────────────────
st = {k: short_term(*span(v)) for k, v in SECTIONS.items()}
print("     the story's sections, short-term LUFS: " + " · ".join(f"{k} {v:.1f}" for k, v in st.items()))
check(min(st["drop 1"], st["drop 2"]) >= st["breakdown"] + 4.0,
      f"the drops ({st['drop 1']:.1f}, {st['drop 2']:.1f}) at least 4 LU over the photograph ({st['breakdown']:.1f})")
check(min(st["drop 1"], st["drop 2"]) >= st["verse"] + 3.0, f"…and 3 LU over her rooms, the verse ({st['verse']:.1f})")
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
    check(hit - gap >= 12.0, f"the gap before {name}: {gap:.1f} dBFS, its first bar {hit:.1f} — {hit - gap:.1f} dB apart (≥ 12)")


def air(a, b):
    """The share of the energy over 4 kHz, dB."""
    x = mono[at(a):at(b)]
    return 10 * np.log10(np.mean(band(x, 4000, 16000) ** 2) / np.mean(x ** 2))


a1, a2 = air(*DROPS[0]), air(*DROPS[1])
check(a2 - a1 >= 1.0, f"light after grief: the second drop's air (over 4 kHz) {a2:.1f} dB of it, the first's {a1:.1f} — {a2 - a1:+.1f} dB (≥ 1)")
snare = load("porchlight.ch_11.wav")[0].mean(axis=0)
worst, miss, back = 0.0, 0, 0
for a, b in DROPS:
    for bar in range(a, b, 4):
        s = at(bar + 2)
        seg = np.abs(snare[s - 960:s + 960])
        pk = np.max(np.abs(snare[s:s + 4800]))
        if pk < 1e-3:
            miss += 1
            continue
        onset = (np.argmax(seg > 0.25 * pk) - 960) / sr * 1000
        worst = max(worst, abs(onset))
        miss += abs(onset) > 2.0
        for off in (1, 3):
            back += np.max(np.abs(snare[at(bar + off):at(bar + off) + 2400])) > 0.25 * pk
check(miss == 0 and back == 0, f"half time: the snare on the third beat of all 32 drop bars (the worst {worst:.2f} ms), {back} on the backbeat")

# ── the theme, as written ───────────────────────────────────────────────────────────────────────────────────────────
T = S.THEME
bar = lambda k: [x for x in T if 4 * k <= x[0] < 4 * k + 4]
steps = lambda notes: [S.diatonic(b[2]) - S.diatonic(a[2]) for a, b in zip(notes, notes[1:])]
check(steps(bar(0)) == [1, 1, 3] and steps(bar(2))[:2] == [1, 1] and (S.diatonic(bar(2)[0][2]) - S.diatonic(bar(0)[0][2])) % 7 == 5,
      f"the motif (G A♭ B♭ E♭: a step, a step, up a fourth) and its SEQUENCE in bar 3 — begun a third lower, an octave up "
      f"(E♭ F G: the same two steps) — turned down at its end ({steps(bar(2))[2]} steps to C5: a modified sequence; up "
      f"would pass the climax)")
chord_pcs = lambda c: {S.m(n) % 12 for n in S.CHORD[c]}
b4 = bar(3)
check(S.m(b4[0][2]) % 12 not in chord_pcs('Abmaj7') and S.diatonic(b4[1][2]) - S.diatonic(b4[0][2]) == 1
      and S.m(b4[1][2]) % 12 in chord_pcs('Abmaj7') and b4[0][0] % 4 == 0,
      f"an APPOGGIATURA in bar 4: {b4[0][2]} on the downbeat over A♭maj7, not of it, resolving up a step to {b4[1][2]}")
b5 = bar(4)
check(S.m(b5[0][2]) - S.m(b5[1][2]) == 1 and S.m(b5[0][2]) % 12 not in EB_MAJOR and S.m(b5[0][2]) % 12 == 11,
      f"the SIGH over the borrowed iv: {b5[0][2]} (♭6, outside E♭ major) falling a semitone to {b5[1][2]}")
top = max(S.m(n) for _, _, n in T)
tops = [x for x in T if S.m(x[2]) == top]
check(len(tops) == 1 and 24 <= tops[0][0] < 28, f"the CLIMAX once — {tops[0][2]}, bar 7, over the borrowed ♭VI (C♭maj7, its major seventh)")
b8 = bar(7)
sus = [x for x in b8 if x[2] == 'Eb5'][0]
res = [x for x in b8 if x[2] == 'D5'][0]
check(sus[0] + sus[1] == res[0] == 30 and S.diatonic(sus[2]) - S.diatonic(res[2]) == 1,
      "a 4–3 SUSPENSION into the dominant: E♭5 over B♭sus4 falling a step to D5 as the chord resolves (beat 30)")
check(T[-1][2] == 'D5' and S.m('D5') % 12 == 2 and S.m('D5') % 12 in chord_pcs('Bb') and S.PROG1[-1] == 'Bbsus',
      "it ends on D5 — the leading tone, the dominant's third — over V: a HALF cadence, a question the song answers later")
check(S.AUGMENTED == [(a * 2, l * 2, n) for a, l, n in S.FIRST_HALF], "AUGMENTED: bars 1–4 with every onset and length doubled")
inv = S.invert(S.FIRST_HALF)
check(all(x == -y for x, y in zip(steps(inv), steps(S.FIRST_HALF))) and S.INVERTED == S.augment(inv),
      f"INVERTED: every step of bars 1–4 mirrored about B♭4 in E♭ major (it begins {inv[0][2]} {inv[1][2]} {inv[2][2]} {inv[3][2]})")
kept = [x for x in T if not (16 <= x[0] < 20 or 24 <= x[0] < 28)]
t2 = {a: n for a, _, n in S.THEME2}
check(all(S.m(t2[a]) - S.m(n) == 4 for a, _, n in kept), "TRANSPOSED a major third up for the second drop (bars 1–4, 6, 8 note for note)")
b5g = [x for x in S.THEME2 if 16 <= x[0] < 20]
check(all(abs(S.m(b[2]) - S.m(a[2])) >= 2 for a, b in zip(b5g, b5g[1:]))
      and max(S.m(n) for _, _, n in S.THEME2) - top == 4 and all(S.m(n) % 12 in G_MAJOR for _, _, n in S.THEME2),
      "…the sigh gone (bar 5 falls by whole steps over ii), the climax a major third higher (D6, over IV), all of it in G major")

# ── the theme, as heard ─────────────────────────────────────────────────────────────────────────────────────────────
def f0(x, fmin=150.0, fmax=1600.0, thresh=0.12):
    """YIN: the fundamental of a short frame."""
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


def heard(stem, bases, notes):
    x = stem
    good, cents = 0, []
    for base in bases:
        for a, l, n in notes:
            s = at(base + a + 0.15)
            e = min(at(base + a + min(l, 1.0) - 0.02), s + 4096 + int(sr / 150) + 1)
            f = f0(x[s:e])
            c = 1200 * np.log2(f / hz(n)) if f > 0 else 9999.0
            cents.append(c)
            good += abs(c) <= 50
    return good, len(cents), float(np.median(np.abs(cents)))


guitar = load("porchlight.ch_2.wav")[0].mean(axis=0)
lead = load("porchlight.ch_6.wav")[0].mean(axis=0)
piano = load("porchlight.ch_4.wav")[0].mean(axis=0)
for who, stem, bases, notes, what in [
        ("the guitar", guitar, (S.VERSE, S.VERSE + 32), T, "her theme in her rooms (the verse)"),
        ("the lead", lead, (S.DROP1, S.DROP1 + 32), T, "her theme through the grief (the first drop)"),
        ("the piano", piano, (S.BREAK,), S.AUGMENTED, "her theme AUGMENTED (the photograph)"),
        ("the guitar", guitar, (S.BREAK + 32,), S.INVERTED, "her theme INVERTED (the answer)"),
        ("the guitar", guitar, (S.DROP2, S.DROP2 + 32), S.THEME2, "her theme in G (carrying her)")]:
    g, n, med = heard(stem, bases, notes)
    check(g >= 0.9 * n, f"{who} sings {what}: {g} of {n} notes within 50 cents (median {med:.0f})")

# ── the harmony, as heard ───────────────────────────────────────────────────────────────────────────────────────────
chords = load("porchlight.ch_5.wav")[0].mean(axis=0)
NFFT = 8192
FR = np.fft.rfftfreq(NFFT, 1 / sr)
SEL = (FR >= 250) & (FR <= 2000)
PCS = (np.round(12 * np.log2(FR[SEL] / 440.0)).astype(int) + 69) % 12


def chroma(x, a, b):
    """Pitch-class energy of the spectral PEAKS over 250 Hz in beats [a, b) — a peak a local maximum 4× over the median
    around it: the notes, not a detuned unison's skirts leaking into the next semitone."""
    c = np.zeros(12)
    s0, s1 = at(a), at(b)
    for s in range(s0, max(s0 + 1, s1 - NFFT), NFFT // 2):
        mag = np.abs(np.fft.rfft(x[s:s + NFFT] * np.hanning(NFFT)))[SEL]
        floor = median_filter(mag, size=31)
        peak = (mag > 4 * floor) & (mag >= np.roll(mag, 1)) & (mag >= np.roll(mag, -1))
        np.add.at(c, PCS[peak], mag[peak] ** 2)
    return c / max(c.sum(), 1e-30)


borrowed_ok, clean_ok, names = 0, 0, []
for base in (S.DROP1, S.DROP1 + 32):
    for k, c in enumerate(S.PROG1):
        ch = chroma(chords, base + 4 * k + 0.4, base + 4 * k + 2.8)   # past the last chord's release, before the sag
        out = ch[11] + ch[6]                    # C♭ and G♭: what E♭ major has not
        if c in ('Abm6', 'Cbmaj7'):
            borrowed_ok += out >= 0.12
            names.append(f"{c} {100 * out:.0f} %")
        else:
            clean_ok += out <= 0.03
check(borrowed_ok == 4 and clean_ok == 12, f"the BORROWED tones (C♭, G♭) sound in their bars of the grief — {', '.join(names[:2])} "
      f"— and in none of the other 12 bars ({clean_ok} clean)")
outside = [float(sum(chroma(chords, b + 4 * k + 0.4, b + 4 * k + 2.8)[p] for p in range(12) if p not in G_MAJOR))
           for b in (S.DROP2, S.DROP2 + 32) for k in range(8)]
check(max(outside) <= 0.05, f"carrying her: the borrowed colours gone — no bar of the second drop over 5 % outside G major (at "
      f"most {100 * max(outside):.1f} %: the saws' own overtones, F♯'s third and A's fifth a C♯), where the grief's had 35–53 %")


def key_of(a, b):
    ch = chroma(mono, a, b)
    fits = sorted(((sum(ch[(t + k) % 12] for k in (0, 2, 4, 5, 7, 9, 11)), t) for t in range(12)), reverse=True)
    return fits[0][1], fits[0][0]


k1, f1 = key_of(*DROPS[0])
k2, f2 = key_of(*DROPS[1])
check(k1 == 3 and k2 == 7 and (k2 - k1) % 12 == 4,
      f"the MODULATION: the grief in {NAMES12[k1]} major ({100 * f1:.0f} %), carrying her in {NAMES12[k2]} major ({100 * f2:.0f} %) — a major third up")
b808 = band(load("porchlight.ch_9.wav")[0].mean(axis=0), 0, 180)


def bass_hz(a, b):
    x = b808[at(a):at(b)]
    z = np.flatnonzero((x[1:] > 0) & (x[:-1] <= 0))
    if len(z) < 4:
        return 0.0
    frac = z + x[z] / (x[z] - x[z + 1])
    return float(sr / np.median(np.diff(frac)))


dec = bass_hz(S.BUILD2 + 0.3, S.BUILD2 + 3.5)
dom = chroma(mono, S.BUILD2 - 8, S.BUILD2 - 0.5)
dom_ok = all(dom[p] >= 0.05 for p in (10, 2, 5, 8))
check(dom_ok and abs(1200 * np.log2(max(dec, 1e-9) / hz('Cb2'))) <= 35,
      f"the DECEPTIVE cadence: the photograph ends on the dominant seventh (B♭ D F A♭ all sounding) and the next root is "
      f"C♭ ({dec:.1f} Hz; E♭ would be {hz('Eb2'):.1f}) — she is not there")
iv = chroma(mono, S.OUTRO + 16.25, S.OUTRO + 23.75)
last = chroma(mono, S.OUTRO + 26.5, S.OUTRO + 31.5)      # past the iv's release (the pad lets go over 1.8 s)
check(iv[3] >= 0.08 and last[11] >= 0.08 and last[3] <= 0.1 * iv[3],
      f"the minor PLAGAL close: E♭ under the borrowed iv ({100 * iv[3]:.0f} % of its peaks), then home on G with its major "
      f"third B ({100 * last[11]:.0f} %), the E♭ faded to {100 * last[3]:.1f} % (under a tenth: its reverb)")

# ── the wonk ────────────────────────────────────────────────────────────────────────────────────────────────────────
LOGF = 200 * 2 ** (np.arange(0, 3.5 * 48) / 48)     # 48 points an octave — a quarter of a semitone — 200 Hz to 2.3 kHz


def logspec(x, a):
    s = at(a)
    seg = x[s:s + 4096] * np.hanning(4096)
    mag = np.abs(np.fft.rfft(seg, 16384))
    f = np.fft.rfftfreq(16384, 1 / sr)
    return np.log(np.interp(LOGF, f, mag) + 1e-9)


sags = []
for base in (S.DROP1, S.DROP1 + 32, S.DROP2, S.DROP2 + 32):
    for end in (15.4, 31.4):
        # the same chord a beat and a bit earlier against the sag: the shift (in quarter semitones) that lines their
        # log-frequency spectra up — a sag of 2 st moves every partial 8 points down
        ref, bent = logspec(chords, base + end - 1.2), logspec(chords, base + end - 0.1)   # the sag held 15.3 … 15.6
        best = max(range(-16, 5), key=lambda k: np.corrcoef(ref[24:-24], np.roll(bent, -k)[24:-24])[0, 1])
        sags.append(best / 4.0)
check(all(-2.5 <= s <= -1.5 for s in sags), f"the WONK: the chords sag {', '.join(f'{s:+.2f}' for s in sags)} semitones at the end of every fourth bar (−2 written)")

# ── the growl ───────────────────────────────────────────────────────────────────────────────────────────────────────
growl = load("porchlight.ch_8.wav")[0].mean(axis=0)
rates, swings = [], []
for start in (S.DROP1 + 12, S.DROP1 + 28, S.DROP1 + 44, S.DROP1 + 60):
    x = growl[at(start + 0.5):at(start + 4)]
    hop = 240
    lo = np.array([np.mean(band(x, 500, 1100)[i:i + 480] ** 2) for i in range(0, len(x) - 480, hop)])
    hi = np.array([np.mean(band(x, 1700, 3000)[i:i + 480] ** 2) for i in range(0, len(x) - 480, hop)])
    ratio = 10 * np.log10((lo + 1e-12) / (hi + 1e-12))
    sp = np.abs(np.fft.rfft((ratio - ratio.mean()) * np.hanning(len(ratio)), 4096))
    fr = np.fft.rfftfreq(4096, hop / sr)
    sel = (fr >= 3) & (fr <= 30)
    rates.append(float(fr[sel][np.argmax(sp[sel])]))
    swings.append(float(np.percentile(ratio, 90) - np.percentile(ratio, 10)))
want = S.BPM / 60 * 2
check(all(abs(r / want - 1) <= 0.06 for r in rates) and min(swings) >= 8,
      f"the GROWL talks — a syllable a sixteenth, open and shut in turn: its formant balance cycling at "
      f"{', '.join(f'{r:.2f}' for r in rates)} Hz (an eighth: {want:.0f}), swinging {min(swings):.0f}–{max(swings):.0f} dB")

# ── the field ───────────────────────────────────────────────────────────────────────────────────────────────────────
corr, width = [], []
for a, b in [span(x) for x in DROPS]:
    lo_l, lo_r = band(L[a:b], 0, 120), band(R[a:b], 0, 120)
    corr.append(float(np.sum(lo_l * lo_r) / np.sqrt(np.sum(lo_l ** 2) * np.sum(lo_r ** 2))))
    hl, hr = band(L[a:b], 1000, 16000), band(R[a:b], 1000, 16000)
    width.append(float(np.sum(((hl - hr) / 2) ** 2) / np.sum(((hl + hr) / 2) ** 2)))
check(min(corr) >= 0.95, f"the low end mono: L/R correlation under 120 Hz {min(corr):.3f} (≥ 0.95)")
check(min(width) >= 0.10, f"the highs wide: side/mid energy over 1 kHz {10 * np.log10(min(width)):.1f} dB (≥ −10 dB)")

print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
