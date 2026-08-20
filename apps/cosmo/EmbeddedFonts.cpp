/*
 *  cosmo_v2 by arstro — hand the embedded faces to the render adapter (R-FONT-1).
 *
 *  The data half of this lives in a generated translation unit (cmake/embed_fonts.cmake);
 *  this is the only place that pairs a family NAME with those bytes, which is the pairing
 *  Theme.h's `font::` accessors depend on. Registration is idempotent and cheap (five
 *  FT_New_Memory_Face calls), so a host that is not sure whether it has run may just run it.
 */
#include "EmbeddedFonts.h"
#include "../../core/Artboard/src/adapter/native/CairoTarget.h"

namespace arstro
{
namespace cosmo_v2
{
    void registerEmbeddedFonts()
    {
#ifdef ARTBOARD_CAIRO_FT
        for (const EmbeddedFont &f : embeddedFonts())
            artboard::CairoTarget::registerFontMemory(f.family, f.bytes, f.size);
#endif
        // Without ARTBOARD_CAIRO_FT there is nowhere to put a face: CairoTarget's text path is
        // then the toy API, which can only name a family the host's font database already knows.
        // A build in that shape draws in the system sans, and it says so here rather than
        // pretending the call did something.
    }
}
}
