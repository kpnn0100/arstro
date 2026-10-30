# Spring Haze (`spring-haze`)

Japanese airy pastel — sakura season, Hamaguchi daylight: bright and low-contrast, mint-cyan shadows, pink-cream highlights, greens to mint, pinks lifted. Light, tender, calm.

- **Prompt words:** airy, pastel, soft, spring, sakura, cherry blossom, japanese, light, dreamy, kawaii, bright
- **Palette:** mint · cream-pink · pale sky · soft — at night a pastel night: mint-navy haze, pink light

## How it is built — the colour lives in the curves and the mixer

**Curves (day)** — master LIFTED everywhere (toe 0.08, mids 0.5→0.58, top 0.97): bright, low contrast.
R lifted in the upper mids (0.85→0.88) with G (0.86) and B (0.84) below it: cream-pink highlights
(R ≥ G > B — an overcast sky went LILAC until B was cut at the top). G and B lifted at the toe: mint shadows.
**Curves (night) — a pastel night**, so the set stays one mood: master toe 0.06 and mids 0.5→0.55 (the
night opens to a haze, p50 ≈ 0.17); G toe 0.03 and B toe 0.07 = mint-navy blacks; R's shoulder 0.9 above
G 0.86 and B 0.85 = pink light. The first night variant (toe 0.05, no channel split) left the night
photos looking merely corrected and split the set in two.
**Mixer** — hue: olive foliage moved, not only true green — decoded RAW greens sit at 60–80°, so 60 +0.05
and 80 +0.10 (≈ +9° / +18°) turn olive into fresh green-mint, while 40° is pinned at 0 so skin and wood
stay put; reds −0.03 (toward pink). Sat: sakura pinks +0.35 at 330 (+0.2 at 350), yellows −0.35, greens
−0.2…−0.25 (pastel), blues −0.1; at night sodium orange −0.1. Lum: yellows, greens, pinks +0.1…+0.12.
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
- day: p50 0.511 sat 0.20 warm +0.21 tint +0.14
- night: p50 0.167 sat 0.38 warm +0.02 tint -0.07
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
2026-10-30 — the approver picked this mood from the nine (*"so try spring haze for me"*). Applied to
`tokyo.cmp` itself (the cinematic version kept as `~/photo/tokyo/cinematic-grade/tokyo.cinematic.cmp`);
2048 px JPEGs in `~/photo/tokyo/spring-haze/jpg/`. Verdict on the result: (pending).
