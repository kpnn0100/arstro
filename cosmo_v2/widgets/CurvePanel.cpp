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
        constexpr double kHitRadiusPx = 7.0;

        // Catmull-Rom -> cubic bezier, matching scene::Path::spline's algorithm
        // (reimplemented inline: Path is a scene Drawable with its own transform,
        // not meant to be invoked mid-onPaint of another Segment).
        void strokeSpline(IRenderTarget &t, const std::vector<Point> &pts)
        {
            if (pts.size() < 2) return;
            t.moveTo(pts[0].x, pts[0].y);
            const int n = (int)pts.size();
            for (int i = 0; i < n - 1; ++i)
            {
                const Point &p0 = pts[i > 0 ? i - 1 : 0];
                const Point &p1 = pts[i];
                const Point &p2 = pts[i + 1];
                const Point &p3 = pts[i + 2 < n ? i + 2 : n - 1];
                const double c1x = p1.x + (p2.x - p0.x) / 6.0, c1y = p1.y + (p2.y - p0.y) / 6.0;
                const double c2x = p2.x - (p3.x - p1.x) / 6.0, c2y = p2.y - (p3.y - p1.y) / 6.0;
                t.cubicTo(c1x, c1y, c2x, c2y, p2.x, p2.y);
            }
        }
    }

    CurvePanel::CurvePanel()
    {
        mChannelPicker = std::make_shared<SegmentedControl>(std::vector<std::string>{"RGB", "R", "G", "B"});
        mChannelPicker->containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        mChannelPicker->idleSegBox = {Paint{}, radius::hairline()};
        mChannelPicker->activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        mChannelPicker->idleText = {palette::mutedForeground(), 9.0, font::sans()};
        mChannelPicker->activeText = {palette::white(), 9.0, font::sans()};
        mChannelPicker->padding = 2.0;
        mChannelPicker->gap = 2.0;
        mChannelPicker->height.set(kPickerH);
        addChild(mChannelPicker);

        mResetBtn = std::make_shared<IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::refreshCw(t, r, c); });
        mResetBtn->idleColor = palette::mutedForeground();
        mResetBtn->activeColor = palette::foreground();
        mResetBtn->width.set(10.0 + 2 * 1.625);
        mResetBtn->height.set(10.0 + 2 * 1.625);
        mResetBtn->onClick = [this] {
            mPoints = {{0, 0}, {1, 1}};
            if (onCurveChange) onCurveChange(mPoints);
        };
        addChild(mResetBtn);
    }

    void CurvePanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        double y = kSectionHeaderHeight;  // "Tone Curve" header drawn in onPaint at y=0

        const double pickerW = innerW - mResetBtn->width.value() - 6.5;
        mChannelPicker->x.set(kPadX); mChannelPicker->y.set(y);
        mChannelPicker->width.set(pickerW); mChannelPicker->layout();
        mResetBtn->x.set(kPadX + pickerW + 6.5); mResetBtn->y.set(y + (kPickerH - mResetBtn->height.value()) * 0.5);
        y += kPickerH + 6.5;  // mb-2

        mPlotY = y;
    }

    Rect CurvePanel::plotRect() const { return Rect{kPadX, mPlotY, kPlotW, kPlotH}; }

    int CurvePanel::hitPoint(const Point &plotLocal) const
    {
        for (int i = 0; i < (int)mPoints.size(); ++i)
        {
            const double px = mPoints[i].first * kPlotW, py = kPlotH - mPoints[i].second * kPlotH;
            const double dx = plotLocal.x - px, dy = plotLocal.y - py;
            if (dx * dx + dy * dy <= kHitRadiusPx * kHitRadiusPx) return i;
        }
        return -1;
    }

    bool CurvePanel::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect plot = plotRect();
        const Point pl{local.x - plot.x, local.y - plot.y};
        const bool inPlot = pl.x >= 0 && pl.x <= kPlotW && pl.y >= 0 && pl.y <= kPlotH;

        if (g.type == Gesture::Type::DragStart)
        {
            if (!inPlot) return Segment::handleGesture(g, local);
            mDragIndex = hitPoint(pl);
            return mDragIndex >= 0;
        }
        if (g.type == Gesture::Type::Drag)
        {
            if (mDragIndex < 0) return Segment::handleGesture(g, local);
            double x = std::clamp(pl.x / kPlotW, 0.0, 1.0);
            const double y = 1.0 - std::clamp(pl.y / kPlotH, 0.0, 1.0);
            const double lo = mDragIndex > 0 ? mPoints[mDragIndex - 1].first + 0.01 : 0.0;
            const double hi = mDragIndex < (int)mPoints.size() - 1 ? mPoints[mDragIndex + 1].first - 0.01 : 1.0;
            if (mDragIndex == 0) x = 0.0;
            else if (mDragIndex == (int)mPoints.size() - 1) x = 1.0;
            else x = std::clamp(x, lo, hi);
            mPoints[mDragIndex] = {(float)x, (float)y};
            if (onCurveChange) onCurveChange(mPoints);
            return true;
        }
        if (g.type == Gesture::Type::Drop) { mDragIndex = -1; return true; }
        if (g.type == Gesture::Type::Click && inPlot)
        {
            const int hit = hitPoint(pl);
            if (hit < 0)
            {
                const float x = (float)std::clamp(pl.x / kPlotW, 0.0, 1.0);
                const float y = (float)(1.0 - std::clamp(pl.y / kPlotH, 0.0, 1.0));
                auto it = std::lower_bound(mPoints.begin(), mPoints.end(), std::make_pair(x, 0.0f),
                                            [](const auto &a, const auto &b) { return a.first < b.first; });
                mPoints.insert(it, {x, y});
                if (onCurveChange) onCurveChange(mPoints);
            }
            return true;
        }
        if (g.type == Gesture::Type::DoubleClick && inPlot)
        {
            const int hit = hitPoint(pl);
            if (hit > 0 && hit < (int)mPoints.size() - 1)
            {
                mPoints.erase(mPoints.begin() + hit);
                if (onCurveChange) onCurveChange(mPoints);
            }
            return true;
        }
        return Segment::handleGesture(g, local);
    }

    void CurvePanel::onPaint(IRenderTarget &t) const
    {
        const double innerW = std::max(0.0, width.value() - 2 * kPadX);
        drawSectionHeader(t, kPadX, 0.0, innerW, "Tone Curve");

        const Rect plot = plotRect();
        drawRoundedRect(t, plot, radius::control(), Paint::filledStroked(palette::curvePlotBg(), palette::border(), 1.0));

        // Offset every coordinate by the plot's own position directly rather than
        // pushing a new setTransform -- setTransform takes an ABSOLUTE transform,
        // so replacing it here would discard the world transform this Segment's
        // render() already composed in, misplacing everything drawn afterward.
        const double ox = plot.x, oy = plot.y;
        for (int i = 1; i <= 3; ++i)
        {
            t.beginPath();
            t.moveTo(ox + i * (kPlotW / 4.0), oy); t.lineTo(ox + i * (kPlotW / 4.0), oy + kPlotH);
            t.moveTo(ox, oy + i * (kPlotH / 4.0)); t.lineTo(ox + kPlotW, oy + i * (kPlotH / 4.0));
            t.setStroke(Color{1, 1, 1, 0.05}, 1.0);
            t.strokePath();
        }
        t.beginPath();
        t.moveTo(ox, oy + kPlotH); t.lineTo(ox + kPlotW, oy);
        t.setStroke(Color{1, 1, 1, 0.08}, 1.0);
        t.strokePath();

        std::vector<Point> plotPts;
        for (const auto &p : mPoints) plotPts.push_back(Point{ox + p.first * kPlotW, oy + kPlotH - p.second * kPlotH});
        t.beginPath();
        strokeSpline(t, plotPts);
        t.setStroke(palette::primary(), 1.5);
        t.strokePath();

        for (const auto &pt : plotPts)
            drawCircle(t, pt.x, pt.y, 4.0, Paint::filledStroked(palette::primary(), palette::white(), 1.5));
    }
}
}
