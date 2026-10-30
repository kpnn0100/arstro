# Cosmo's controls, MEASURED — they are not Lightroom's

Every number below was measured 2026-10-30 through `cosmo-cc mcp` (`cosmo_run` a `set`, then
`cosmo_analyze`) on `_DSF6609.RAF` (overcast castle moat, Fuji X-S10, ISO 320) from a neutral start:
p5 0.153, p50 0.377, p95 0.942, sd 0.260, warmth +0.227, tint +0.195, sat 0.245, clipH 1.4 %.
Re-measure when the engine changes: `python3 scripts/calibrate.py <project.cmp> <node> "temp=5500" "contrast=30" …`
(reset, set one key, analyze, print).

| set | p5 | p50 | p95 | effect worth knowing |
|---|---|---|---|---|
| `temp=5500` | — | — | — | warmth −0.19 (+0.040). **Higher temp = WARMER result.** ≈ 0.19 warmth per 1000 K near 6500 |
| `temp=7500` / `9000` | — | — | — | warmth +0.416 / +0.705; clipH rises (warm channel clips) |
| `tint=+25` | — | — | — | tint −0.073 → **positive tint = MAGENTA**, ≈ −0.003 per unit; a RAW's green needs +30…+50 |
| `exposure=0.5` | 0.185 | 0.444 | 0.999 | clipH 1.4 % → **15.5 %**: always pair exposure with `highlights` |
| `contrast=30` | **0.001** | 0.347 | 0.998 | brutal: blacks crushed, clipH 10 %. Use 0–8; shape with the curve instead |
| `highlights=-50` | 0.153 | 0.377 | 0.834 | clipH → 0: the right tool to protect a sky, −20…−90 |
| `shadows=50` | **0.555** | 0.612 | 0.942 | flattens everything, saturation 0.245 → 0.067. Use ≤ 15 |
| `blacks=-30` | 0.001 | 0.175 | — | drags the MEDIAN down by half. Use single digits |
| `whites=20` | — | — | 0.968 | mild |
| `saturation=-20` | — | — | — | sat 0.245 → 0.199 |
| `vibrance=20` | — | — | — | sat 0.245 → 0.345 — strong; 6–14 is plenty |
| `grade0=200,20,0` (shadows) | 0.163 | — | — | shadow warmth +0.93 → −1.69: **a shadow wheel at 20 is a lot** |
| `grade0=200,40,0` | 0.224 | — | — | shadow warmth −18.8, blacks LIFTED: additive in linear light. Keep 3–8 |
| `grade0=0,0,-20` (shadow lum) | 0.001 | 0.272 | — | crushes; avoid the shadow lum |
| `grade2=35,20,0` / `35,40,0` | — | — | — | highlight warmth +0.20 → +0.24 / +0.37: gentle, 10–30 is normal |
| `grade1=30,20,0` (mids) | — | — | — | +0.03 overall warmth: gentle |
| `curve=0,0.04;0.25,0.2;0.75,0.82;1,0.97` | 0.138 | 0.358 | 0.935 | sd +0.017, clipH → 0: a film S with toe and shoulder |
| `curve=0,0;0.5,0.6;1,1` | 0.184 | 0.453 | 0.953 | brightens mids without clipping — the gentle exposure |
| `dehaze=20` | 0.144 | 0.358 | 0.936 | a little contrast and saturation |
| `clarity=20` | — | — | — | no global change (local contrast) — judge by eye |
| `lensVignette=-30` | | | | mean L 0.449 → 0.423: **negative darkens the corners** |

Night frame `DSCF5186.RAF`, R/G/B curves `curveR=0,0;0.3,0.26;0.7,0.72;1,1 curveG=0,0.01;0.3,0.31;1,0.99
curveB=0,0.04;0.3,0.34;0.75,0.72;1,0.95`: shadow warmth **+1.43 → −0.16** (teal) for a p5 rise of only
0.002 → 0.012, highlights warmed (−0.30 → −0.11). This is how to split-tone a night.

## Pitfalls

- **A wheel's hue ADDS across group levels (D-68).** Group `grade2=28,…` + photo `grade2=35,…` renders
  hue 63° (yellow). Put wheels on the look group ONLY. Everything else composes sensibly: scalars
  add, temp adds its offset from 6500, curves and mixer add as deviation from identity, crop and
  `curveLog` follow the photo.
- **The shadow wheel and `shadows` lift blacks** — they add in linear light. A night grade done with
  them goes milky; use R/G/B curves.
- **`exposure` clips fast** in Cosmo's flat RAW rendering (no base curve): the before render is low
  contrast and every photo needs an S-curve.
- **Node ids are tree positions**, renumbered when a project with groups is reopened. Key photos by
  name across sessions.
- **Groups**: `select a`, `select b add`, …, `group new "Name"` → the new group is `selectedNode`.
  `bypass <group> on|off` toggles a look for comparison (and for normalising with the look off).
- **`set` of an unknown key** is refused today on the MCP path only when the tool argument is wrong;
  inside a line, a misspelled param key may still be accepted (cosmo D-59) — check `params.changed`
  names the key you meant.
- **Measure, then look.** `analyze` numbers and the eye disagree in two known places: a perfectly
  neutral overcast sky looks lilac next to warm wood (target tint +0.04, not 0); a night frame's
  `keyEv` is dominated by black (use p50 and your eyes).
