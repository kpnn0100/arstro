# Bundled fonts (app-private)

`theme/Fonts.cpp` registers these TTFs with Fontconfig for the shell process only
(`FcConfigAppFontAddFile`), so the type ramp's family names resolve without a system install —
the same pattern cosmo uses for DM Sans. **The TTF files themselves are not yet committed**; drop
them here and the family names below start resolving to real glyphs. Until then text falls back to
the adapter's generic sans (same size/layout, different letterforms) and `registerBundledFonts()`
logs a "font not registered" line per missing file.

Expected layout (paths must match `theme/Fonts.cpp`):

```
assets/fonts/
  Roboto/
    Roboto-Regular.ttf      -> family "Roboto"
    Roboto-Medium.ttf       -> family "Roboto Medium"   (FR-22: static weight = own family)
  RobotoFlex/
    RobotoFlex-Regular.ttf  -> family "Roboto Flex"     (variable, for the large clock)
```

**License:** Roboto and Roboto Flex are Apache-2.0 — redistributable. When the TTFs are added,
also add their `LICENSE`/`NOTICE` here and reference them from the packaging `licenses/` dir (M9).

Source: fonts.google.com/specimen/Roboto and /specimen/Roboto+Flex (or the `fonts-roboto` /
`google-roboto-fonts` distro packages).
