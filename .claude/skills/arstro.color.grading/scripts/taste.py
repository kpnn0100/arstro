"""taste.py — the approver's taste as code: which look a photo gets, and the steps a refine can take.

Learned in the Grade Lab (data/judgments.jsonl; taste.md "THE TASTE"): 27 verdicts on 18 Tokyo photos,
2026-10-30. `choose()` is what `edit.py run --mood taste` (the default) asks for every photo, so a
thousand photos are graded the way the approver picked, not the way a recipe guessed.

| scene / content                               | look                          | the verdicts behind it |
|-----------------------------------------------|-------------------------------|------------------------|
| daylight / overcast: sky, foliage, buildings  | natural-teal 100 %            | 6 wins of 7: "really love the sky and the contrast", "only #1 can achieve the depth" |
| … with a person, animal or tower as SUBJECT   | natural-cinema 100 % + ½ contrast | woman, both deer, pagoda: "good depth in color" (one deer took it without contrast) |
| night city                                    | natural-cinema 70 % + warmer  | Shinjuku (NC 70 %), the alley (cinematic + warmer: "highlight the important and have good color") |
| aquarium                                      | cinematic + warmer            | the jellyfish |
| dark interior lit by art (teamLab)            | corrected only                | 17: corrected won; 18: every look that warmed or opened the fire went lime ("make sure this mistake doesn't happen") |
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
    """The photo's whole edit for a variant {look, strength, tweaks}: correction + look + steps + the plan's `set`."""
    line = look_line(v["look"], fam)
    d = E.compose(corr, M.scale(line, v["strength"]) if line else "", plan or {})
    for name, step in v["tweaks"]:
        TWEAKS[name][1](d, step)
    return d


# ── the profile ─────────────────────────────────────────────────────────────────────────────

# A subject the approver wanted held in natural colour with depth: a person, an animal, a tower.
SUBJECT = re.compile(r"\b(wom[ae]n|m[ae]n|person|people|girl|boy|child|kid|face|portrait|couple|bride|deer|dog|cat|"
                     r"horse|bird|animal|pagoda|tower)\b", re.I)


def choose(scene, analysis, plan_entry=None):
    """{look, strength, tweaks, why} for one photo — the approver's choice for photos like it."""
    pe = plan_entry or {}
    if pe.get("look"):                                     # the agent (or the approver) named one: it wins
        return {"look": pe["look"], "strength": pe.get("strength", 1.0), "tweaks": pe.get("tweaks", []),
                "why": "named in the plan"}
    if scene == "dark-interior":
        return {"look": "corrected", "strength": 1.0, "tweaks": [],
                "why": "art light is the artist's: 'corrected only' won teamLab, every warming look burned the fire"}
    if scene == "aquarium":
        return {"look": "cinematic", "strength": 1.0, "tweaks": [["warmer", 1.0]], "why": "the jellyfish: cinematic + warmer"}
    if scene == "night-city":
        return {"look": "natural-cinema", "strength": 0.7, "tweaks": [["warmer", 1.0]],
                "why": "night city: natural cinema 70 % (Shinjuku) + warmer (the alley)"}
    subject = pe.get("subject", "")
    if SUBJECT.search(subject) or (not subject and analysis.get("skinFraction", 0) >= 0.08):
        return {"look": "natural-cinema", "strength": 1.0, "tweaks": [["contrast", 0.5]],
                "why": "a person / animal / tower as subject: natural cinema with depth (woman, deer, pagoda)"}
    return {"look": "natural-teal", "strength": 1.0, "tweaks": [],
            "why": "sky, foliage, architecture: natural teal won 6 of 7 such photos"}
