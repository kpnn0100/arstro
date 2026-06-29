/*
 *  Cosmo by arstro — shared panel chrome. A calm, professional surface: an elevated
 *  rounded body, a quiet title, and a hairline divider under the header — no accent
 *  stripe or gradient sheen (accent is reserved for interactive controls). One
 *  function so every panel reads identically (DRY).
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <string>

namespace arstro
{
namespace cosmo
{
    void drawPanelChrome(artboard::IRenderTarget &t, double w, double h, const std::string &title);
}
}
