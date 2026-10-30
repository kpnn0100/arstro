# Spring Haze (`spring-haze`)

Japanese airy pastel — sakura season, Hamaguchi daylight: bright and low-contrast, mint-cyan shadows, pink-cream highlights, greens to mint, pinks lifted. Light, tender, calm.

- **Prompt words:** airy, pastel, soft, spring, sakura, cherry blossom, japanese, light, dreamy, kawaii, bright
- **Palette:** mint · cream-pink · pale sky · soft

## How it is built — the colour lives in the curves and the mixer

**Curves** — master LIFTED everywhere (toe 0.08, mids 0.5→0.58, top 0.97): bright, low contrast. R lifted in the upper mids (0.85→0.88) with G (0.86) and B (0.84) below it: cream-pink highlights (R ≥ G > B — an overcast sky went lilac until B was cut at the top). B and G lifted at the toe: mint-cyan shadows.
**Mixer** — hue: yellow-greens +0.1 (toward fresh green-mint), reds −0.03 (toward pink). Sat: pinks/magenta +0.25 (sakura), yellows and greens −0.25 (pastel), blues −0.1. Lum: yellows, greens, pinks +0.1 (light).
Clarity −8 (soft), saturation −10.

## Use it for
cherry blossom, spring, gardens, temples on a bright overcast day, portraits of women and children, cafés, anything tender.

## Avoid it for
night (it lifts the blacks — the night variant is mild); dramatic skies; dark subjects.

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood spring-haze [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood spring-haze
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["spring-haze"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'pinker' → mixer1 at 330 up to 0.4; 'brighter' → strength 1.2; 'less washed' → master toe to 0.04.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.507 sat 0.20 warm +0.22 tint +0.12
- night: p50 0.160 sat 0.40 warm -0.01 tint -0.07
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)
