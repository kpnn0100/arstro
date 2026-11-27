/*
 *  arstro kit — the typeface, compiled into the binary.
 *
 *  The same faces and the same mechanism as cosmo (its R-FONT-1): no font file on the device, no
 *  Fontconfig in the text path, so a 5" board with nothing installed draws the same glyphs as a
 *  desktop. Call once before the first frame.
 */
#pragma once
namespace arstro
{
namespace kit
{
    void registerFonts();
}
}
