#include "CurvePanel.h"
#include "SectionHeader.h"
#include "Icons.h"
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
        constexpr double kPadX = 9.75;
        constexpr double kPickerH = 16.25;  // ~9px*1.3 + 2*py-0.5(1.625)
        const Color kRefColor{0.298, 0.710, 0.451, 0.5};  // #4cb573 @ .5 = the effective (final) line
    }

    CurvePanel::CurvePanel()
    {
        mChannelPicker = std::make_shared<SegmentedControl>(std::vector<std::string>{"RGB", "R", "G", "B"});
        mChannelPicker->containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        mChannelPicker->idleSegBox = {Paint{}, radius::hairline()};
        mChannelPicker->activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        mChannelPicker->edgeRadius = radius::control();
        mChannelPicker->idleText = {palette::mutedForeground(), 9.0, font::sans()};
        mChannelPicker->activeText = {palette::white(), 9.0, font::sans()};
        mChannelPicker->padding = 2.0;
        mChannelPicker->gap = 2.0;
        mChannelPicker->height.set(kPickerH);
        mChannelPicker->onChange = [this](int idx) { showChannel(idx); };  // instant swap to that channel's curve
        addChild(mChannelPicker);

        mResetBtn = std::make_shared<IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::refreshCw(t, r, c); });
        mResetBtn->idleColor = palette::mutedForeground();
        mResetBtn->activeColor = palette::foreground();
        mResetBtn->width.set(10.0 + 2 * 1.625);
        mResetBtn->height.set(10.0 + 2 * 1.625);
        mResetBtn->onClick = [this] {
            active() = {CurvePoint{0, 0}, CurvePoint{1, 1}};  // reset only the active channel to identity
            emitChange();
        };
        addChild(mResetBtn);
    }

    Color CurvePanel::channelColor() const
    {
        switch (mChannel)
        {
            case 1: return Color{0.90, 0.32, 0.32, 1.0};  // R
            case 2: return Color{0.38, 0.80, 0.42, 1.0};  // G
            case 3: return Color{0.42, 0.58, 0.96, 1.0};  // B
            default: return palette::primary();           // RGB master (accent)
        }
    }

    void CurvePanel::emitChange()
    {
        if (onCurveChange) onCurveChange(mChannel, active());
    }

    void CurvePanel::setCurves(const Points &master, const std::array<Points, 3> &channels)
    {
        mCurves[0] = master;
        for (int c = 0; c < 3; ++c) mCurves[c + 1] = channels[c];
    }

    void CurvePanel::setReferenceCurves(const Points &master, const std::array<Points, 3> &channels)
    {
        mReference[0] = master;
        for (int c = 0; c < 3; ++c) mReference[c + 1] = channels[c];
    }

    void CurvePanel::showChannel(int channel)
    {
        mChannel = channel < 0 ? 0 : (channel > 3 ? 3 : channel);
        mDragIdx = -1;  // a channel swap cancels any in-flight drag on the old curve
    }

    void CurvePanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        mPlotW = innerW;  // plot spans the full edit-section width
        double y = kSectionHeaderHeight;  // "Tone Curve" header drawn in onPaint at y=0

        const double pickerW = innerW - mResetBtn->width.value() - 6.5;
        mChannelPicker->x.set(kPadX); mChannelPicker->y.set(y);
        mChannelPicker->width.set(pickerW); mChannelPicker->layout();
        mResetBtn->x.set(kPadX + pickerW + 6.5); mResetBtn->y.set(y + (kPickerH - mResetBtn->height.value()) * 0.5);
        y += kPickerH + 6.5;  // mb-2

        mPlotY = y;
    }

    Rect CurvePanel::plotRect() const { return Rect{kPadX, mPlotY, mPlotW, kPlotH}; }

    double CurvePanel::nx(double localX) const { return std::clamp(localX / mPlotW, 0.0, 1.0); }
    double CurvePanel::ny(double localY) const { return std::clamp(1.0 - localY / kPlotH, 0.0, 1.0); }

    int CurvePanel::pointAt(const Point &pl) const
    {
        // Nearest node within the forgiving pick radius (see metrics::anchorHitRadius).
        const double r2 = metrics::anchorHitRadius() * metrics::anchorHitRadius();
        const Points &pts = active();
        int best = -1;
        double bestD2 = r2;
        for (int i = 0; i < (int)pts.size(); ++i)
        {
            const double dx = pl.x - px(pts[i].x), dy = pl.y - py(pts[i].y);
            const double d2 = dx * dx + dy * dy;
            if (d2 <= bestD2) { bestD2 = d2; best = i; }
        }
        return best;
    }

    bool CurvePanel::handleAt(const Point &pl, int &idx, int &kind) const
    {
        const double r2 = metrics::anchorHitRadius() * metrics::anchorHitRadius();
        const Points &pts = active();
        for (int i = 0; i < (int)pts.size(); ++i)
        {
            if (!pts[i].smooth) continue;
            const double ox = px(pts[i].x + pts[i].ox), oy = py(pts[i].y + pts[i].oy);
            const double ix = px(pts[i].x + pts[i].ix), iy = py(pts[i].y + pts[i].iy);
            if ((pl.x - ox) * (pl.x - ox) + (pl.y - oy) * (pl.y - oy) <= r2) { idx = i; kind = 2; return true; }
            if ((pl.x - ix) * (pl.x - ix) + (pl.y - iy) * (pl.y - iy) <= r2) { idx = i; kind = 1; return true; }
        }
        return false;
    }

    bool CurvePanel::handleGesture(const Gesture &g, const Point &local)
    {
        using T = Gesture::Type;
        const Rect plot = plotRect();
        const Point pl{local.x - plot.x, local.y - plot.y};
        const bool inPlot = pl.x >= 0 && pl.x <= mPlotW && pl.y >= 0 && pl.y <= kPlotH;

        if (g.type == T::DoubleClick && inPlot)
        {
            Points &pts = active();
            const int hit = pointAt(pl);
            if (hit > 0 && hit < (int)pts.size() - 1)   // remove an interior node (never the endpoints)
                pts.erase(pts.begin() + hit);
            else if (hit < 0)                            // add a corner where the user clicked
            {
                CurvePoint c; c.x = (float)nx(pl.x); c.y = (float)ny(pl.y);
                auto it = std::lower_bound(pts.begin(), pts.end(), c,
                                           [](const CurvePoint &a, const CurvePoint &b) { return a.x < b.x; });
                pts.insert(it, c);
            }
            else return true;
            emitChange();
            return true;
        }
        if (g.type == T::Down)
        {
            if (!inPlot) return Segment::handleGesture(g, local);
            int idx, kind;
            if (handleAt(pl, idx, kind)) { mDragIdx = idx; mDragKind = kind; return true; }
            const int p = pointAt(pl);
            if (p >= 0)
            {
                mDragIdx = p;
                if (g.alt) { active()[p].smooth = true; mDragKind = 3; } else mDragKind = 0;
                return true;
            }
            mDragIdx = -1;
            return true;  // consume the press (a following double-click adds a point)
        }
        if ((g.type == T::Drag || g.type == T::DragStart) && mDragIdx >= 0)
        {
            Points &pts = active();
            CurvePoint &cp = pts[mDragIdx];
            const float hx = (float)(nx(pl.x) - cp.x), hy = (float)(ny(pl.y) - cp.y);
            if (mDragKind == 0)  // move the node (endpoints locked in x, interior clamped between neighbours)
            {
                double x = nx(pl.x);
                if (mDragIdx == 0) x = 0.0;
                else if (mDragIdx == (int)pts.size() - 1) x = 1.0;
                else
                {
                    const double lo = pts[mDragIdx - 1].x + 0.01, hi = pts[mDragIdx + 1].x - 0.01;
                    x = std::clamp(x, lo, hi);
                }
                cp.x = (float)x;
                cp.y = (float)ny(pl.y);
            }
            else if (mDragKind == 3)  // Alt-drag on a node: pull symmetric tangent handles (make a spline)
            {
                cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy;
            }
            else if (g.alt)  // drag a handle with Alt: break symmetry (independent in/out)
            {
                if (mDragKind == 1) { cp.ix = hx; cp.iy = hy; } else { cp.ox = hx; cp.oy = hy; }
            }
            else  // drag a handle: mirror the opposite one
            {
                if (mDragKind == 1) { cp.ix = hx; cp.iy = hy; cp.ox = -hx; cp.oy = -hy; }
                else { cp.ox = hx; cp.oy = hy; cp.ix = -hx; cp.iy = -hy; }
            }
            emitChange();
            return true;
        }
        if (g.type == T::Up || g.type == T::Drop) { mDragIdx = -1; return true; }
        return Segment::handleGesture(g, local);
    }

    void CurvePanel::onPaint(IRenderTarget &t) const
    {
        const double innerW = std::max(0.0, width.value() - 2 * kPadX);
        drawSectionHeader(t, kPadX, 0.0, innerW, "Tone Curve");

        const Rect plot = plotRect();
        drawRoundedRect(t, plot, radius::control(), Paint::filledStroked(palette::curvePlotBg(), palette::border(), 1.0));

        // Offset every coordinate by the plot's own position directly (setTransform takes
        // an ABSOLUTE transform, so replacing it would discard the composed world one).
        const double ox = plot.x, oy = plot.y;
        for (int i = 1; i <= 3; ++i)
        {
            t.beginPath();
            t.moveTo(ox + i * (mPlotW / 4.0), oy); t.lineTo(ox + i * (mPlotW / 4.0), oy + kPlotH);
            t.moveTo(ox, oy + i * (kPlotH / 4.0)); t.lineTo(ox + mPlotW, oy + i * (kPlotH / 4.0));
            t.setStroke(Color{1, 1, 1, 0.05}, 1.0);
            t.strokePath();
        }
        t.beginPath();
        t.moveTo(ox, oy + kPlotH); t.lineTo(ox + mPlotW, oy);
        t.setStroke(Color{1, 1, 1, 0.08}, 1.0);
        t.strokePath();

        auto strokeCurve = [&](const Points &pts, const Color &col, double wdt) {
            const auto dense = curve::sample(pts, false, 0.f);
            if (dense.size() < 2) return;
            t.beginPath();
            t.moveTo(ox + dense[0].first * mPlotW, oy + kPlotH - dense[0].second * kPlotH);
            for (size_t i = 1; i < dense.size(); ++i)
                t.lineTo(ox + dense[i].first * mPlotW, oy + kPlotH - dense[i].second * kPlotH);
            t.setStroke(col, wdt);
            t.strokePath();
        };

        // The effective (group-stacked) curve, faint, behind the editable one.
        const Points &ref = mReference[mChannel];
        if (ref.size() >= 2 && ref != active()) strokeCurve(ref, kRefColor, 1.5);

        const Color accent = channelColor();
        strokeCurve(active(), accent, 1.5);

        // Tangent handles for smooth nodes, then the node circles.
        for (const auto &p : active())
        {
            if (p.smooth)
                for (int side = 0; side < 2; ++side)
                {
                    const double hx = ox + px(p.x + (side ? p.ox : p.ix)), hy = oy + py(p.y + (side ? p.oy : p.iy));
                    t.beginPath(); t.moveTo(ox + px(p.x), oy + py(p.y)); t.lineTo(hx, hy);
                    t.setStroke(Color{accent.r, accent.g, accent.b, 0.5}, 1.0); t.strokePath();
                    drawCircle(t, hx, hy, 3.0, Paint::filled(Color{accent.r, accent.g, accent.b, 0.7}));
                }
        }
        for (const auto &p : active())
            drawCircle(t, ox + px(p.x), oy + py(p.y), 4.0, Paint::filledStroked(accent, palette::white(), 1.5));
    }
}
}
