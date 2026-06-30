#include "Filmstrip.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 8.0;
        constexpr double kCellW = 96.0;
        constexpr double kCellH = 64.0;
        constexpr double kGap = 8.0;
        constexpr double kInset = 3.0;
        double cellX(int i) { return kPad + i * (kCellW + kGap); }
    }

    Filmstrip::Filmstrip(const Color &accent) : mAccent(accent)
    {
        height.set(kCellH + 2 * kPad);
    }

    void Filmstrip::addThumb(const uint8_t *rgba, int w, int h)
    {
        auto v = std::make_shared<ImageView>();
        v->setImage(rgba, w, h);
        v->width.set(kCellW - 2 * kInset);
        v->height.set(kCellH - 2 * kInset);
        v->x.set(cellX((int)mThumbs.size()) + kInset);
        v->y.set(kPad + kInset);
        mThumbs.push_back(v);
        addChild(v);
        if (mSelected < 0)
        {
            mSelected = 0;
            mSelection = {0};
            mAnchor = 0;
        }
    }

    void Filmstrip::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::panel(),
                        Paint::filledStroked(palette::panel(), palette::line(), 1.0));
        // per-cell dark slot, and an accent border on the selected one
        for (int i = 0; i < (int)mThumbs.size(); ++i)
        {
            const Rect cell{cellX(i), kPad, kCellW, kCellH};
            drawRoundedRect(t, cell, radius::control(), Paint::filled(palette::bg()));
            const bool inSel = std::find(mSelection.begin(), mSelection.end(), i) != mSelection.end();
            if (i == mSelected)
                drawRoundedRect(t, cell, radius::control(), Paint::stroked(mAccent, 2.0));
            else if (inSel)  // part of the multi-selection (sync target)
                drawRoundedRect(t, cell, radius::control(), Paint::stroked(Color{mAccent.r, mAccent.g, mAccent.b, 0.55f}, 1.5));
        }
    }

    bool Filmstrip::handleGesture(const Gesture &g, const Point &localPoint)
    {
        if (g.type == Gesture::Type::Click)
        {
            for (int i = 0; i < (int)mThumbs.size(); ++i)
            {
                const double x0 = cellX(i);
                if (localPoint.x >= x0 && localPoint.x <= x0 + kCellW &&
                    localPoint.y >= kPad && localPoint.y <= kPad + kCellH)
                {
                    if (g.shift && mAnchor >= 0)  // Shift: select the range from the anchor
                    {
                        mSelection.clear();
                        const int lo = mAnchor < i ? mAnchor : i, hi = mAnchor < i ? i : mAnchor;
                        for (int k = lo; k <= hi; ++k) mSelection.push_back(k);
                        mSelected = i;
                    }
                    else if (g.ctrl || g.alt)  // Ctrl/Cmd (or Alt): add/remove from the selection
                    {
                        auto it = std::find(mSelection.begin(), mSelection.end(), i);
                        if (it != mSelection.end()) { if (mSelection.size() > 1) mSelection.erase(it); }
                        else mSelection.push_back(i);
                        mSelected = i; mAnchor = i;
                    }
                    else  // plain click: single-select
                    {
                        mSelected = i;
                        mSelection = {i};
                        mAnchor = i;
                        if (onSelect) onSelect(i);
                    }
                    return true;
                }
            }
        }
        return Segment::handleGesture(g, localPoint);
    }
}
}
