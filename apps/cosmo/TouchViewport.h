/*
 *  cosmo_v2 by arstro — where the TOUCH shell is drawn inside a desktop window (R-TOUCH-6).
 *
 *  Pure geometry, in its own header for one reason: the rule has to be the same in the GTK host
 *  (which draws the shell and translates input into it) and in the shot renderer (which has to be
 *  able to show what the desktop window actually looks like in touch mode). A rule that lives only
 *  in linux_main.cpp cannot be photographed, and a screenshot of a layout nobody can reproduce is
 *  not evidence.
 */
#pragma once
#include "../../core/Artboard/include/artboard/artboard.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    /** The widest the phone layout is designed for (the brief holds at 360-430 dp). */
    inline constexpr double kTouchColumnW = 430.0;

    /**
     *  A portrait-ish window gets the whole thing. A window WIDER than it is tall gets a centred
     *  portrait column instead, because the touch shell has no landscape layout yet (R-TOUCH-3,
     *  ledger task T2): drawing it into a 1600x1000 box today stacks the tray, the section chips,
     *  the action bar and the tool bar into one band (D-38). A deliberate letterbox is honest
     *  about that where a broken layout would not be, and when the two-pane landscape layout
     *  lands this collapses to "fill the window".
     */
    inline artboard::Rect touchViewport(double windowW, double windowH)
    {
        if (windowW <= 0 || windowH <= 0) return artboard::Rect{0, 0, 1, 1};
        if (windowH >= windowW) return artboard::Rect{0, 0, windowW, windowH};
        const double cw = std::min(windowW, kTouchColumnW);
        return artboard::Rect{(windowW - cw) * 0.5, 0, cw, windowH};
    }
}
}
