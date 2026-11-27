#include "Fonts.h"
#include "../../../cosmo/EmbeddedFonts.h"

namespace arstro
{
namespace kit
{
    void registerFonts() { cosmo_v2::registerEmbeddedFonts(); }
}
}
