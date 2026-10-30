# Moods — colour blending with the curves and the mixer

The approver, on the first Tokyo grade (2026-10-30): *"this is just basic correction … do more color
blending, use curve and mixer more for apply a mood"*. Correction makes a photo TRUE; a mood makes it
FEEL something. This file is how a mood is built, so a new one can be made, not only the nine in
`moods/`.

## 1. A mood = colour in the tones + a reduced palette

A filter tints the picture. A grade puts DIFFERENT colour at different brightness (blacks one colour,
mids another, light a third — that is "blending"), and lets only two or three hues speak. Cosmo has the
two tools that do exactly this, and the earlier `cinematic` look barely used them:

| tool | what it does to the mood | the move |
|---|---|---|
| **R/G/B curves** | colour BY TONE — tinted blacks, coloured highlights, crossovers | lift one channel's toe, cut another's shoulder |
| **master curve** | the film's character — density, fade, contrast | toe, mids, shoulder |
| **mixer hue** | the palette — pull every hue toward a few | shift neighbours toward the target hue |
| **mixer sat** | what speaks and what is silent | up the palette, down everything else |
| **mixer lum** | each colour's weight (a heavy sky, glowing skin) | down for depth, up for glow |
| wheels / grain / vignette | finishing | small |

## 2. The R/G/B curve moves (display space, `x,y;…`)

Each channel curve trims ONE channel at one brightness. Think of the curve as three dials per channel —
toe (0–0.2), mids (0.3–0.7), shoulder (0.8–1):

| you want | move | example |
|---|---|---|
| teal / navy blacks | B toe UP, R toe DOWN | `curveB=0,0.07;0.3,0.35;…` `curveR=0,0;0.25,0.2;…` |
| green-black (WKW) | G toe up, wheel 150° in shadows | `curveG=0,0.03;0.4,0.44;…` |
| warm / cream highlights | B shoulder DOWN (R ≥ G > B) | `curveB=…;1,0.88` |
| magenta highlights (neon) | G shoulder DOWN | `curveG=…;0.75,0.72;1,0.95` |
| pink highlights (sakura) | R upper-mids up, G a little less, B least | `curveR=…;0.85,0.88;1,1` |
| amber through the image | R mids up, B mids down | `curveR=0,0.01;0.5,0.55;1,1` `curveB=0,0.04;0.5,0.44;1,0.85` |
| green-yellow cast | G mids up, B mids down | `curveG=…;0.4,0.44;…` `curveB=…;0.4,0.36;…` |
| cold through the image | R mids down, B mids up | `curveR=0,0;0.5,0.46;1,0.95` `curveB=0,0.08;0.5,0.55;1,1` |
| cross-process | a channel's toe up AND shoulder down (crossing the diagonal) | `curveB=0,0.06;0.5,0.47;1,0.88` = blue blacks, yellow whites |

Rules learned on the Tokyo set:
- **Points are joined by straight lines** — use 4–6 points or the bend shows as a kink in a gradient.
- **Watch the sky.** An overcast sky is the brightest neutral in a day photo, so the SHOULDER of the
  three channels decides its colour: R ≥ G > B = cream (good), G low = pink/lilac (often bad by day),
  B low alone = peach/yellow. Neon Noir and Spring Haze both needed their day shoulders softened
  because the temple sky went pink or lilac.
- **The toe moves are safe at night** because they tint without lifting much — Neon Noir's B toe +0.08
  turned the night street navy and barely moved p5.
- A curve on the GROUP stacks on a photo's own curve as deviation from identity — fine.

## 3. The mixer moves (`hue_deg,y`, cyclic)

- **Hue (`mixer0`)** — y × 180°. Collapse the palette: to send greens (120°) to teal (≈160°), y = +0.22;
  yellows (60°) to amber (≈45°), y = −0.08; blues (240°) toward cyan, y = −0.06. Neighbouring points
  spread the move — put a 0 point where the move must stop (skin at 20–40° in most moods).
- **Sat (`mixer1`)** — ×(1+y). The palette's hues +0.15…+0.3, everything else −0.25…−0.45. This, more
  than anything, is what makes a set feel designed.
- **Lum (`mixer2`)** — gain 2^(1.5y). Greens −0.25…−0.35 = dense foliage (moody); blues −0.2 = a heavy
  sky; skin/orange +0.05…+0.15 = glow; pinks +0.1 = sakura light.
- The mixer's spread (`mixerSpread`, default 25) carries the move into grain and bokeh — leave it.

## 4. Designing a new mood — the recipe

1. **Name the feeling and the palette in words**: "lonely, rainy: cold blue, slate, a little skin".
2. **Pick the tone character** (master): dark & dense / bright & airy / faded / hard.
3. **Colour the tones** (R/G/B): what colour are the blacks? the light? (§2 table)
4. **Reduce the palette** (mixer): which 2-3 hues stay, where do the others go, what is silenced.
5. **Make a day AND a night variant** — night keeps its blacks (smaller toe lifts), usually more
   saturation in the palette hues (the lights).
6. **Board it** on 4 photos of different scenes: `mood.py board <p> --moods <new>`. Look at the SKY,
   the SKIN, the GREENS and a NIGHT frame — those break first.
7. **Measure the fingerprint** (`mood.py apply` writes mood.json) and write `moods/<name>.md` from the
   template of the others. Add it to `scripts/moods.py`.
8. Get the approver's verdict → the mood's file and `taste.md`.

## 5. Strength

`--strength s` scales a mood: curve points move toward the diagonal, mixer y, wheel sat/lum and scalars
scale; hues do not rotate. 0.5 = a hint, 1 = as designed, 1.2–1.3 = bold. "Subtle", "a touch",
"lightly" → 0.5–0.6; "strong", "heavy", "more" → 1.2–1.3.
