/*
 *  cosmo_touch — line icons for the phone UI, matching the lucide glyphs used in the
 *  Figma design (ref/phone-ui/src/App.tsx). Stroke-only, drawn from the design's exact
 *  24x24 SVG coordinates mapped into `box` via IRenderTarget primitives (Artboard has no
 *  SVG loader). Complements cosmo_v2::icon:: (panelLeft/save/upload/download/trash/
 *  chevrons/rotateCcw) with the glyphs it lacks (back, undo, redo, more, the 5 tab icons).
 */
#pragma once
#include "../../Artboard/src/render/RenderTarget.h"
#include "../../Artboard/src/core/Geometry.h"

namespace arstro
{
namespace cosmo_touch
{
namespace icon
{
    using artboard::Color;
    using artboard::IRenderTarget;
    using artboard::Rect;

    void back(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);   // chevron-left
    void undo(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);
    void redo(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);
    void more(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);    // 3 dots
    void chevronUp(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);
    void plusCircle(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);
    void folderOpen(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);
    void importDown(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);
    void search(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);

    // bottom tool-bar tab glyphs
    void tabBasic(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);  // aperture
    void tabMask(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);   // shield
    void tabCurve(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);  // s-curve
    void tabGrade(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);  // sun/dial
    void tabXform(IRenderTarget &t, const Rect &box, const Color &c, double sw = 1.75);  // play/crop
}
}
}
