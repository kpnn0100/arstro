#include "CompareView.h"
#include "../CosmoTheme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    CompareView::CompareView(const Color &accent) : mAccent(accent) {}

    void CompareView::setBefore(const uint8_t *rgba, int w, int h)
    {
        if (!rgba || w <= 0 || h <= 0) { mPixels.clear(); mW = mH = 0; return; }
        mPixels.assign(rgba, rgba + (size_t)w * h * 4);
        mW = w; mH = h; mDirty = true;
    }

    void CompareView::setSplitFrom(const Point &local)
    {
        if (mFitted.w > 0) mSplit = std::clamp((local.x - mFitted.x) / mFitted.w, 0.0, 1.0);
    }

    bool CompareView::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mActive) return false;
        switch (g.type)
        {
        case Gesture::Type::Down:
        case Gesture::Type::DragStart:
        case Gesture::Type::Drag:
            setSplitFrom(local);
            return true;
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
            return true;
        default:
            return false;
        }
    }

    void CompareView::onPaint(IRenderTarget &t) const
    {
        if (!mActive || mW <= 0 || mFitted.w <= 0) return;
        if (mLastTarget != &t || mId == 0) { mId = t.registerImage(mPixels.data(), mW, mH); mLastTarget = &t; mDirty = false; }
        else if (mDirty) { t.updateImage(mId, mPixels.data(), mW, mH); mDirty = false; }

        const double sx = mFitted.x + mSplit * mFitted.w;
        t.save();
        t.clipRect(mFitted.x, mFitted.y, mSplit * mFitted.w, mFitted.h);  // left band = before
        t.drawImage(mId, mFitted);
        t.restore();

        t.setStroke(mAccent, 2.0);
        t.beginPath(); t.moveTo(sx, mFitted.y); t.lineTo(sx, mFitted.y + mFitted.h); t.strokePath();

        t.setFill(palette::ink());
        t.drawText("BEFORE", mFitted.x + 8, mFitted.y + 18, 11.0);
        t.drawText("AFTER", mFitted.x + mFitted.w - 44, mFitted.y + 18, 11.0);
    }
}
}
