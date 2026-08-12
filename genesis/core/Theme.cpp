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
        // The Arstro desktop palette, matching cosmo's tokens so a component authored here
        // and a control drawn by the app agree on what "accent" means.
        static const std::vector<ThemeColor> t = {
            {"background",         c(0x14), c(0x14), c(0x14), 1.0},
            {"foreground",         c(0xDB), c(0xDB), c(0xDB), 1.0},
            {"card",               c(0x1C), c(0x1C), c(0x1C), 1.0},
            {"popover",            c(0x22), c(0x22), c(0x22), 1.0},
            {"accent",             c(0x4F), c(0x7E), c(0xF7), 1.0},
            {"primary",            c(0x4F), c(0x7E), c(0xF7), 1.0},
            {"primaryForeground",  1.0,     1.0,     1.0,     1.0},
            {"accentSoft",         c(0x4F), c(0x7E), c(0xF7), 0.22},
            {"secondary",          c(0x25), c(0x25), c(0x25), 1.0},
            {"secondaryForeground",c(0xAA), c(0xAA), c(0xAA), 1.0},
            {"surface",            c(0x25), c(0x25), c(0x25), 1.0},
            {"muted",              c(0x19), c(0x19), c(0x19), 1.0},
            {"mutedForeground",    c(0x63), c(0x63), c(0x63), 1.0},
            {"border",             1.0,     1.0,     1.0,     0.072},
            {"success",            c(0x3F), c(0xB9), c(0x50), 1.0},
            {"warning",            c(0xE0), c(0xA6), c(0x4B), 1.0},
            {"danger",             c(0xE5), c(0x53), c(0x4B), 1.0},
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
