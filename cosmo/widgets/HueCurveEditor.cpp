#include "HueCurveEditor.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 12.0;
        constexpr double kHit = 11.0;
        double clampY(double v) { return v < -1 ? -1 : (v > 1 ? 1 : v); }
        double wrap360(double h) { h = std::fmod(h, 360.0); return h < 0 ? h + 360.0 : h; }

        Color hueColor(double h)
        {
            h = wrap360(h);
            const double c = 1.0, x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
            double r = 0, g = 0, b = 0;
            if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; }
            else if (h < 180) { g = c; b = x; } else if (h < 240) { g = x; b = c; }
            else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
            return Color{r, g, b, 1.0};
        }
    }

    HueCurveEditor::HueCurveEditor(const Color &accent) : mAccent(accent)
    {
        width.set(300.0); height.set(180.0);
        mPts = {{0.f, 0.f}, {120.f, 0.f}, {240.f, 0.f}};
    }

    void HueCurveEditor::setPoints(const std::vector<std::pair<float, float>> &pts)
    {
        mPts.clear();
        for (auto &p : pts) { CtrlPoint c; c.x = (float)wrap360(p.first); c.y = (float)clampY(p.second); mPts.push_back(c); }
        if (mPts.size() < 2) mPts = {{0.f, 0.f}, {120.f, 0.f}, {240.f, 0.f}};
    }

    double HueCurveEditor::pxh(double hue) const { return kPad + hue / 360.0 * (width.value() - 2 * kPad); }
    double HueCurveEditor::pyv(double y) const { return midY() - y * halfH(); }
    double HueCurveEditor::nxh(double px, bool wrap) const
    {
        const double h = (px - kPad) / (width.value() - 2 * kPad) * 360.0;
        return wrap ? wrap360(h) : h;
    }
    double HueCurveEditor::nyv(double py) const { return clampY((midY() - py) / halfH()); }

    int HueCurveEditor::pointAt(const Point &p) const
    {
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            const double dx = p.x - pxh(mPts[i].x), dy = p.y - pyv(mPts[i].y);
            if (dx * dx + dy * dy <= kHit * kHit) return i;
        }
        return -1;
    }

    bool HueCurveEditor::handleAt(const Point &p, int &idx, int &kind) const
    {
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            if (!mPts[i].smooth) continue;
            const double ox = pxh(mPts[i].x + mPts[i].ox), oy = pyv(mPts[i].y + mPts[i].oy);
            const double ix = pxh(mPts[i].x + mPts[i].ix), iy = pyv(mPts[i].y + mPts[i].iy);
            if ((p.x - ox) * (p.x - ox) + (p.y - oy) * (p.y - oy) <= kHit * kHit) { idx = i; kind = 2; return true; }
            if ((p.x - ix) * (p.x - ix) + (p.y - iy) * (p.y - iy) <= kHit * kHit) { idx = i; kind = 1; return true; }
        }
        return false;
    }

    void HueCurveEditor::emit()
    {
        if (onChange) onChange(sampleCurve(mPts, /*cyclic*/ true, 360.0f));
    }

    bool HueCurveEditor::handleGesture(const Gesture &g, const Point &local)
    {
        using T = Gesture::Type;
        if (g.type == T::DoubleClick)
        {
            const int hit = pointAt(local);
            if (hit >= 0 && mPts.size() > 2) mPts.erase(mPts.begin() + hit);
            else { CtrlPoint c; c.x = (float)nxh(local.x, true); c.y = (float)nyv(local.y); mPts.push_back(c); }
            emit();
            return true;
        }
        if (g.type == T::Down)
        {
            int idx, kind;
            if (handleAt(local, idx, kind)) { mDragIdx = idx; mDragKind = kind; return true; }
            const int p = pointAt(local);
            if (p >= 0) { mDragIdx = p; if (g.alt) { mPts[p].smooth = true; mDragKind = 3; } else mDragKind = 0; return true; }
            return false;
        }
        if ((g.type == T::Drag || g.type == T::DragStart) && mDragIdx >= 0)
        {
            CtrlPoint &cp = mPts[mDragIdx];
            if (mDragKind == 0) { cp.x = (float)nxh(local.x, true); cp.y = (float)nyv(local.y); }
            else
            {
                const float hx = (float)nxh(local.x, false) - cp.x, hy = (float)nyv(local.y) - cp.y;
                if (mDragKind == 1) { cp.ix = hx; cp.iy = hy; }
                else if (mDragKind == 2) { cp.ox = hx; cp.oy = hy; }
                else { cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy; }
            }
            emit();
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop) mDragIdx = -1;
        return Segment::handleGesture(g, local);
    }

    void HueCurveEditor::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 6.0, Paint::filledStroked(Color::hex(0x0a0c11), Color::hex(0x2a3040), 1.0));
        t.beginPath(); t.moveTo(kPad, midY()); t.lineTo(w - kPad, midY()); t.setStroke(Color{1, 1, 1, 0.12}, 1.0); t.strokePath();
        for (int d = 60; d < 360; d += 60)
        { const double gx = pxh(d); t.beginPath(); t.moveTo(gx, plotTop()); t.lineTo(gx, plotBot()); t.setStroke(Color{1, 1, 1, 0.05}, 1.0); t.strokePath(); }
        // hue context strip
        const double sy = plotBot() + 4.0, sh = 8.0;
        for (int i = 0; i < 48; ++i)
        { const double h0 = (double)i / 48 * 360.0; drawRoundedRect(t, Rect{pxh(h0), sy, pxh((double)(i + 1) / 48 * 360.0) - pxh(h0) + 1.0, sh}, 0.0, Paint::filled(hueColor(h0))); }

        // dense cyclic curve; break the polyline where x wraps so the seam joins continuously.
        const auto dense = sampleCurve(mPts, true, 360.0f);
        for (size_t i = 0; i + 1 < dense.size(); ++i)
        {
            if (dense[i + 1].first < dense[i].first) continue;  // wrap fold -> skip the jump
            const Color col = mMappedHue ? hueColor(dense[i].first + dense[i].second * 60.0) : mAccent;
            t.beginPath(); t.moveTo(pxh(dense[i].first), pyv(dense[i].second));
            t.lineTo(pxh(dense[i + 1].first), pyv(dense[i + 1].second));
            t.setStroke(col, 2.0); t.strokePath();
        }
        // handles + nodes
        for (const auto &p : mPts)
        {
            if (p.smooth)
                for (int side = 0; side < 2; ++side)
                {
                    const double hx = pxh(p.x + (side ? p.ox : p.ix)), hy = pyv(p.y + (side ? p.oy : p.iy));
                    t.beginPath(); t.moveTo(pxh(p.x), pyv(p.y)); t.lineTo(hx, hy); t.setStroke(Color{mAccent.r, mAccent.g, mAccent.b, 0.5}, 1.0); t.strokePath();
                    drawCircle(t, hx, hy, 3.0, Paint::filled(Color{mAccent.r, mAccent.g, mAccent.b, 0.7}));
                }
            drawCircle(t, pxh(p.x), pyv(p.y), 4.0, Paint::filled(mAccent));
        }
    }
}
}
