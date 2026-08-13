/*
 *  cosmo_v2 by arstro — text width estimation for centering labels.
 *
 *  Artboard's render HAL has no text-measurement primitive (a documented,
 *  deliberate gap -- see Artboard's architecture.md "Known Architectural
 *  Gaps" and detailed_design.md TextBox notes), so every control that needs
 *  to center or right-align a label estimates its width the same way
 *  Artboard's own TextBox does (sizePx * 0.6 * charCount). This header just
 *  gives that estimate one shared, named home instead of a magic constant
 *  re-derived per widget.
 */
#pragma once
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    inline double estimateTextWidth(const std::string &text, double sizePx)
    {
        return (double)text.size() * sizePx * 0.6;
    }
}
}
