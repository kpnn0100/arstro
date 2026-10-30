# Mono Noir (`mono-noir`)

Black and white with a film's toe — Moriyama's Tokyo, Kurosawa: colour removed, each hue's brightness chosen (skin up, sky and greens down), hard contrast, heavy grain.

- **Prompt words:** black and white, b&w, bw, monochrome, mono, noir, moriyama, grainy, street
- **Palette:** black and white

## How it is built — the colour lives in the curves and the mixer

**Saturation −100**, but the MIXER LUM decides the greys first: reds +0.1, orange (skin) +0.15, yellow +0.05, greens −0.2…−0.25, blues −0.35…−0.45 (the sky goes dark like a red filter on film), magenta −0.1. Master curve: hard S with a deep toe (0.15→0.07 at night) and bright shoulder. Clarity +14…16, heavy grain (18-20, size 26-28), vignette −20.

## Use it for
street photography, Tokyo at night, portraits with character, architecture, rain.

## Avoid it for
a photo whose story IS its colour (the jellyfish, cherry blossom).

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood mono-noir [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood mono-noir
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["mono-noir"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'darker sky' → mixer2 at 220 to −0.6; 'softer' → strength 0.7 (the saturation scales too: 0.7 keeps 30 % colour — use a separate `set saturation=-100` on the group if it must stay mono).

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.342 sat 0.00
- night: p50 0.071 sat 0.01
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)
