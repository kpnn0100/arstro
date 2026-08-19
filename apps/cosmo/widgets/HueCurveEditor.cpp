#include "HueCurveEditor.h"
#include "DashedLine.h"
#include <cstdio>
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 12.0;
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

    HueCurveEditor::HueCurveEditor()
    {
        width.set(300.0); height.set(150.0);
        mPts = {{0.f, 0.f}, {120.f, 0.f}, {240.f, 0.f}};
    }

    void HueCurveEditor::setPoints(const std::vector<CurvePoint> &pts)
    {
        // Restore the control points verbatim (handles + smooth flag preserved), only
        // wrapping/clamping the anchor position back into range.
        mPts.clear();
        for (auto &p : pts) { CurvePoint c = p; c.x = (float)wrap360(p.x); c.y = (float)clampY(p.y); mPts.push_back(c); }
        if (mPts.size() < 2) mPts = {{0.f, 0.f}, {120.f, 0.f}, {240.f, 0.f}};
    }

    void HueCurveEditor::reset()
    {
        mPts = {{0.f, 0.f}, {120.f, 0.f}, {240.f, 0.f}};
        emit();
    }

    double HueCurveEditor::pxh(double hue) const { return kPad + hue / 360.0 * (width.value() - 2 * kPad); }
    double HueCurveEditor::pyv(double y) const { return midY() - y * halfH(); }
    double HueCurveEditor::nxh(double px, bool wrap) const
    {
        const double h = (px - kPad) / (width.value() - 2 * kPad) * 360.0;
        return wrap ? wrap360(h) : h;
    }
    double HueCurveEditor::nyv(double py) const { return clampY((midY() - py) / halfH()); }

    // D-32. `wrap` matches nxh's own flag: a NODE may travel across the 360/0 seam, a tangent
    // handle is measured relative to its node and must not.
    void HueCurveEditor::beginGrab(const Point &local, double tx, double ty)
    {
        mGrabDX = tx - nxh(local.x, false);
        mGrabDY = ty - nyv(local.y);
    }

    double HueCurveEditor::grabbedX(const Point &local, bool wrap) const
    {
        double x = nxh(local.x, false) + mGrabDX;
        if (wrap) { while (x < 0.0) x += 360.0; while (x >= 360.0) x -= 360.0; }
        return x;
    }

    double HueCurveEditor::grabbedY(const Point &local) const
    {
        return std::clamp(nyv(local.y) + mGrabDY, -1.0, 1.0);
    }

    int HueCurveEditor::pointAt(const Point &p) const
    {
        // Nearest anchor within the forgiving pick radius (see metrics::
        // anchorHitRadius) -- not the first, so overlapping targets resolve to
        // the anchor the user clicked closest to.
        const double r2 = metrics::anchorHitRadius() * metrics::anchorHitRadius();
        int best = -1;
        double bestD2 = r2;
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            const double dx = p.x - pxh(mPts[i].x), dy = p.y - pyv(mPts[i].y);
            const double d2 = dx * dx + dy * dy;
            if (d2 <= bestD2) { bestD2 = d2; best = i; }
        }
        return best;
    }

    bool HueCurveEditor::handleAt(const Point &p, int &idx, int &kind) const
    {
        const double r2 = metrics::anchorHitRadius() * metrics::anchorHitRadius();
        for (int i = 0; i < (int)mPts.size(); ++i)
        {
            if (!mPts[i].smooth) continue;
            const double ox = pxh(mPts[i].x + mPts[i].ox), oy = pyv(mPts[i].y + mPts[i].oy);
            const double ix = pxh(mPts[i].x + mPts[i].ix), iy = pyv(mPts[i].y + mPts[i].iy);
            if ((p.x - ox) * (p.x - ox) + (p.y - oy) * (p.y - oy) <= r2) { idx = i; kind = 2; return true; }
            if ((p.x - ix) * (p.x - ix) + (p.y - iy) * (p.y - iy) <= r2) { idx = i; kind = 1; return true; }
        }
        return false;
    }

    void HueCurveEditor::emit()
    {
        // Emit the CONTROL points (with handles), not a sampling — the engine flattens
        // them to its LUT and the file persists them, so the curve round-trips exactly.
        if (onChange) onChange(mPts);
    }

    bool HueCurveEditor::handleGesture(const Gesture &g, const Point &local)
    {
        using T = Gesture::Type;
        if (g.type == T::DoubleClick)
        {
            const int hit = pointAt(local);
            if (hit >= 0 && mPts.size() > 2) mPts.erase(mPts.begin() + hit);
            else { CurvePoint c; c.x = (float)nxh(local.x, true); c.y = (float)nyv(local.y); mPts.push_back(c); }
            emit();
            return true;
        }
        if (g.type == T::Down)
        {
            mPressed = true;   // D-31: the readout steps aside for the whole gesture
            int idx, kind;
            if (handleAt(local, idx, kind))
            {
                mDragIdx = idx; mDragKind = kind;
                const CurvePoint &cp = mPts[idx];
                beginGrab(local, cp.x + (kind == 1 ? cp.ix : cp.ox),
                                 cp.y + (kind == 1 ? cp.iy : cp.oy));
                return true;
            }
            const int p = pointAt(local);
            if (p >= 0)
            {
                mDragIdx = p;
                if (g.alt) { mPts[p].smooth = true; mDragKind = 3; } else mDragKind = 0;
                beginGrab(local, mPts[p].x, mPts[p].y);
                return true;
            }
            return false;
        }
        if ((g.type == T::Drag || g.type == T::DragStart) && mDragIdx >= 0)
        {
            CurvePoint &cp = mPts[mDragIdx];
            if (mDragKind == 0) { cp.x = (float)grabbedX(local, true); cp.y = (float)grabbedY(local); }
            else if (mDragKind == 3)
            {
                const float hx = (float)grabbedX(local, false) - cp.x, hy = (float)grabbedY(local) - cp.y;
                cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy;
            }
            else
            {
                const float hx = (float)grabbedX(local, false) - cp.x, hy = (float)grabbedY(local) - cp.y;
                if (g.alt)
                {
                    if (mDragKind == 1) { cp.ix = hx; cp.iy = hy; }
                    else { cp.ox = hx; cp.oy = hy; }
                }
                else
                {
                    if (mDragKind == 1) { cp.ix = hx; cp.iy = hy; cp.ox = -hx; cp.oy = -hy; }
                    else { cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy; }
                }
            }
            emit();
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop)
        {
            mDragIdx = -1;
            mPressed = false;
            mRevealAtMs = mNowMs + kRefHoldMs;   // D-31: wait out a possible second click
        }
        return Segment::handleGesture(g, local);
    }

    void HueCurveEditor::advance(double nowMs)
    {
        mNowMs = nowMs;
        const double want = referenceVisible(nowMs) ? 1.0 : 0.0;
        if (want != mRefTarget)
        {
            mRefTarget = want;
            mRefFade.animateTo(want, want > 0.5 ? 180.0 : 110.0, Easing::EaseOutCubic, nowMs);
        }
        if (want > 0.5) mRefShown = mReference;
        mRefFade.update(nowMs);
        Segment::advance(nowMs);
    }

    std::string HueCurveEditor::uiDetail() const
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "pts=%d ref=%d refDrawn=%d mappedHue=%d plot=y%.0f..%.0f dragIdx=%d dragKind=%d",
                      (int)mPts.size(), (int)mReference.size(),
                      (mReference.size() >= 2 && mReference != mPts) ? 1 : 0,
                      mMappedHue ? 1 : 0, plotTop(), plotBot(), mDragIdx, mDragKind);
        return buf;
    }

    void HueCurveEditor::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, radius::control(),
                        Paint::filledStroked(palette::curvePlotBg(), palette::border(), 1.0));
        t.beginPath(); t.moveTo(kPad, midY()); t.lineTo(w - kPad, midY());
        t.setStroke(Color{1, 1, 1, 0.12}, 1.0); t.strokePath();
        for (int d = 60; d < 360; d += 60)
        {
            const double gx = pxh(d);
            t.beginPath(); t.moveTo(gx, plotTop()); t.lineTo(gx, plotBot());
            t.setStroke(Color{1, 1, 1, 0.05}, 1.0); t.strokePath();
        }
        // hue context strip below the plot
        const double sy = plotBot() + 4.0, sh = 8.0;
        for (int i = 0; i < 48; ++i)
        {
            const double h0 = (double)i / 48 * 360.0;
            drawRoundedRect(t, Rect{pxh(h0), sy, pxh((double)(i + 1) / 48 * 360.0) - pxh(h0) + 1.0, sh}, 0.0,
                            Paint::filled(hueColor(h0)));
        }

        // The effective (group-stacked) curve, faint (#4cb573), behind the editable one.
        // Read-only, so it is drawn as a readout: DASHED, thinner and dimmer than the curve
        // this editor edits (D-28 — solid at 1.5 against an editable 2.0 it read as a second
        // curve to grab, and it has no nodes, so grabbing it does nothing).
        const double refA = mRefFade.value();
        if (refA > 0.001 && mRefShown.size() >= 2)
        {
            Color refc{0.298, 0.710, 0.451, 0.34 * refA};
            const auto rd = curve::sample(mRefShown, true, 360.0f);
            std::vector<Point> poly;
            std::vector<bool> brk;
            poly.reserve(rd.size()); brk.reserve(rd.size());
            for (size_t i = 0; i < rd.size(); ++i)
            {
                poly.push_back(Point{pxh(rd[i].first), pyv(rd[i].second)});
                brk.push_back(i > 0 && rd[i].first < rd[i - 1].first);  // the 360/0 seam
            }
            strokeDashedPolyline(t, poly, refc, 1.0, 4.0, 3.5, &brk);
            // Caption in the free strip above the plot, the same words the tone curve uses.
            Color tc = palette::mutedForeground(); tc.a *= refA;
            strokeDashedPolyline(t, {Point{kPad, 5.0}, Point{kPad + 14.0, 5.0}}, refc, 1.0, 3.0, 2.5);
            t.setFill(tc);
            t.drawText("final, with group", kPad + 20.0, 5.0 + 8.5 * 0.35, 8.5, font::sans());
        }

        // dense cyclic curve; break the polyline where x wraps so the seam joins continuously
        const auto dense = curve::sample(mPts, true, 360.0f);
        for (size_t i = 0; i + 1 < dense.size(); ++i)
        {
            if (dense[i + 1].first < dense[i].first) continue;  // wrap fold -> skip the jump
            const Color col = mMappedHue ? hueColor(dense[i].first + dense[i].second * 180.0) : palette::primary();
            t.beginPath(); t.moveTo(pxh(dense[i].first), pyv(dense[i].second));
            t.lineTo(pxh(dense[i + 1].first), pyv(dense[i + 1].second));
            t.setStroke(col, 2.0); t.strokePath();
        }
        // handles + nodes
        const Color accent = palette::primary();
        for (const auto &p : mPts)
        {
            if (p.smooth)
                for (int side = 0; side < 2; ++side)
                {
                    const double hx = pxh(p.x + (side ? p.ox : p.ix)), hy = pyv(p.y + (side ? p.oy : p.iy));
                    t.beginPath(); t.moveTo(pxh(p.x), pyv(p.y)); t.lineTo(hx, hy);
                    t.setStroke(Color{accent.r, accent.g, accent.b, 0.5}, 1.0); t.strokePath();
                    drawCircle(t, hx, hy, 3.0, Paint::filled(Color{accent.r, accent.g, accent.b, 0.7}));
                }
            drawCircle(t, pxh(p.x), pyv(p.y), 4.0, Paint::filledStroked(accent, palette::white(), 1.5));
        }
    }
}
}
