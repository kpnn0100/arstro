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

        // Simple hue (0..360) -> fully-saturated RGB for the context strip.
        Color hueColor(double h)
        {
            const double c = 1.0, x = c * (1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0));
            double r = 0, g = 0, b = 0;
            if (h < 60) { r = c; g = x; }
            else if (h < 120) { r = x; g = c; }
            else if (h < 180) { g = c; b = x; }
            else if (h < 240) { g = x; b = c; }
            else if (h < 300) { r = x; b = c; }
            else { r = c; b = x; }
            return Color{r, g, b, 1.0};
        }
    }

    HueCurveEditor::HueCurveEditor(const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(180.0);
        mPts = {{0.f, 0.f}, {120.f, 0.f}, {240.f, 0.f}};  // flat identity, 3 handles
    }

    void HueCurveEditor::setPoints(const std::vector<std::pair<float, float>> &pts)
    {
        mPts = pts.empty() ? std::vector<std::pair<float, float>>{{0.f, 0.f}, {120.f, 0.f}, {240.f, 0.f}}
                           : pts;
    }

    double HueCurveEditor::pxh(double hue) const { return kPad + hue / 360.0 * (width.value() - 2 * kPad); }
    double HueCurveEditor::pyv(double y) const { return midY() - y * halfH(); }
    double HueCurveEditor::nxh(double px) const
    {
        double h = (px - kPad) / (width.value() - 2 * kPad) * 360.0;
        h = std::fmod(h, 360.0);
        if (h < 0) h += 360.0;
        return h;
    }
    double HueCurveEditor::nyv(double py) const
    {
        double y = (midY() - py) / halfH();
        return y < -1 ? -1 : (y > 1 ? 1 : y);
    }

    int HueCurveEditor::pointAt(const Point &p) const
    {
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            const double dx = p.x - pxh(mPts[i].first), dy = p.y - pyv(mPts[i].second);
            if (dx * dx + dy * dy <= kHit * kHit)
                return i;
        }
        return -1;
    }

    void HueCurveEditor::emit()
    {
        if (onChange)
            onChange(mPts);  // engine sorts/wraps
    }

    bool HueCurveEditor::handleGesture(const Gesture &g, const Point &local)
    {
        using T = Gesture::Type;
        if (g.type == T::DoubleClick)
        {
            const int hit = pointAt(local);
            if (hit >= 0 && mPts.size() > 1)
                mPts.erase(mPts.begin() + hit);
            else
                mPts.push_back({(float)nxh(local.x), (float)nyv(local.y)});
            emit();
            return true;
        }
        if (g.type == T::Down)
        {
            mDrag = pointAt(local);
            return mDrag >= 0;
        }
        if ((g.type == T::Drag || g.type == T::DragStart) && mDrag >= 0)
        {
            mPts[mDrag].first = (float)nxh(local.x);   // free in hue (cyclic)
            mPts[mDrag].second = (float)nyv(local.y);
            emit();
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop)
            mDrag = -1;
        return Segment::handleGesture(g, local);
    }

    void HueCurveEditor::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 6.0,
                        Paint::filledStroked(Color::hex(0x0a0c11), Color::hex(0x2a3040), 1.0));
        // zero line + 60-degree grid
        t.beginPath(); t.moveTo(kPad, midY()); t.lineTo(w - kPad, midY());
        t.setStroke(Color{1, 1, 1, 0.12}, 1.0); t.strokePath();
        for (int d = 60; d < 360; d += 60)
        {
            const double gx = pxh(d);
            t.beginPath(); t.moveTo(gx, plotTop()); t.lineTo(gx, plotBot());
            t.setStroke(Color{1, 1, 1, 0.05}, 1.0); t.strokePath();
        }
        // hue context strip along the bottom
        const double sy = plotBot() + 4.0, sh = 8.0;
        const int seg = 48;
        for (int i = 0; i < seg; ++i)
        {
            const double h0 = (double)i / seg * 360.0;
            const double x0 = pxh(h0), x1 = pxh((double)(i + 1) / seg * 360.0);
            drawRoundedRect(t, Rect{x0, sy, x1 - x0 + 1.0, sh}, 0.0, Paint::filled(hueColor(h0)));
        }
        // curve: sort a copy by hue, draw segments incl. the wrap (last -> first+360)
        std::vector<std::pair<float, float>> s = mPts;
        std::sort(s.begin(), s.end(), [](auto &a, auto &b) { return a.first < b.first; });
        t.beginPath();
        t.moveTo(pxh(s.front().first), pyv(s.front().second));
        for (size_t i = 1; i < s.size(); ++i)
            t.lineTo(pxh(s[i].first), pyv(s[i].second));
        t.setStroke(mAccent, 2.0);
        t.strokePath();
        // wrap segment (drawn separately so it visibly closes the cycle)
        t.beginPath();
        t.moveTo(pxh(s.back().first), pyv(s.back().second));
        t.lineTo(pxh(360.0), pyv(s.front().second));
        t.setStroke(Color{mAccent.r, mAccent.g, mAccent.b, 0.5}, 1.5);
        t.strokePath();
        // node dots
        for (const auto &p : mPts)
            drawCircle(t, pxh(p.first), pyv(p.second), 4.0, Paint::filled(mAccent));
    }
}
}
