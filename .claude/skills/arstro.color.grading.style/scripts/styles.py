"""styles.py — what a user MEANS when they ask for a look, in their own words, and the edit that answers it.

The approver (2026-11-05): *"now i need to teach you with multiple style and wording since user will input a lot,
you need to understand what they mean — generate a list of edit that represent for a style, a desire with photo, i
will judge that — each style just give me 3 photo, i will pick the best suitable for the style"*.

A STYLE is a desire a user can put in many words ("warm", "cozy", "ấm áp", "make it feel like a summer evening").
Each has three CANDIDATES — three honest readings of the word — rendered on a photo that suits it; the approver
picks the one that IS the word (Grade Lab `/styles`, verdicts in data/style-verdicts.json). `recipe(style)` is the
picked candidate, or candidate A (the grader's first reading) until the approver has judged it.

A candidate is applied per photo exactly like the taste (arstro.color.grading edit.py): the photo is CORRECTED
first (white balance, exposure, highlights), then

    look      a mood of arstro.color.grading/scripts/moods.py (its day or night variant), "taste" (the approver's
              look for THIS photo, taste.choose) or "corrected" (none)
    strength  the look's COLOUR (its depth is never scaled — taste.md)
    tweaks    the shared refine steps (taste.TWEAKS): warmer, cooler, brighter, darker, contrast, soft, vivid, muted,
              faded, deep, magenta, green
    set       Cosmo `set` keys on top: `k=v` replaces, `k+=v` / `k-=v` adjusts the correction (exposure, temp ...)
    night     a `set` used INSTEAD of `set` on a night photo (when absent, a night photo gets `set` without its
              exposure / shadows lifts — night is meant to be dark, taste.md)
    depth     True: the cinematic floor (edit.depth: deep blacks, bright highlights). False where lifted blacks ARE
              the style the user asked for (faded, airy, dreamy, high key) — the clipping guard still runs.

Wordings are English and Vietnamese (with and without diacritics — users type both). The matcher is a baseline
for scripts and small local models; an agent READS the prompt (SKILL.md of arstro.color.grading.style).
"""
import json
import re
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
VERDICTS = HERE.parent / "data" / "style-verdicts.json"


def _crv(*pts):
    return ";".join("%g,%g" % p for p in pts)


S_CURVE = _crv((0, 0), (0.15, 0.105), (0.5, 0.5), (0.85, 0.895), (1, 0.98))


def C(label, look="corrected", strength=1.0, tweaks=(), set="", night=None, depth=True):
    return {"label": label, "look": look, "strength": strength, "tweaks": [list(t) for t in tweaks],
            "set": " ".join(set.split()), "night": None if night is None else " ".join(night.split()), "depth": depth}


STYLES = {
    # ── the default, and its opposite ────────────────────────────────────────────────────────────────────
    "cinematic": {
        "title": "Cinematic",
        "means": "looks like a frame from a film: full tonal range, deep blacks, colour that carries a mood, the "
                 "subject separated from its background",
        "words": ["cinematic", "cinema", "movie", "film look", "filmic", "like a movie", "like a film", "movie scene",
                  "film still", "như phim", "kiểu phim", "màu phim", "điện ảnh", "chất điện ảnh"],
        "modifier": [["contrast", 0.5]],
        "demo": "DSCF5186.RAF",
        "candidates": [
            C("the approver's taste — the look the Grade Lab chose for photos like this one", look="taste"),
            C("anamorphic blockbuster — teal & orange 70 %, heavy vignette, grain", look="teal-orange", strength=0.7,
              tweaks=[("contrast", 1)], set="lensVignette=-24 grainAmount=10 grainSize=20"),
            C("moody film print — natural colour, darker, muted, grain", look="natural-cinema",
              tweaks=[("darker", 1), ("muted", 0.5), ("contrast", 1)], set="grainAmount=12 grainSize=22 lensVignette=-20"),
        ],
    },
    "natural": {
        "title": "Natural",
        "means": "true to life: the colours as they were, only fixed (white balance, exposure) and given depth — no "
                 "visible look",
        "words": ["natural", "realistic", "true to life", "real colour", "real color", "accurate", "just fix",
                  "just correct", "basic correction", "clean edit", "as it was", "tự nhiên", "chân thật", "chỉnh cơ bản",
                  "cân bằng màu", "chỉ chỉnh sáng", "màu thật", "nguyên bản"],
        "modifier": [],
        "demo": "_DSF6592.RAF",
        "candidates": [
            C("corrected only — white balance, exposure, the cinematic depth", look="corrected"),
            C("natural with a whisper of film — Natural Cinema 40 %", look="natural-cinema", strength=0.4),
            C("natural, a touch richer — Natural Cinema 70 %, a little more colour", look="natural-cinema", strength=0.7,
              tweaks=[("vivid", 0.5)]),
        ],
    },
    # ── temperature ──────────────────────────────────────────────────────────────────────────────────────
    "warm": {
        "title": "Warm",
        "means": "warm and inviting: the light leans gold or orange, the frame feels cozy — not a filter of one colour",
        "words": ["warm", "warmer", "cozy", "cosy", "warm tone", "inviting", "sunny", "honey", "ấm", "ấm áp", "tông ấm",
                  "màu ấm", "ấm cúng"],
        "modifier": [["warmer", 1.5]],
        "demo": "_DSF5417.RAF",
        "candidates": [
            C("golden glow over everything — Golden Dream 70 %", look="golden-dream", strength=0.7),
            C("natural colour, warmer light — Natural Cinema, +800 K", look="natural-cinema", tweaks=[("warmer", 2)]),
            # (first board: with a cool toe the white sky split into cream and grey, and the sky check took the
            # warmth back out of it — "the sky in several colours"; the blacks now stay neutral)
            C("warm light — orange highlights and mids on the wheels, neutral blacks", set=(
                "curve=%s grade1=35,10,0 grade2=32,24,0 temp+=300 "
                "mixer1=0,0;30,0.12;60,0.05;95,0;220,-0.1;300,0 saturation=4 clarity=6 lensVignette=-12" % S_CURVE)),
        ],
    },
    "cool": {
        "title": "Cool",
        "means": "cool, calm and fresh: the light leans blue-cyan, the air feels clean — not sad, not frozen",
        "words": ["cool", "cooler", "cold tone", "cool tone", "fresh", "crisp", "icy", "chilly", "mát", "mát mẻ",
                  "tông lạnh", "màu lạnh", "xanh mát", "lạnh"],
        "modifier": [["cooler", 1.5]],
        "demo": "DSCF5411.RAF",
        "candidates": [
            C("natural teal, cooler light — Natural Teal, -600 K", look="natural-teal", tweaks=[("cooler", 1.5)]),
            C("cool blue mood — Blue Melancholy 60 %", look="blue-melancholy", strength=0.6),
            C("crisp cool teal — teal shadows and highlights, greens to teal, -500 K", set=(
                "curve=%s curveR=%s curveB=%s grade0=200,6,0 grade2=200,5,0 temp-=500 "
                "mixer0=0,0;60,0.06;120,0.1;200,0;300,0 mixer1=0,0;30,-0.05;60,-0.15;120,-0.05;200,0.1;300,0 "
                "saturation=-4 clarity=8" % (S_CURVE, _crv((0, 0), (0.3, 0.28), (1, 0.99)),
                                             _crv((0, 0.03), (0.3, 0.33), (1, 1))))),
        ],
    },
    # ── mood ─────────────────────────────────────────────────────────────────────────────────────────────
    "moody": {
        "title": "Dark & moody",
        "means": "dark, dramatic, atmospheric: dense mids, deep shadows, quieter colour, light that picks the subject "
                 "out of the dark",
        "words": ["moody", "dark", "darker mood", "dramatic", "brooding", "mysterious", "atmospheric", "intense",
                  "low key", "low-key", "gothic", "tối", "trầm", "u tối", "huyền bí", "kịch tính", "bí ẩn", "tông tối"],
        "modifier": [["darker", 1.5], ["contrast", 1]],
        # Style Lab 1 (2026-11-05): none fits — "can not dark and moody on this photo" (Nikko, sunlit cedars: a
        # bright day has nothing dark to be moody with). Round 2 on the overcast castle moat (dark water, stone) —
        # the melancholy photo too, so the two words can be told apart on one frame; the Moody Forest reading
        # (flat: toe lifted, p99 0.74) replaced by a dense, deep one.
        # Round 2 (2026-11-05): C picked, low key — moody is DARKNESS with the warm light kept, not a colour scheme
        # (the olive/slate and dark-teal readings lost).
        "demo": "_DSF6609.RAF",
        "candidates": [
            C("dark & moody — dense mids, deep shadows, greens to deep olive, blues to slate, the light warm-neutral",
              set=("exposure-=0.3 highlights-=20 curve=%s saturation=-10 grade0=195,6,0 grade2=30,4,0 "
                   "mixer1=0,0;30,0.05;60,-0.1;95,-0.25;120,-0.25;200,-0.2;300,0 "
                   "mixer2=0,0;60,0;95,-0.15;120,-0.15;200,-0.1;300,0 clarity=12 lensVignette=-25 "
                   "grainAmount=6 grainSize=18" % _crv((0, 0), (0.25, 0.17), (0.5, 0.42), (0.8, 0.78), (1, 0.97))),
              night=("curve=%s saturation=-10 grade0=195,6,0 mixer1=0,0;30,0.05;95,-0.2;120,-0.2;200,-0.15;300,0 "
                     "clarity=10 lensVignette=-25 grainAmount=8 grainSize=18"
                     % _crv((0, 0), (0.25, 0.2), (0.5, 0.46), (0.85, 0.88), (1, 0.97)))),
            C("dark teal — the approver's teal, darker, more contrast, a heavy vignette", look="natural-teal",
              tweaks=[("darker", 2), ("contrast", 1)], set="lensVignette=-28"),
            C("low key — exposure down, colour quiet except the warm light, the corners black", set=(
                "exposure-=0.45 curve=%s saturation=-18 "
                "mixer1=0,0;30,0.15;60,0.05;95,0;120,-0.2;220,-0.25;300,0 "
                "mixer2=0,0;30,0.05;60,0;95,0;120,-0.15;220,-0.2;300,0 "
                "clarity=14 lensVignette=-35 grainAmount=8 grainSize=18"
                % _crv((0, 0), (0.2, 0.1), (0.5, 0.43), (0.8, 0.82), (1, 0.97))),
              night=("curve=%s saturation=-15 mixer1=0,0;30,0.1;95,0;120,-0.2;220,-0.2;300,0 clarity=12 "
                     "lensVignette=-30 grainAmount=10 grainSize=20" % _crv((0, 0), (0.2, 0.1), (0.5, 0.45), (0.85, 0.9), (1, 0.97)))),
        ],
    },
    "bright-airy": {
        "title": "Bright & airy",
        "means": "light, open and clean: bright mids, soft shadows, gentle colour, a lot of air",
        "words": ["bright", "airy", "light and airy", "bright and airy", "clean bright", "high key", "high-key",
                  "open", "luminous", "sáng", "trong trẻo", "trong sáng", "tươi sáng", "thoáng", "nhẹ nhàng sáng"],
        "modifier": [["brighter", 1.5], ["soft", 0.5]],
        "demo": "_DSF6686.RAF",
        "candidates": [
            C("airy pastel — Spring Haze 80 % (mint shadows, cream light)", look="spring-haze", strength=0.8, depth=False),
            C("clean and bright, natural colour — brighter, softer contrast", look="natural-cinema", strength=0.6,
              tweaks=[("brighter", 2), ("soft", 1)], set="exposure+=0.2 highlights-=20", depth=False),
            C("high key — bright, shadows open, colour gentle", set=(
                "exposure+=0.35 highlights-=35 shadows+=12 curve=%s saturation=-6 vibrance=6 clarity=-4"
                % _crv((0, 0.03), (0.25, 0.3), (0.5, 0.6), (0.8, 0.88), (1, 0.98))), depth=False),
        ],
    },
    "dreamy": {
        "title": "Dreamy & soft",
        "means": "soft, hazy, romantic: a glow in the light, pastel colour, edges that melt",
        "words": ["dreamy", "dream", "soft", "ethereal", "romantic", "fairy tale", "fairytale", "hazy", "glow",
                  "glowy", "pastel", "whimsical", "mộng mơ", "mơ màng", "lãng mạn", "dịu", "dịu dàng", "pastel",
                  "huyền ảo"],
        "modifier": [["soft", 1], ["faded", 0.5]],
        # Style Lab 1 (2026-11-05): none fits — "it just low contrast, lost the depth of the photo, dreamy soft got
        # depth too". All three lifted the blacks (depth off). Dreamy is SOFT LIGHT on a photo that keeps its
        # blacks: local contrast down (clarity, texture), highlights rolled and tinted, the black point untouched,
        # and the cinematic depth floor ON.
        # Round 2 (2026-11-05): B picked, romantic glow — the softness AND a pink-cream tint in the light; Spring Haze
        # with depth and plain soft light both lost.
        "demo": "DSCF5288.RAF",
        "candidates": [
            C("dreamy with depth — Spring Haze 80 %, blacks kept deep, local contrast down (a soft glow)",
              look="spring-haze", strength=0.8, set="clarity=-18 texture=-8"),
            # no mixer: on the first render a pink-only mixer (sat or lum, any anchoring) turned the gold
            # ornaments lime (hue 45 -> 55-58) and the sakura cream; a flat mixer changes nothing. The pink comes
            # from the highlight wheel and vibrance instead.
            C("romantic glow — pink-cream light, deep shadows, edges soft", set=(
                "exposure+=0.1 highlights-=20 curve=%s grade0=190,4,0 grade2=345,10,0 "
                "saturation=-4 vibrance=8 clarity=-24 texture=-10"
                % _crv((0, 0), (0.2, 0.17), (0.5, 0.54), (0.85, 0.88), (1, 0.97)))),
            C("soft light, natural colour — Natural Cinema 60 %, a step brighter, local contrast down, highlights "
              "rolled", look="natural-cinema", strength=0.6, tweaks=[("brighter", 1)],
              set="clarity=-22 texture=-10 highlights-=15 vibrance=4"),
        ],
    },
    "melancholy": {
        "title": "Melancholy",
        "means": "sad, lonely, still: cold and quiet colour, little saturation, a grey or blue air",
        "words": ["sad", "melancholy", "melancholic", "lonely", "loneliness", "gloomy", "somber", "sombre", "quiet",
                  "rainy day", "blue mood", "buồn", "u buồn", "cô đơn", "trầm buồn", "lạnh lẽo", "tâm trạng", "chill buồn"],
        "modifier": [["cooler", 1], ["muted", 1.5]],
        # Style Lab 1 (2026-11-05): Blue Melancholy picked — "but too much blue it lost the natural color". Round 2:
        # the same reading with the natural colour kept, at two strengths, and a quiet one with no blue look at all.
        # Round 2 (2026-11-05): B picked, Blue Melancholy 35 % muted — sad is LESS colour, a little blue; cool (the same
        # look at 60 %, not muted) is MORE blue. The two words stay apart.
        "demo": "_DSF6609.RAF",
        "candidates": [
            C("blue melancholy, natural colour kept — Blue Melancholy 50 %", look="blue-melancholy", strength=0.5),
            C("blue melancholy, quieter — Blue Melancholy 35 %, muted", look="blue-melancholy", strength=0.35,
              tweaks=[("muted", 1)]),
            C("quiet natural — natural colour, a step cooler, muted, a little darker; no blue look",
              look="natural-cinema", strength=0.7, tweaks=[("cooler", 1), ("muted", 1.5), ("darker", 0.5)]),
        ],
    },
    # ── film and era ─────────────────────────────────────────────────────────────────────────────────────
    "vintage-film": {
        "title": "Vintage film",
        "means": "an old print or a film camera: faded or creamy blacks, film colour, grain — nostalgic",
        "words": ["vintage", "retro", "film", "analog", "analogue", "old photo", "old film", "70s", "80s", "90s",
                  "kodak", "portra", "fuji film", "fujifilm", "disposable", "nostalgic", "nostalgia", "film camera",
                  "hoài cổ", "cổ điển", "phim cũ", "ảnh cũ", "màu film", "film máy", "retro"],
        "modifier": [["faded", 1], ["muted", 0.5]],
        "demo": "_DSF5417.RAF",
        "candidates": [
            C("faded print — Portra left in the sun: navy lifted blacks, cream highlights", look="faded-film", depth=False),
            C("Kodak Gold — warm yellow light, rich reds, olive greens, grain", set=(
                "curve=%s curveR=%s curveG=%s curveB=%s temp+=300 "
                "mixer0=0,0;60,-0.03;120,-0.08;220,-0.04;300,0 mixer1=0,0.12;30,0.1;60,0.1;120,-0.2;220,-0.15;300,0 "
                "saturation=4 clarity=-4 grainAmount=18 grainSize=26 lensVignette=-14"
                % (_crv((0, 0.04), (0.25, 0.25), (0.5, 0.53), (0.8, 0.85), (1, 0.96)),
                   _crv((0, 0.01), (0.5, 0.53), (1, 1)), _crv((0, 0.01), (0.5, 0.51), (1, 0.98)),
                   _crv((0, 0.03), (0.5, 0.45), (1, 0.88)))), depth=False),
            C("90s drugstore film — green-cyan shadows, magenta-red, heavy grain", set=(
                "curve=%s curveR=%s curveG=%s curveB=%s mixer0=0,0.02;120,0.05;220,-0.03;300,0 "
                "mixer1=0,0.15;60,0;120,-0.1;220,-0.1;300,0 saturation=-4 grainAmount=24 grainSize=30 lensVignette=-20"
                % (_crv((0, 0.03), (0.2, 0.17), (0.5, 0.5), (0.85, 0.9), (1, 0.97)),
                   _crv((0, 0), (0.3, 0.28), (0.7, 0.72), (1, 1)), _crv((0, 0.04), (0.3, 0.32), (0.7, 0.71), (1, 0.97)),
                   _crv((0, 0.05), (0.3, 0.31), (0.7, 0.68), (1, 0.93)))), depth=False),
        ],
    },
    "teal-orange": {
        "title": "Teal & orange",
        "means": "the blockbuster split: warm skin and light against teal shadows and backgrounds",
        "words": ["teal and orange", "orange and teal", "teal orange", "teal & orange", "blockbuster", "hollywood",
                  "action movie", "michael bay", "complementary", "xanh cam", "cam xanh", "tông xanh cam"],
        "modifier": [],
        "demo": "_DSF6332.RAF",
        "candidates": [
            C("teal & orange — the full split", look="teal-orange"),
            C("teal & orange 60 % — the split, quieter", look="teal-orange", strength=0.6),
            C("natural colour, teal shadows and orange light on the wheels only", look="natural-cinema", strength=0.8,
              set="grade0=195,10,0 grade2=32,18,0"),
        ],
    },
    "neon": {
        "title": "Neon / cyberpunk",
        "means": "night city at its most electric: cyan and magenta light, teal-black shadows, wet contrast",
        "words": ["neon", "cyberpunk", "cyber punk", "blade runner", "futuristic", "synthwave", "night city",
                  "tokyo night", "sci-fi", "đèn neon", "cyberpunk", "tương lai", "phố đêm"],
        "modifier": [["magenta", 0.5], ["contrast", 0.5]],
        "demo": "DSCF5194.RAF",
        "candidates": [
            C("neon noir — teal-black shadows, magenta and cyan light", look="neon-noir"),
            C("cyberpunk pink-cyan — yellows bent to pink, greens to cyan", look="neon-noir", set=(
                "mixer0=0,-0.06;30,-0.1;60,-0.12;120,0.25;180,0.03;240,0.04;300,0 "
                "mixer1=0,0.25;30,-0.1;60,-0.2;120,-0.3;190,0.3;240,0.1;300,0.4 tint+=10")),
            C("neon noir deeper — stronger colour, harder blacks", look="neon-noir", strength=1.3,
              tweaks=[("contrast", 1), ("deep", 1)]),
        ],
    },
    "golden-hour": {
        "title": "Golden hour",
        "means": "the low sun: orange light, long warm glow, soft haze, shadows that stay a little cool",
        "words": ["golden hour", "sunset", "sunrise", "evening sun", "magic hour", "late afternoon", "sunlit",
                  "hoàng hôn", "nắng chiều", "bình minh", "giờ vàng", "nắng vàng", "chiều tà"],
        "modifier": [["warmer", 2]],
        "demo": "_DSF6904.RAF",
        "candidates": [
            C("golden dream — amber through the whole frame", look="golden-dream"),
            C("low sun glow — orange light, a warm haze, cool toe", set=(
                "curve=%s grade0=220,5,0 grade1=35,10,0 grade2=30,30,0 temp+=500 dehaze=-6 highlights-=15 "
                "mixer1=0,0.05;30,0.2;60,0.05;95,0;220,-0.1;300,0 lensVignette=-16"
                % _crv((0, 0.02), (0.2, 0.17), (0.5, 0.53), (0.85, 0.9), (1, 0.98)))),
            C("sunset — orange light, violet shadows", set=(
                "curve=%s curveR=%s curveB=%s grade0=275,8,0 grade2=28,26,0 temp+=300 "
                "mixer0=0,0;60,-0.06;220,0.04;300,0 mixer1=0,0;30,0.2;95,0;220,0.1;300,0.15 lensVignette=-18"
                % (S_CURVE, _crv((0, 0.01), (0.5, 0.52), (1, 1)), _crv((0, 0.04), (0.35, 0.34), (0.8, 0.75), (1, 0.9))))),
        ],
    },
    # ── colour amount ────────────────────────────────────────────────────────────────────────────────────
    "vibrant": {
        "title": "Vibrant",
        "means": "colour that pops: rich, saturated, punchy, clear — still believable",
        "words": ["vibrant", "vivid", "colorful", "colourful", "punchy", "pop", "poppy", "saturated", "bold colour",
                  "bold color", "rich colour", "rich color", "hdr", "rực rỡ", "tươi", "màu tươi", "nổi màu", "đậm màu",
                  "rực", "sặc sỡ"],
        "modifier": [["vivid", 1.5]],
        "demo": "_DSF6592.RAF",
        "candidates": [
            C("rich natural — the approver's teal at 130 %, more saturated", look="natural-teal", strength=1.3,
              tweaks=[("vivid", 1.5), ("contrast", 0.5)]),
            # First board: a yellow boost (60 deg) and vibrance turned the white sky through the branches MINT, and
            # `shadows +14` lifted the blacks to p1 0.24 (Cosmo's shadows is several times Lightroom's). The colour now
            # rises only outside a white sky's hues (30-95 deg after white balance), with no vibrance, and the HDR
            # reading opens its shadows with a touch of `shadows` and its clarity, not a lift. (Its `highlights-=40` on
            # a correction already at -80 went to -120 and flattened it: relative steps add to the correction's.)
            C("punchy pop — reds, greens and blues richer, clear and crisp", set=(
                "curve=%s saturation=8 mixer1=0,0.15;20,0.1;30,0;95,0;120,0.25;220,0.2;300,0.1 clarity=14 dehaze=10 "
                "sharpenAmount=30" % S_CURVE)),
            C("HDR travel — shadows open, highlights held, local contrast high", set=(
                "highlights-=15 shadows+=5 clarity=22 texture=10 dehaze=8 saturation=6 "
                "mixer1=0,0.1;30,0;95,0;120,0.2;220,0.15;300,0 curve=%s" % S_CURVE)),
        ],
    },
    "muted": {
        "title": "Muted",
        "means": "quiet, desaturated colour: calm, minimal, documentary — the tones carry the photo",
        "words": ["muted", "desaturated", "subtle", "calm", "minimal", "minimalist", "quiet colour", "quiet color",
                  "soft colour", "soft color", "less colour", "less color", "understated", "classic chrome",
                  "documentary", "nhạt", "nhạt màu", "giảm màu", "tối giản", "trầm màu", "màu trầm"],
        "modifier": [["muted", 2]],
        "demo": "DSCF5411.RAF",
        "candidates": [
            C("muted natural — Natural Cinema 80 %, colour taken down", look="natural-cinema", strength=0.8,
              tweaks=[("muted", 2.5)]),
            C("documentary film — muted, hard shadows, brick reds, slate blues (Classic Chrome-like)", set=(
                "curve=%s saturation=-18 grade0=200,4,0 mixer0=0,0.02;30,-0.02;120,0.04;220,-0.02;300,0 "
                "mixer1=0,-0.1;30,0.05;60,-0.25;120,-0.3;220,-0.2;300,-0.2 mixer2=0,0;95,0;150,0;220,-0.12;300,0 "
                "clarity=8" % _crv((0, 0), (0.2, 0.13), (0.5, 0.48), (0.8, 0.82), (1, 0.97)))),
            C("bleach bypass — colour drained, hard silver contrast", set=(
                "saturation=-38 curve=%s clarity=18 texture=8 temp-=150 grainAmount=8 grainSize=18 lensVignette=-16"
                % _crv((0, 0), (0.2, 0.12), (0.5, 0.5), (0.8, 0.88), (1, 0.98)))),
        ],
    },
    "bw": {
        "title": "Black & white",
        "means": "no colour: the photo in tones only, each colour's brightness chosen",
        "words": ["black and white", "black & white", "b&w", "b/w", "bw", "monochrome", "mono", "grayscale",
                  "greyscale", "noir", "đen trắng", "trắng đen"],
        "modifier": [],
        "demo": "DSCF5186.RAF",
        "candidates": [
            C("hard street B&W — Moriyama: deep contrast, heavy grain", look="mono-noir"),
            C("clean fine-art B&W — a full grey scale, sky darkened, no grain", set=(
                "saturation=-100 curve=%s mixer2=0,0.08;30,0.12;60,0.05;120,-0.1;220,-0.3;300,0 clarity=8" % S_CURVE)),
            C("warm-toned B&W — a sepia / selenium print", set=(
                "saturation=-100 curve=%s grade0=35,6,0 grade2=40,8,0 grainAmount=10 grainSize=20 lensVignette=-14"
                % S_CURVE)),
        ],
    },
    # ── regional looks users ask for by name ────────────────────────────────────────────────────────────
    "korean": {
        "title": "Korean tone",
        "means": "the K-drama / Korean café look: bright, clean, soft contrast, skin luminous, colour light and "
                 "slightly cool or creamy",
        "words": ["korean", "korean tone", "korea", "k-drama", "kdrama", "k drama", "kpop", "tông hàn", "màu hàn",
                  "kiểu hàn", "hàn quốc"],
        "modifier": [["brighter", 1], ["soft", 1], ["muted", 0.5]],
        "demo": "_DSF6686.RAF",
        "candidates": [
            C("clean bright, cool-pink — luminous, a touch of pink, greens light", set=(
                "exposure+=0.2 highlights-=25 curve=%s tint+=6 temp-=150 saturation=-8 "
                "mixer0=0,0;60,0.08;120,0.06;300,0 mixer1=0,0.05;30,0.02;60,-0.25;120,-0.1;300,0.1 "
                "mixer2=0,0.05;30,0.08;60,0;95,0;300,0 clarity=-6"
                % _crv((0, 0.04), (0.25, 0.29), (0.5, 0.57), (0.8, 0.86), (1, 0.98))), depth=False),
            C("soft peach, creamy — milky, warm, low saturation", set=(
                "exposure+=0.15 highlights-=20 curve=%s curveR=%s curveB=%s temp+=200 saturation=-12 "
                "mixer1=0,0;30,0.05;60,-0.2;120,-0.15;300,0 clarity=-8"
                % (_crv((0, 0.05), (0.25, 0.3), (0.5, 0.56), (0.8, 0.85), (1, 0.97)),
                   _crv((0, 0.01), (0.5, 0.52), (1, 1)), _crv((0, 0.03), (0.5, 0.49), (1, 0.95)))), depth=False),
            C("mint fresh — bright, greens to mint, clean blues", set=(
                "exposure+=0.15 highlights-=20 curve=%s curveG=%s curveB=%s saturation=-6 "
                "mixer0=0,0;60,0.12;100,0.12;150,0.04;300,0 mixer1=0,0;60,-0.2;120,-0.1;300,0"
                % (_crv((0, 0.04), (0.25, 0.29), (0.5, 0.56), (0.8, 0.86), (1, 0.98)),
                   _crv((0, 0.02), (0.5, 0.51), (1, 1)), _crv((0, 0.04), (0.5, 0.51), (1, 0.99)))), depth=False),
        ],
    },
    "japanese": {
        "title": "Japanese film",
        "means": "the Japanese film / Shunji Iwai look: soft and a little overexposed, cyan-green shadows, pale "
                 "greens, quiet and tender",
        "words": ["japanese", "japanese film", "japan tone", "japanese tone", "shunji iwai", "hamaguchi",
                  "tông nhật", "màu nhật", "kiểu nhật", "film nhật", "nhật bản"],
        "modifier": [["brighter", 1], ["soft", 1], ["faded", 0.5]],
        "demo": "_DSF6904.RAF",
        "candidates": [
            C("spring haze — airy pastel, mint shadows, pink light", look="spring-haze", depth=False),
            C("Japanese film cyan — overexposed, cyan shadows, pale greens", set=(
                "exposure+=0.25 highlights-=30 curve=%s curveR=%s curveG=%s curveB=%s "
                "mixer0=0,0;60,0.1;120,0.12;300,0 mixer1=0,-0.05;60,-0.25;120,-0.2;200,0.05;300,0 saturation=-10 "
                "clarity=-6 grainAmount=8 grainSize=18"
                % (_crv((0, 0.06), (0.25, 0.3), (0.5, 0.57), (0.8, 0.85), (1, 0.96)),
                   _crv((0, 0), (0.4, 0.38), (1, 0.98)), _crv((0, 0.03), (0.5, 0.51), (1, 0.99)),
                   _crv((0, 0.05), (0.4, 0.42), (1, 0.98)))), depth=False),
            C("Japanese summer — warm and faded, yellow-greens, grain", set=(
                "exposure+=0.15 highlights-=25 curve=%s temp+=250 mixer0=0,0;100,-0.06;150,-0.04;300,0 "
                "mixer1=0,0;30,0.05;60,0.05;120,-0.1;220,-0.1;300,0 saturation=-6 grainAmount=8 grainSize=18"
                % _crv((0, 0.05), (0.25, 0.28), (0.5, 0.55), (0.8, 0.85), (1, 0.97))), depth=False),
        ],
    },
}

LETTERS = "ABC"
# Styles that are a FEELING rather than a named look: when a prompt names both, the look is the base and the
# feeling refines it ("warm vintage film" = vintage film, warmer).
FEELINGS = {"natural", "warm", "cool", "moody", "bright-airy", "dreamy", "melancholy", "vibrant", "muted"}


# ── the approver's verdicts ──────────────────────────────────────────────────────────────────────────────

def verdicts():
    try:
        return json.loads(VERDICTS.read_text())
    except (OSError, ValueError):
        return {}


def chosen(style):
    """(index, judged): the candidate the approver picked for this style, else A (not judged yet). A style judged
    'none fits' keeps A and is reported as such — the next round needs new candidates, not a guess."""
    v = verdicts().get(style) or {}
    best = v.get("best")
    if best and best in LETTERS[:len(STYLES[style]["candidates"])]:
        return LETTERS.index(best), True
    return 0, False


def recipe(style):
    i, _ = chosen(style)
    return dict(STYLES[style]["candidates"][i])


# ── reading a prompt ─────────────────────────────────────────────────────────────────────────────────────

def fold(text):
    """lower case, Vietnamese diacritics removed (users type 'tong han' as often as 'tông hàn')."""
    t = unicodedata.normalize("NFD", text.lower().replace("đ", "d"))
    return "".join(c for c in t if unicodedata.category(c) != "Mn")


SUBTLE = ["a little", "a bit", "a touch", "slightly", "subtle", "subtly", "light touch", "gentle", "soft touch",
          "not too", "hơi", "một chút", "nhẹ", "chút", "một tí", "tí"]
STRONG = ["very", "really", "super", "strong", "strongly", "extreme", "heavy", "heavily", "more", "much more",
          "intense", "rất", "thật", "đậm", "mạnh", "nhiều", "cực"]
LESS = ["less", "not so", "not too", "bớt", "giảm", "không quá", "đừng quá"]
TRAILING = ["a bit", "a little", "a touch", "slightly", "chút", "một chút", "một tí", "tí", "xíu", "một xíu"]
# Adjustment words that are steps, not styles: "warmer", "a bit brighter" refine whatever style is chosen.
STEPS = [("warmer", "warmer"), ("cooler", "cooler"), ("brighter", "brighter"), ("lighter", "brighter"),
         ("darker", "darker"), ("more contrast", "contrast"), ("punchier", "contrast"), ("softer", "soft"),
         ("less contrast", "soft"), ("more saturated", "vivid"), ("more colour", "vivid"), ("more color", "vivid"),
         ("less saturated", "muted"), ("less colour", "muted"), ("less color", "muted"), ("faded", "faded"),
         ("deeper blacks", "deep"), ("more magenta", "magenta"), ("more pink", "magenta"), ("less green", "magenta"),
         ("more green", "green"), ("ấm hơn", "warmer"), ("lạnh hơn", "cooler"), ("mát hơn", "cooler"),
         ("sáng hơn", "brighter"), ("tối hơn", "darker"), ("tương phản hơn", "contrast"), ("đậm hơn", "vivid"),
         ("nhạt hơn", "muted"), ("dịu hơn", "soft")]
NIGHT_WORDS = ["night", "đêm", "buổi tối"]
DAY_WORDS = ["day", "daytime", "ban ngày"]


def _vn(w):
    return any(ord(c) > 127 for c in w)


class _Text:
    """A prompt, read twice: as typed (lower case, NFC) and folded (no diacritics). A Vietnamese word is matched
    as typed; folded only when the user typed WITHOUT diacritics and the word has two syllables or more — a
    folded single syllable collides ("ấm" -> "am" as in "I am", "nhạt" pale and "nhật" Japan are both "nhat")."""

    def __init__(self, text):
        self.low = unicodedata.normalize("NFC", text.lower())
        self.fold = fold(self.low)
        self.ascii = not _vn(self.low.replace("đ", "d"))

    def find(self, phrase, lo=0, hi=None):
        hi = len(self.low) if hi is None else hi
        pats = []
        if _vn(phrase):
            pats.append((self.low, unicodedata.normalize("NFC", phrase.lower())))
            if self.ascii and " " in phrase.strip():
                pats.append((self.fold, fold(phrase)))
        else:
            pats.append((self.fold, phrase.lower()))
        out = set()
        for text, p in pats:
            for m in re.finditer(r"(?<![\w])%s(?![\w])" % re.escape(p), text[lo:hi]):
                out.add(lo + m.start())
        return sorted(out)

    def _clause_start(self, pos):
        """where the clause holding `pos` begins: a comma, "and" or "và" ends what a "less" or "a bit" reaches"""
        cut = 0
        for m in re.finditer(r"[,.;!?]|\band\b|\bbut\b|\bvà\b|\bnhưng\b", self.low[:pos]):
            cut = m.end()
        return cut

    def before(self, pos, words, span=14):
        lo = max(pos - span, self._clause_start(pos))
        return any(self.find(w, lo, pos) for w in words)

    def after(self, end, words, span=12):
        """a word said right AFTER a phrase: 'sáng hơn chút', 'warmer, just a bit'"""
        return any(self.find(w, end, min(len(self.low), end + span)) for w in words)


def match(prompt):
    """What a prompt asks for, as data:
        {"style": id|None, "also": [ids used as modifiers], "strength": x, "tweaks": [[step, amount]],
         "scope": "night"|"day"|None, "found": [(style, phrase)], "why": "..."}
    The longest phrase decides the base style (a specific wording beats a general one: "golden hour" over "warm");
    every other style named becomes a modifier (its `modifier` steps) — "warm and moody" = moody, warmer."""
    t = _Text(prompt)
    hits = []          # (style, phrase, pos, length)
    for sid, s in STYLES.items():
        for w in set(s["words"]) | {sid.replace("-", " "), s["title"].lower()}:
            for pos in t.find(w):
                hits.append((sid, w, pos, len(w)))
    # a phrase inside a longer matched phrase does not count on its own ("film" inside "japanese film")
    hits = [h for h in hits if not any(o[2] <= h[2] and h[2] + h[3] <= o[2] + o[3] and o[3] > h[3] for o in hits)]
    steps = []
    for word, step in STEPS:
        for pos in t.find(word):
            subtle = t.before(pos, SUBTLE) or t.after(pos + len(word), TRAILING)
            amt = 0.5 if subtle else 1.5 if t.before(pos, STRONG) else 1.0
            steps.append([-amt if t.before(pos, LESS) else amt, step][::-1])
            # "warmer" is a step, not the style "warm": drop a style hit that sits inside the step's words
            hits = [h for h in hits if not (pos <= h[2] < pos + len(word))]
    by = {}
    for sid, w, pos, n in hits:
        by.setdefault(sid, []).append((w, pos, n))
    if not by:
        return {"style": None, "also": [], "strength": 1.0, "tweaks": _norm(steps), "scope": _scope(t),
                "found": [], "why": "no style named" + (" — adjustments only" if steps else "")}
    # A named LOOK (Korean tone, neon, B&W, golden hour ...) is the base over a feeling word (warm, soft, moody):
    # "tông Hàn nhẹ nhàng" is the Korean tone, gently; then the longer phrase, then the first said.
    order = sorted(by, key=lambda s: (s in FEELINGS, -max(n for _, _, n in by[s]), min(p for _, p, _ in by[s])))
    # "less warm" / "bớt ấm" asks to take a feeling AWAY: it is a step the other way, never the style itself
    less = [s for s in order if s in FEELINGS and all(t.before(p, LESS) for _, p, _ in by[s])]
    order = [s for s in order if s not in less] + less
    if order[0] in less:
        tweaks = list(steps)
        for o in less:
            for step, amt in STYLES[o]["modifier"]:
                tweaks.append([step, -0.7 * amt])
        return {"style": None, "also": less, "strength": 1.0, "tweaks": _norm(tweaks), "scope": _scope(t),
                "found": [(s, x[0]) for s in order for x in by[s]],
                "why": "no style to apply: " + ", ".join("less %s" % STYLES[o]["title"].lower() for o in less)}
    base = order[0]
    w, pos, _ = max(by[base], key=lambda x: x[2])
    strength = 0.6 if (t.before(pos, LESS) or t.before(pos, SUBTLE)) else 1.3 if t.before(pos, STRONG) else 1.0
    tweaks = list(steps)
    for other in order[1:]:
        ow, opos, _ = max(by[other], key=lambda x: x[2])
        f = -0.7 if t.before(opos, LESS) else 0.5 if t.before(opos, SUBTLE) else 1.5 if t.before(opos, STRONG) else 1.0
        for step, amt in STYLES[other]["modifier"]:
            tweaks.append([step, amt * f])
    return {"style": base, "also": order[1:], "strength": strength, "tweaks": _norm(tweaks), "scope": _scope(t),
            "found": [(s, x[0]) for s in order for x in by[s]],
            "why": "'%s' -> %s%s" % (w, STYLES[base]["title"],
                                    "".join(" + %s as a modifier" % STYLES[o]["title"] for o in order[1:]))}


OPP = {"warmer": "cooler", "cooler": "warmer", "brighter": "darker", "darker": "brighter", "contrast": "soft",
       "soft": "contrast", "vivid": "muted", "muted": "vivid", "faded": "deep", "deep": "faded", "magenta": "green",
       "green": "magenta"}


def _norm(tweaks):
    """negative amounts become the opposite step; repeated steps add (taste.simplify then cancels opposites)."""
    acc = {}
    for step, a in tweaks:
        if a < 0:
            step, a = OPP[step], -a
        acc[step] = round(acc.get(step, 0) + a, 3)
    return [[k, v] for k, v in acc.items() if v > 1e-6]


def _scope(t):
    if any(t.find(w) for w in NIGHT_WORDS):
        return "night"
    if any(t.find(w) for w in DAY_WORDS):
        return "day"
    return None


def variant(m):
    """The edit a match asks for, as one candidate dict ({look, strength, tweaks, set, night, depth}), from the
    style's chosen recipe; None when the prompt named no style."""
    if not m or not m.get("style"):
        return None
    v = recipe(m["style"])
    v["strength"] = v["strength"] * m.get("strength", 1.0)
    v["tweaks"] = [list(x) for x in v["tweaks"]] + [list(x) for x in m.get("tweaks", [])]
    v["style"] = m["style"]
    return v


# ── what real prompts must read as (style.py test) ──────────────────────────────────────────────────────
# (prompt, style or None, strength, steps that must be present). Add every prompt a user wrote that was misread.
EXAMPLES = [
    ("make it cinematic", "cinematic", 1.0, []),
    ("make these Tokyo photos cinematic", "cinematic", 1.0, []),
    ("chỉnh màu điện ảnh", "cinematic", 1.0, []),
    ("just fix the white balance, keep it natural", "natural", 1.0, []),
    ("warm and moody", "moody", 1.0, ["warmer"]),
    ("dark and moody, slightly warm", "moody", 1.0, ["warmer"]),
    ("a bit warmer", None, 1.0, ["warmer"]),
    ("bớt ấm, sáng hơn chút", None, 1.0, ["cooler", "brighter"]),
    ("chỉnh màu ấm áp cho ảnh", "warm", 1.0, []),
    ("tông Hàn nhẹ nhàng", "korean", 1.0, []),
    ("tong han", "korean", 1.0, []),
    ("a bit Korean, warmer", "korean", 0.6, ["warmer"]),
    ("make these photos look like a japanese film", "japanese", 1.0, []),
    ("màu nhạt", "muted", 1.0, []),
    ("warm vintage film", "vintage-film", 1.0, ["warmer"]),
    ("Kodak Portra", "vintage-film", 1.0, []),
    ("hoài cổ", "vintage-film", 1.0, []),
    ("golden hour sunset vibe", "golden-hour", 1.0, []),
    ("black and white please", "bw", 1.0, []),
    ("đen trắng", "bw", 1.0, []),
    ("very vibrant colours", "vibrant", 1.3, []),
    ("make it pop", "vibrant", 1.0, []),
    ("cyberpunk neon for the night ones", "neon", 1.0, []),
    ("make them feel lonely and sad", "melancholy", 1.0, []),
    ("hơi buồn", "melancholy", 0.6, []),
    ("bright and airy", "bright-airy", 1.0, []),
    ("dreamy pastel", "dreamy", 1.0, []),
    ("teal and orange blockbuster", "teal-orange", 1.0, []),
    ("I am happy, make it warm", "warm", 1.0, []),
    ("not too warm, more cinematic", "cinematic", 1.3, ["cooler"]),
    ("less warm", None, 1.0, ["cooler"]),
]
