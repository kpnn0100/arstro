# Blue Melancholy (`blue-melancholy`)

Kieslowski's Blue / a winter evening: cold, nearly monochrome except the blues, navy lifted blacks, pale cold highlights, skin kept but quiet. Lonely, still, sad.

- **Prompt words:** blue, cold, melancholy, sad, lonely, winter, cool, blue hour, rainy, kieslowski
- **Palette:** cold blue · slate · desaturated skin · navy

## How it is built — the colour lives in the curves and the mixer

**Curves** — R cut in mids and top (0.5→0.46, 1→0.95), B LIFTED (toe 0.08, mids 0.55): blue through the tones, navy blacks, pale cold highlights. Master: soft, low contrast, lowered white (0.94).
**Mixer** — sat: every warm hue down (reds −0.3, orange −0.25, yellow −0.4, green −0.4), blue +0.1…0.15: near-monochrome with blue left in. Hue: greens +0.15 (toward cyan).
Group temp 6000 (−500 K).

## Use it for
rain, blue hour, winter, lonely figures, empty streets, the sea — sadness, stillness.

## Avoid it for
anything meant to feel warm or alive; people who must look healthy.

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood blue-melancholy [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood blue-melancholy
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["blue-melancholy"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'colder' → curveB mids 0.58; 'some warmth in the skin' → mixer1 at 30 to −0.1.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.345 sat 0.19 warm -0.16 tint +0.02
- night: p50 0.111 sat 0.47 warm -0.16 tint -0.13
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)
