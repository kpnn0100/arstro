# Golden baselines — android_theme sample sheet (M2.5)

`samplesheet_{dark,light}.png` are L1 golden references for the design-token sample sheet
(`theme/SampleSheet.h`), rendered headlessly via CairoTarget. Regenerate with
`../update-goldens.sh` after any theme-token change, and eyeball the diff.

> ⚠ **PROVISIONAL — text is DejaVu Sans, not Roboto.** The Roboto / Roboto Flex TTFs are not yet
> vendored (see `../../assets/fonts/README.md`), so every text run here falls back to the adapter's
> generic sans. The colours, radii, icons, and masks are final; only the letterforms will change.
> **Regenerate these baselines once the fonts are added** — the sizes/layout stay, the glyphs correct.
