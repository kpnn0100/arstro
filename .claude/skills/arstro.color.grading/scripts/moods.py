"""moods.py — moods: the creative half of a grade, as Cosmo `set` lines a GROUP carries.

A mood is not a correction. The photo is corrected first (autograde's normalise: white balance,
exposure, highlights), and the mood then BLENDS colour through the tones and REDUCES the palette:

  * the R/G/B curves tint the blacks, cross the channels and colour the highlights — the colour lives
    IN the tones, which is what separates a grade from a filter (references/moods.md §1);
  * the mixer pulls every hue toward two or three (hue), mutes what is not in the palette (sat) and
    sets each hue's density (lum);
  * the master curve sets the film character (toe, shoulder, density); wheels and grain finish it.

Each mood has a DAY and a NIGHT variant (one group each), and every one is written up in
moods/<name>.md — what it is for, its palette, when not to use it, what the approver said.

Curves: `x,y;x,y` in display space, joined by straight lines (use 5-7 points for a smooth bend).
Mixer: `hue_deg,y` cyclic, y in -1..1 — hue shifts y*180 deg, sat scales by (1+y), lum gains 2^(1.5y).
Never put a colour wheel on a photo as well as its group: wheel hues ADD across levels (D-68).
"""
import re


def _crv(*pts):
    return ";".join("%g,%g" % p for p in pts)


MOODS = {
    # ── 1 ─────────────────────────────────────────────────────────────────────────────────────
    "neon-noir": {
        "title": "Neon Noir",
        "about": "Tokyo after rain, Blade Runner 2049 / Cinestill 800T: teal-black shadows, magenta and cyan "
                 "light, sodium yellow bent to amber, greens gone to cyan, hard contrast.",
        "keywords": ["neon", "cyberpunk", "blade runner", "noir", "tokyo night", "cinestill", "night city", "futuristic"],
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,-0.05;45,0;60,-0.08;120,0.2;180,0.02;240,-0.06;300,0 "
                "mixer1=0,0.15;60,-0.2;120,-0.35;190,0.2;240,0;300,0.25 "
                "mixer2=0,0;120,-0.25;220,-0.2;300,0.08 "
                "saturation=-8 vibrance=8 clarity=10 dehaze=6 grainAmount=10 grainSize=22 lensVignette=-18"
                % (_crv((0, 0), (0.15, 0.09), (0.5, 0.5), (0.85, 0.89), (1, 0.97)),
                   _crv((0, 0), (0.25, 0.2), (0.6, 0.6), (1, 1)),
                   _crv((0, 0.02), (0.3, 0.3), (0.75, 0.735), (1, 0.975)),
                   _crv((0, 0.07), (0.3, 0.35), (0.7, 0.71), (1, 0.97)))),
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,-0.06;45,0;60,-0.08;120,0.22;180,0.03;240,-0.08;300,0 "
                  "mixer1=0,0.2;60,-0.1;120,-0.3;190,0.25;240,0.05;300,0.3 "
                  "mixer2=0,0;120,-0.3;220,-0.2;300,0.1 "
                  "saturation=-5 vibrance=10 clarity=12 dehaze=6 grainAmount=12 grainSize=25 lensVignette=-20"
                  % (_crv((0, 0), (0.15, 0.08), (0.5, 0.5), (0.85, 0.9), (1, 0.97)),
                     _crv((0, 0), (0.25, 0.2), (0.6, 0.62), (1, 1)),
                     _crv((0, 0.02), (0.3, 0.3), (0.75, 0.72), (1, 0.95)),
                     _crv((0, 0.08), (0.3, 0.36), (0.7, 0.72), (1, 0.98)))),
    },
    # ── 2 ─────────────────────────────────────────────────────────────────────────────────────
    "teal-orange": {
        "title": "Teal & Orange",
        "about": "The blockbuster split: skin and warm light pushed to orange, everything cool pushed to teal, "
                 "greens folded into the teal, contrast strong.",
        "keywords": ["teal and orange", "blockbuster", "hollywood", "action", "complementary", "michael bay"],
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,0.04;30,0;60,-0.12;100,0.12;150,0.15;200,0;240,-0.08;300,0 "
                "mixer1=0,0.05;30,0.15;60,-0.1;120,-0.35;190,0.2;240,-0.1;300,-0.2 "
                "mixer2=0,0;30,0.08;120,-0.2;200,-0.1;300,0 "
                "grade1=30,6,0 saturation=-4 vibrance=10 clarity=6 grainAmount=6 grainSize=15 lensVignette=-12"
                % (_crv((0, 0), (0.2, 0.15), (0.5, 0.5), (0.8, 0.85), (1, 0.98)),
                   _crv((0, 0), (0.25, 0.21), (0.5, 0.52), (0.8, 0.85), (1, 1)),
                   _crv((0, 0.02), (0.5, 0.5), (1, 0.98)),
                   _crv((0, 0.08), (0.25, 0.31), (0.5, 0.49), (0.8, 0.78), (1, 0.95)))),
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,0.04;30,0;60,-0.1;100,0.12;150,0.15;200,0;240,-0.08;300,0 "
                  "mixer1=0,0.05;30,0.2;60,0;120,-0.3;190,0.25;300,-0.1 "
                  "mixer2=0,0;120,-0.2;200,-0.1;300,0 "
                  "saturation=-4 vibrance=10 clarity=8 grainAmount=8 grainSize=20 lensVignette=-16"
                  % (_crv((0, 0), (0.15, 0.09), (0.5, 0.5), (0.85, 0.9), (1, 0.97)),
                     _crv((0, 0), (0.25, 0.2), (0.5, 0.52), (0.8, 0.86), (1, 1)),
                     _crv((0, 0.01), (0.5, 0.5), (1, 0.98)),
                     _crv((0, 0.07), (0.25, 0.31), (0.5, 0.49), (0.8, 0.76), (1, 0.92)))),
    },
    # ── 3 ─────────────────────────────────────────────────────────────────────────────────────
    "wong-kar-wai": {
        "title": "Wong Kar-wai",
        "about": "Hong Kong heat — Chungking Express, In the Mood for Love: green-yellow mids, saturated red, "
                 "amber light, green-black shadows, heavy grain. Romantic, sweaty, nostalgic.",
        "keywords": ["wong kar-wai", "wong kar wai", "chungking", "in the mood for love", "hong kong", "romantic night",
                     "green tint", "90s"],
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,0;60,0.04;120,-0.05;200,-0.06;300,-0.05 "
                "mixer1=0,0.3;30,0.1;120,0.05;220,-0.2;300,0.1 "
                "mixer2=0,-0.1;220,-0.2 "
                "grade0=150,4,0 vibrance=8 clarity=6 grainAmount=16 grainSize=28 lensVignette=-15"
                % (_crv((0, 0.02), (0.2, 0.14), (0.5, 0.5), (0.85, 0.92), (1, 0.98)),
                   _crv((0, 0), (0.4, 0.38), (1, 1)),
                   _crv((0, 0.03), (0.4, 0.44), (0.8, 0.82), (1, 0.98)),
                   _crv((0, 0.02), (0.4, 0.36), (1, 0.88)))),
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,0;60,0.04;120,-0.05;200,-0.06;300,-0.05 "
                  "mixer1=0,0.35;30,0.15;120,0.1;220,-0.15;300,0.15 "
                  "mixer2=0,-0.05;220,-0.2 "
                  "grade0=150,4,0 vibrance=10 clarity=8 grainAmount=18 grainSize=30 lensVignette=-18"
                  % (_crv((0, 0), (0.15, 0.09), (0.5, 0.5), (0.85, 0.92), (1, 0.98)),
                     _crv((0, 0), (0.4, 0.39), (1, 1)),
                     _crv((0, 0.03), (0.4, 0.45), (0.8, 0.83), (1, 0.98)),
                     _crv((0, 0.01), (0.4, 0.35), (1, 0.86)))),
    },
    # ── 4 ─────────────────────────────────────────────────────────────────────────────────────
    "faded-film": {
        "title": "Faded Film",
        "about": "A print found in a drawer — Portra 400 left in the sun: lifted navy blacks, creamy warm "
                 "highlights, pastel greens and blues, soft. Nostalgic, quiet, travel diary.",
        "keywords": ["faded", "film", "vintage", "nostalgic", "portra", "matte", "analog", "memory", "diary", "retro"],
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,0;60,-0.05;120,-0.08;220,-0.06;300,0 "
                "mixer1=0,0;30,0.05;120,-0.3;220,-0.25;300,-0.2 "
                "mixer2=0,0;30,0.08;120,0.05;220,0.08;300,0 "
                "saturation=-12 grainAmount=10 grainSize=22 lensVignette=-8"
                % (_crv((0, 0.07), (0.25, 0.27), (0.5, 0.52), (0.75, 0.77), (1, 0.93)),
                   _crv((0, 0.02), (0.5, 0.52), (1, 1)),
                   _crv((0, 0.01), (0.5, 0.5), (1, 0.97)),
                   _crv((0, 0.06), (0.5, 0.47), (1, 0.88)))),
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,0;60,-0.05;120,-0.08;220,-0.06;300,0 "
                  "mixer1=0,0;30,0.05;120,-0.25;220,-0.2;300,-0.1 "
                  "saturation=-10 grainAmount=12 grainSize=25 lensVignette=-10"
                  % (_crv((0, 0.05), (0.2, 0.19), (0.5, 0.52), (0.8, 0.82), (1, 0.93)),
                     _crv((0, 0.02), (0.5, 0.52), (1, 1)),
                     _crv((0, 0.01), (0.5, 0.5), (1, 0.97)),
                     _crv((0, 0.06), (0.5, 0.47), (1, 0.88)))),
    },
    # ── 5 ─────────────────────────────────────────────────────────────────────────────────────
    "moody-forest": {
        "title": "Moody Forest",
        "about": "Dark and moody — Pacific Northwest / Nordic: dense mids, greens folded to deep olive-teal, "
                 "oranges to brown, cool shadows, nothing bright. Quiet, heavy, earthy.",
        "keywords": ["moody", "dark", "forest", "nordic", "earthy", "pnw", "dramatic", "brooding", "autumn"],
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,0;60,-0.06;100,0.08;140,0.12;220,0.03;300,0 "
                "mixer1=0,-0.1;30,-0.05;60,-0.35;110,-0.45;150,-0.3;220,-0.3;300,-0.2 "
                "mixer2=0,0;60,-0.15;110,-0.35;150,-0.25;220,-0.25;300,0 "
                "saturation=-10 clarity=10 dehaze=8 grainAmount=8 grainSize=18 lensVignette=-22"
                % (_crv((0, 0.04), (0.25, 0.18), (0.5, 0.42), (0.75, 0.7), (1, 0.92)),
                   _crv((0, 0), (0.5, 0.49), (1, 0.98)),
                   _crv((0, 0.02), (0.5, 0.5), (1, 0.97)),
                   _crv((0, 0.05), (0.4, 0.42), (1, 0.9)))),
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,0;60,-0.06;100,0.08;140,0.12;220,0.03;300,0 "
                  "mixer1=0,-0.1;60,-0.3;110,-0.4;220,-0.2;300,-0.2 "
                  "mixer2=0,0;110,-0.3;220,-0.2;300,0 "
                  "saturation=-12 clarity=10 grainAmount=10 grainSize=20 lensVignette=-22"
                  % (_crv((0, 0.02), (0.2, 0.12), (0.5, 0.45), (0.8, 0.78), (1, 0.93)),
                     _crv((0, 0), (0.5, 0.49), (1, 0.98)),
                     _crv((0, 0.02), (0.5, 0.5), (1, 0.97)),
                     _crv((0, 0.05), (0.4, 0.42), (1, 0.9)))),
    },
    # ── 6 ─────────────────────────────────────────────────────────────────────────────────────
    "spring-haze": {
        "title": "Spring Haze",
        "about": "Japanese airy pastel — sakura season, Hamaguchi daylight: bright and low-contrast, mint-cyan "
                 "shadows, pink-cream highlights, greens to fresh mint, pinks lifted. Light, tender, calm. At night "
                 "a pastel night: mint haze in the blacks, pink in the lights.",
        "keywords": ["airy", "pastel", "soft", "spring", "sakura", "cherry blossom", "japanese", "light", "dreamy",
                     "kawaii", "bright", "spring haze"],
        # Olive foliage (decoded RAW greens sit at 60-80 deg) is moved too, not only true green, or the
        # deer and the mountain stay olive; 40 deg is pinned at 0 so skin and wood do not turn green.
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,-0.03;40,0;60,0.05;80,0.1;130,0.08;220,-0.04;330,0 "
                "mixer1=0,0.05;40,0;60,-0.35;100,-0.25;140,-0.2;200,-0.1;330,0.35;350,0.2 "
                "mixer2=0,0;60,0.12;120,0.1;330,0.12 "
                "saturation=-10 vibrance=6 clarity=-8 grainAmount=4 grainSize=12"
                % (_crv((0, 0.08), (0.25, 0.32), (0.5, 0.58), (0.75, 0.82), (1, 0.97)),
                   _crv((0, 0), (0.5, 0.5), (0.85, 0.88), (1, 1)),
                   _crv((0, 0.02), (0.5, 0.51), (0.85, 0.86), (1, 0.985)),
                   _crv((0, 0.05), (0.5, 0.52), (0.85, 0.84), (1, 0.95)))),
        # A pastel NIGHT, so the set stays one mood: the blacks get a mint haze (G and B toes), the lights
        # go pink (R's shoulder above G and B), sodium orange is quieted, magenta-pink lifted.
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,-0.03;40,0;60,0.04;90,0.1;130,0.08;330,0 "
                  "mixer1=0,0.05;30,-0.1;60,-0.25;120,-0.2;330,0.3 "
                  "mixer2=0,0;330,0.1 "
                  "saturation=-10 vibrance=4 clarity=-6 grainAmount=6 grainSize=15"
                  % (_crv((0, 0.06), (0.2, 0.21), (0.5, 0.55), (0.8, 0.84), (1, 0.97)),
                     _crv((0, 0), (0.5, 0.5), (0.85, 0.9), (1, 1)),
                     _crv((0, 0.03), (0.5, 0.51), (0.85, 0.86), (1, 0.98)),
                     _crv((0, 0.07), (0.5, 0.53), (0.85, 0.85), (1, 0.96)))),
    },
    # ── 7 ─────────────────────────────────────────────────────────────────────────────────────
    "golden-dream": {
        "title": "Golden Dream",
        "about": "Every hour is golden hour: amber light through the whole frame, orange mids, greens turned "
                 "to gold, blues pushed back to a soft teal. Warm, happy, summer memory.",
        "keywords": ["golden", "warm", "sunset", "amber", "summer", "glow", "honey", "cozy"],
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,0;60,-0.06;100,-0.12;220,-0.05;300,0 "
                "mixer1=0,0;30,0.15;60,0.1;100,-0.15;220,-0.3;300,-0.1 "
                "mixer2=0,0;30,0.06;220,-0.2;300,0 "
                "temp=7100 vibrance=6 grainAmount=8 grainSize=18 lensVignette=-15"
                % (_crv((0, 0.03), (0.25, 0.24), (0.5, 0.53), (0.8, 0.86), (1, 0.97)),
                   _crv((0, 0.01), (0.5, 0.55), (1, 1)),
                   _crv((0, 0.01), (0.5, 0.51), (1, 0.96)),
                   _crv((0, 0.04), (0.5, 0.44), (1, 0.85)))),
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,0;60,-0.06;100,-0.1;220,-0.05;300,0 "
                  "mixer1=0,0;30,0.2;60,0.1;220,-0.2;300,-0.1 "
                  "temp=6900 vibrance=6 grainAmount=10 grainSize=20 lensVignette=-16"
                  % (_crv((0, 0.01), (0.2, 0.15), (0.5, 0.53), (0.85, 0.9), (1, 0.97)),
                     _crv((0, 0.01), (0.5, 0.55), (1, 1)),
                     _crv((0, 0.01), (0.5, 0.51), (1, 0.96)),
                     _crv((0, 0.04), (0.5, 0.44), (1, 0.85)))),
    },
    # ── 8 ─────────────────────────────────────────────────────────────────────────────────────
    "blue-melancholy": {
        "title": "Blue Melancholy",
        "about": "Kieslowski's Blue / a winter evening: cold, nearly monochrome except the blues, navy lifted "
                 "blacks, pale cold highlights, skin kept but quiet. Lonely, still, sad.",
        "keywords": ["blue", "cold", "melancholy", "sad", "lonely", "winter", "cool", "blue hour", "rainy", "kieslowski"],
        "day": ("curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,0;30,0.02;120,0.15;300,0 "
                "mixer1=0,-0.3;30,-0.25;60,-0.4;120,-0.4;210,0.1;300,-0.3 "
                "mixer2=0,0;210,-0.1;300,0 "
                "temp=6000 saturation=-15 grainAmount=10 grainSize=20 lensVignette=-15"
                % (_crv((0, 0.04), (0.25, 0.22), (0.5, 0.47), (0.75, 0.74), (1, 0.94)),
                   _crv((0, 0), (0.5, 0.46), (1, 0.95)),
                   _crv((0, 0.02), (0.5, 0.49), (1, 0.97)),
                   _crv((0, 0.08), (0.5, 0.55), (1, 1)))),
        "night": ("curve=%s curveR=%s curveG=%s curveB=%s "
                  "mixer0=0,0;30,0.02;120,0.15;300,0 "
                  "mixer1=0,-0.25;30,-0.2;60,-0.35;120,-0.35;210,0.15;300,-0.2 "
                  "temp=6100 saturation=-12 grainAmount=12 grainSize=22 lensVignette=-18"
                  % (_crv((0, 0.03), (0.2, 0.15), (0.5, 0.48), (0.8, 0.8), (1, 0.94)),
                     _crv((0, 0), (0.5, 0.46), (1, 0.95)),
                     _crv((0, 0.02), (0.5, 0.49), (1, 0.97)),
                     _crv((0, 0.08), (0.5, 0.55), (1, 1)))),
    },
    # ── 9 ─────────────────────────────────────────────────────────────────────────────────────
    "mono-noir": {
        "title": "Mono Noir",
        "about": "Black and white with a film's toe — Moriyama's Tokyo, Kurosawa: colour removed, each hue's "
                 "brightness chosen (skin up, sky and greens down), hard contrast, heavy grain.",
        "keywords": ["black and white", "b&w", "bw", "monochrome", "mono", "noir", "moriyama", "grainy", "street"],
        "day": ("curve=%s mixer2=0,0.1;30,0.15;60,0.05;120,-0.25;220,-0.45;300,-0.1 "
                "saturation=-100 clarity=14 grainAmount=18 grainSize=26 lensVignette=-20"
                % _crv((0, 0.01), (0.2, 0.12), (0.5, 0.5), (0.8, 0.88), (1, 0.99))),
        "night": ("curve=%s mixer2=0,0.1;30,0.15;60,0.05;120,-0.2;220,-0.35;300,0 "
                  "saturation=-100 clarity=16 grainAmount=20 grainSize=28 lensVignette=-22"
                  % _crv((0, 0), (0.15, 0.07), (0.5, 0.52), (0.85, 0.92), (1, 0.99))),
    },
    # ── 10 ── learned in the Grade Lab, 2026-10-30 (taste.md): the approver's winner was the first cinematic
    # grade — "keep original color of object well", "good contrast on human and background", "balance with a
    # story" — and every one-colour mood lost on a photo with people ("fall for one color … not natural").
    # So: natural object colours (no strong channel crossover), DEPTH (deeper toe, rolled shoulder, sky
    # density, vignette), subtle split (wheels, not curves), a slight WARM bias (the one refine pick: warmer).
    "natural-cinema": {
        "title": "Natural Cinema",
        "about": "The approver's own taste, from the Grade Lab: true object colours, real depth (deep blacks, "
                 "a soft film shoulder, a dense sky, subject against background), a whisper of cool shadow and "
                 "warm light, slightly warm overall. Balanced, with a story — never one colour over everything.",
        "keywords": ["natural", "natural cinema", "my taste", "balanced", "true color", "depth", "cinematic natural",
                     "default"],
        "day": ("curve=%s grade0=210,4,0 grade1=35,4,0 grade2=45,8,0 balance=-10 temp=6800 "
                "mixer0=0,0;60,0;105,-0.02;160,0.03;215,-0.05;300,0 "
                "mixer1=0,0;30,0.04;100,-0.18;150,-0.15;210,0;300,0 "
                "mixer2=0,0;30,0.04;110,-0.08;220,-0.18;300,0 "
                "saturation=-6 vibrance=8 clarity=10 dehaze=5 grainAmount=5 grainSize=14 lensVignette=-14 sharpenAmount=25"
                % _crv((0, 0), (0.15, 0.11), (0.5, 0.5), (0.82, 0.87), (1, 0.975))),
        # Night: the first version opened the mids (0.5 -> 0.54), warmed the light (+200 K, warm highlight
        # wheel) and bent yellow (55 deg -0.03) — on the teamLab fire that clipped red first and turned the
        # flames LIME ("#3 make it so bad on highlight … make sure this mistake doesn't happen in skill").
        # So at night: no mid lift, no warming, no hue move in the 40-70 deg band; brightness comes from the
        # correction (dark-interior median 0.15), which the clipping guard can hold.
        "night": ("curve=%s curveR=%s curveB=%s "
                  "mixer0=0,0;120,0.06;300,0 mixer1=0,0;30,0.05;120,-0.1;300,0.05 "
                  "saturation=-2 vibrance=6 clarity=8 grainAmount=8 grainSize=18 lensVignette=-12"
                  % (_crv((0, 0), (0.12, 0.08), (0.5, 0.5), (0.85, 0.9), (1, 0.975)),
                     _crv((0, 0), (0.3, 0.29), (1, 1)),
                     _crv((0, 0.02), (0.3, 0.31), (1, 0.98)))),
    },
    # ── 11 ── the landscape variant: where nothing has a "true" colour to keep (Fuji, the pond), the approver
    # chose Neon Noir's story — teal foliage, a deep cyan sky, hard depth. Natural Cinema's balance with that.
    "natural-teal": {
        "title": "Natural Teal",
        "about": "Natural Cinema for landscapes and nature: foliage turned toward teal, a deep sky, more depth — "
                 "the story the approver liked in Neon Noir on Fuji and the pond, kept balanced, skin and wood untouched.",
        "keywords": ["landscape", "nature", "teal foliage", "natural teal", "deep sky", "travel landscape"],
        "day": ("curve=%s grade0=200,5,0 grade1=35,4,0 grade2=40,8,0 balance=-10 temp=6700 "
                "mixer0=0,0;40,0;60,-0.04;100,0.1;140,0.14;200,0.02;230,-0.04;300,0 "
                "mixer1=0,0;30,0.05;60,-0.15;110,-0.2;160,0.05;200,0.1;300,0 "
                "mixer2=0,0;30,0.04;110,-0.18;160,-0.1;220,-0.25;300,0 "
                "saturation=-4 vibrance=10 clarity=12 dehaze=8 grainAmount=5 grainSize=14 lensVignette=-16 sharpenAmount=25"
                % _crv((0, 0), (0.15, 0.1), (0.5, 0.49), (0.82, 0.87), (1, 0.975))),
        "night": ("curve=%s curveR=%s curveB=%s grade2=30,10,0 "
                  "mixer0=0,0;55,-0.04;120,0.14;300,0 mixer1=0,0;120,-0.15;190,0.15;300,0.05 mixer2=0,0;120,-0.2;220,-0.15;300,0 "
                  "saturation=-2 vibrance=8 clarity=10 grainAmount=8 grainSize=18 lensVignette=-14"
                  % (_crv((0, 0), (0.12, 0.08), (0.5, 0.53), (0.85, 0.9), (1, 0.975)),
                     _crv((0, 0), (0.3, 0.28), (1, 1)),
                     _crv((0, 0.03), (0.3, 0.32), (1, 0.98)))),
    },
}


# ── strength: "softer", "stronger", 0.5 of a mood ─────────────────────────────────────────────

_WHEEL = re.compile(r"^grade[012]$")


def scale(params, s):
    """`params` at strength `s` (0 = neutral, 1 = as written, >1 exaggerated): curve points move toward
    the identity line, mixer y and wheel sat/lum scale, scalars scale (temp around 6500). Hue angles do
    NOT scale — a wheel at half strength is the same hue, weaker."""
    if abs(s - 1.0) < 1e-6:
        return params
    out = []
    for tok in params.split():
        if "=" not in tok:
            out.append(tok)
            continue
        k, v = tok.split("=", 1)
        if k in ("curve", "curveR", "curveG", "curveB"):
            pts = []
            for p in v.split(";"):
                x, y = (float(a) for a in p.split(","))
                pts.append("%g,%g" % (x, min(1.0, max(0.0, x + s * (y - x)))))
            out.append("%s=%s" % (k, ";".join(pts)))
        elif k in ("mixer0", "mixer1", "mixer2") and v:
            pts = []
            for p in v.split(";"):
                h, y = (float(a) for a in p.split(","))
                pts.append("%g,%g" % (h, max(-1.0, min(1.0, s * y))))
            out.append("%s=%s" % (k, ";".join(pts)))
        elif _WHEEL.match(k):
            h, sat, lum = (float(a) for a in v.split(","))
            out.append("%s=%g,%g,%g" % (k, h, s * sat, s * lum))
        elif k == "temp":
            out.append("temp=%g" % (6500 + s * (float(v) - 6500)))
        elif k in ("sharpenRadius", "mixerSpread", "curveLog", "remapEnable", "remapSrc", "remapDst", "remapRange"):
            out.append(tok)
        else:
            try:
                out.append("%s=%g" % (k, s * float(v)))
            except ValueError:
                out.append(tok)
    return " ".join(out)


def find(text):
    """The mood a prompt names, by key, title or keyword; None when none matches."""
    t = text.lower()
    for key, m in MOODS.items():
        if key in t or m["title"].lower() in t:
            return key
    best = None
    for key, m in MOODS.items():
        for kw in m["keywords"]:
            if kw in t and (best is None or len(kw) > best[1]):
                best = (key, len(kw))
    return best[0] if best else None
