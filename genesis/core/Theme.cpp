#include "Theme.h"

namespace genesis
{
    namespace
    {
        constexpr double c(int v) { return v / 255.0; }
    }

    const std::vector<ThemeColor> &themeColors()
    {
        // The Arstro desktop language: near-black neutral surfaces, ONE accent, a restrained
        // grey ramp. An app that themes differently overrides these by passing params.
        static const std::vector<ThemeColor> t = {
            {"background",  c(20),  c(20),  c(20),  1.0},
            {"surface",     c(28),  c(28),  c(30),  1.0},
            {"card",        c(34),  c(34),  c(37),  1.0},
            {"foreground",  c(244), c(244), c(245), 1.0},
            {"muted",       c(150), c(152), c(158), 1.0},
            {"border",      c(58),  c(58),  c(63),  1.0},
            {"accent",      c(58),  c(199), c(255), 1.0},
            {"accentSoft",  c(58),  c(199), c(255), 0.22},
            {"success",     c(78),  c(201), c(139), 1.0},
            {"warning",     c(232), c(178), c(70),  1.0},
            {"danger",      c(233), c(94),  c(94),  1.0},
        };
        return t;
    }

    bool themeColor(const std::string &role, double &r, double &g, double &b, double &a)
    {
        for (const auto &t : themeColors())
            if (t.role == role)
            {
                r = t.r; g = t.g; b = t.b; a = t.a;
                return true;
            }
        return false;
    }
}
