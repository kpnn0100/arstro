/*
 *  Cosmo by arstro — theme factory. Pulsar-style dark UI: knobs with accent value
 *  arcs, dark surfaces, accent-striped panels. One accent per call (mirrors
 *  pulsar's makePulsarTheme).
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"

namespace arstro
{
namespace cosmo
{
    artboard::Theme makeCosmoTheme(const artboard::Color &accent);

    // Shared palette (so panels and the app chrome agree).
    namespace palette
    {
        inline artboard::Color bg() { return artboard::Color::hex(0x0a0c11); }
        inline artboard::Color panel() { return artboard::Color::hex(0x14161c); }
        inline artboard::Color border() { return artboard::Color::hex(0x2a3040); }
        inline artboard::Color ink() { return artboard::Color::hex(0xe6e9ef); }
        inline artboard::Color muted() { return artboard::Color::hex(0x8b94a7); }
        inline artboard::Color surface() { return artboard::Color::hex(0x222838); }
        inline artboard::Color accent() { return artboard::Color::hex(0xf2b24a); } // amber
    }
}
}
