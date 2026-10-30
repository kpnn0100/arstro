"""looks.py — the grader's numbers: per-scene targets and the looks, in Cosmo's own `set` keys.

Every value here was chosen by MEASURING its effect through `cosmo_analyze` and LOOKING at the
render (references/cosmo-controls.md has the calibration). Cosmo's shadow wheel, `shadows`,
`blacks` and `contrast` are far stronger than their Lightroom namesakes — keep them small.
"""


# What "normalised" means per scene, BEFORE the look: the cast the light should have (wb), the
# median display luminance the scene lives at (p50), how much may clip, the ceiling on exposure,
# and after the look the window the median must stay in (final) and the highest 1st-percentile
# luminance a black may have (blackMax). Night is meant to be dark; a white sky is meant to be white.
TARGETS = {
    # Daylight is judged against a slightly WARM, slightly GREEN neutral (warmth +0.10, tint +0.04):
    # a mathematically grey overcast sky reads lilac next to warm wood and skin (measured on the
    # Asakusa pagoda, 2026-10-30).
    "daylight":      dict(wb="full", warmth=0.12, tint=0.04, p50=0.40, final=(0.33, 0.85), clip=0.003,
                          maxEv=1.0, blackMax=0.06, openShadows=True, floor=True, tempRange=(5000, 8000), tintRange=(-10, 45)),
    "overcast-day":  dict(wb="full", warmth=0.10, tint=0.04, p50=0.43, final=(0.36, 0.85), clip=0.004,
                          maxEv=1.0, blackMax=0.06, openShadows=True, floor=True, tempRange=(5000, 8000), tintRange=(-10, 45)),
    "golden-hour":   dict(wb="tint-only", warmth=0.0, tint=0.04, p50=0.40, final=(0.34, 0.85), clip=0.004,
                          maxEv=1.0, blackMax=0.05, openShadows=True, floor=True, tempRange=(6500, 6500), tintRange=(-10, 30)),
    # Night: the light sources ARE the subject. Neutralise only the gross cast of the street light,
    # never the neon's own colour, and keep the frame dark — a night photo lifted to mid-grey is a
    # day photo with noise.
    "night-city":    dict(wb="full", warmth=0.05, tint=0.04, p50=0.12, final=(0.09, 0.16), clip=0.01,
                          maxEv=2.5, blackMax=0.03, openShadows=False, floor=False, tempRange=(5500, 7500), tintRange=(-15, 30)),
    # Art installations (teamLab), concerts, bars: the coloured light is the author's — remove a
    # decoder's green only. Median 0.15, not 0.10: in the Grade Lab every teamLab version was "too dark".
    "dark-interior": dict(wb="tint-only", warmth=0.0, tint=0.03, p50=0.15, final=(0.11, 0.19), clip=0.008,
                          maxEv=2.5, blackMax=0.025, openShadows=False, floor=False, tempRange=(6500, 6500), tintRange=(-25, 25)),
    # Aquarium: the blue IS the water. No white balance at all.
    "aquarium":      dict(wb=None, warmth=0.0, tint=0.0, p50=0.19, final=(0.15, 0.23), clip=0.004,
                          maxEv=1.5, blackMax=0.04, openShadows=False, floor=False, tempRange=(6500, 6500), tintRange=(0, 0)),
}


# Which look group a scene belongs to.
FAMILY = {"daylight": "day", "overcast-day": "day", "golden-hour": "day",
          "night-city": "night", "dark-interior": "night", "aquarium": "night"}


def noise_for_iso(iso):
    """Noise reduction by ISO. Luminance NR kept LOW: at ISO 1600 the first setting (22) smeared the teamLab
    face ("why the man's face cover in blur") — grain reads as film, smear reads as plastic. Colour NR
    stays, colour speckle is never wanted."""
    if iso >= 3200:
        return {"nrLuminance": 16.0, "nrColor": 40.0}
    if iso >= 1600:
        return {"nrLuminance": 10.0, "nrColor": 35.0}
    if iso >= 800:
        return {"nrLuminance": 5.0, "nrColor": 25.0}
    return {"nrLuminance": 0.0, "nrColor": 15.0}


# A look = one group per scene family (the shared decision) + small per-scene extras on the photo.
LOOKS = {
    # "Cinematic": a film print look in the Kodak 2383 / Fuji Eterna family. A soft-shouldered S
    # curve; cool, nearly neutral shadows against warm skin-coloured mids and highlights (the
    # complementary split every feature grade uses, kept subtle); greens pulled toward olive and
    # down in saturation, blues toward teal and darker; grain and a vignette to give the frame a
    # surface. Night is the same idea darker and more saturated: neon greens bent to teal, yellow
    # sodium light to amber, magenta kept — the Tokyo-at-night palette of Blade Runner 2049 and
    # Lost in Translation, without its haze.
    "cinematic": {
        "groups": {
            "day": {
                "name": "Cinematic - Day",
                "params": "curve=0,0.012;0.22,0.165;0.72,0.80;1,0.975 grade0=205,4,0 grade1=35,5,0 "
                          "grade2=45,10,0 balance=-10 saturation=-10 vibrance=10 clarity=6 dehaze=4 "
                          "grainAmount=6 grainSize=15 lensVignette=-8 sharpenAmount=25 "
                          "mixer0=0,0;60,0;105,-0.03;160,0.02;215,-0.05;300,0 "
                          "mixer1=0,0;30,0.06;100,-0.3;150,-0.25;210,-0.06;300,0 "
                          "mixer2=0,0;30,0.04;110,-0.06;220,-0.15;300,0",
            },
            "night": {
                "name": "Cinematic - Night",
                # The split is done with the R/G/B curves, not the shadow wheel: the wheel ADDS colour in
                # linear light and so lifts a night's blacks to milk; the curves bend the channels
                # apart and leave the black point where it was (measured: shadow warmth +1.43 ->
                # -0.16 for a p5 rise of 0.010).
                "params": "curve=0,0;0.12,0.07;0.5,0.53;0.85,0.9;1,0.975 "
                          "curveR=0,0;0.3,0.26;0.7,0.72;1,1 curveG=0,0.01;0.3,0.31;1,0.99 "
                          "curveB=0,0.04;0.3,0.34;0.75,0.72;1,0.95 grade2=28,12,0 "
                          "saturation=-4 vibrance=6 clarity=10 grainAmount=10 grainSize=22 "
                          "lensVignette=-14 mixer0=0,0;55,-0.04;120,0.1;200,0;300,0 "
                          "mixer1=0,0;30,0.06;120,-0.15;300,0.05 mixer2=0,0;220,-0.12;300,0",
            },
        },
        "scene": {
            # NEVER a colour wheel here: wheel hues ADD across group levels (28 + 35 = 63 deg, yellow,
            # not "more amber") — see references/cosmo-controls.md. Wheels live on the group only.
            "aquarium": "vibrance=6",
            "overcast-day": "dehaze=6",
        },
    },
}


# The moods (moods.py, one write-up each in moods/) are looks too: `autograde.py grade --look neon-noir`.
from moods import MOODS as _MOODS  # noqa: E402

for _k, _m in _MOODS.items():
    LOOKS[_k] = {"groups": {"day": {"name": _m["title"] + " - Day", "params": _m["day"]},
                            "night": {"name": _m["title"] + " - Night", "params": _m["night"]}},
                 "scene": {}}

