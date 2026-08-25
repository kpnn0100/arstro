#include "CurvePanel.h"
#include "SectionHeader.h"
#include "DashedLine.h"
#include "WidgetLog.h"
#include "Icons.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kPickerH = 16.25;  // ~9px*1.3 + 2*py-0.5(1.625)
        // #4cb573 = the stacked-reach green the sliders use for the same idea (DR-EDIT-4).
        // Dimmer than it was (.5 -> .34) and drawn thinner and dashed: this line is a readout,
        // and at the editable curve's weight it read as a second curve to grab (D-28).
        constexpr double kRefAlpha = 0.34;
        const Color kRefColor{0.298, 0.710, 0.451, kRefAlpha};
        constexpr double kRefWidth = 1.0;    // the editable curve is 1.5
        constexpr double kCurveWidth = 1.5;
        constexpr double kLegendPx = 8.5;    // caption under the plot

        /** A point list, short enough for one log line: at most six points, then an ellipsis. */
        std::string briefPts(const std::vector<CurvePoint> &p)
        {
            std::string s;
            char b[48];
            for (size_t i = 0; i < p.size() && i < 6; ++i)
            {
                std::snprintf(b, sizeof(b), "%s%.3f,%.3f", i ? " " : "", (double)p[i].x, (double)p[i].y);
                s += b;
            }
            if (p.size() > 6) s += " ...";
            return "[" + std::to_string(p.size()) + "] " + s;
        }
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
        WLOG("curve: EMIT ch=%d %s", mChannel, briefPts(active()).c_str());
        if (onCurveChange) onCurveChange(mChannel, active());
    }

    void CurvePanel::setCurves(const Points &master, const std::array<Points, 3> &channels)
    {
        // A GESTURE IN FLIGHT OUTRANKS THE MODEL.
        //
        // R-SVC-12 re-seeds every panel from the view-model whenever the model's revision
        // moves, and during a drag a frame lands every few tens of milliseconds — so this
        // runs *between* the Down and the Drag of the same gesture. Alt+drag sets `smooth`
        // on the press and only emits on the first move, so the re-seed arrived carrying a
        // model that had never heard of it and flattened the node back to a corner. The
        // handles were then written to a point the sampler ignores, and the bezier the user
        // was dragging out simply never appeared.
        //
        // The rule is general, and it is the same one R-VIEW-1a states for pixels and D-34
        // for slider values: state the user is actively editing may not be overwritten by a
        // refresh. It is safe to skip — an undo, a preset or a selection change cannot
        // happen while a button is held, and the next re-seed after the release is
        // unconditional.
        if (mDragIdx >= 0)
        {
            WLOG("curve: setCurves IGNORED (drag in flight on node %d)", mDragIdx);
            return;
        }
        // Logged because this is the panel being RE-SEEDED from the model: if the curve the
        // user is looking at is ever replaced by something else it happens on this line, and a
        // trace has to show what arrived and what it displaced (D-33's territory).
        if (widgetLogEnabled() && master != mCurves[0])
            WLOG("curve: setCurves REPLACES master %s -> %s",
                 briefPts(mCurves[0]).c_str(), briefPts(master).c_str());
        mCurves[0] = master;
        for (int c = 0; c < 3; ++c) mCurves[c + 1] = channels[c];
    }

    void CurvePanel::setReferenceCurves(const Points &master, const std::array<Points, 3> &channels)
    {
        if (widgetLogEnabled() && master != mReference[0])
            WLOG("curve: setReference master %s", briefPts(master).c_str());
        mReference[0] = master;
        for (int c = 0; c < 3; ++c) mReference[c + 1] = channels[c];
    }

    bool CurvePanel::referenceWanted() const
    {
        const Points &ref = mReference[mChannel];
        return ref.size() >= 2 && ref != active();
    }

    bool CurvePanel::referenceVisible(double nowMs) const
    {
        // D-31: shown only when nothing is being aimed inside the plot. `mRevealAtMs` holds it
        // away for a moment after release so a double-click's two presses read as one gesture.
        return referenceWanted() && !mPressed && nowMs >= mRevealAtMs;
    }

    void CurvePanel::advance(double nowMs)
    {
        mNowMs = nowMs;
        const double want = referenceVisible(nowMs) ? 1.0 : 0.0;
        if (want != mRefTarget)
        {
            mRefTarget = want;
            // Out faster than in: getting out of the way should feel immediate, coming back
            // should not startle. Both eased, both collapse under reducedMotion() (R-G-1).
            mRefFade.animateTo(want, want > 0.5 ? 180.0 : 110.0, Easing::EaseOutCubic, nowMs);
        }
        // Keep the last drawable copy so the fade-OUT has a line to fade; refreshed while the
        // reference is live so it tracks the drag (DR-EDIT-5).
        if (want > 0.5) mRefShown = mReference[mChannel];
        mRefFade.update(nowMs);
        Segment::advance(nowMs);
    }

    void CurvePanel::showChannel(int channel)
    {
        WLOG("curve: showChannel %d -> %d", mChannel, channel);
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

    // D-32: the pick radius is 13 px so a node is easy to hit; that generosity must not become
    // a teleport. Adding the grab offset back means grabbing a node 12 px off-centre simply
    // keeps it 12 px from the cursor for the rest of the drag, which is what every other drag
    // in the app does and what a user assumes without being told.
    double CurvePanel::grabbedX(const Point &pl) const { return std::clamp(nx(pl.x) + mGrabDX, 0.0, 1.0); }
    double CurvePanel::grabbedY(const Point &pl) const { return std::clamp(ny(pl.y) + mGrabDY, 0.0, 1.0); }

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

        WLOG("curve: gesture=%d local=%.1f,%.1f plot=%.1f,%.1f inPlot=%d ch=%d pts=%d "
             "dragIdx=%d alt=%d",
             (int)g.type, local.x, local.y, pl.x, pl.y, inPlot ? 1 : 0, mChannel,
             (int)active().size(), mDragIdx, g.alt ? 1 : 0);

        if (g.type == T::DoubleClick && inPlot)
        {
            Points &pts = active();
            const int hit = pointAt(pl);
            WLOG("curve: dblclick hit=%d curve=%s", hit, briefPts(pts).c_str());
            if (hit > 0 && hit < (int)pts.size() - 1)   // remove an interior node (never the endpoints)
            {
                WLOG("curve: dblclick REMOVES node %d at %.3f,%.3f", hit,
                     (double)pts[hit].x, (double)pts[hit].y);
                pts.erase(pts.begin() + hit);
            }
            else if (hit < 0)                            // add a corner where the user clicked
            {
                CurvePoint c; c.x = (float)nx(pl.x); c.y = (float)ny(pl.y);
                WLOG("curve: dblclick ADDS node at %.3f,%.3f", (double)c.x, (double)c.y);
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
            mPressed = true;   // D-31: the readout steps aside for the whole gesture
            int idx, kind;
            if (handleAt(pl, idx, kind))
            {
                mDragIdx = idx; mDragKind = kind;
                const CurvePoint &cp = active()[idx];
                WLOG("curve: down GRABBED HANDLE idx=%d kind=%d", idx, kind);
                // A handle lives at the node plus its own offset — that is the thing grabbed.
                beginGrab(pl, cp.x + (kind == 1 ? cp.ix : cp.ox),
                              cp.y + (kind == 1 ? cp.iy : cp.oy));
                return true;
            }
            const int p = pointAt(pl);
            if (p >= 0)
            {
                mDragIdx = p;
                if (g.alt) { active()[p].smooth = true; mDragKind = 3; } else mDragKind = 0;
                beginGrab(pl, active()[p].x, active()[p].y);
                // The distance is the interesting number: a grab from 12 px away used to
                // teleport the node 12 px (D-32), so a trace that omits it cannot show whether
                // the pick radius or the drag arithmetic is at fault.
                {
                    const double ddx = pl.x - px(active()[p].x), ddy = pl.y - py(active()[p].y);
                    WLOG("curve: down GRABBED NODE idx=%d kind=%d at %.3f,%.3f  %.1f px away  "
                         "grab=%.4f,%.4f", p, mDragKind, (double)active()[p].x,
                         (double)active()[p].y, std::sqrt(ddx * ddx + ddy * ddy), mGrabDX, mGrabDY);
                }
                return true;
            }
            mDragIdx = -1;
            WLOG("curve: down HIT NOTHING (pointer at %.3f,%.3f in curve space) - consumed",
                 nx(pl.x), ny(pl.y));
            return true;  // consume the press (a following double-click adds a point)
        }
        if ((g.type == T::Drag || g.type == T::DragStart) && mDragIdx >= 0)
        {
            Points &pts = active();
            CurvePoint &cp = pts[mDragIdx];
            // D-32: the grab-corrected pointer, everywhere the raw pointer used to be used.
            const double gx = grabbedX(pl), gy = grabbedY(pl);
            const float hx = (float)(gx - cp.x), hy = (float)(gy - cp.y);
            WLOG("curve: drag idx=%d kind=%d node=%.3f,%.3f  pointer=%.3f,%.3f  "
                 "grabCorrected=%.3f,%.3f", mDragIdx, mDragKind, (double)cp.x, (double)cp.y,
                 nx(pl.x), ny(pl.y), gx, gy);
            if (mDragKind == 0)  // move the node (endpoints locked in x, interior clamped between neighbours)
            {
                double x = gx;
                if (mDragIdx == 0) x = 0.0;
                else if (mDragIdx == (int)pts.size() - 1) x = 1.0;
                else
                {
                    const double lo = pts[mDragIdx - 1].x + 0.01, hi = pts[mDragIdx + 1].x - 0.01;
                    x = std::clamp(x, lo, hi);
                }
                cp.x = (float)x;
                cp.y = (float)gy;
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
        if (g.type == T::Up || g.type == T::Drop)
        {
            mDragIdx = -1;
            mPressed = false;
            mRevealAtMs = mNowMs + kRefHoldMs;   // D-31: wait out a possible second click
            return true;
        }
        return Segment::handleGesture(g, local);
    }

    std::string CurvePanel::uiDetail() const
    {
        const Points &ref = mReference[mChannel];
        const bool refDrawn = ref.size() >= 2 && ref != active();
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "channel=%d pts=%d ref=%d refDrawn=%d plot=%.0f,%.0f %.0fx%.0f "
                      "dragIdx=%d dragKind=%d grab=%.4f,%.4f pressed=%d",
                      mChannel, (int)active().size(), (int)ref.size(), refDrawn ? 1 : 0,
                      kPadX, mPlotY, mPlotW, kPlotH, mDragIdx, mDragKind,
                      mGrabDX, mGrabDY, mPressed ? 1 : 0);
        return buf;
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

        // The effective (group-stacked) curve, behind the editable one — read-only, so it is
        // drawn as a readout and not as a curve: dashed, thinner, dimmer, and captioned
        // below the plot. Drawn solid at 1.5 it was indistinguishable from the curve the
        // panel actually edits, and a press on it does nothing because it has no nodes to
        // grab — a plot with two identical-looking lines, one of them inert (D-28).
        const double refA = mRefFade.value();
        if (refA > 0.001 && mRefShown.size() >= 2)
        {
            const auto dense = curve::sample(mRefShown, false, 0.f);
            std::vector<Point> poly;
            poly.reserve(dense.size());
            for (const auto &d : dense)
                poly.push_back(Point{ox + d.first * mPlotW, oy + kPlotH - d.second * kPlotH});
            Color rc = kRefColor; rc.a *= refA;
            strokeDashedPolyline(t, poly, rc, kRefWidth);
        }

        const Color accent = channelColor();
        strokeCurve(active(), accent, kCurveWidth);

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

        // Caption, in the strip under the plot: a dashed swatch and what the dashes mean.
        // A second line in a plot needs a name — without one the only way to find out what it
        // is, is to try to drag it, which is exactly the interaction that has no answer.
        if (refA > 0.001)
        {
            const double ly = oy + kPlotH + 11.0;
            Color rc = kRefColor; rc.a *= refA;
            strokeDashedPolyline(t, {Point{ox, ly}, Point{ox + 14.0, ly}}, rc, kRefWidth, 3.0, 2.5);
            Color tc = palette::mutedForeground(); tc.a *= refA;
            t.setFill(tc);
            t.drawText("final, with group", ox + 20.0, ly + kLegendPx * 0.35, kLegendPx, font::sans());
        }
    }
}
}
