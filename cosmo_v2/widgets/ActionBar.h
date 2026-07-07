/*
 *  cosmo_v2 by arstro — ActionBar: the pinned Save/Import/Export row at the
 *  literal bottom of the right column (App.tsx: `px-3 py-2.5 border-t
 *  bg-[#141414]`, three equal-width buttons, Save filled-primary, the other
 *  two outline). Always visible regardless of which tab or how tall its
 *  content is, per the design brief.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>

namespace arstro
{
namespace cosmo_v2
{
    class ActionBar : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 39.0;

        ActionBar();

        std::function<void()> onSave, onImport, onExport;
        void layout();  // call after width changes

        void advance(double nowMs) override;  // drives the button-hover fade

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Btn { double x = 0, w = 0; };
        int btnAt(const artboard::Point &local) const;

        Btn mBtns[3];  // Save, Import, Export
        int mHoverIndex = -1;                       // button under the pointer
        bool mHoverPrev = false;
        artboard::AnimatedProperty mHoverAmt{0.0};  // hover fade (R-G-1)
    };
}
}
