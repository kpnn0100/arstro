/*
 *  Cosmo by arstro — shared panel chrome (rounded body, accent top stripe, header
 *  sheen, faux-bold title). One free function so every panel looks identical (DRY).
 *  Mirrors pulsar/Chrome.h.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <string>

namespace arstro
{
namespace cosmo
{
    void drawPanelChrome(artboard::IRenderTarget &t, double w, double h,
                         const artboard::Color &accent, const std::string &title);
}
}
