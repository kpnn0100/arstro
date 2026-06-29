/*
 *  Cosmo by arstro — CompareView: the before/after split inspector. The ImageView
 *  underneath draws the edited "after"; when active, this overlay draws the "before"
 *  image (same framing, no tonal edits) clipped to the LEFT of a draggable split
 *  line, so sliding the divider reveals the difference. It owns + registers the
 *  before pixels like ImageView and uses the IRenderTarget clipRect primitive.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <cstdint>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class CompareView : public artboard::Segment
    {
    public:
        explicit CompareView(const artboard::Color &accent);

        void setBefore(const uint8_t *rgba, int w, int h);
        void setFittedRect(const artboard::Rect &localFitted) { mFitted = localFitted; }
        void setActive(bool a) { mActive = a; }
        bool active() const { return mActive; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mActive; }

    private:
        void setSplitFrom(const artboard::Point &local);

        artboard::Color mAccent;
        std::vector<uint8_t> mPixels;
        int mW = 0, mH = 0;
        mutable int mId = 0;
        mutable bool mDirty = false;
        mutable artboard::IRenderTarget *mLastTarget = nullptr;
        artboard::Rect mFitted{0, 0, 1, 1};
        double mSplit = 0.5;
        bool mActive = false;
    };
}
}
