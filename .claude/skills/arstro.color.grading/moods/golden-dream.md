# Golden Dream (`golden-dream`)

Every hour is golden hour: amber light through the whole frame, orange mids, greens turned to gold, blues pushed back to a soft teal. Warm, happy, summer memory.

- **Prompt words:** golden, warm, sunset, amber, summer, glow, honey, cozy
- **Palette:** amber · gold · warm brown · soft teal blue

## How it is built — the colour lives in the curves and the mixer

**Curves** — R LIFTED in the mids (0.5→0.55), B CUT in mids and highs (0.5→0.44, 1→0.85), G slightly down at the top: the whole tonal range turns amber, strongest in the light. B lifted at the toe so shadows stay faintly teal (depth).
**Mixer** — hue: greens −0.12 (to gold), yellows −0.06 (to amber), blues −0.05. Sat: orange +0.15, yellow +0.1, greens −0.15, blues −0.3. Lum: orange +0.06, blues −0.2.
Group temp 7100 (+600 K on every photo).

## Use it for
summer, sunsets, warm interiors, food, family, beaches, autumn leaves — happiness and nostalgia.

## Avoid it for
snow, blue-hour, a cold story; overcast skies at full strength (they go yellow) — use strength 0.7.

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood golden-dream [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood golden-dream
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["golden-dream"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'less yellow' → strength 0.7 or temp 6800; 'more orange than yellow' → mixer0 at 60 to −0.1.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.423 sat 0.44 warm +0.84 tint +0.20
- night: p50 0.106 sat 0.49 warm +0.31 tint -0.05
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)

Grade Lab 2026-10-30: BAD on the woman; WON on the jellyfish — "the color is really impressive, eye catching" (amber subject on blue water).
