# Reading light and scene — context before pixels

The grade must agree with the light the photo was taken in. Three sources of evidence, and they must
AGREE before you act: the metadata, the measurement, and your own eyes.

## 1. Metadata → what the photographer was dealing with

| clue | reads as |
|---|---|
| `Taken` 05–08 / 16–19 h | golden / blue hour possible — check the warmth |
| `Taken` ≥ 18 h or < 5 h + median < 0.15 | night |
| ISO ≥ 1600 + shutter ≤ 1/125 + f/1.4–2 | low light handheld: night, interior, aquarium, concert |
| ISO 100–400 + fast shutter | daylight; ≥ 1/2000 and f/5+ = bright sun |
| ISO 800–1600 in daylight | shade, forest, or shot through glass (train, aquarium) |
| focal 50–200 mm, f/1.4–2.8 | subject isolation — the subject matters more than the background |
| `As-shot WB` multipliers | what the camera believed; a big R multiplier = it saw warm light |

## 2. The witness — what tells you the colour of the LIGHT

White balance is only as right as the pixels you trust. In order of reliability:

1. **A white/overcast sky, cloud, snow, a white wall in the same light** — the highlight zone, when it
   is ≥ 8 % of the frame and colourless (zone saturation < 0.2). Trust fully.
2. **…unless it measures BLUE** (zone warmth < −0.05): then it is a pale blue sky, not a grey card.
   Neutralising it warms everything (Kamakura temple). Trust ~30 %.
3. **Near-neutral pixels overall** (`castFromNeutrals`): fine for streets and buildings.
4. **Foliage-heavy frames** (`foliageFraction` > 0.25): grey-green leaves pass as "neutral" and drag
   the estimate green → trust ≤ 25 %; let the camera's balance mostly stand (Nikko).
5. **Never**: neon, LEDs, stage light, an aquarium's water, a sunset — they are the subject's colour.

## 3. Scenes and what each wants

| scene | evidence | white balance | exposure target (display median) | protect |
|---|---|---|---|---|
| **daylight** | sun/clear, ISO low | to warmth +0.12, tint +0.04 | ≥ 0.40 (floor) | sky, skin, foliage not neon-green |
| **overcast-day** | ≥ 12 % bright colourless (white sky) | sky → warmth +0.10, tint +0.04 | ≥ 0.43 (floor) | the sky's texture; it is soft light — add contrast |
| **golden-hour** | late/early + warmth > 0.6 | tint only — the warmth IS the photo | ≥ 0.40 | the gradient of the light |
| **blue-hour** | dusk, sky deep blue, lamps on | keep the blue, neutralise only green | 0.15–0.25 | lamp pools warm vs blue ambient |
| **night-city** | night + neon > 0.5 % | a little (±1000 K) toward neutral street light | 0.09–0.16 | neon colour; black is black |
| **dark-interior** (art light, bars, concerts) | dark + high ISO, no neon pattern | remove decoder green only | 0.07–0.14 | the artist's colour |
| **aquarium / underwater** | dark, > 55 % of colour blue-cyan | none | 0.15–0.23 | the blue; the subject's complement (amber jellyfish) |
| **interior tungsten** | warmth > 0.8 indoors, ISO high | most of the way to neutral, keep warmth +0.2 | 0.30–0.40 | skin |
| **snow / beach** | huge bright colourless area | to neutral-cool | 0.60–0.75 (floor) | texture in white |
| **fog / haze** | low range (< 5 stops), low saturation | slight cool | 0.45–0.55 | the mood — don't over-dehaze |

## 4. Mixed light (the hard case)
- A street at dusk: blue sky ambient + sodium/LED lamps. Balance for the AMBIENT you want to keep
  (usually a little cool) and let the lamps go warm — "motivated" colour. Never fully neutral.
- Neon signs: keep their hue; they clip easily in one channel — `highlights` −60…−90, and the
  measurement's `clipHighlights` up to ~1 % is acceptable (the sources themselves).
- Faces under neon: a magenta face is the light, but if the face is the subject, a mask
  (radial, `tint` toward green a little) is better than a global correction.

## 5. Low key vs under-exposed
A night frame at median 0.05 can be right (a silhouette against light, teamLab 18) or under-exposed
(a street where the subject is lost in black). Ask: is there detail in the shadows the story needs?
If yes, lift the mids with exposure + highlight protection; if not, keep it dark and make the blacks
clean (`blackMax`).
