"""taste.py — the approver's taste as code: which look a photo gets, skin kept true, and the refine steps.

Learned in the Grade Lab (data/judgments.jsonl; taste.md "THE TASTE"): 45 verdicts, 2026-10-30 and 11-02.
`choose()` is what `edit.py run --mood taste` (the default) asks for every photo.

| photo                                         | look                         | the verdicts behind it |
|-----------------------------------------------|------------------------------|------------------------|
| daylight / overcast: sky, foliage, buildings, animals, landscape | natural-teal 100 % | 14 bests, never marked bad: "really love the sky", "the mood is very clear, it feel lonely" (Fuji), "keeping the main object color clean" (deer) |
| a PERSON is the subject                        | natural-cinema 70 % colour, full depth, skin protected | the woman twice, the teamLab face: "apply a mood with the skin color still correct" |
| water and reflections                         | neon-noir 60 % colour at FULL depth | the pond: "#11 [natural cinema] got the depth but #9 [neon noir 60 %] got the color" |
| night city                                    | golden-dream 50 % colour, full depth | Shinjuku "really have warm mood", the alley "feel warm" |
| aquarium                                      | golden-dream 50 %            | the jellyfish twice: "i like the water color, teal really hit it" |
| art light (teamLab)                           | corrected only               | orbs: "bring the mood without destroying face color"; the face too (3 of the last 4) |

The basic mechanism, in the approver's words: **"keep the skin color for human"** — `protect_skin`.
"""
import re

import edit as E
import looks as L
import moods as M

# ── the steps (shared by the Grade Lab's refine rounds and the profile's tweaks) ────────────

def offset_mid(v, d):
    return E.curve_str([(x, y + d * max(0.0, 1.0 - abs(x - 0.5) / 0.5)) for x, y in E.curve_grid(v)])


def _add(d, k, v, base=0.0):
    d[k] = "%g" % (float(d.get(k, base) or base) + v)


TWEAKS = {
    "warmer":   ("warmer", lambda d, a: _add(d, "temp", 400 * a, 6500)),
    "cooler":   ("cooler", lambda d, a: _add(d, "temp", -400 * a, 6500)),
    "magenta":  ("more magenta", lambda d, a: _add(d, "tint", 8 * a)),
    "green":    ("more green", lambda d, a: _add(d, "tint", -8 * a)),
    "contrast": ("more contrast", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), -0.03 * a, 0.02 * a))),
    "soft":     ("softer", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), 0.03 * a, -0.03 * a))),
    "brighter": ("brighter", lambda d, a: d.__setitem__("curve", offset_mid(d.get("curve", "0,0;1,1"), 0.05 * a))),
    "darker":   ("darker", lambda d, a: d.__setitem__("curve", offset_mid(d.get("curve", "0,0;1,1"), -0.05 * a))),
    "vivid":    ("more saturated", lambda d, a: _add(d, "saturation", 10 * a)),
    "muted":    ("less saturated", lambda d, a: _add(d, "saturation", -10 * a)),
    "faded":    ("faded blacks", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), 0.05 * a, 0.0))),
    "deep":     ("deeper blacks", lambda d, a: d.__setitem__("curve", E.offset_curve(d.get("curve", "0,0;1,1"), -0.03 * a, 0.0))),
}
OPPOSITE = {"warmer": "cooler", "cooler": "warmer", "magenta": "green", "green": "magenta", "contrast": "soft",
            "soft": "contrast", "brighter": "darker", "darker": "brighter", "vivid": "muted", "muted": "vivid",
            "faded": "deep", "deep": "faded"}


def simplify(tweaks):
    """Merge repeated steps and cancel opposites, so a recipe after many refine rounds stays readable."""
    acc = {}
    for n, a in tweaks:
        o = OPPOSITE[n]
        if acc.get(o):
            take = min(acc[o], a)
            acc[o] -= take
            a -= take
        if a > 1e-6:
            acc[n] = acc.get(n, 0) + a
    return [(n, round(a, 3)) for n, a in acc.items() if a > 1e-6]


def look_line(look, fam):
    if look == "corrected":
        return ""
    if look == "cinematic":
        return L.LOOKS["cinematic"]["groups"][fam]["params"]
    return M.MOODS[look][fam]


def look_title(look):
    return {"corrected": "Corrected only", "cinematic": "Cinematic (first grade)"}.get(look) or M.MOODS[look]["title"]


def params(corr, fam, v, plan=None):
    """The photo's whole edit for a variant {look, strength, tweaks, skin}: correction + look (colour scaled,
    depth kept) + steps + the plan's `set`, with the skin line protected when a person is in the photo."""
    line = look_line(v["look"], fam)
    d = E.compose(corr, M.scale(line, v["strength"]) if line else "", plan or {})
    for name, step in v["tweaks"]:
        TWEAKS[name][1](d, step)
    if v.get("skin"):
        protect_skin(d)
    if v.get("white"):
        protect_white(d)
    return d


# ── the profile ─────────────────────────────────────────────────────────────────────────────

# Who is in the photo, read from the plan's `subject` (what the agent SAW), never guessed from hue alone when a
# subject was written: a deer's fur sits on the skin line too.
PEOPLE = re.compile(r"\b(wom[ae]n|m[ae]n|person|people|girl|boy|child|kid|face|portrait|couple|bride|groom|lady|guy)\b", re.I)
# Still water whose REFLECTIONS are the picture (the pond). Not any water: Fuji's river, the moat and the sea
# wall all chose Natural Teal.
WATER = re.compile(r"\b(pond|lake|reflection|reflections)\b", re.I)
SKIN_BAND = (8.0, 48.0)          # hue degrees: every human skin tone sits on this line


def protect_skin(d):
    """The approver's basic mechanism: "keep the skin color for human". Whatever a look does to the palette,
    the mixer does NOTHING to hue on the skin line, never desaturates it, and lifts it at most a touch. The
    other hues keep the look's moves; the curve between interpolates from 0 at the band's edges."""
    lo, hi = SKIN_BAND
    pins = {"mixer0": [(lo, 0.0), (25.0, 0.0), (hi, 0.0)],
            "mixer1": [(lo, 0.0), (25.0, 0.02), (hi, 0.0)],
            "mixer2": [(lo, 0.0), (25.0, 0.03), (hi, 0.0)]}
    for k, pin in pins.items():
        pts = []
        for p in (d.get(k) or "").split(";"):
            if "," in p:
                h, y = (float(x) for x in p.split(","))
                if not (lo - 4 <= h <= hi + 4):
                    pts.append((h, y))
        pts = sorted(pts + pin)
        d[k] = ";".join("%g,%g" % p for p in pts)
    return d


# A WHITE subject (a white car, a white facade, snow) — read from the plan's subject, what the agent saw. Only the
# subject's own words count, before its setting: "white roadster under the billboard" is white, "pagoda against
# white sky" is not (the first build cooled the pagoda -50 for its sky).
WHITE = re.compile(r"\b(white|silver|snow|snowy|snow-capped)\b", re.I)
SETTING = re.compile(r"\b(against|under|over|before|by|in|on|through|framed|at|among|with|into|behind|beside)\b", re.I)


def is_white(subject):
    return bool(WHITE.search(SETTING.split(subject or "", 1)[0]))


def protect_white(d):
    """A white subject stays white under a warm (or cool) mood — exam 2, the white roadster under Golden Dream 80 %:
    "would be better if the car don't fall for gold too much". Measured on the car's paint (saturation, camera
    0.36): the correction alone kept it (0.34); the LOOK added +0.30 — the paint sits in the night's MIDS (0.48 on
    screen), where Golden Dream's R/G/B curves lay the gold, plus the mixer's yellow-orange boost.
    So: the R/G/B curves return to the diagonal from x 0.40 (full at 0.65) — the gold keeps the shadows and the
    lights' own colour —, the mixer gives no saturation to 30-65 deg, and the highlight wheel is halved."""
    for k in ("curveR", "curveG", "curveB"):
        if d.get(k):
            pts = []
            for x, y in E.curve_grid(d[k]) + [(0.4, None), (0.65, None)]:
                y = E.sample(E.curve_points(d[k]), x) if y is None else y
                w = max(0.0, min(1.0, (x - 0.4) / 0.25))
                pts.append((x, y + (x - y) * w))
            d[k] = E.curve_str(sorted(set(pts)))
    if d.get("mixer1"):
        pts = [(float(h), float(y)) for h, y in (p.split(",") for p in d["mixer1"].split(";") if "," in p)]
        pts = [(h, y) for h, y in pts if not 26 <= h <= 69] + [(30.0, 0.0), (45.0, 0.0), (65.0, 0.0)]
        d["mixer1"] = ";".join("%g,%g" % p for p in sorted(pts))
    if d.get("grade2"):
        h, s, l = (float(x) for x in d["grade2"].split(","))
        d["grade2"] = "%g,%g,%g" % (h, s / 2, l)
    return d


def has_person(analysis, plan_entry):
    subject = (plan_entry or {}).get("subject", "")
    if subject:
        return bool(PEOPLE.search(subject))
    return analysis.get("skinFraction", 0) >= 0.03 and analysis.get("foliageFraction", 0) < 0.3


def choose(scene, analysis, plan_entry=None):
    """{look, strength, tweaks, skin, white, why} for one photo: the look (below), and the two things no look may
    change — a person's skin (`skin`) and a white subject's white (`white`)."""
    c = _choose(scene, analysis, plan_entry)
    c["white"] = is_white((plan_entry or {}).get("subject", ""))
    return c


def _choose(scene, analysis, plan_entry=None):
    """{look, strength, tweaks, skin, why} for one photo — the approver's choice for photos like it.

    v4, BOLDER (2026-11-02): the exam passed at 94 %, and the approver: "if i don't comment that mean you are
    playing it too safe, you just correct the lighting, i need to apply mood by playing with color, try something
    riskier". The commented goods were the teal-colour ones ("color real hit", "love the sky color", "this photo
    have mood"); the silent ones were the safe ones (correction, NC 70 %, Golden Dream 50 %). So colour goes UP
    everywhere — depth stays full, skin stays true."""
    pe = plan_entry or {}
    person = has_person(analysis, pe)
    subject = pe.get("subject", "")
    if pe.get("look"):                                     # the agent (or the approver) named one: it wins
        return {"look": pe["look"], "strength": pe.get("strength", 1.0), "tweaks": pe.get("tweaks", []),
                "skin": person, "why": "named in the plan"}
    if scene == "dark-interior":
        return {"look": "neon-noir", "strength": 0.5, "tweaks": [], "skin": True,
                "why": "art light, bolder: neon noir's cyan and magenta at 50 % — the light's own palette pushed, faces "
                       "kept true ('corrected only' was too safe in the exam)"}
    if scene == "aquarium":
        return {"look": "golden-dream", "strength": 0.8, "tweaks": [], "skin": False,
                "why": "amber bell on teal water, bolder: golden dream 80 % ('teal really hit it'; 50 % was too safe)"}
    if scene == "blue-hour":
        return {"look": "natural-teal", "strength": 1.0, "tweaks": [], "skin": person,
                "why": "blue hour: the blue ambient IS the mood, the lamps its warm counterpoint — natural teal's night "
                       "variant (cool shadows, warm lights); golden dream would warm the hour away"}
    if scene == "night-city":
        return {"look": "golden-dream", "strength": 0.8, "tweaks": [], "skin": person,
                "why": "night city, bolder: golden dream 80 % ('really have warm mood'; 50 % was too safe)"}
    if WATER.search(subject):
        return {"look": "neon-noir", "strength": 0.8, "tweaks": [], "skin": False,
                "why": "still water and reflections: neon noir's colour at 80 %, full depth ('really good')"}
    if person:
        return {"look": "natural-teal", "strength": 1.0, "tweaks": [], "skin": True,
                "why": "a person: the teal mood the approver loves on everything around her, her skin kept true "
                       "(natural cinema 70 % was 'just balance, doesn't create a mood')"}
    return {"look": "natural-teal", "strength": 1.3, "tweaks": [], "skin": False,
            "why": "sky, foliage, buildings, animals: natural teal at 130 % colour — teal blended through the tones, deep "
                   "greens ('color real hit', 'love the sky color'; the green 'need to be more depth')"}
