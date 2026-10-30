# The professional workflow — order of operations, and the checks

A colourist's day, compressed. The ORDER matters more than any single setting: a look applied to an
uncorrected image grades the error, not the photo.

## 1. Prep — know the material
- Camera, lens, ISO, shutter, aperture, time, location (metadata). Same camera all set? Same lens?
  A camera's RAW rendering has its own bias (this repo's X-S10 RAWs render green-yellow through
  LibRaw's as-shot WB: tint +0.15…+0.45 on almost every frame — correct it per set, not per photo).
- Sort the set into **scenes** (same light, same place, same time) — they will be matched.
- Pick a **hero frame** per scene — the one you grade first and match the rest to.

## 2. Technical / primary correction — make it TRUE
In this order, because each step changes what the next one sees:
1. **Exposure & highlights** — protect what clips first (`highlights`, then lower `exposure` if the
   sky still clips), then place the median. Daylight median ~0.40–0.48 display; overcast with a lot
   of white sky higher; night 0.08–0.16; a dark interior 0.07–0.14.
2. **White balance** — against a witness that really is neutral (light.md §2), toward the scene's
   target (slightly warm daylight, NOT dead grey; leave artistic light alone).
3. **Black point & white point** — black ≈ 0.01–0.03 display at the 1st percentile for a punchy
   image, 0.03–0.06 for a matte one; white 0.95–0.98 for a soft shoulder.
4. **Noise** — by ISO, before sharpening; never smear skin.

## 3. Balance / match — one scene, one light
- Bring every frame of a scene to the hero's median, cast and contrast. Measure them side by side
  (`analyze`) rather than trusting the eye across a filmstrip.
- Put shared decisions on a GROUP; a photo carries only its own correction.

## 4. Secondary — targeted
- **Hue/sat/lum by hue** (Cosmo's mixer): mute greens, deepen the sky, protect skin, tame a neon
  that clips a channel.
- **Local** (Cosmo's masks): brighten the subject, darken a hot corner, grade sky separately.
- Hue remap: bend one hue range to another (sodium yellow → amber, signage green → teal).

## 5. The look — creative
- Tone curve (film toe/shoulder), split tone (shadow/mid/highlight wheels or R/G/B curves),
  saturation shaping, grain, vignette. One look per FAMILY of scenes, not one per photo.
- Strength check: toggle it (`bypass <group> on/off` + `cosmo_preview`). If the look is the first
  thing you notice, halve it.

## 6. Finish & check — every photo, then the set
- [ ] Nothing important clipped (`clipHighlights` < 0.3 % for daylight; < 1 % at night, where a lamp
  may clip); shadows not crushed where there is detail that matters.
- [ ] Blacks are as intended — black, or deliberately lifted — not milky by accident (p1, p5).
- [ ] Skin on the skin line; whites and greys where they should be (sky, walls, snow).
- [ ] The set on ONE contact sheet reads as one story: same palette, matching density.
- [ ] Before | after at a glance: is it better, or only different?

## 7. Deliver for CHECKING
- Save the project (the grade is editable, not baked), export JPEGs for viewing, before|after sheets,
  a short per-photo note (scene, what was corrected, what the look did).
- Ask the approver what they would change. Record verdicts in `taste.md` with the reason; a rule
  that two verdicts confirm becomes a rule in `looks.py`.

## 8. Mistakes that cost the most (all made, and caught, on the Tokyo set)
- **Neutralising against the wrong witness** — grey-green foliage made a Nikko forest yellow; a pale
  blue sky "corrected" to white warmed a whole temple. Witness rules: light.md §2.
- **A median target that pulls a sky-dominated frame down** — the Asakusa pagoda became a silhouette.
  Daylight median is a floor.
- **Lifting night shadows with the shadow wheel / `shadows`** — milky blacks. Use R/G/B curves for
  the teal and keep the black point.
- **Correcting artist's light** — teamLab's magenta is the work, not a cast.
- **Putting the same colour wheel on a photo and its group** — the hues ADD (D-68).
