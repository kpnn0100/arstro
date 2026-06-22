#include "CurveEditor.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 14.0;
        constexpr double kHit = 11.0;
        double clamp01(double v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }
    }

    CurveEditor::CurveEditor(const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(200.0);
        reset();
    }

    void CurveEditor::reset()
    {
        mPts = {{0.f, 0.f}, {1.f, 1.f}};
    }

    void CurveEditor::setPoints(const std::vector<std::pair<float, float>> &pts)
    {
        mPts = pts;
        if (mPts.size() < 2) reset();
        std::sort(mPts.begin(), mPts.end(),
                  [](auto &a, auto &b) { return a.first < b.first; });
    }

    double CurveEditor::px(double v) const { return kPad + clamp01(v) * (width.value() - 2 * kPad); }
    double CurveEditor::py(double v) const { return (height.value() - kPad) - clamp01(v) * (height.value() - 2 * kPad); }
    double CurveEditor::nx(double v) const { return clamp01((v - kPad) / (width.value() - 2 * kPad)); }
    double CurveEditor::ny(double v) const { return clamp01(((height.value() - kPad) - v) / (height.value() - 2 * kPad)); }

    int CurveEditor::nodeAt(const Point &p) const
    {
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            const double dx = p.x - px(mPts[i].first), dy = p.y - py(mPts[i].second);
            if (dx * dx + dy * dy <= kHit * kHit)
                return i;
        }
        return -1;
    }

    void CurveEditor::emit()
    {
        if (onChange)
            onChange(mPts);
    }

    bool CurveEditor::handleGesture(const Gesture &g, const Point &local)
    {
        using T = Gesture::Type;
        if (g.type == T::DoubleClick)
        {
            const int hit = nodeAt(local);
            if (hit > 0 && hit < (int)mPts.size() - 1)  // remove an interior point
            {
                mPts.erase(mPts.begin() + hit);
                emit();
                return true;
            }
            // add a point at the click position
            mPts.push_back({(float)nx(local.x), (float)ny(local.y)});
            std::sort(mPts.begin(), mPts.end(), [](auto &a, auto &b) { return a.first < b.first; });
            emit();
            return true;
        }
        if (g.type == T::Down)
        {
            mDrag = nodeAt(local);
            return mDrag >= 0;
        }
        if ((g.type == T::Drag || g.type == T::DragStart) && mDrag >= 0)
        {
            const int last = (int)mPts.size() - 1;
            float newY = (float)ny(local.y);
            if (mDrag == 0 || mDrag == last)
            {
                mPts[mDrag].second = newY;  // endpoints keep their x
            }
            else
            {
                float newX = (float)nx(local.x);
                const float lo = mPts[mDrag - 1].first + 1e-3f;
                const float hi = mPts[mDrag + 1].first - 1e-3f;
                mPts[mDrag].first = std::min(std::max(newX, lo), hi);
                mPts[mDrag].second = newY;
            }
            emit();
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop)
        {
            mDrag = -1;
        }
        return Segment::handleGesture(g, local);
    }

    void CurveEditor::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 6.0,
                        Paint::filledStroked(Color::hex(0x0a0c11), Color::hex(0x2a3040), 1.0));
        // grid (quarters)
        for (int i = 1; i < 4; ++i)
        {
            const double gx = px(i / 4.0), gy = py(i / 4.0);
            t.beginPath(); t.moveTo(gx, kPad); t.lineTo(gx, h - kPad);
            t.setStroke(Color{1, 1, 1, 0.06}, 1.0); t.strokePath();
            t.beginPath(); t.moveTo(kPad, gy); t.lineTo(w - kPad, gy);
            t.setStroke(Color{1, 1, 1, 0.06}, 1.0); t.strokePath();
        }
        // curve polyline (piecewise-linear through the points)
        t.beginPath();
        t.moveTo(px(mPts.front().first), py(mPts.front().second));
        for (size_t i = 1; i < mPts.size(); ++i)
            t.lineTo(px(mPts[i].first), py(mPts[i].second));
        t.setStroke(mAccent, 2.0);
        t.strokePath();
        // node dots
        for (const auto &p : mPts)
            drawCircle(t, px(p.first), py(p.second), 4.0, Paint::filled(mAccent));
    }
}
}
