# Moody Forest (`moody-forest`)

Dark and moody — Pacific Northwest / Nordic: dense mids, greens folded to deep olive-teal, oranges to brown, cool shadows, nothing bright. Quiet, heavy, earthy.

- **Prompt words:** moody, dark, forest, nordic, earthy, pnw, dramatic, brooding, autumn
- **Palette:** deep olive-teal · brown · slate · no bright colour

## How it is built — the colour lives in the curves and the mixer

**Curves** — master DARKENED through the mids (0.5→0.42, 0.75→0.70) with a lifted toe (0.04) and lowered white (0.92): dense, low-key, matte. B lifted at the toe and cut at the top: cool shadows, neutral-warm lights.
**Mixer** — sat: yellows −0.35, greens −0.45, cyan-greens −0.3, blues −0.3, reds −0.1: almost every colour pulled down. Lum: greens −0.35, yellows −0.15, blues −0.25 (foliage and sky get heavy). Hue: greens +0.08…0.12 (toward teal-olive), yellows −0.06.
Clarity +10, dehaze +8, vignette −22.

## Use it for
forests, temples, rain, overcast, autumn, lone figures, architecture — anywhere quiet and heavy.

## Avoid it for
celebrations, food, bright beaches; a photo already dark (it goes muddy — check p50 ≥ 0.2).

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood moody-forest [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood moody-forest
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["moody-forest"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'darker' → strength 1.2; 'too muddy' → master mids back to 0.45; 'keep some green' → mixer1 at 110 to −0.25.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.289 sat 0.21 warm +0.17 tint +0.09
- night: p50 0.093 sat 0.41 warm +0.33 tint -0.05
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)
