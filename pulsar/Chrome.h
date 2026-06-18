/*
 *  Pulsar by arstro — shared section-panel chrome. Every synth section (filter,
 *  sub, envelope, LFO, macro) draws the same frame: a rounded body, an accent top
 *  stripe, a header sheen (vertical linear gradient), and a faux-bold title. Kept
 *  as one free function so the look stays identical across sections (DRY).
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include <string>

namespace arstro
{
namespace pulsar
{
    void drawPanelChrome(artboard::IRenderTarget &t, double w, double h,
                         const artboard::Color &accent, const std::string &title);
}
}
