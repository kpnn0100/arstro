/*
 *  Genesis — the `theme.*` palette a Gene expression may read.
 *
 *  Shared by the preview interpreter and the code emitter so a component previewed with
 *  `theme.accent` and one compiled with it are the SAME colour — the emitter writes the
 *  literal, so the generated class carries no dependency on Genesis or on a theme lookup.
 */
#pragma once
#include <string>
#include <vector>

namespace genesis
{
    struct ThemeColor
    {
        std::string role;
        double r, g, b, a;
    };

    /** Every role, in inspector order. */
    const std::vector<ThemeColor> &themeColors();
    /** Look up a role; returns false when the name is not a theme role. */
    bool themeColor(const std::string &role, double &r, double &g, double &b, double &a);
}
