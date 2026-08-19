/*
 *  Arstrobench by arstro — design tokens.
 *
 *  R-G-2: Arstrobench uses cosmo's design language VERBATIM. Rather than copying hex
 *  literals (which would immediately be a second source of truth and would drift), this
 *  header aliases cosmo's token namespaces and the build compiles apps/cosmo/Theme.cpp
 *  into this target. apps/cosmo/Theme.{h,cpp} depends only on artboard_core -- not on
 *  cosmo_core, GTK or the image engine -- so the reuse costs nothing.
 *
 *  The only tokens declared here are the two DERIVED ones a benchmark needs and a photo
 *  editor does not: a per-workload accent for each of the two score cards. Both are built
 *  from cosmo's own palette, never picked by eye.
 */
#pragma once
#include "../cosmo/Theme.h"

namespace arstro
{
namespace arstrobench
{
    // cosmo's tokens, under this app's namespace. One palette, one radius scale, one
    // type ramp, one metric set -- the consistency lock the design system asks for.
    namespace palette = ::arstro::cosmo_v2::palette;
    namespace radius = ::arstro::cosmo_v2::radius;
    namespace font = ::arstro::cosmo_v2::font;

    /** The shared artboard::Theme instance (cosmo's), for controls that read styles. */
    inline const artboard::Theme &sharedTheme() { return ::arstro::cosmo_v2::sharedTheme(); }

    namespace accent
    {
        /** Image workload: the single accent, unchanged -- the image pipeline is the
         *  headline measurement, so it wears the app's primary colour. */
        inline artboard::Color image() { return palette::primary(); }
        /** Signal workload: the same accent rotated toward the palette's success green,
         *  so the two cards are distinguishable at a glance without introducing a third
         *  hue nobody chose. Derived, not invented. */
        inline artboard::Color signal()
        {
            const artboard::Color a = palette::primary(), b = palette::success();
            return artboard::Color{a.r + (b.r - a.r) * 0.55, a.g + (b.g - a.g) * 0.55,
                                   a.b + (b.b - a.b) * 0.55, 1.0};
        }
    }
}
}
