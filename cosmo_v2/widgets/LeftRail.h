/*
 *  cosmo_v2 by arstro — LeftRail: the collapsible preset browser dock.
 *  Pixel spec (App.tsx): a 196px-wide `bg-[#161616]` column, animating to 0
 *  when collapsed (200ms ease-out, matching the design brief's motion note),
 *  with a "PRESETS" header (`px-3 py-2 border-b`, 9px SemiBold uppercase,
 *  tracking 0.13em) above a scrollable PresetTree (`py-1` inset).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "PresetTree.h"
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    class LeftRail : public artboard::Segment
    {
    public:
        static constexpr double kOpenWidth = 196.0;
        static constexpr double kHeaderH = 24.7;  // 9px*~1.3 line height + 2*py-2(6.5)

        LeftRail();

        std::shared_ptr<PresetTree> tree() { return mTree; }
        void scrollBy(double delta) { mTree->scrollBy(delta); }
        void layout();  // call after width/height changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::shared_ptr<PresetTree> mTree;
    };
}
}
