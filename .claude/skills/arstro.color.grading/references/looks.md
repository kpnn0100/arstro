# Looks — a catalogue, with Cosmo recipes

> The MOODS in `moods/` (built with the R/G/B curves and the mixer, proven on the Tokyo set, applied by
> `scripts/mood.py`) supersede most of the starting points below — use a mood first; this file keeps
> the older recipes and the film references they came from.

Each recipe is a GROUP's `set` line (the look), applied on top of per-photo correction. Values are
calibrated to Cosmo (cosmo-controls.md) — do not copy Lightroom numbers in. Only `cinematic` is
proven on a full set (Tokyo, 2026-10-30, `scripts/looks.py`); the others are starting points: render,
look, adjust, and record what worked in `taste.md`.

Curves are written display-domain `x,y;x,y` (Cosmo applies them in its log domain — gentle). Mixer
curves are `hue_deg,y` with y in −1..1 (hue: y·180°; sat: ×(1+y); lum: gain 2^(1.5·y)).

## 1. "Cinematic" — what people mean, and the default
Film-print tone, cool shadows vs warm mids/highlights, muted greens, a set-wide palette, grain.
- **Day** (`cinematic` → "Cinematic - Day"):
  `curve=0,0.012;0.22,0.165;0.72,0.80;1,0.975 grade0=205,4,0 grade1=35,5,0 grade2=45,10,0 balance=-10
  saturation=-10 vibrance=10 clarity=6 dehaze=4 grainAmount=6 grainSize=15 lensVignette=-8 sharpenAmount=25
  mixer0=0,0;60,0;105,-0.03;160,0.02;215,-0.05;300,0 mixer1=0,0;30,0.06;100,-0.3;150,-0.25;210,-0.06;300,0
  mixer2=0,0;30,0.04;110,-0.06;220,-0.15;300,0`
- **Night** ("Cinematic - Night"): R/G/B curves for the teal-shadow / warm-light split (they leave the
  black point alone, the shadow wheel does not):
  `curve=0,0;0.12,0.07;0.5,0.53;0.85,0.9;1,0.975 curveR=0,0;0.3,0.26;0.7,0.72;1,1 curveG=0,0.01;0.3,0.31;1,0.99
  curveB=0,0.04;0.3,0.34;0.75,0.72;1,0.95 grade2=28,12,0 saturation=-4 vibrance=6 clarity=10 grainAmount=10
  grainSize=22 lensVignette=-14 mixer0=0,0;55,-0.04;120,0.1;200,0;300,0 mixer1=0,0;30,0.06;120,-0.15;300,0.05
  mixer2=0,0;220,-0.12;300,0`

## 2. Teal & orange (blockbuster)
Stronger complementary split; skin pops against teal. Danger: teal skin edges, orange skies.
- `curve=0,0;0.2,0.15;0.75,0.82;1,0.98 curveR=0,0;0.3,0.25;0.7,0.73;1,1 curveB=0,0.05;0.3,0.36;0.7,0.66;1,0.94
  grade1=30,8,0 saturation=-6 vibrance=12 mixer0=0,0;30,0.02;60,-0.06;150,0.12;220,-0.06;300,0
  mixer1=0,0;30,0.1;120,-0.35;200,0.1;300,0` (greens → teal, yellows → orange, skin up)

## 3. Eterna / quiet cinema (Kore-eda, Japanese drama)
Low contrast, low saturation, cyan-green shadows, soft. Perfect for overcast Japan.
- `curve=0,0.04;0.25,0.24;0.75,0.76;1,0.95 grade0=180,4,0 saturation=-18 vibrance=6
  mixer1=0,0;30,0.05;110,-0.35;210,-0.15;300,0 grainAmount=5 grainSize=12`

## 4. Classic Chrome (documentary / street)
Muted, harder shadows, blues to cyan and desaturated, reds to brick.
- `curve=0,0.01;0.25,0.18;0.7,0.76;1,0.97 contrast=4 saturation=-12
  mixer0=0,0;10,0.02;220,-0.06;300,0 mixer1=0,0;10,-0.1;220,-0.3;300,0 mixer2=0,0;220,-0.2;300,0`

## 5. Kodak Portra (warm, people)
Warm mids, gentle contrast, creamy highlights, pastel greens, best skin.
- `curve=0,0.03;0.25,0.23;0.75,0.79;1,0.96 grade1=30,8,0 grade2=40,8,0 saturation=-6
  mixer0=0,0;100,-0.04;300,0 mixer1=0,0;30,0.04;100,-0.25;300,0 mixer2=0,0;30,0.06;300,0`

## 6. Tokyo neon / Cinestill night (Blade Runner 2049, Lost in Translation, Wong Kar-wai)
Deep blacks, cyan-teal ambient, magenta and amber signs saturated, halation feel.
- Night base + `vibrance=14 mixer0=0,0;55,-0.06;120,0.14;300,0 mixer1=0,0;300,0.12;330,0.1`
  (greens to teal, sodium yellow to amber, magenta up). Per photo: `highlights=-70…-90`,
  exposure to median ~0.10–0.14. For Wong Kar-wai: add `grade1=120,6,0` (green mids) and more grain.

## 7. Golden hour (keep it, don't fight it)
- White balance: tint only. `grade2=35,12,0 grade0=210,3,0 saturation=-4 vibrance=8
  curve=0,0.02;0.25,0.2;0.75,0.8;1,0.97`. Protect the sun's highlight with `highlights`.

## 8. Moody / Nordic / Fincher
Cool-green overall, low saturation, dense mids, yellow-green highlights (Fincher).
- `temp` −600 from neutral, `curve=0,0.01;0.3,0.24;0.75,0.74;1,0.95 saturation=-25
  grade2=70,8,0 grade0=190,4,0 clarity=8 lensVignette=-15`

## 9. Bleach bypass / gritty
- `contrast=8 curve=0,0;0.25,0.18;0.75,0.84;1,0.99 saturation=-35 clarity=15 texture=10 grainAmount=12`

## 10. Pastel / airy (Japanese "kawaii", Hamaguchi daylight)
- `exposure` +0.3 (with highlight protection) `curve=0,0.08;0.3,0.36;0.8,0.85;1,0.97 saturation=-15
  vibrance=10 grade1=330,4,0 grade2=200,4,0 mixer1=0,0;110,-0.3;300,0.1`

## 11. Black & white (cinematic)
- `saturation=-100` then shape with mixer Lum BEFORE desaturating mentally: darken blue sky
  (`mixer2=220,-0.4`), lighten skin (`30,0.15`); `curve` with a deep toe; grain 10–15.

## Choosing
- Ask what the story is: travel diary (Eterna / cinematic), city night (Tokyo neon), people (Portra),
  documentary (Classic Chrome), drama (moody). A SET gets one look per light family, never twelve.
- Check against memory colours after the look: skin, sky, foliage, white.
