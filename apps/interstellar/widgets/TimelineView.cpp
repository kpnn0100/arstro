#include "TimelineView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace artboard;

namespace arstro
{
namespace interstellar_v1
{
    namespace
    {
        /** A tick spacing that keeps labels legible at ANY zoom: the smallest of a fixed ladder
         *  that is at least 64 px wide. Derived from the live zoom every frame, so the ruler and
         *  the clips cannot disagree mid-tween. */
        double tickStep(double pps)
        {
            static const double kLadder[] = {0.04, 0.1, 0.2, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600};
            for (double s : kLadder)
                if (s * pps >= 64.0) return s;
            return 600.0;
        }
        std::string timecode(double t, double fps)
        {
            if (t < 0) t = 0;
            const long long total = (long long)std::llround(t * fps);
            const long long f = total % (long long)std::llround(fps);
            const long long secs = total / (long long)std::llround(fps);
            char b[32];
            std::snprintf(b, sizeof b, "%02lld:%02lld:%02lld", secs / 3600, (secs / 60) % 60, secs % 60);
            (void)f;
            return b;
        }
    }

    TimelineView::TimelineView() { clipToBounds = true; }

    void TimelineView::setModel(const interstellar::AppModel &m)
    {
        // A gesture in flight outranks the model: the model refreshes every few tens of ms and
        // would otherwise flatten the very clip being dragged (arstro.design.rule §5).
        if (!mDragging.empty()) { mModel = &m; return; }
        mModel = &m;
        mSnapPoints.clear();
        mSnapPoints.push_back(0.0);
        for (const auto &c : m.clips) { mSnapPoints.push_back(c.at); mSnapPoints.push_back(c.at + c.duration); }
        std::sort(mSnapPoints.begin(), mSnapPoints.end());
    }

    double TimelineView::trackY(int order) const
    {
        return time::rulerHeight() + order * time::trackHeight();
    }

    double TimelineView::snap(double t) const
    {
        // The threshold is 8 px, converted through the LIVE zoom so it feels identical at every
        // zoom level. And the SNAPPED value is what the command will carry — the service must
        // never receive an unsnapped value and re-derive it.
        const double tol = 8.0 / std::max(1e-6, mPps.value());
        double best = t, bestD = tol;
        for (double s : mSnapPoints)
        {
            const double d = std::fabs(s - t);
            if (d < bestD) { bestD = d; best = s; }
        }
        if (std::fabs(mPlayhead - t) < bestD) best = mPlayhead;
        return best;
    }

    Rect TimelineView::clipRect(const std::string &name) const
    {
        if (!mModel) return {};
        for (const auto &c : mModel->clips)
        {
            if (c.name != name) continue;
            int order = 0;
            for (const auto &tr : mModel->tracks)
                if (tr.id == c.track) order = tr.order;
            const double x = xForTime(c.at);
            const double w = std::max(time::clipMinWidth(), c.duration * mPps.value());
            return Rect{x, trackY(order) + 2.0, w, time::trackHeight() - 4.0};
        }
        return {};
    }

    double TimelineView::playheadX() const { return xForTime(mPlayhead); }

    void TimelineView::layout() {}

    void TimelineView::advance(double nowMs)
    {
        mLastMs = nowMs;
        // A zoom is a visible change and must ease; 200 ms is cosmo's rail/indicator duration.
        if (std::fabs(mPpsTarget - mPps.value()) > 1e-9 && !mPps.isAnimating())
            mPps.animateTo(mPpsTarget, 200.0, Easing::EaseOutCubic, nowMs);
        if (std::fabs(mScrollTarget - mScroll.value()) > 1e-9 && !mScroll.isAnimating())
            mScroll.animateTo(mScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
        mPps.update(nowMs);
        mScroll.update(nowMs);
        Segment::advance(nowMs);
    }

    void TimelineView::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        auto fillRect = [&](double x, double y, double rw, double rh, const Color &c) {
            if (rw <= 0 || rh <= 0) return;
            t.setFill(c);
            t.beginPath();
            t.moveTo(x, y); t.lineTo(x + rw, y); t.lineTo(x + rw, y + rh); t.lineTo(x, y + rh);
            t.closePath();
            t.fillPath();
        };

        const double hdr = time::headerWidth();
        fillRect(0, 0, w, h, surface::deckBg());
        fillRect(0, 0, w, time::rulerHeight(), surface::rulerBg());

        if (!mModel)
        {
            const std::string msg = "drag a source here";
            const double tw = t.measureText(msg, 12.0, font::sans(), 0.0);
            t.setFill(palette::mutedForeground());
            t.drawText(msg, (w - tw) * 0.5, h * 0.5, 12.0, font::sans(), 0.0);
            return;
        }

        // ── the ruler, from the LIVE zoom ──
        const double pps = mPps.value();
        const double step = tickStep(pps);
        const double t0 = std::floor(mScroll.value() / step) * step;
        t.setFill(palette::mutedForeground());
        for (double tt = t0; xForTime(tt) < w; tt += step)
        {
            const double x = xForTime(tt);
            if (x < hdr) continue;   // never under the header column
            t.setStroke(palette::border(), 1.0);
            t.beginPath();
            t.moveTo(x, time::rulerHeight() - 6.0);
            t.lineTo(x, time::rulerHeight());
            t.closePath();
            t.strokePath();
            t.setFill(palette::mutedForeground());
            t.drawText(timecode(tt, mModel->fps), x + 3.0, time::rulerHeight() - 8.0, 9.0,
                       font::mono(), 0.0);
        }

        // ── tracks, banded so a lane boundary is visible without a divider per row ──
        int rows = 0;
        for (const auto &tr : mModel->tracks) rows = std::max(rows, tr.order + 1);
        for (int i = 0; i < rows; ++i)
            fillRect(hdr, trackY(i), w - hdr, time::trackHeight(),
                     i % 2 ? surface::trackBgAlt() : surface::trackBg());
        // The header column: each track's bind name, which is also the address prefix a user
        // types in an expression.
        for (const auto &tr : mModel->tracks)
        {
            const double y = trackY(tr.order);
            fillRect(0, y, hdr, time::trackHeight(), palette::card());
            std::string nm = tr.name + (tr.audio ? "  audio" : "");
            while (!nm.empty() && t.measureText(nm, 10.0, font::sansMedium(), 0.0) > hdr - 16.0)
                nm.pop_back();
            t.setFill(tr.mute ? palette::mutedForeground() : palette::foreground());
            t.drawText(nm, 8.0, y + time::trackHeight() * 0.5 + 3.5, 10.0, font::sansMedium(), 0.0);
        }

        // ── clips. Culled by the same rect the paint uses, so a clip cannot be drawn but
        //    unhittable, nor hittable but undrawn.
        for (const auto &c : mModel->clips)
        {
            const Rect r = clipRect(c.name);
            if (r.w <= 0 || r.x > w || r.x + r.w < hdr) continue;
            const bool sel = c.name == mSelected;
            t.setFill(sel ? surface::clipFillSel() : surface::clipFill());
            drawRoundedRect(t, r, radius::control(), Paint::filled(sel ? surface::clipFillSel() : surface::clipFill()));
            if (c.srcOffline)
            {
                // Offline reads as MISSING, never as a stall: the project stays openable and the
                // clip says what is wrong (R-RACK-5).
                t.setStroke(palette::destructive(), 1.0);
                t.beginPath();
                t.moveTo(r.x, r.y); t.lineTo(r.x + r.w, r.y + r.h);
                t.closePath();
                t.strokePath();
            }
            // The label is ellipsized against the space ACTUALLY left, measured — not estimated.
            std::string label = c.srcName.empty() ? c.name : c.name + "  " + c.srcName;
            const double avail = r.w - 8.0;
            while (!label.empty() && t.measureText(label, 10.0, font::sans(), 0.0) > avail)
                label.pop_back();
            if (avail > 12.0)
            {
                t.setFill(palette::foreground());
                t.drawText(label, r.x + 4.0, r.y + r.h * 0.5 + 3.5, 10.0, font::sans(), 0.0);
            }
        }

        // ── the playhead, last so it is never covered ──
        const double px = playheadX();
        if (px >= hdr && px <= w)
        {
            t.setStroke(surface::playhead(), 1.0);
            t.beginPath();
            t.moveTo(px, 0); t.lineTo(px, h);
            t.closePath();
            t.strokePath();
        }

        t.setStroke(palette::border(), 1.0);
        t.beginPath();
        t.moveTo(hdr, 0); t.lineTo(hdr, h);
        t.closePath();
        t.strokePath();
    }

    bool TimelineView::handleGesture(const Gesture &g, const Point &local)
    {
        if (!mModel) return Segment::handleGesture(g, local);

        const bool inRuler = local.y < time::rulerHeight();
        switch (g.type)
        {
            case Gesture::Type::Down:
            {
                if (inRuler)
                {
                    mScrubbing = true;
                    if (onScrub) onScrub(std::max(0.0, timeForX(local.x)));
                    return true;
                }
                for (const auto &c : mModel->clips)
                {
                    const Rect r = clipRect(c.name);
                    if (!r.contains(local)) continue;
                    mDragging = c.name;
                    mSelected = c.name;
                    // A drag must never teleport: store the grab offset and add it back, so the
                    // clip keeps the same relationship to the pointer it had on Down.
                    mDragGrab = c.at - timeForX(local.x);
                    if (onClipPicked) onClipPicked(c.name);
                    return true;
                }
                return true;
            }
            case Gesture::Type::Drag:
            case Gesture::Type::Move:
            {
                if (mScrubbing && onScrub) { onScrub(std::max(0.0, timeForX(local.x))); return true; }
                if (!mDragging.empty() && onClipMoved)
                {
                    const double raw = timeForX(local.x) + mDragGrab;
                    onClipMoved(mDragging, std::max(0.0, snap(raw)));
                    return true;
                }
                break;
            }
            case Gesture::Type::Up:
                mScrubbing = false;
                mDragging.clear();
                break;
            case Gesture::Type::Scroll:
            {
                // A wheel notch is not one pixel. Ctrl+wheel zooms about the pointer, so the
                // time under the cursor stays put; a plain wheel scrolls by ~10 px of time.
                if (g.ctrl)
                {
                    const double anchorT = timeForX(local.x);
                    const double next = std::max(2.0, std::min(4000.0,
                                                               mPpsTarget * (g.delta.y > 0 ? 1.25 : 0.8)));
                    zoomTo(next);
                    // Keep the anchor under the pointer, at the TARGET zoom — the scroll then
                    // eases there alongside the zoom rather than fighting it.
                    mScrollTarget = std::max(0.0, anchorT - local.x / next);
                    return true;
                }
                setScroll(mScrollTarget - g.delta.y * 10.0 / std::max(1e-6, mPps.value()));
                return true;
            }
            default: break;
        }
        return Segment::handleGesture(g, local);
    }
}
}
