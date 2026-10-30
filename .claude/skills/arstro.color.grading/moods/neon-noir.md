# Neon Noir (`neon-noir`)

Tokyo after rain, Blade Runner 2049 / Cinestill 800T: teal-black shadows, magenta and cyan light, sodium yellow bent to amber, greens gone to cyan, hard contrast.

- **Prompt words:** neon, cyberpunk, blade runner, noir, tokyo night, cinestill, night city, futuristic
- **Palette:** teal-black · magenta/pink · cyan · amber (sodium)

## How it is built — the colour lives in the curves and the mixer

**Curves** — R cut in the shadows (0.25→0.20) and B lifted from the toe (0→0.07-0.08, 0.3→0.35): the blacks go teal-navy without getting lighter. G lifted a hair at the toe (teal, not blue) and CUT at the top (0.75→0.72, 1→0.95 at night): highlights lean magenta — the neon, the wet street. Master: deep toe (0.15→0.08), rolled shoulder.
**Mixer** — hue: greens +0.2 (≈ +36°, to cyan), yellows −0.08 (to amber), reds −0.05 (toward crimson-pink), blues −0.06 (toward cyan). Sat: greens −0.35, magenta +0.25…0.3, cyan +0.2, reds +0.15. Lum: greens −0.25, blues −0.2 (dense), magenta +0.1 (glow).

## Use it for
night streets, signs, wet asphalt, rain, taxis, convenience-store light, teamLab-like installations, anything lit by LEDs. Day: modern architecture, glass, overcast city — reads as a cold 'cyberpunk day'.

## Avoid it for
sunny nature and skin close-ups in daylight (skin goes pink-grey); a warm golden scene (it will fight the light).

## Apply

```bash
python3 scripts/mood.py apply <project.cmp> --mood neon-noir [--strength 0.8] [--save-as <copy.cmp>]
# or mix: --mood spring-haze --night-mood neon-noir
```

By hand (MCP): the group's line is in `scripts/moods.py` (`MOODS["neon-noir"]["day" | "night"]`) — `cosmo_run "select <group>\nset <that line>"`.

## Tuning by prompt
'more neon' → strength 1.2; 'less pink' → raise curveG's top back toward 1 (day 0.975 already) ; 'bluer blacks' → curveB toe +0.02; 'keep the yellow signs yellow' → mixer0 at 60 back to 0.

## Fingerprint (measured, Tokyo set, means)
- day: p50 0.338 sat 0.33 warm +0.15 tint +0.02
- night: p50 0.084 sat 0.71 warm -0.22 tint -0.32
- baseline (corrected only): day p50 0.391 sat 0.34 warm +0.17 tint +0.16 · night p50 0.084 sat 0.60 warm +0.10 tint −0.05

If `cosmo_analyze` after applying is nowhere near this, the mood did not land (a stale group, a reset
that failed) — check `cosmo_state` before judging the look.

## Approver's verdict
(pending)

Grade Lab 2026-10-30: BAD on the woman; WON on Fuji ("more balance" than Teal & Orange) and on the garden pond ("really create a story") — nature with no true colour to keep.
