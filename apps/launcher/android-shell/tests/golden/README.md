# Golden baselines (M2.5 sample sheet, M3.2 status bar)

L1 golden references rendered headlessly via CairoTarget; regenerate all with
`../update-goldens.sh` after any theme/surface change, and eyeball the diff.

- `samplesheet_{dark,light}.png` — the design-token sample sheet (`theme/SampleSheet.h`, M2.5).
- `statusbar_{dark,light,charging,nowifi,dnd}.png` — the status bar in each state (`shell/StatusBar.h`,
  M3.2): dark = white glyphs over wallpaper, light = dark glyphs on a light bar, charging = battery
  bolt, nowifi = dimmed signal, dnd = bluetooth + moon icons.

> ⚠ **PROVISIONAL — text is DejaVu Sans, not Roboto.** The Roboto / Roboto Flex TTFs are not yet
> vendored (see `../../assets/fonts/README.md`), so every text run in these PNGs (sample-sheet labels;
> the status bar's clock + battery %) falls back to the adapter's generic sans. The colours, radii,
> icons, battery/wifi glyphs, and masks are final; only the letterforms will change.
> **Regenerate these baselines once the fonts are added** — the sizes/layout stay, the glyphs correct.
