#include "CurveEditor.h"
#include "../CosmoTheme.h"
#include <algorithm>

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

    CurveEditor::CurveEditor(const Color &accent) : mAccent(accent) { width.set(300.0); height.set(200.0); reset(); }

    void CurveEditor::reset() { mPts = {{0.f, 0.f}, {1.f, 1.f}}; }

    void CurveEditor::setPoints(const std::vector<std::pair<float, float>> &pts)
    {
        mPts.clear();
        for (auto &p : pts) { CtrlPoint c; c.x = clamp01(p.first); c.y = clamp01(p.second); mPts.push_back(c); }
        if (mPts.size() < 2) reset();
        std::sort(mPts.begin(), mPts.end(), [](const CtrlPoint &a, const CtrlPoint &b) { return a.x < b.x; });
    }

    double CurveEditor::px(double x) const { return kPad + x * (width.value() - 2 * kPad); }
    double CurveEditor::py(double y) const { return (height.value() - kPad) - y * (height.value() - 2 * kPad); }
    double CurveEditor::nx(double v) const { return (v - kPad) / (width.value() - 2 * kPad); }
    double CurveEditor::ny(double v) const { return ((height.value() - kPad) - v) / (height.value() - 2 * kPad); }

    int CurveEditor::pointAt(const Point &p) const
    {
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            const double dx = p.x - px(mPts[i].x), dy = p.y - py(mPts[i].y);
            if (dx * dx + dy * dy <= kHit * kHit) return i;
        }
        return -1;
    }

    bool CurveEditor::handleAt(const Point &p, int &idx, int &kind) const
    {
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            if (!mPts[i].smooth) continue;
            const double ox = px(mPts[i].x + mPts[i].ox), oy = py(mPts[i].y + mPts[i].oy);
            const double ixp = px(mPts[i].x + mPts[i].ix), iyp = py(mPts[i].y + mPts[i].iy);
            if ((p.x - ox) * (p.x - ox) + (p.y - oy) * (p.y - oy) <= kHit * kHit) { idx = i; kind = 2; return true; }
            if ((p.x - ixp) * (p.x - ixp) + (p.y - iyp) * (p.y - iyp) <= kHit * kHit) { idx = i; kind = 1; return true; }
        }
        return false;
    }

    void CurveEditor::emit()
    {
        if (onChange) onChange(sampleCurve(mPts, /*cyclic*/ false, 0.0f));
    }

    bool CurveEditor::handleGesture(const Gesture &g, const Point &local)
    {
        using T = Gesture::Type;
        if (g.type == T::DoubleClick)
        {
            const int hit = pointAt(local);
            if (hit > 0 && hit < (int)mPts.size() - 1) mPts.erase(mPts.begin() + hit);  // remove interior
            else { CtrlPoint c; c.x = (float)clamp01(nx(local.x)); c.y = (float)clamp01(ny(local.y)); mPts.push_back(c);
                   std::sort(mPts.begin(), mPts.end(), [](const CtrlPoint &a, const CtrlPoint &b) { return a.x < b.x; }); }
            emit();
            return true;
        }
        if (g.type == T::Down)
        {
            int idx, kind;
            if (handleAt(local, idx, kind)) { mDragIdx = idx; mDragKind = kind; return true; }
            const int p = pointAt(local);
            if (p >= 0)
            {
                mDragIdx = p;
                if (g.alt) { mPts[p].smooth = true; mDragKind = 3; }  // Alt: pull symmetric handles
                else mDragKind = 0;
                return true;
            }
            return false;
        }
        if ((g.type == T::Drag || g.type == T::DragStart) && mDragIdx >= 0)
        {
            CtrlPoint &cp = mPts[mDragIdx];
            if (mDragKind == 0)  // move the point
            {
                const bool endp = (mDragIdx == 0 || mDragIdx == (int)mPts.size() - 1);
                cp.y = (float)clamp01(ny(local.y));
                if (!endp)
                {
                    const float lo = mPts[mDragIdx - 1].x + 1e-3f, hi = mPts[mDragIdx + 1].x - 1e-3f;
                    cp.x = std::min(std::max((float)nx(local.x), lo), hi);
                }
            }
            else  // adjust a handle (or symmetric pull)
            {
                const float hx = (float)(nx(local.x)) - cp.x, hy = (float)(ny(local.y)) - cp.y;
                if (mDragKind == 1) { cp.ix = hx; cp.iy = hy; }
                else if (mDragKind == 2) { cp.ox = hx; cp.oy = hy; }
                else { cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy; }  // symmetric
            }
            emit();
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop) { mDragIdx = -1; }
        return Segment::handleGesture(g, local);
    }

    void CurveEditor::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(), Paint::filledStroked(palette::bg(), palette::line(), 1.0));

        // faint luminance histogram behind the curve (post-edit tones of the image)
        if (mHist.size() >= 2)
        {
            const int n = (int)mHist.size();
            t.setFill(Color{1, 1, 1, 0.10f});
            t.beginPath();
            t.moveTo(px(0.0), py(0.0));
            for (int i = 0; i < n; ++i)
            {
                double v = mHist[i]; if (v < 0) v = 0; else if (v > 1) v = 1;
                t.lineTo(px((double)i / (n - 1)), py(v));
            }
            t.lineTo(px(1.0), py(0.0));
            t.closePath();
            t.fillPath();
        }
        for (int i = 1; i < 4; ++i)
        {
            const double gx = px(i / 4.0), gy = py(i / 4.0);
            t.beginPath(); t.moveTo(gx, kPad); t.lineTo(gx, h - kPad); t.setStroke(Color{1, 1, 1, 0.06}, 1.0); t.strokePath();
            t.beginPath(); t.moveTo(kPad, gy); t.lineTo(w - kPad, gy); t.setStroke(Color{1, 1, 1, 0.06}, 1.0); t.strokePath();
        }
        // dense sampled curve (smooth)
        const auto dense = sampleCurve(mPts, false, 0.0f);
        if (dense.size() >= 2)
        {
            t.beginPath();
            t.moveTo(px(dense.front().first), py(dense.front().second));
            for (size_t i = 1; i < dense.size(); ++i) t.lineTo(px(dense[i].first), py(dense[i].second));
            t.setStroke(mAccent, 2.0);
            t.strokePath();
        }
        // handles + nodes
        for (const auto &p : mPts)
        {
            if (p.smooth)
            {
                for (int side = 0; side < 2; ++side)
                {
                    const double hx = px(p.x + (side ? p.ox : p.ix)), hy = py(p.y + (side ? p.oy : p.iy));
                    t.beginPath(); t.moveTo(px(p.x), py(p.y)); t.lineTo(hx, hy);
                    t.setStroke(Color{mAccent.r, mAccent.g, mAccent.b, 0.5}, 1.0); t.strokePath();
                    drawCircle(t, hx, hy, 3.0, Paint::filled(Color{mAccent.r, mAccent.g, mAccent.b, 0.7}));
                }
            }
            drawCircle(t, px(p.x), py(p.y), 4.0, Paint::filled(mAccent));
        }
    }
}
}
