# Wong Kar-wai (`wong-kar-wai`)

Hong Kong heat — Chungking Express, In the Mood for Love: green-yellow mids, saturated red, amber light, green-black shadows, heavy grain. Romantic, sweaty, nostalgic.

- **Prompt words:** wong kar-wai, wong kar wai, chungking, in the mood for love, hong kong, romantic night, green tint, 90s
- **Palette:** green-yellow mids · saturated red · amber light · green-black

## How it is built — the colour lives in the curves and the mixer

**Curves** — G LIFTED through the mids (0.4→0.44) and B pulled down through mids and highs (0.4→0.36, 1→0.88): the whole frame leans green-yellow, the signature cast of Christopher Doyle's stock and fluorescent Hong Kong. R slightly down in the mids so the green wins. Master: small toe lift.
**Mixer** — sat: reds +0.3…0.35 (the red dress, the neon, the lantern), blues −0.2. Lum: reds −0.1 (dense, rich red), blues −0.2. Hue: greens −0.05 (toward yellow-green), yellows +0.04 (toward lime).
**Wheel** — shadows 150°/4 (green-black). Heavy grain (16-18, size 28-30).

## Use it for
night, rain, neon, food stalls, couples, lonely figures, red objects — romance and nostalgia.

## Avoid it for
clean daylight landscapes (they look sick-green); weddings, products.

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood wong-kar-wai [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood wong-kar-wai
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["wong-kar-wai"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'less green' → strength 0.6 or curveG mids back to 0.42; 'redder' → mixer1 at 0 up to 0.45.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.384 sat 0.49 warm +0.57 tint +0.40
- night: p50 0.099 sat 0.71 warm +0.45 tint +0.42
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)
