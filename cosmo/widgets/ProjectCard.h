/*
 *  cosmo_v2 by arstro — ProjectCard: the single source of truth for how a recent-
 *  project card CHROME is drawn (background + border, thumbnail placeholder, Edited
 *  badge, name, "photos · size" meta line, right-aligned date). Both the home grid
 *  (`HomeScreen`) and the open-project loading screen (`App`, R-LOADING) render the
 *  same card through this, so "the item flying to centre" is visually identical to
 *  the item in the grid.
 *
 *  The thumbnail IMAGE is drawn by the caller (a child `ImageView` in the grid; an
 *  ImageView blitted imperatively on the loading screen), so each caller keeps its
 *  own z-order and clipping — this function draws only the chrome.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../Theme.h"
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    /** The meta band under a card's 16:9 thumbnail (name + row). Shared so the loading
     *  screen sizes a card exactly like the grid does. */
    namespace projectcard { constexpr double kMetaH = 46.0; }

    struct ProjectCardData
    {
        std::string name;
        std::string photos;  // "10 photos"
        std::string size;    // "2.4 GB" ("" hides the size dot)
        std::string date;    // "2h ago"
        bool edited = false;
    };

    /** Draw the card chrome into `card` (grid/screen space). `hover` in [0,1] brightens
     *  the surface and lifts the border toward the accent; `alpha` in [0,1] fades the
     *  whole card (1 = opaque). The thumbnail 16:9 area is a placeholder fill — the
     *  caller draws the real thumbnail over it. */
    void drawProjectCardChrome(artboard::IRenderTarget &t, const artboard::Rect &card,
                               const ProjectCardData &d, double hover, double alpha = 1.0);
}
}
