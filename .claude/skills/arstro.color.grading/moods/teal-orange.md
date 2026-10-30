# Teal & Orange (`teal-orange`)

The blockbuster split: skin and warm light pushed to orange, everything cool pushed to teal, greens folded into the teal, contrast strong.

- **Prompt words:** teal and orange, blockbuster, hollywood, action, complementary, michael bay
- **Palette:** orange (skin, warm light, wood) · teal (sky, shadow, foliage)

## How it is built — the colour lives in the curves and the mixer

**Curves** — R pulled down in the shadows (0.25→0.21) and up in the upper mids (0.8→0.85): warm highlights; B lifted at the toe (0→0.08) and cut at the top (0.8→0.78, 1→0.95): teal shadows, warm highs. G barely moves (a tint of green in the toe keeps the teal from going purple).
**Mixer** — hue: yellows −0.12 (to orange), yellow-greens and greens +0.12…+0.15 (into teal), blues −0.08 (toward teal). Sat: orange +0.15, cyan +0.2, greens −0.35, magenta −0.2 (out of the palette). Lum: orange +0.08 (skin glows), greens −0.2.
**Wheel** — midtones 30°/6 (skin).

## Use it for
people, street, travel with skin and sky, action, cars, anything you want to look like a trailer.

## Avoid it for
a frame with no warm subject (all greenery: it just turns teal); overcast skies at high strength (they go peach).

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood teal-orange [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood teal-orange
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["teal-orange"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'more teal' → curveB toe +0.02 or strength 1.2; 'skin too orange' → mixer1 at 30 down to 0.08; 'subtle' → strength 0.6.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.380 sat 0.35 warm +0.35 tint +0.07
- night: p50 0.088 sat 0.69 warm +0.17 tint -0.11
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)

Grade Lab 2026-10-30: BAD on the woman; GOOD (runner-up) on the grazing deer and on Fuji.
