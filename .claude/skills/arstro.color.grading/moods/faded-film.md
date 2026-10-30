# Faded Film (`faded-film`)

A print found in a drawer — Portra 400 left in the sun: lifted navy blacks, creamy warm highlights, pastel greens and blues, soft. Nostalgic, quiet, travel diary.

- **Prompt words:** faded, film, vintage, nostalgic, portra, matte, analog, memory, diary, retro
- **Palette:** navy-lifted blacks · cream highlights · pastel green and blue

## How it is built — the colour lives in the curves and the mixer

**Curves** — master toe LIFTED (0→0.07) and shoulder dropped (1→0.93): nothing is black, nothing is white — the faded print. B lifted at the toe (0.06) but cut at the top (0.88): blue-navy blacks, cream-yellow highlights — the classic aged-print crossover. R slightly up in the mids.
**Mixer** — hue: greens −0.08 (toward yellow-olive), blues −0.06 (toward cyan). Sat: greens −0.3, blues −0.25, magenta −0.2 (pastel). Lum: greens +0.05, blues +0.08 (lighter, airy).
Saturation −12, grain 10-12.

## Use it for
travel diaries, everyday scenes, memories, portraits outdoors, a set that should feel 'found in a drawer'.

## Avoid it for
night scenes that need black (lifted blacks look grey at night — the night variant keeps a smaller lift); high-energy subjects.

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood faded-film [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood faded-film
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["faded-film"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'more faded' → master toe 0.1, shoulder 0.9; 'warmer' → curveB top to 0.85; 'less washed out' → strength 0.6.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.433 sat 0.23 warm +0.40 tint +0.10
- night: p50 0.151 sat 0.32 warm +0.09 tint -0.04
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)
