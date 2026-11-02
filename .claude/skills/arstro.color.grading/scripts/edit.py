#!/usr/bin/env python3
"""edit.py — edit EVERY photo on its own: look, plan, correct, mood, local, match. The photo editor.

    python3 edit.py review <project.cmp> --out DIR [--per-sheet 6]
            # as-shot previews + review sheets with a 0.25 grid, for the agent to LOOK at every photo
    python3 edit.py run <project.cmp> --mood spring-haze --plan DIR/plan.json --out DIR [--names a,b] [--save | --save-as P]
            # the whole edit, per photo, then the set matched

The approver (2026-10-30): *"i see you apply color for whole group — that not what i want — i need you to
see the actual image and apply edit for each image so that it become cinematic, look good and have
consistent mood … photographer take 1000 photo, prompt agent to edit, 1000 photo got edit nicely"*.

So no look group: each photo carries its WHOLE edit, and consistency comes from MATCHING, not sharing.
Per photo, in order (SKILL.md §2c):

 1. correct   — autograde.normalise: white balance against this photo's witness, exposure with the
                highlights first, noise by ISO (the scene from the plan if the agent named it).
 2. mood      — the mood's day or night variant (moods.py), written onto the photo itself.
 3. plan      — what the agent decided LOOKING at this photo (plan.json): `set` tweaks (k=v, k+=v,
                k-=v), local masks (a subject lifted, a sky graded down, a light given glow).
 4. match     — after every photo has 1-3, the set's median shadow cast, highlight cast and saturation
                (per day / night family) become the targets, and each photo's OWN R/G/B curve toe and
                shoulder and saturation are nudged toward them — a colourist matching shots to a hero.
                Same mood, different photos: identical settings never give identical results.
 5. guard     — nothing that matters clips.

plan.json (written by the agent; every key optional, a photo with no entry is edited automatically):
    {"mood": "spring-haze",
     "photos": {"_DSF6686.RAF": {
        "scene": "daylight", "subject": "deer grazing, lower right", "intent": "lift the deer, deepen the sky",
        "set": "vibrance+=4 lensVignette-=6",
        "masks": [{"type": "radial", "cx": 0.6, "cy": 0.66, "rx": 0.22, "ry": 0.2, "feather": 0.7,
                   "exposure": 0.2, "clarity": 8},
                  {"type": "linear", "x0": 0.5, "y0": 0.45, "x1": 0.5, "y1": 0.0, "exposure": -0.3,
                   "saturation": 10}],
        "match": true}}}
Mask geometry is 0..1 of the framed photo, y down. A linear mask is 0 at (x0,y0) and full at (x1,y1).
Mask adjustments: exposure (EV), contrast, highlights, shadows, whites, blacks, temp, tint, saturation,
texture, clarity, dehaze (-100..100, temp/tint relative).
"""
import argparse, json, math, statistics, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import autograde as A  # noqa: E402
import looks as L  # noqa: E402
import moods as M  # noqa: E402

PHOTO_RESET = A.GROUP_RESET          # every key a correction, a mood or a plan may set
CURVE_X = [0.0, 0.15, 0.3, 0.5, 0.7, 0.85, 1.0]
MASK_KEYS = ("exposure", "contrast", "highlights", "shadows", "whites", "blacks", "temp", "tint",
             "saturation", "texture", "clarity", "dehaze")
CLIP = {"day": 0.004, "night": 0.012}


# ── params as a dict ────────────────────────────────────────────────────────────────────────

def parse(line):
    out = {}
    for tok in (line or "").split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def render(d):
    return " ".join("%s=%s" % (k, v) for k, v in d.items())


def curve_points(v):
    pts = []
    for p in (v or "0,0;1,1").split(";"):
        x, y = (float(a) for a in p.split(","))
        pts.append((x, y))
    return sorted(pts)


def sample(pts, x):
    if x <= pts[0][0]:
        return pts[0][1]
    for (x0, y0), (x1, y1) in zip(pts, pts[1:]):
        if x <= x1:
            return y0 + (y1 - y0) * (x - x0) / max(1e-9, x1 - x0)
    return pts[-1][1]


def curve_grid(v):
    """A curve resampled on CURVE_X, so offsets at the toe and the shoulder have points to land on."""
    pts = curve_points(v)
    xs = sorted(set(CURVE_X) | {p[0] for p in pts})
    return [(x, sample(pts, x)) for x in xs]


def curve_str(pts):
    return ";".join("%g,%g" % (x, max(0.0, min(1.0, y))) for x, y in pts)


def toe_w(x):
    return max(0.0, 1.0 - x / 0.35)


def shoulder_w(x):
    return max(0.0, (x - 0.6) / 0.4)


def offset_curve(v, toe=0.0, shoulder=0.0):
    return curve_str([(x, y + toe * toe_w(x) + shoulder * shoulder_w(x)) for x, y in curve_grid(v)])


def apply_ops(d, ops):
    """`k=v` sets, `k+=v` / `k-=v` adjust (temp too). The agent's per-photo tweaks."""
    for tok in (ops or "").split():
        if "+=" in tok or "-=" in tok:
            sign = 1.0 if "+=" in tok else -1.0
            k, v = tok.split("+=" if sign > 0 else "-=", 1)
            base = float(d.get(k, "6500" if k == "temp" else "0") or 0)
            d[k] = "%g" % (base + sign * float(v))
        elif "=" in tok:
            k, v = tok.split("=", 1)
            d[k] = v
    return d


def mask_blob(m):
    t = {"radial": 0, "linear": 1}[m.get("type", "radial")]
    g = [t, 1 if m.get("inverted") else 0, m.get("feather", 0.5),
         m.get("cx", 0.5), m.get("cy", 0.5), m.get("rx", 0.3), m.get("ry", 0.3),
         m.get("x0", 0.5), m.get("y0", 0.35), m.get("x1", 0.5), m.get("y1", 0.65)]
    a = [m.get(k, 0) for k in MASK_KEYS]
    return ",".join("%g" % v for v in g) + "|" + ",".join("%g" % v for v in a) + "|"


# ── one photo ───────────────────────────────────────────────────────────────────────────────

def clear_masks(cs, node):
    own = own_params(cs, node)
    n = sum(1 for k in own if k == "mask")
    lines = own.get("_masks", 0)
    for _ in range(max(n, lines)):
        cs.run("select %d\nmask delete 0" % node)


def own_params(cs, node):
    cs.run("select %d" % node)
    m = json.loads(cs.c.call("cosmo_state", params=True)[1])
    text = m.get("ownParams") or m.get("params") or ""
    d = {}
    masks = 0
    for l in text.splitlines():
        if "=" in l:
            k, v = l.split("=", 1)
            if k == "mask":
                masks += 1
            d[k] = v
    d["_masks"] = masks
    return d


def compose(correction, mood_line, plan):
    """correction + the mood variant + the plan's tweaks, as one dict for the photo. A mood's temp is
    an OFFSET from 6500 on top of the photo's own white balance, as a group's would have been."""
    d = {k: ("%g" % v if isinstance(v, float) else str(v)) for k, v in correction.items()}
    for k, v in parse(mood_line).items():
        if k == "temp":
            d["temp"] = "%g" % (float(d.get("temp", 6500)) + float(v) - 6500)
        elif k in d and k in ("exposure", "highlights", "shadows", "whites", "blacks", "nrLuminance", "nrColor"):
            d[k] = "%g" % (float(d[k]) + float(v))
        else:
            d[k] = v
    return apply_ops(d, plan.get("set", ""))


def set_photo(cs, node, d, masks):
    clear_masks(cs, node)
    cs.run("select %d\nset %s\nset %s" % (node, PHOTO_RESET, render(d)))
    for m in masks:
        cs.run("select %d\nset mask=%s" % (node, mask_blob(m)))


# Matching matches NEUTRALS only. On Tokyo the day set's median highlight tint was +0.18 (green) because most
# frames' brightest pixels are sunlit LEAVES, and matching every photo to it turned the Asakusa facade's white
# green after white balance had made it right (exam, 2026-11-02). A zone takes part — as a target and as a
# photo being matched — only when it is near-neutral: a sky, a wall, stone, a white facade.
NEUTRAL_ZONE_SAT = 0.22


def zone(a, z):
    s = a["zones"][z]
    return s if s["fraction"] >= 0.06 and s["saturation"] < NEUTRAL_ZONE_SAT else None


def match(cs, node, d, target, fam, strength, log, skin=False):
    """Nudge this photo's own R/G/B curves (toe for the shadows, shoulder for the highlights) and its
    saturation toward the set's medians. Two secant steps per axis; partial (`strength`) on purpose —
    a photo keeps its own character, only the outliers are pulled in."""
    for z, where in (("shadows", "toe"), ("highlights", "shoulder")):
        t = target.get(z)
        a = cs.analyze(node)["analysis"]
        cur = zone(a, z)
        if not t or not cur:
            continue
        gw, gt = 0.0, 0.0                    # applied offsets: warmth axis (R-/B+), tint axis (G-)
        kw, kt = -6.0, -10.0                 # starting slopes: d(zone warmth)/d(offset) etc.
        for _ in range(2):
            ew = cur["warmth"] - t["warmth"]
            et = cur["tint"] - t["tint"]
            if abs(ew) < 0.04 and abs(et) < 0.03:
                break
            # Move the zone by -strength * error along each axis, through the measured slope.
            dw = max(-0.05, min(0.05, -strength * ew / kw)) if abs(ew) >= 0.04 else 0.0
            dt = max(-0.04, min(0.04, -strength * et / kt)) if abs(et) >= 0.03 else 0.0
            # dw > 0 means "cooler": B up, R down. dt > 0 means "less green": G down.
            # A NUDGE, not a fight: at ±0.07 / ±0.05 the shoulder fought the look's warm wheels on the moat (the
            # look made its clouds +0.40 warm, the match pulled only the brightest back, blue-magenta) and the
            # sky tore into cream and lilac patches (exam 2: "the sky … seperated from the photo").
            gw2, gt2 = max(-0.035, min(0.035, gw + dw)), max(-0.03, min(0.03, gt + dt))
            for ch, val in (("curveR", -gw2 / 2), ("curveB", gw2 / 2), ("curveG", -gt2)):
                key = d.get(ch, "0,0;1,1")
                base = d.setdefault("_base_" + ch, key)
                d[ch] = offset_curve(base, toe=val if where == "toe" else 0.0,
                                     shoulder=val if where == "shoulder" else 0.0) \
                    if not d.get("_other_" + ch) else offset_curve(d["_other_" + ch],
                                                                    toe=val if where == "toe" else 0.0,
                                                                    shoulder=val if where == "shoulder" else 0.0)
            cs.run("select %d\nset curveR=%s curveG=%s curveB=%s" % (node, d["curveR"], d["curveG"], d["curveB"]))
            a2 = cs.analyze(node)["analysis"]
            new = zone(a2, z)
            if not new:
                break
            if abs(gw2 - gw) > 1e-4 and abs(new["warmth"] - cur["warmth"]) > 1e-3:
                kw = max(-40.0, min(-1.0, (new["warmth"] - cur["warmth"]) / (gw2 - gw)))
            if abs(gt2 - gt) > 1e-4 and abs(new["tint"] - cur["tint"]) > 1e-3:
                kt = max(-40.0, min(-1.0, (new["tint"] - cur["tint"]) / (gt2 - gt)))
            gw, gt, cur = gw2, gt2, new
        for ch in ("curveR", "curveG", "curveB"):
            d["_other_" + ch] = d[ch]       # the shoulder pass builds on the toe pass
        log.append("%s %s %+.3f/%+.3f -> w %+.2f t %+.2f (set %+.2f %+.2f)" % (
            z, where, gw, gt, cur["warmth"], cur["tint"], t["warmth"], t["tint"]))
    a = cs.analyze(node)["analysis"]
    if target.get("sat") and a["meanSaturation"] > 0.02:
        ratio = target["sat"] / a["meanSaturation"]
        # With a person (or a white subject) in the photo, matching may not push global saturation up — it reaches
        # skin too ("keep the skin color for human"), and it turned the white roadster gold (exam 2).
        # Upward by at most +6 in daylight: +11 / +15 on the overcast moat and sea wall went straight into their
        # skies' faint tints — "the sky is over saturated" (exam 2). An overcast photo is SUPPOSED to be quieter
        # than the set's sunny median; its colour comes from the look, not from being matched to the sun.
        up = 5.0 if skin else 6.0 if fam == "day" else 15.0
        dp = max(-15.0, min(up, 100.0 * (ratio - 1.0) * 0.5 * strength))
        if abs(dp) >= 3:
            d["saturation"] = "%g" % (float(d.get("saturation", 0)) + dp)
            cs.run("select %d\nset saturation=%s" % (node, d["saturation"]))
            a = cs.analyze(node)["analysis"]
            log.append("saturation %+.0f -> %.2f (set %.2f)" % (dp, a["meanSaturation"], target["sat"]))
    return a


# What "cinematic" means to the approver, MEASURED on their Grade Lab picks (taste.md): the best versions had
# the deepest blacks and the brightest highlights of their round; the bad ones were flat. Day frames are
# held to p1 <= 0.035 and p99 >= 0.90, night frames to p1 <= 0.02 and p99 >= 0.80 (their lights).
DEPTH = {"day": (0.035, 0.90), "night": (0.02, 0.80)}


def depth(cs, node, d, fam, log):
    """Steepen THIS photo's master curve until it has the range a cinematic frame needs: the toe down while
    the blacks are lifted, the shoulder up while the highlights are dull (and not near clipping). At most
    three steps of 0.02 each way — the look keeps its character, a flat result does not survive."""
    lo, hi = DEPTH[fam]
    a = cs.analyze(node)["analysis"]
    base = d.get("curve", "0,0;1,1")
    toe = sh = 0.0
    for _ in range(3):
        p1, p99 = a["lumPct"][0], a["lumPct"][6]
        dt = -0.02 if p1 > lo and toe > -0.06 else 0.0
        ds = 0.02 if p99 < hi and sh < 0.06 and a["clipHighlights"] < CLIP[fam] * 0.5 else 0.0
        if not dt and not ds:
            break
        toe += dt
        sh += ds
        d["curve"] = offset_curve(base, toe=toe, shoulder=sh)
        cs.run("select %d\nset curve=%s" % (node, d["curve"]))
        a = cs.analyze(node)["analysis"]
    if toe or sh:
        log.append("depth toe %+.2f shoulder %+.2f -> p1 %.3f p99 %.3f" % (toe, sh, a["lumPct"][0], a["lumPct"][6]))
    return a


def guard(cs, node, d, fam, log):
    a = cs.analyze(node)["analysis"]
    hl = float(d.get("highlights", 0))
    while a["clipHighlights"] > CLIP[fam] and hl > -95:
        hl = max(-95.0, hl - 15)
        cs.run("select %d\nset highlights=%g" % (node, hl))
        a = cs.analyze(node)["analysis"]
    if hl != float(d.get("highlights", 0)):
        d["highlights"] = "%g" % hl
        log.append("highlights -> %g" % hl)
    return a


# ── LOOK at the result: the sky and the subject, measured on the render the approver sees ──────────────────
# Exam 2 (2026-11-02), 14 of 16 good, and: *"overall you did it well for the color but the sky is over saturated,
# you completely destroy the sky, the sky is now look sharp and seperated from the photo, please take a look at
# the photo again after grading"*. Measured on the exam's renders against the originals, per control switched off:
#   SHARP     — the sky's fine texture rose 2-4x (sea wall 0.0026 -> 0.0100). Not grain, sharpening, clarity or
#               dehaze (each < 10 %): the colour MIXER's Lum channel. A white sky after white balance sits on
#               the yellow-green hues the deep-greens move darkens, its pixels' hue is noise, so each pixel took
#               a different darkening -> sand. Colour noise reduction (the hue made smooth) and a gentler Lum
#               are the cures (nrColor 90: pagoda 0.0068 -> 0.0033, the original's 0.0034).
#   SATURATED — Fuji's sky 0.17 -> 0.26, the moat's 0.07 -> 0.11 (the look x 1.3 + saturation matched UP +15).
#   SEPARATED — the moat's cloud colour spread 0.013 -> 0.049: cream mids, lilac tops, a teal band — one sky in
#               three lights (the match's shoulder fighting the look's warm wheels).
# So after the edit, the agent LOOKS: the sky (found on the original: upper, bright, smooth, not leaves) must
# keep the original's smoothness, roughly its saturation and ONE colour; the subject must not lose its light.
# after <= before * a + b. Set by EYE on the exam-3 renders: at 1.5x texture Fuji's cloud still read sandy, at 1.8x
# colour spread the moat's clouds still read cream-and-lilac; at these limits both are fixed and the sea wall's soft
# cloud shapes (1.55x spread, one colour family) stay.
SKY_LIMITS = {"tex": (1.25, 0.0003), "sat": (1.2, 0.01), "spread": (1.3, 0.006),
              # Exam 3: "don't make the sky and cloud too seperated, make sure the edit don't give too much contract
              # on them, especially #10, the sky look too fake because of the clouds". Measured on the WHOLE sky,
              # clouds and their edges included: gap = the colour distance between the clouds (the camera's
              # brightest quarter of the sky) and the sky behind them (its darkest third) — Fuji 0.151 -> 0.215,
              # olive clouds on blue; contrast = the sky's p5-p95 luminance — Kamakura 0.204 -> 0.239.
              # (+0.02 absolute: the moat's 0.010 -> 0.025 is invisible, Fuji's 0.17 -> 0.29 is stickers)
              # (contrast 1.1x let Kamakura's cloud pop out of a darker sky: "too much contract on them")
              "gap": (1.15, 0.02), "contrast": (1.03, 0.003),
              # structure: the clouds' LOCAL contrast (|blur 2 px - blur 12 px| of luma) — no more than the camera's
              "structure": (1.0, 0.0005)}


def _img(path, edge=600):
    import numpy as np
    from PIL import Image
    im = Image.open(path).convert("RGB")
    im = im.resize((edge, max(1, round(im.height * edge / im.width))))
    return np.asarray(im).astype("float32") / 255


def _luma(a):
    return a[..., 0] * 0.2126 + a[..., 1] * 0.7152 + a[..., 2] * 0.0722


def _blur(x, r):
    import numpy as np
    from PIL import Image, ImageFilter
    im = Image.fromarray((np.clip(x, 0, 1) * 255).astype("uint8")).filter(ImageFilter.GaussianBlur(r))
    return np.asarray(im).astype("float32") / 255


def sky_region(before):
    """The sky as the camera saw it: the upper half, bright, smooth, not leaves. None if there is no sky."""
    import numpy as np
    l = _luma(before)
    top = np.zeros(l.shape, bool)
    top[: int(l.shape[0] * 0.55)] = True
    mx, mn = before.max(2), before.min(2)
    s = (mx - mn) / np.maximum(mx, 1e-6)
    leaf = (before[..., 1] >= before[..., 0]) & (before[..., 1] >= before[..., 2]) & (s > 0.25)
    m = top & (l > np.percentile(l, 70)) & (np.hypot(*np.gradient(l)) < 0.02) & ~leaf
    return m if m.mean() >= 0.04 else None


def sky_stats(a, m):
    """tex: fine texture (|luma - its 2 px blur|); sat: HSV saturation; spread: how many colours the sky is
    (the spread of its low-frequency chroma) — all on the sky's pixels."""
    import numpy as np
    l = _luma(a)
    mx, mn = a.max(2), a.min(2)
    sm = _blur(a, 6)
    cb, cg = sm[..., 2] - sm[..., 0], sm[..., 1] - (sm[..., 0] + sm[..., 2]) / 2
    return {"tex": float(np.abs(l - _blur(l, 2))[m].mean()),
            "sat": float(((mx - mn) / np.maximum(mx, 1e-6))[m].mean()),
            "spread": float(np.hypot(cb[m].std(), cg[m].std()))}


def sky_all(before):
    """The whole sky, clouds and their edges too (sky_region keeps only its smooth inside)."""
    import numpy as np
    l = _luma(before)
    top = np.zeros(l.shape, bool)
    top[: int(l.shape[0] * 0.55)] = True
    mx, mn = before.max(2), before.min(2)
    s = (mx - mn) / np.maximum(mx, 1e-6)
    leaf = (before[..., 1] >= before[..., 0]) & (before[..., 1] >= before[..., 2]) & (s > 0.25)
    return top & (l > np.percentile(l, 70)) & ~leaf


def cloud_stats(a, m, ref_l):
    """gap: colour distance between the clouds and the sky behind them (split by the CAMERA's luminance, so the
    same pixels are compared before and after); contrast: the sky's p5-p95 luminance."""
    import numpy as np
    sm, l = _blur(a, 3), _blur(_luma(a), 3)
    cl = m & (ref_l >= np.percentile(ref_l[m], 75))
    sk = m & (ref_l <= np.percentile(ref_l[m], 35))
    v = lambda r: np.array([(sm[..., 2] - sm[..., 0])[r].mean(), (sm[..., 1] - (sm[..., 0] + sm[..., 2]) / 2)[r].mean()])
    return {"gap": float(np.linalg.norm(v(cl) - v(sk))), "contrast": float(np.percentile(l[m], 95) - np.percentile(l[m], 5)),
            "cloud_y": float((((sm[..., 0] + sm[..., 1]) / 2 - sm[..., 2])[cl]).mean()),
            "cloud_hi": float(np.percentile(l[cl], 95)),
            "structure": float(np.abs(_blur(_luma(a), 2) - _blur(_luma(a), 12))[m].mean())}


def _ellipse(shape, x, k=1.0):
    import numpy as np
    H, W = shape
    yy, xx = np.mgrid[0:H, 0:W]
    return ((xx / W - x["cx"]) / (k * x["rx"])) ** 2 + ((yy / H - x["cy"]) / (k * x["ry"])) ** 2 <= 1


def sky_faults(now, was):
    bad = [k for k, (a, b) in SKY_LIMITS.items() if now[k] > was[k] * a + b]
    # Clouds (and snow) stay as WHITE as white balance left them — measured on the yellow axis ((R+G)/2 - B): once
    # Fuji's sky had calmed, its clouds and snow read cream (+0.044 white-balanced -> +0.062): the look's +260 K
    # (+0.012), the depth curve (+0.009), dehaze (+0.006); the grader's own look at exam 3, #10.
    if "cloud_y" in was and now["cloud_y"] > was["cloud_y"] + 0.01:
        bad.append("warm")
    # ...and as BRIGHT: a dimmed white reads beige-grey. Fuji's clouds went from 0.90 (white balance alone) to 0.77
    # (the correction's highlights -20 on top of the look's depth, the vignette, the sky's darkening).
    if "cloud_hi" in was and now["cloud_hi"] < was["cloud_hi"] - 0.05:
        bad.append("dull")
    return bad
    return bad


def reapply(cs, node, d, masks):
    set_photo(cs, node, {k: v for k, v in d.items() if not k.startswith("_")}, masks)


def _scale_mixer(v, f):
    return ";".join("%g,%g" % (float(h), float(y) * f) for h, y in (p.split(",") for p in v.split(";") if "," in p))


def sky_check(cs, node, d, masks, before_jpg, scratch, log, corr=None):
    """Render, LOOK at the sky against the original, and take back what broke it — one fix per round, the
    cheapest first, the land's colour untouched wherever possible:
      sharp     -> colour NR 60, then 90 (smooths the hue the mixer reads); then the mixer's Lum halved, then off
      saturated -> a graduated filter over the sky: saturation -12 per round (never dehaze, never clarity)
      separated -> the match's R/G/B shoulder taken back; then the highlight wheels halved
      cloud gap -> vibrance halved, then off; then the sky's filter desaturates (to -24); then the wheels halved
      contrast  -> the plan's sky darkening eased +0.1 EV a round, a subject lift reaching into the sky halved,
                   then the clouds softened (clarity/dehaze down)
      warm      -> clouds cream: the look's extra warmth halved, then off; then the warm highlight wheels
      dull      -> clouds dimmed: highlights back toward 0, the vignette halved, the sky's darkening eased
      structure -> the clouds' local contrast above the camera's: the highlight recovery eased to -40, then the
                   sky filter's contrast -10 a round (to -30)
    The guard runs after: whatever the clouds get back may not clip."""
    was_img = _img(before_jpg)
    m = sky_region(was_img)
    if m is None:
        return masks
    was = sky_stats(was_img, m)
    if was["tex"] > 0.008:             # not a smooth sky — a bright wall, a blossom canopy: the look may work there
        return masks
    m_all = sky_all(was_img)
    ref_l = _blur(_luma(was_img), 3)
    was.update(cloud_stats(was_img, m_all, ref_l))
    if corr:
        # The clouds' COLOUR against their sky is judged on the white-balanced frame, not the camera's: Fuji was shot
        # through a green window — clouds and sky the same green — and white clouds on a blue sky are the truth.
        set_photo(cs, node, {k: ("%g" % v if isinstance(v, float) else str(v)) for k, v in corr.items()}, [])
        cs.preview(scratch, node, edge=900)
        wb = cloud_stats(_img(scratch), m_all, ref_l)
        was["gap"], was["cloud_y"], was["cloud_hi"] = wb["gap"], wb["cloud_y"], wb["cloud_hi"]
        reapply(cs, node, d, [{k: v for k, v in x.items() if not k.startswith("_")} for x in masks])
    rows = [i for i in range(m.shape[0]) if m[i].any()]
    bottom = (sorted(rows)[int(0.85 * (len(rows) - 1))] + 1) / m.shape[0]
    masks = [dict(x) for x in masks]
    first = None
    for _ in range(14):
        cs.preview(scratch, node, edge=900)
        img = _img(scratch)
        now = sky_stats(img, m)
        now.update(cloud_stats(img, m_all, ref_l))
        first = first or now
        bad = sky_faults(now, was)
        if not bad:
            break
        fix = None
        if "tex" in bad:
            nr = float(d.get("nrColor", 0) or 0)
            if nr < 60:
                d["nrColor"], fix = "60", "colour NR 60"
            elif nr < 90:
                d["nrColor"], fix = "90", "colour NR 90"
            elif d.get("mixer2") and not d.get("_lum_halved"):
                d["mixer2"], d["_lum_halved"], fix = _scale_mixer(d["mixer2"], 0.5), "1", "mixer Lum halved"
            elif d.get("mixer2"):
                d.pop("mixer2")
                fix = "mixer Lum off (the plan's masks keep the land's depth)"
        if not fix and "sat" in bad:
            g = next((x for x in masks if x.get("_sky")), None)
            if g is None:
                g = {"type": "linear", "x0": 0.5, "y0": min(1.0, bottom + 0.1), "x1": 0.5, "y1": bottom * 0.5,
                     "saturation": 0, "_sky": True}
                masks.append(g)
            if g["saturation"] > -48:
                g["saturation"] -= 12
                fix = "sky graduated filter saturation %d" % g["saturation"]
        if not fix and "spread" in bad:
            if any(("_base_" + c) in d for c in ("curveR", "curveG", "curveB")) and not d.get("_unmatched"):
                for c in ("curveR", "curveG", "curveB"):
                    if ("_base_" + c) in d:
                        d[c] = d["_base_" + c]
                d["_unmatched"], fix = "1", "the match's R/G/B shoulder taken back"
            elif not d.get("_wheels_halved"):
                for w in ("grade1", "grade2"):
                    if d.get(w):
                        h, s, l = (float(x) for x in d[w].split(","))
                        d[w] = "%g,%g,%g" % (h, s / 2, l)
                d["_wheels_halved"], fix = "1", "highlight wheels halved"
        if not fix and "gap" in bad:
            # The clouds and their sky drifted apart in colour. Measured on Fuji (#10, exam 3: "the sky look too fake
            # because of the clouds"): VIBRANCE — it pushes the palest colours hardest, so a pale blue sky went bluer
            # and its pale cream clouds creamier (gap 0.169 white-balanced -> 0.288; vibrance 0 -> 0.201); the wheels,
            # curves and masks moved it < 0.01. So: vibrance halved, then off (the cause; the land's colour lives in
            # the mixer and saturation), then the sky's filter desaturates a little (-36 there greyed Fuji's sky and
            # left its clouds peach), then the warm/cool wheels.
            g = next((x for x in masks if x.get("_sky")), None)
            if float(d.get("vibrance", 0) or 0) > 3 and not d.get("_vib_halved"):
                d["vibrance"], d["_vib_halved"] = "%g" % (float(d["vibrance"]) / 2), "1"
                fix = "vibrance halved to %s" % d["vibrance"]
            elif float(d.get("vibrance", 0) or 0) > 0 and not d.get("_vib_off"):
                d["vibrance"], d["_vib_off"], fix = "0", "1", "vibrance off"
            elif g is None or g["saturation"] > -24:
                if g is None:
                    g = {"type": "linear", "x0": 0.5, "y0": min(1.0, bottom + 0.1), "x1": 0.5, "y1": bottom * 0.5,
                         "saturation": 0, "_sky": True}
                    masks.append(g)
                g["saturation"] -= 12
                fix = "sky graduated filter saturation %d" % g["saturation"]
            elif not d.get("_wheels_halved"):
                for w in ("grade1", "grade2"):
                    if d.get(w):
                        h, s_, l_ = (float(x) for x in d[w].split(","))
                        d[w] = "%g,%g,%g" % (h, s_ / 2, l_)
                d["_wheels_halved"], fix = "1", "highlight wheels halved"
            elif d.get("grade0") and not d.get("_shadow_halved"):
                h, s_, l_ = (float(x) for x in d["grade0"].split(","))
                d["grade0"], d["_shadow_halved"], fix = "%g,%g,%g" % (h, s_ / 2, l_), "1", "shadow wheel halved"
        if not fix and "warm" in bad:
            look_k = float(d.get("temp", 0)) - float((corr or {}).get("temp", d.get("temp", 0)))
            if corr and look_k > 50 and not d.get("_temp_halved"):
                d["temp"] = "%g" % (float(corr["temp"]) + look_k / 2)
                d["_temp_halved"], fix = "1", "the look's warmth on the clouds halved (temp %s)" % d["temp"]
            elif corr and look_k > 20 and not d.get("_temp_off"):
                d["temp"] = "%g" % float(corr["temp"])
                d["_temp_off"], fix = "1", "the look's extra warmth off (temp %s, the white balance)" % d["temp"]
            elif not d.get("_wheels_halved"):
                for w in ("grade1", "grade2"):
                    if d.get(w):
                        h, s_, l_ = (float(x) for x in d[w].split(","))
                        d[w] = "%g,%g,%g" % (h, s_ / 2, l_)
                d["_wheels_halved"], fix = "1", "warm highlight wheels halved"
            elif not d.get("_wheels_off") and (d.get("grade1") or d.get("grade2")):
                for w in ("grade1", "grade2"):
                    d.pop(w, None)
                d["_wheels_off"], fix = "1", "warm highlight wheels off"
        if not fix and "dull" in bad:
            hl = float(d.get("highlights", 0) or 0)
            if hl < 0:
                d["highlights"] = "%g" % min(0.0, hl + 20)
                fix = "the clouds' light back: highlights %s" % d["highlights"]
            elif float(d.get("lensVignette", 0) or 0) < -4 and not d.get("_vig_halved"):
                d["lensVignette"], d["_vig_halved"] = "%g" % (float(d["lensVignette"]) / 2), "1"
                fix = "vignette halved (%s) — it dimmed the clouds in the corners" % d["lensVignette"]
            else:
                dark = [x for x in masks if x.get("type") == "linear" and x.get("y1", 1) < x.get("y0", 0)
                        and x.get("exposure", 0) < 0 and not x.get("_sky")]
                for x in dark:
                    x["exposure"] = round(min(0.0, x["exposure"] + 0.1), 2)
                if dark:
                    fix = "the sky's darkening eased to %+g EV" % dark[0]["exposure"]
        if not fix and "structure" in bad:
            # Highlight RECOVERY is what pulls a cloud out of a bright sky: Kamakura's correction held its sky with
            # highlights -80 and the cloud the camera barely showed stood out of a grey-blue sky; at -40 it is the
            # camera's airy sky with a soft white cloud (clip 0.06 %; the guard still runs after). A contrast-down
            # filter on the sky only greyed it.
            hl = float(d.get("highlights", 0) or 0)
            g = next((x for x in masks if x.get("_sky")), None)
            if hl < -40:
                d["highlights"] = "%g" % min(-40.0, hl + 20)
                fix = "the highlight recovery eased (highlights %s) — it pulled the clouds out of the sky" % d["highlights"]
            elif g is None:
                g = {"type": "linear", "x0": 0.5, "y0": min(1.0, bottom + 0.1), "x1": 0.5, "y1": bottom * 0.5,
                     "saturation": 0, "_sky": True}
                masks.append(g)
            if not fix and g.get("contrast", 0) > -30:
                g["contrast"] = g.get("contrast", 0) - 10
                fix = "the clouds' contrast eased (sky filter contrast %d)" % g["contrast"]
        if not fix and "contrast" in bad:
            # Too much contrast between the clouds and the sky: first the plan's darkening of the sky eases (it
            # sinks the blue behind a bright cloud — Kamakura), then the sky's cloud edges are softened.
            dark = [x for x in masks if x.get("type") == "linear" and x.get("y1", 1) < x.get("y0", 0)
                    and x.get("exposure", 0) < 0 and not x.get("_sky")]
            # A subject's radial lift that reaches into the sky brightens the clouds there (Kamakura: the blossom's
            # +0.15 EV lit the cloud above it, which popped out of its sky).
            onsky = [x for x in masks if x.get("type", "radial") == "radial" and x.get("exposure", 0) > 0.05
                     and _ellipse(m_all.shape, x).any() and m_all[_ellipse(m_all.shape, x)].mean() > 0.2]
            if dark:
                for x in dark:
                    x["exposure"] = round(min(0.0, x["exposure"] + 0.1), 2)
                fix = "the sky's darkening eased to %+g EV" % dark[0]["exposure"]
            elif onsky:
                for x in onsky:
                    x["exposure"] = round(x["exposure"] / 2, 3)
                fix = "a lift reaching into the sky halved (%+g EV)" % onsky[0]["exposure"]
            else:
                g = next((x for x in masks if x.get("_sky")), None)
                if g is None:
                    g = {"type": "linear", "x0": 0.5, "y0": min(1.0, bottom + 0.1), "x1": 0.5, "y1": bottom * 0.5,
                         "saturation": 0, "_sky": True}
                    masks.append(g)
                if g.get("clarity", 0) > -30:
                    g["clarity"], g["dehaze"] = g.get("clarity", 0) - 10, g.get("dehaze", 0) - 8
                    fix = "the clouds softened (sky clarity %d, dehaze %d)" % (g["clarity"], g["dehaze"])
        if not fix:
            break
        log.append("sky: %s (%s)" % (fix, ", ".join(bad)))
        reapply(cs, node, d, [{k: v for k, v in x.items() if not k.startswith("_")} for x in masks])
    log.append("sky check: texture %.4f -> %.4f, saturation %.3f -> %.3f, colour spread %.3f -> %.3f "
               "(original %.4f / %.3f / %.3f); clouds vs sky colour %.3f -> %.3f, contrast %.3f -> %.3f "
               "(white-balanced %.3f / camera %.3f)%s" % (first["tex"], now["tex"], first["sat"], now["sat"], first["spread"],
                                             now["spread"], was["tex"], was["sat"], was["spread"], first["gap"], now["gap"],
                                             first["contrast"], now["contrast"], was["gap"], was["contrast"],
                                             " — still: " + ", ".join(bad) if bad else ""))
    return masks


def subject_check(cs, node, d, masks, before_jpg, scratch, log, skin=False, white=False, fam="day"):
    """A person and a white subject keep what the camera gave them — the mood lives around them. The subject is
    the plan's first radial LIFT (exposure > 0); on its inner part, measured on screen against the camera's frame,
    each round (other subjects are left alone: a deer or a shrine sunk with the look's depth was judged GOOD):
      light  — a person must end no darker than the camera had them (at night 10 % brighter: the camera's frame was
               under), else the lift rises +0.2 EV (to +1.6):
               a dodge, the face back in its light while the room stays deep. Exam 2, the teamLab man: "the face
               is too dark" — Neon Noir's deep toe at full depth took it from 0.128 to 0.088 on screen although
               its mask lifted +0.2; on screen +0.8 EV gives back only ~30 %, so the cap is high.
      skin   — at night, a person's face keeps the camera's warmth (R-B within 0.03), else the mask warms +10 a round (to
               +50), and its colour (saturation <= camera x 1.15 + 0.02), else -8 a round (to -32): the look's teal
               shadows (Neon Noir's R/G/B toe) turned that face from pink 338 deg to violet 279 deg, and under art
               light a face sits off the 8-48 deg band protect_skin pins, so the mixer's magenta reached it.
      white  — a white subject gains no colour: on the pixels that were WHITE in the camera's frame (bright, sat <
               0.45 — not the tail lights) saturation <= camera + 0.02, else the mask cools -10 a round (to -50), a
               local white balance. Exam 2, the white roadster: "would be better if the car don't fall for gold too
               much" — white balance, brightening, the look and set matching EACH added a little (paint 0.31 ->
               0.46). Desaturating the mask instead turned the red tail lights pink."""
    import numpy as np
    i = next((k for k, x in enumerate(masks) if x.get("type", "radial") == "radial" and x.get("exposure", 0) > 0), None)
    if i is None or not (skin or white):
        return masks
    s = masks[i]
    was_img = _img(before_jpg)
    H, W = was_img.shape[:2]
    yy, xx = np.mgrid[0:H, 0:W]
    inner = ((xx / W - s["cx"]) / (0.6 * s["rx"])) ** 2 + ((yy / H - s["cy"]) / (0.6 * s["ry"])) ** 2 <= 1
    if inner.sum() < 50:
        return masks

    def sat(img):
        mx, mn = img.max(2), img.min(2)
        return (mx - mn) / np.maximum(mx, 1e-6)

    lw = _luma(was_img)
    lit = inner & (lw > np.percentile(lw[inner], 50))             # the subject's lit half: paint, skin — not tyres
    whites = lit & (sat(was_img) < 0.45)                            # what the camera saw as white(ish)

    def read(img):
        l, s_ = _luma(img), sat(img)
        return {"lum": float(np.median(l[inner])), "rb": float((img[..., 0] - img[..., 2])[lit].mean()),
                "sat": float(s_[lit].mean()), "white": float(s_[whites].mean()) if whites.sum() >= 30 else 0.0}

    was = read(was_img)
    masks = [dict(x) for x in masks]
    start = dict(masks[i])
    for _ in range(8):
        cs.preview(scratch, node, edge=900)
        now = read(_img(scratch))
        m, moved = masks[i], False
        if skin and now["lum"] < was["lum"] * (1.1 if fam == "night" else 1.0) and m["exposure"] < 1.6:
            m["exposure"], moved = round(min(1.6, m["exposure"] + 0.2), 2), True
        if skin and fam == "night" and now["rb"] < was["rb"] - 0.03 and m.get("temp", 0) < 50:
            m["temp"], moved = m.get("temp", 0) + 10, True
        if skin and fam == "night" and now["sat"] > was["sat"] * 1.15 + 0.02 and m.get("saturation", 0) > -32:
            m["saturation"], moved = m.get("saturation", 0) - 8, True
        if white and was["white"] and now["white"] > was["white"] + 0.02 and m.get("temp", 0) > -50:
            m["temp"], moved = m.get("temp", 0) - 10, True
        if not moved:
            break
        reapply(cs, node, d, [{k: v for k, v in x.items() if not k.startswith("_")} for x in masks])
    ch = ["%s %+g -> %+g" % (k, start.get(k, 0), masks[i].get(k, 0)) for k in ("exposure", "temp", "saturation")
          if masks[i].get(k, 0) != start.get(k, 0)]
    if ch:
        log.append("subject: %s; on screen light %.3f (camera %.3f), warmth R-B %+.3f (camera %+.3f), saturation %.3f "
                   "(camera %.3f)%s" % (", ".join(ch), now["lum"], was["lum"], now["rb"], was["rb"], now["sat"], was["sat"],
                                        ", white paint saturation %.3f (camera %.3f)" % (now["white"], was["white"]) if white else ""))
    return masks


# ── commands ────────────────────────────────────────────────────────────────────────────────

def ungroup_looks(cs):
    """Per-photo editing: a look group would apply a SECOND, shared edit on top. Dissolve them —
    the photos keep their own params (the group's are gone, which is the point)."""
    for _ in range(8):
        st = cs.state()
        g = [n for n in st["nodes"] if n.get("kind") == "group" and
             (n["name"].endswith(" - Day") or n["name"].endswith(" - Night"))]
        if not g:
            return
        cs.run("group ungroup %d" % g[0]["node"])


def cmd_review(args):
    from PIL import Image, ImageDraw
    out = Path(args.out)
    (out / "review").mkdir(parents=True, exist_ok=True)
    cs = A.Cosmo(str(out / "cosmo-mcp.log"))
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        names = A.nodes_by_name(cs.state())
        files = []
        for name, node in names.items():
            p = out / "review" / (Path(name).stem + ".jpg")
            cs.preview(p, node, before=True, edge=900)
            files.append((name, p))
        per = args.per_sheet
        cell = 620
        for k in range(0, len(files), per):
            chunk = files[k:k + per]
            cols = 3
            rows = (len(chunk) + cols - 1) // cols
            S = Image.new("RGB", (cols * cell, rows * (cell + 24)), (18, 18, 18))
            dr = ImageDraw.Draw(S)
            for i, (name, p) in enumerate(chunk):
                im = Image.open(p)
                im.thumbnail((cell - 10, cell - 10))
                g = ImageDraw.Draw(im)
                for f in (0.25, 0.5, 0.75):      # the grid the plan's mask coordinates are read from
                    g.line([(f * im.width, 0), (f * im.width, im.height)], fill=(255, 255, 0), width=1)
                    g.line([(0, f * im.height), (im.width, f * im.height)], fill=(255, 255, 0), width=1)
                x = (i % cols) * cell + (cell - im.width) // 2
                y = (i // cols) * (cell + 24)
                S.paste(im, (x, y))
                dr.text(((i % cols) * cell + 6, y + cell - 6), name, fill=(235, 235, 235))
            S.save(out / ("review_%02d.jpg" % (k // per + 1)), quality=84)
        print("review sheets ->", out, "(%d photos; grid lines at 0.25 / 0.5 / 0.75)" % len(files))
    finally:
        cs.close()


def cmd_run(args):
    out = Path(args.out)
    for sub in ("before", "after"):
        (out / sub).mkdir(parents=True, exist_ok=True)
    plan = json.loads(Path(args.plan).read_text()) if args.plan and Path(args.plan).exists() else {"photos": {}}
    # The default is the approver's TASTE (taste.py): each photo gets the look the Grade Lab verdicts chose
    # for photos like it. A named mood ("spring-haze", or words) puts that one mood on every photo.
    mood = args.mood or plan.get("mood") or "taste"
    if mood != "taste":
        mood = mood if mood in M.MOODS else M.find(mood)
    import taste as T
    if not mood:
        sys.exit("run: no mood matches %r" % args.mood)
    only = set(n for n in args.names.split(",") if n) if args.names else None
    # A partial re-run (fixing the few photos that missed) keeps the WHOLE set's targets and merges
    # into its record — two photos' medians would match them to each other, not to the set.
    prev = json.loads((out / "edit.json").read_text()) if (out / "edit.json").exists() else None
    cs = A.Cosmo(str(out / "cosmo-mcp.log"))
    rows = []
    try:
        cs.run("project open %s" % Path(args.project).resolve())
        ungroup_looks(cs)
        names = A.nodes_by_name(cs.state())
        # ── pass 1: read, correct, mood, plan — every photo on its own ──
        for name, node in names.items():
            if only and name not in only:
                continue
            pp = plan["photos"].get(name, {})
            r = cs.analyze(node, before=True)
            md = A.meta_dict(r["metadata"])
            scene, fam, why = A.classify(r["analysis"], md)
            if pp.get("scene"):
                scene, why = pp["scene"], ["set from the agent's reading of the photo (plan.json)"]
                fam = L.FAMILY[scene]
            log = []
            clear_masks(cs, node)
            cs.run("select %d\nset %s" % (node, PHOTO_RESET))
            corr, _ = A.normalise(cs, node, scene, int(A.number(md.get("ISO"), 100)), log)
            skin = white = False
            if mood == "taste":
                ch = T.choose(scene, r["analysis"], pp)
                v = {"look": ch["look"], "strength": ch["strength"] * args.strength * pp.get("strength", 1.0),
                     "tweaks": ch["tweaks"], "skin": ch["skin"], "white": ch.get("white", False)}
                d = T.params(corr, fam, v, pp)
                skin, white = ch["skin"], ch.get("white", False)
                log.append("look %s %.0f%%%s%s — %s" % (ch["look"], v["strength"] * 100,
                                                        "".join(" +%s" % t[0] for t in ch["tweaks"]),
                                                        (" +skin" if ch["skin"] else "") + (" +white" if ch.get("white") else ""),
                                                        ch["why"]))
            else:
                d = compose(corr, M.scale(M.MOODS[mood][fam], args.strength * pp.get("strength", 1.0)), pp)
            set_photo(cs, node, d, pp.get("masks", []))
            a = cs.analyze(node)["analysis"]
            rows.append({"name": name, "node": node, "scene": scene, "family": fam, "why": why,
                         "skin": skin, "white": white,
                         "meta": {k: md.get(k) for k in ("Taken", "ISO", "Shutter", "Aperture", "Focal length",
                                                          "Lens", "Camera model")},
                         "before": {k: r["analysis"][k] for k in ("keyEv", "lumPct", "castWarmth", "castTint",
                                                                  "meanSaturation", "clipHighlights", "skinFraction",
                                                                  "skyFraction", "foliageFraction", "neonFraction")},
                         "reading": pp.get("reading", ""),
                         "subject": pp.get("subject", ""), "intent": pp.get("intent", ""),
                         "d": d, "corr": corr, "masks": pp.get("masks", []), "match": pp.get("match", True),
                         "pass1": a, "log": log})
            print("%-18s %-14s pass1 p50 %.3f sat %.2f  %s" % (name, scene, a["lumPct"][3], a["meanSaturation"],
                                                               pp.get("subject", "")))
        # ── the set's medians, per family: what "consistent" means for THIS set in THIS mood ──
        targets = {}
        for fam in (() if (only and prev and prev.get("targets")) else ("day", "night")):
            rs = [r for r in rows if r["family"] == fam]
            if not rs:
                continue
            t = {}
            for z in ("shadows", "highlights"):
                zs = [zone(r["pass1"], z) for r in rs if zone(r["pass1"], z)]
                if len(zs) >= 2:
                    t[z] = {"warmth": statistics.median(x["warmth"] for x in zs),
                            "tint": statistics.median(x["tint"] for x in zs)}
            # Whites stay CLEAN: whatever the set's median says, a neutral highlight is never matched toward more
            # than slightly warm (warmth 0.15) or a hair green (tint 0.06) — the set's median after the look was
            # +0.26 / +0.18 (yellow-olive) and dragged the moat's and Asakusa's skies there (bold exam review).
            if "highlights" in t:
                t["highlights"]["warmth"] = max(-0.05, min(0.15, t["highlights"]["warmth"]))
                t["highlights"]["tint"] = max(-0.03, min(0.06, t["highlights"]["tint"]))
            t["sat"] = statistics.median(r["pass1"]["meanSaturation"] for r in rs)
            targets[fam] = t
        if only and prev and prev.get("targets"):
            targets = prev["targets"]
        # ── pass 2: match each photo to its family, guard, look ──
        for r in rows:
            node, fam, d = r["node"], r["family"], r["d"]
            if r["match"] and targets.get(fam):
                a = match(cs, node, d, targets[fam], fam, args.match, r["log"],
                          skin=r.get("skin", False) or r.get("white", False))
            depth(cs, node, d, fam, r["log"])
            stem = Path(r["name"]).stem
            if not (out / "before" / (stem + ".jpg")).exists():
                cs.preview(out / "before" / (stem + ".jpg"), node, before=True)
            # LOOK at the result before it is delivered (exam 2: "take a look at the photo again after grading")
            scratch = out / "after" / (".check-" + stem + ".jpg")
            masks = subject_check(cs, node, d, r["masks"], out / "before" / (stem + ".jpg"), scratch, r["log"],
                                  skin=r.get("skin", False), white=r.get("white", False), fam=fam)
            if fam == "day":
                masks = sky_check(cs, node, d, masks, out / "before" / (stem + ".jpg"), scratch, r["log"], corr=r.get("corr"))
            r["masks"] = [{k: v for k, v in x.items() if not k.startswith("_")} | ({"sky": True} if x.get("_sky") else {})
                          for x in masks]
            scratch.unlink(missing_ok=True)
            a = guard(cs, node, d, fam, r["log"])
            cs.preview(out / "after" / (stem + ".jpg"), node)
            r["after"] = a
            r["look"] = next((l for l in r["log"] if l.startswith("look ")), "")
            print("%-18s %-5s p50 %.3f sat %.2f clip %.3f | %s" % (r["name"], fam, a["lumPct"][3], a["meanSaturation"],
                                                                  a["clipHighlights"], " | ".join(r["log"][-3:])))
        if args.save_as:
            cs.run("project save %s" % Path(args.save_as).resolve())
        elif args.save:
            cs.run("project save")
        for r in rows:
            r["params"] = {k: v for k, v in r.pop("d").items() if not k.startswith("_")}
            r.pop("corr", None)
            r.pop("pass1", None)
        if only and prev:
            keep = [p for p in prev.get("photos", []) if p["name"] not in only]
            rows = keep + rows
        (out / "edit.json").write_text(json.dumps({"mood": mood, "strength": args.strength, "match": args.match,
                                                   "targets": targets, "photos": rows}, indent=1))
    finally:
        cs.close()
    A.sheets(out)
    import mood as MOODTOOL
    MOODTOOL.overview(out, "%s - edited per photo" % ("The approver's taste" if mood == "taste" else M.MOODS[mood]["title"]))
    print("-> %s (overview.jpg, sheet_*.jpg, edit.json)" % out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd", choices=("review", "run"))
    ap.add_argument("project")
    ap.add_argument("--out", required=True)
    ap.add_argument("--mood", default="", help="'taste' (default: the approver's look per photo) or a mood name / words")
    ap.add_argument("--plan", default="")
    ap.add_argument("--names", default="", help="only these photos (names, comma-separated)")
    ap.add_argument("--strength", type=float, default=1.0, help="the mood's strength")
    ap.add_argument("--match", type=float, default=0.7, help="how far each photo is pulled to the set (0..1)")
    ap.add_argument("--per-sheet", type=int, default=6)
    ap.add_argument("--save", action="store_true")
    ap.add_argument("--save-as", default="")
    args = ap.parse_args()
    {"review": cmd_review, "run": cmd_run}[args.cmd](args)


if __name__ == "__main__":
    main()
