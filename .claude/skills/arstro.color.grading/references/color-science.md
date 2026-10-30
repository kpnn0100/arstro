# Colour science a grader actually uses

Not a textbook — the parts that change a decision.

## 1. Light, scene-referred and display-referred

- **Light adds linearly.** Twice the photons = twice the value in *linear* (scene-referred) light.
  Exposure is a multiply there: +1 EV = ×2. Cosmo's engine works in linear light (decode once,
  encode once at the end), so exposure, white balance and the colour wheels act on light.
- **The eye does not see linearly.** Perceived lightness is roughly a cube root (CIE L*) of
  luminance; a display's sRGB encoding (≈ gamma 2.2) approximates it. Middle grey — what looks
  halfway between black and white — is **18 % linear ≈ 0.46 encoded (118/255)**. So "the median
  is 0.45 display" is a normal daytime frame; a night street at 0.03–0.10 is correct.
- **A stop is a factor of 2.** A camera holds ~12–14 stops; a print or an sRGB screen shows ~6–8 with
  a pleasing curve. Grading is mostly deciding *which* stops survive and how they roll off.
- **Clipping is information destroyed.** A clipped channel is not "bright", it is a flat plate.
  A clipped single channel also SHIFTS hue (a sunset's red channel clips → orange goes yellow).
  Highlights first, always.

## 2. Colour: hue, saturation, and the cast

- **Hue** is the angle (red 0°, orange 30°, yellow 60°, green 120°, cyan 180°, blue 240°,
  magenta 300°). **Saturation/chroma** is distance from grey. **Value/lightness** is separate —
  and a grade that changes one usually disturbs the others (HSL lightness in linear light is not
  perceptual: Cosmo's mixer Lum is a gain for that reason).
- **White balance** says what colour the light was, so that what was white renders white. Two axes:
  **temperature** (blue ↔ amber, the black-body line: candle 1900 K, tungsten 3200 K, sunrise
  3500 K, noon sun 5500 K, overcast 6500–7500 K, open shade 8000–10000 K) and **tint** (green ↔
  magenta, off the black-body line: fluorescent and many LEDs are green; some LEDs and sodium
  mixes are magenta). In Cosmo, the TEMP slider is the light you are correcting FOR: a higher value
  makes the picture warmer.
- **Measured cast** (cosmo `analyze`): `warmth = log2(R/B)`, `tint = log2(G/√(R·B))` in linear
  light. 0/0 is neutral; +0.10 warmth is "a touch warm"; ±0.3 is clearly cast; ±1 is a strong colour.
  Measured on the right pixels (references/light.md §2) it tells you what the light did.
- **Complementary pairs** sit opposite on the wheel: orange–teal (skin vs sky/shadow), red–cyan,
  yellow–blue, green–magenta. Opposites make each other look more saturated — this is the entire
  physics of "teal and orange".
- **Memory colours** — skin, sky, grass, foliage — are what a viewer judges a grade by. They have
  narrow acceptable ranges; everything else can move freely.

## 3. Skin

- Every human skin tone, light to dark, sits on one **hue line** (~20–30° in HSV/HSL, the
  vectorscope's "skin line" at ~123° in Y′CbCr). Lightness and saturation differ; hue barely does.
  A grade that pushes skin off it (toward green or magenta) reads as ill or alien instantly.
- So: protect hue 15–45° when shifting greens/yellows; warm the MIDTONES (where skin lives) rather
  than everything; check skin after every look.
- `analyze` reports `skinFraction` — when it is above ~3 % treat skin as a subject.

## 4. Contrast — global and local

- **Global contrast** is the tone curve: an S lifts highlights and deepens shadows around a pivot.
  How the curve *ends* is the character: a **toe** (black lifted to 0.02–0.05 = matte/faded film),
  a **shoulder** (whites rolled to 0.95–0.98 = film's soft highlight compression, no hard clip).
- **Local contrast** (clarity, texture, dehaze) changes edges and haze, not the histogram — a frame
  can measure identical and look twice as crisp. Clarity at night gives grit; on skin it ages.

## 5. What "cinematic" is made of

A still reads as a frame from a film when it has most of these — measurable, not mystical:

1. **A film-print tone curve**: soft toe, rolled shoulder, mids a little dense. Nothing clips hard.
2. **Restrained, SHAPED saturation**: overall saturation down a little, the subject's colour up;
   greens are the first thing a colourist mutes (to olive or teal).
3. **Colour separation by luminance**: cool (teal/blue-green) shadows against warm (amber/skin)
   mids and highlights — the split every feature grade uses, usually subtly.
4. **A consistent palette across the set** — 2–3 dominant hues, everything else suppressed.
5. **Darkness allowed**: low key, vignette, blacks that are black (or deliberately lifted, never milky
   by accident).
6. **Texture**: grain, slight softness of the highlights (halation), no digital over-sharpening.
7. **Motivated light**: the grade agrees with where the light comes from (warm lamp → warm pools,
   cool ambient fill in the shadows). Never invert the scene's own logic.

## 6. Film references (what each look borrows)

- **Kodak 2383 / Vision print**: warm-neutral mids, cyan-leaning shadows, dense saturated reds, rich
  blacks, soft roll-off — "the Hollywood print".
- **Fuji Eterna (cinema negative / film simulation)**: low contrast, low saturation, cyan-green
  leaning shadows, gentle — "quiet cinematic".
- **Fuji Classic Chrome**: documentary — muted, hard shadows, blues desaturated toward cyan,
  reds toward brick.
- **Kodak Portra**: warm, soft, beautiful skin, pastel greens.
- **Cinestill 800T** (tungsten film at night): halation glow around lights, tungsten-balanced so
  daylight-ish sources go cyan, red halos — "night city on film".
- **Bleach bypass**: silver retained — high contrast, low saturation, metallic.
