/*
 *  arstro-android-shell — Fonts (M2.2): app-private font registration.
 *
 *  Registers the bundled Roboto / Roboto Flex TTFs with Fontconfig for THIS process only
 *  (FcConfigAppFontAddFile), so the type-ramp family names ("Roboto", "Roboto Medium",
 *  "Roboto Flex") resolve without a system install — the same pattern cosmo uses for DM Sans.
 *  No-op-safe: if a TTF is missing it logs and continues, and text falls back to the adapter's
 *  generic sans. Call once at startup, before rendering.
 */
#pragma once

namespace arstro
{
namespace androidshell
{
    void registerBundledFonts();
}
} // namespace arstro
