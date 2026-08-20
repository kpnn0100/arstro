/*
 *  cosmo_v2 by arstro — the typeface, compiled into the binary (R-FONT-1).
 *
 *  cosmo used to register its vendored TTFs with Fontconfig from a path baked in at build time,
 *  which meant the text depended on a directory existing next to the source tree and on
 *  Fontconfig resolving the family the same way on every host. Both are things that differ
 *  between machines, and the app's own type is not a thing that may differ between machines.
 *
 *  So the faces are generated into a C++ array at build time (cmake/embed_fonts.cmake) and handed
 *  straight to the render adapter (`CairoTarget::registerFontMemory`, Artboard FR-22a). The
 *  binary needs no font file, no Fontconfig and no system-installed family, and draws the same
 *  glyphs on Linux and Windows.
 *
 *  The `family` strings are the names `Theme.h`'s `font::` accessors ask `drawText` for — the two
 *  lists have to agree, and the registration is the only place that pairs a name with bytes.
 */
#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    struct EmbeddedFont
    {
        const char *family;          // the name drawText asks for
        const unsigned char *bytes;  // static storage: outlives every face built from it
        std::size_t size;
    };

    /** Every face compiled into this binary, in registration order. */
    const std::vector<EmbeddedFont> &embeddedFonts();

    /** Register all of them with the native render adapter. Call once, before the first frame. */
    void registerEmbeddedFonts();
}
}
