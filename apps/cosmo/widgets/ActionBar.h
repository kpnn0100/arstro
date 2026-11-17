/*
 *  cosmo_v2 by arstro — ActionBar: the pinned PRESET row at the literal bottom
 *  of the right column (App.tsx: `px-3 py-2.5 border-t bg-[#141414]`, three
 *  equal-width buttons). Always visible regardless of which tab or how tall its
 *  content is, per the design brief.
 *
 *  R-UX-4 amends the mock: it read "Save / Import / Export" with Save filled in
 *  the accent, which made a preset action look like the project's save and the
 *  photo's export. The labels now name what they act on - "Save preset / Import
 *  preset / Export preset" - and all three are outline chips; the accent fill
 *  belongs to the top bar's Export, the photo's.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "HoverFade.h"
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
        /** Button `i`'s box (0 Save preset, 1 Import preset, 2 Export preset) - for a test's click. */
        artboard::Rect buttonRect(int i) const;

        void advance(double nowMs) override;  // drives the button-hover fade

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Btn { double x = 0, w = 0; };
        int btnAt(const artboard::Point &local) const;

        Btn mBtns[3];  // Save, Import, Export
        HoverFade mHover;  // per-button hover cross-fade (R-G-3)
    };
}
}
