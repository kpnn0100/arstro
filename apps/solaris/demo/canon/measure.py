#!/usr/bin/env python3
"""Measures canon.wav and its stems (R-SVC-9): a render that "succeeded" can be silent or off the grid.
Usage: measure.py <dir with canon.wav, canon.ch_2.wav (kick), canon.ch_4.wav (bass), canon.ch_6.wav (canon)>"""
import sys, wave, numpy as np
d = sys.argv[1] if len(sys.argv) > 1 else "."
BPM, SR = 128.0, 48000
SPB = 60.0 / BPM * SR
def load(p):
    w = wave.open(f"{d}/{p}"); n = w.getnframes(); ch = w.getnchannels(); sw = w.getsampwidth()
    raw = np.frombuffer(w.readframes(n), np.uint8)
    if sw == 3:
        b = raw.reshape(-1, 3); x = b[:, 0].astype(np.int32) | b[:, 1].astype(np.int32) << 8 | b[:, 2].astype(np.int32) << 16
        x = np.where(x >= 1 << 23, x - (1 << 24), x) / 2**23
    elif sw == 4: x = np.frombuffer(raw.tobytes(), np.float32).astype(np.float64)
    else: x = np.frombuffer(raw.tobytes(), np.int16) / 32768.0
    return x.reshape(-1, ch).mean(axis=1), w.getframerate()
def db(v): return 20 * np.log10(max(v, 1e-12))
def rms(x): return float(np.sqrt(np.mean(x * x))) if len(x) else 0.0
ok = True
def check(cond, what):
    global ok
    print(("PASS " if cond else "FAIL ") + what); ok &= bool(cond)

mix, sr = load("canon.wav")
check(sr == SR, f"rate {sr}")
dur = len(mix) / sr
check(60.0 <= dur <= 66.0, f"length {dur:.2f} s (128 beats at 128 bpm = 60 s, plus the tail)")
peak = float(np.max(np.abs(mix)))
check(peak < 1.0, f"peak {db(peak):.2f} dBFS (under 0)")
check(db(rms(mix)) > -30, f"RMS {db(rms(mix)):.1f} dBFS (not silence)")
def section(a, b): return mix[int(a * SPB):int(b * SPB)]
intro, verse, build, drop = (db(rms(section(*s))) for s in ((0, 16), (32, 64), (64, 80), (80, 112)))
check(drop > verse > intro, f"energy rises: intro {intro:.1f}, verse {verse:.1f}, build {build:.1f}, drop {drop:.1f} dBFS")
# the kick on the beats, and the tempo
kick, _ = load("canon.ch_2.wav")
env = np.abs(kick)
hits = [b for b in list(range(32, 64)) + list(range(80, 112))]
late = []
for b in hits:
    s = int(round(b * SPB)); w = env[s - 200:s + 2000]
    on = int(np.argmax(w > 0.02 * max(1e-9, w.max()))) - 200
    late.append(on)
check(max(abs(x) for x in late) <= 2, f"kick onsets on all {len(hits)} beats (worst {max(abs(x) for x in late)} samples off)")
# the pump: the bass just after a kick vs between kicks, in the drop
bass, _ = load("canon.ch_4.wav")
dips = []
for b in range(84, 108):
    s = int(b * SPB)
    after = rms(bass[s + int(0.02 * sr):s + int(0.06 * sr)])          # 20–60 ms after the kick
    mid = rms(bass[s + int(0.55 * SPB):s + int(0.75 * SPB)])          # the off-beat note, released
    if after > 0 and mid > 0: dips.append(db(mid) - db(after))
check(np.median(dips) > 6, f"sidechain: the bass {np.median(dips):.1f} dB lower just after each kick than on its off-beat")
# the canon's first line (F#5 E5 D5 C#5 B4 A4 B4 C#5), a note per 2 beats from beat 32
canon, _ = load("canon.ch_6.wav")
want = [739.99, 659.26, 587.33, 554.37, 493.88, 440.0, 493.88, 554.37]
got = []
for i, f in enumerate(want):
    s = int((32 + 2 * i + 0.25) * SPB); x = canon[s:s + 16384] * np.hanning(16384)
    spec = np.abs(np.fft.rfft(x, 1 << 17)); freqs = np.fft.rfftfreq(1 << 17, 1 / sr)
    band = (freqs > f / 1.5) & (freqs < f * 1.5)
    got.append(freqs[band][np.argmax(spec[band])])
err = max(abs(1200 * np.log2(g / w)) for g, w in zip(got, want))
check(err < 25, f"the canon's line A: {' '.join(f'{g:.0f}' for g in got)} Hz (worst {err:.1f} cents)")
# the pad's filter opens through the intro: brightness rises
pad, _ = load("canon.ch_5.wav")
def centroid(a, b):
    x = pad[int(a * SPB):int(b * SPB)]; spec = np.abs(np.fft.rfft(x)); f = np.fft.rfftfreq(len(x), 1 / sr)
    return float((spec * f).sum() / max(spec.sum(), 1e-12))
c0, c1 = centroid(2, 6), centroid(26, 30)
check(c1 > 1.5 * c0, f"the pad opens: spectral centroid {c0:.0f} Hz → {c1:.0f} Hz across the intro")
print("ALL PASS" if ok else "SOME FAILED")
sys.exit(0 if ok else 1)
