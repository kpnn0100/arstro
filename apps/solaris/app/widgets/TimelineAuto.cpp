// Timeline — the AUTOMATION rows (R-AUTO-6): curves drawn, points edited, every gesture one command.
// Bezier handles (R-AUTO-10) and the automation's window (R-AUTO-11) start here too.
#include "Timeline.h"
#include "Expr.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;

    namespace
    {
        constexpr double kPad = 6.5;       // space::u(2): the curve keeps clear of the row's edges
        constexpr double kHandle = 4.0;    // a point's radius as drawn
        constexpr double kGrab = 7.0;      // … and as hit
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string num(double v)
        {
            char b[32];
            std::snprintf(b, sizeof b, "%.6g", v);
            return b;
        }
        std::string beats(double b) { return Timeline::beatText(b); } // to the tick
        bool logShown(const solaris::AutomationModel &m) { return (m.unit == "Hz" || m.unit == "ms") && m.min > 0 && m.max > m.min; }
        constexpr double kHandleDot = 3.0;      // a bezier handle's end as drawn
        constexpr double kDoubleClickMs = 300.0; // Artboard's recognizer: a click waits this out (+ a frame or three)
        namespace engine = ::arstro::solaris::engine;
        namespace anim = ::arstro::interstellar::anim;

        bool sameShapes(const std::vector<solaris::AutoPointModel> &a, const std::vector<solaris::AutoPointModel> &b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (a[i].shape != b[i].shape) return false;
            return true;
        }
        /** The ENGINE's keys for the drawn points, in beats (speeds per beat): what is drawn is what plays. */
        std::vector<anim::Key> keysOf(const std::vector<solaris::AutoPointModel> &pts)
        {
            std::vector<engine::CurvePoint> cps;
            cps.reserve(pts.size());
            for (const auto &p : pts)
            {
                engine::CurvePoint c;
                c.t = p.at;
                c.v = p.value;
                if (!engine::shapeNamed(p.shape, c.shape)) c.shape = engine::CurvePoint::Linear;
                c.speedIn = p.speedIn;
                c.inflIn = p.inflIn;
                c.speedOut = p.speedOut;
                c.inflOut = p.inflOut;
                cps.push_back(c);
            }
            return engine::curveKeys(cps);
        }
        /** Where a bezier point's handle ends (beat, value): its influence of the segment on that side, along its speed. */
        bool handleEnd(const std::vector<solaris::AutoPointModel> &pts, int i, int side, double &beat, double &value)
        {
            if (i < 0 || i >= (int)pts.size() || pts[(size_t)i].shape != "bezier") return false;
            const auto &p = pts[(size_t)i];
            if (side == 2)
            {
                if (i + 1 >= (int)pts.size()) return false;
                const double reach = std::clamp(p.inflOut, 0.1, 100.0) / 100.0 * (pts[(size_t)i + 1].at - p.at);
                beat = p.at + reach;
                value = p.value + p.speedOut * reach;
                return true;
            }
            if (i == 0) return false;
            const double reach = std::clamp(p.inflIn, 0.1, 100.0) / 100.0 * (p.at - pts[(size_t)i - 1].at);
            beat = p.at - reach;
            value = p.value - p.speedIn * reach;
            return true;
        }
        double parsed(const std::string &text) { return std::strtod(text.c_str(), nullptr); }
    }

    std::vector<solaris::AutoPointModel> Timeline::shownPoints(const AutoLive &l) const
    {
        const double f = l.t.value();
        if (f >= 1.0 || !sameShapes(l.from, l.to)) return f >= 0.5 || l.from.empty() ? l.to : l.from;
        std::vector<solaris::AutoPointModel> out = l.to;
        auto mix = [f](double a, double b) { return a + (b - a) * f; };
        for (size_t i = 0; i < out.size(); ++i)
        {
            out[i].at = mix(l.from[i].at, l.to[i].at);
            out[i].value = mix(l.from[i].value, l.to[i].value);
            out[i].speedIn = mix(l.from[i].speedIn, l.to[i].speedIn);   // a handle a shell moved eases too
            out[i].inflIn = mix(l.from[i].inflIn, l.to[i].inflIn);
            out[i].speedOut = mix(l.from[i].speedOut, l.to[i].speedOut);
            out[i].inflOut = mix(l.from[i].inflOut, l.to[i].inflOut);
        }
        return out;
    }

    double Timeline::valueToY(const AutoLive &l, double v, const Rect &row) const
    {
        const auto &m = l.model;
        double u = 0.0;
        if (logShown(m)) u = std::log(std::max(v, m.min) / m.min) / std::log(m.max / m.min);
        else if (m.max > m.min) u = (v - m.min) / (m.max - m.min);
        u = std::clamp(u, 0.0, 1.0);
        return row.bottom() - kPad - u * (row.h - 2.0 * kPad);
    }

    double Timeline::yToValue(const AutoLive &l, double y, const Rect &row) const
    {
        const auto &m = l.model;
        const double u = std::clamp((row.bottom() - kPad - y) / std::max(1.0, row.h - 2.0 * kPad), 0.0, 1.0);
        if (logShown(m)) return m.min * std::pow(m.max / m.min, u);
        return m.min + u * (m.max - m.min);
    }

    Rect Timeline::autoRowRect(const std::string &au) const
    {
        for (size_t k = 0; k < mAutoIds.size(); ++k)
            if (mAutoIds[k] == au) return rowRect((int)(mRows.size() + k)); // after the lanes, in the same eased list
        return Rect{};
    }

    std::string Timeline::autoAt(double y) const
    {
        for (const auto &id : mAutoIds)
        {
            const Rect r = autoRowRect(id);
            if (y >= r.y && y < r.bottom()) return id;
        }
        return std::string();
    }

    Point Timeline::autoPointAt(const std::string &au, int i) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end()) return Point{-1, -1};
        const auto pts = shownPoints(it->second);
        if (i < 0 || i >= (int)pts.size()) return Point{-1, -1};
        const Rect row = autoRowRect(au);
        return Point{beatToX(pts[(size_t)i].at + autoOffset(it->second)), valueToY(it->second, pts[(size_t)i].value, row)};
    }

    double Timeline::autoValueAt(const std::string &au, double y) const
    {
        const auto it = mAutos.find(au);
        return it == mAutos.end() ? 0.0 : yToValue(it->second, y, autoRowRect(au));
    }

    double Timeline::autoEase(const std::string &au) const
    {
        const auto it = mAutos.find(au);
        return it == mAutos.end() ? 1.0 : it->second.t.value();
    }

    Point Timeline::autoDragPoint() const
    {
        const auto it = mAutos.find(mAutoPress);
        if (!mAutoDragging || mAutoHandle != 0 || it == mAutos.end()) return Point{-1, -1}; // a handle held moves no point
        return Point{beatToX(mAutoAt + autoOffset(it->second)), valueToY(it->second, mAutoValue, autoRowRect(mAutoPress))};
    }

    std::vector<solaris::AutoPointModel> Timeline::drawnPoints(const AutoLive &l) const
    {
        std::vector<solaris::AutoPointModel> pts = shownPoints(l);
        if (!mAutoDragging || mAutoPress != l.model.id || mAutoPoint < 0 || mAutoPoint >= (int)pts.size()) return pts;
        auto &p = pts[(size_t)mAutoPoint];
        if (mAutoHandle != 0)
        {
            // a handle held: the pointer's numbers (R-AUTO-10)
            p.shape = "bezier";
            p.speedIn = mAutoHeld.speedIn;
            p.inflIn = mAutoHeld.inflIn;
            p.speedOut = mAutoHeld.speedOut;
            p.inflOut = mAutoHeld.inflOut;
            return pts;
        }
        p.at = mAutoAt; // the pointer is the animation
        p.value = mAutoValue;
        std::stable_sort(pts.begin(), pts.end(), [](const auto &x, const auto &y) { return x.at < y.at; });
        return pts;
    }

    Point Timeline::autoHandleAt(const std::string &au, int i, int side) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end()) return Point{-1, -1};
        double b = 0, v = 0;
        if (!handleEnd(drawnPoints(it->second), i, side, b, v)) return Point{-1, -1};
        return Point{beatToX(b + autoOffset(it->second)), valueToY(it->second, v, autoRowRect(au))};
    }

    int Timeline::handleNear(const std::string &au, const Point &local, int &side) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end()) return -1;
        const auto pts = shownPoints(it->second);
        int best = -1;
        double bestD = kGrab;
        for (int i = 0; i < (int)pts.size(); ++i)
            for (int sd : {1, 2})
            {
                const Point h = autoHandleAt(au, i, sd);
                if (h.x < 0) continue;
                const double d = std::hypot(h.x - local.x, h.y - local.y);
                if (d <= bestD) { best = i; bestD = d; side = sd; }
            }
        return best;
    }

    void Timeline::dragHandle(const Point &local, bool alt)
    {
        // cosmo's gesture over Interstellar's numbers (R-AUTO-10): a handle's end is its influence of the
        // segment on its side (time) along its speed (slope). Alt-pull: both sides the pointer's slope and
        // reach; a handle: its own side, the opposite mirrored (Alt: left alone — the symmetry breaks).
        const auto it = mAutos.find(mAutoPress);
        if (it == mAutos.end()) return;
        const auto &pts = it->second.to;
        const int i = mAutoPoint;
        if (i < 0 || i >= (int)pts.size()) return;
        const auto &p = pts[(size_t)i];
        const double b = xToBeat(local.x) - autoOffset(it->second), v = yToValue(it->second, local.y, autoRowRect(mAutoPress));
        const double dtPrev = i > 0 ? p.at - pts[(size_t)i - 1].at : 0.0, dtNext = i + 1 < (int)pts.size() ? pts[(size_t)i + 1].at - p.at : 0.0;
        auto pct = [](double reach, double dt) { return std::clamp(reach / dt * 100.0, 0.1, 100.0); };
        solaris::AutoPointModel &h = mAutoHeld;
        h.shape = "bezier";
        if (mAutoHandle == 3)
        {
            const double d = b - p.at, reach = std::fabs(d);
            const double speed = reach > 1e-9 ? (v - p.value) / d : 0.0;
            h.speedIn = h.speedOut = speed;
            if (dtNext > 0) h.inflOut = pct(reach, dtNext);
            if (dtPrev > 0) h.inflIn = pct(reach, dtPrev);
            return;
        }
        if (mAutoHandle == 2 && dtNext > 0)
        {
            const double reach = std::clamp(b - p.at, 0.001 * dtNext, dtNext); // never behind its point, never past the next
            h.inflOut = reach / dtNext * 100.0;
            h.speedOut = (v - p.value) / reach;
            if (!alt)
            {
                h.speedIn = h.speedOut;
                if (dtPrev > 0) h.inflIn = pct(reach, dtPrev);
            }
        }
        else if (mAutoHandle == 1 && dtPrev > 0)
        {
            const double reach = std::clamp(p.at - b, 0.001 * dtPrev, dtPrev);
            h.inflIn = reach / dtPrev * 100.0;
            h.speedIn = (p.value - v) / reach;
            if (!alt)
            {
                h.speedOut = h.speedIn;
                if (dtNext > 0) h.inflOut = pct(reach, dtNext);
            }
        }
    }

    double Timeline::autoOffset(const AutoLive &l) const
    {
        if (l.model.loop <= 0) return 0.0;
        double a = 0, b = 0;
        spanOf(l, a, b);
        return a;
    }

    void Timeline::spanOf(const AutoLive &l, double &a, double &b) const
    {
        // eased — but a grip in hand is the pointer's (the other end stays)
        a = l.spanPlaced ? l.spanA.value() : l.model.at;
        b = l.spanPlaced ? l.spanB.value() : (l.model.length > 0 ? l.model.at + l.model.length : std::max(mLength, l.model.at + l.model.loop));
        if (mSpanDragging && mSpanAu == l.model.id) (mSpanEnd == 0 ? a : b) = mSpanLive;
    }

    int Timeline::spanGripAt(const std::string &au, const Point &local) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end() || it->second.model.loop <= 0) return -1;
        const Rect row = autoRowRect(au);
        if (local.y < row.y || local.y > row.y + 9.75) return -1; // the brace's band along the row's top: space::u(3)
        double a = 0, b = 0;
        spanOf(it->second, a, b);
        if (std::fabs(local.x - beatToX(a)) <= 5.0) return 0;
        if (std::fabs(local.x - beatToX(b)) <= 5.0) return 1;
        return -1;
    }

    Rect Timeline::autoSpanRect(const std::string &au) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end() || it->second.model.loop <= 0) return Rect{};
        double a = 0, b = 0;
        spanOf(it->second, a, b);
        const Rect row = autoRowRect(au);
        return Rect{beatToX(a), row.y, beatToX(b) - beatToX(a), 9.75};
    }

    void Timeline::paintLoop(IRenderTarget &t, const AutoLive &l, const std::vector<solaris::AutoPointModel> &pts, const Rect &row, double a) const
    {
        if (pts.empty() || a <= 0.001) return;
        const double W = width.value(), L = l.model.loop;
        double A = 0, B = 0;
        spanOf(l, A, B);
        const bool open = l.model.length <= 0 && !(mSpanDragging && mSpanAu == l.model.id && mSpanEnd == 1); // to the song's end, and on
        const std::vector<anim::Key> keys = keysOf(pts);
        // the value the engine plays at a beat: its place in the cycle inside the span, the cycle's end outside it
        auto value = [&](double beat) {
            const double c = beat < A || (!open && beat >= B) ? L : std::fmod(beat - A, L);
            return std::clamp(anim::eval(keys, c), l.model.min, l.model.max);
        };
        t.beginPath();
        bool first = true;
        for (double x = kHeaderW; x <= W + 2.0; x += 2.0)
        {
            const double y = valueToY(l, value(xToBeat(x)), row);
            if (first) t.moveTo(x, y);
            else t.lineTo(x, y);
            first = false;
        }
        t.setStroke(fade(palette::primary(), 0.4 * a), 1.5);
        t.strokePath();
        // a seam at each repeat's start, when the cycles are wide enough to tell apart
        if (beatToX(L) - beatToX(0.0) >= 6.0)
        {
            t.setStroke(fade(palette::whiteAlpha(0.16), a), 1.0);
            const double end = open ? xToBeat(W) : B;
            const double k0 = std::max(1.0, std::floor((xToBeat(kHeaderW) - A) / L));
            for (double b = A + k0 * L; b < end - 1e-9; b += L)
            {
                const double x = std::round(beatToX(b)) + 0.5;
                if (x > W) break;
                if (x < kHeaderW) continue;
                t.beginPath(); t.moveTo(x, row.y + kPad); t.lineTo(x, row.bottom() - kPad); t.strokePath();
            }
        }
        // the span: a brace along the row's top, a grip at each end
        const double xa = beatToX(A), xb = beatToX(B);
        drawRoundedRect(t, Rect{xa, row.y + 2.0, std::max(0.0, xb - xa), 3.0}, radius::pill(), Paint::filled(fade(palette::primaryAlpha(0.55), a)));
        for (double x : {xa, xb})
            drawRoundedRect(t, Rect{x - 1.5, row.y + 1.0, 3.0, 8.0}, radius::pill(), Paint::filled(fade(palette::primary(), a)));
    }

    void Timeline::advanceAuto(double nowMs)
    {
        // R-AUTO-12: a span set from anywhere eases there (a grip let go is already there — it was the pointer's)
        for (auto &e : mAutos)
        {
            AutoLive &l = e.second;
            const double a = l.model.at, b = l.model.length > 0 ? l.model.at + l.model.length : std::max(mLength, l.model.at + l.model.loop);
            if (!l.spanPlaced) { l.spanA.set(a); l.spanB.set(b); l.spanALast = a; l.spanBLast = b; l.spanPlaced = true; }
            if (a != l.spanALast) { l.spanA.animateTo(a, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.spanALast = a; }
            if (b != l.spanBLast) { l.spanB.animateTo(b, motion::kSelectMs, Easing::EaseOutCubic, nowMs); l.spanBLast = b; }
            l.spanA.update(nowMs);
            l.spanB.update(nowMs);
        }
        // a click on a curve, once it is not a double-click, is the point it asked for (R-AUTO-6, R-AUTO-11)
        if (mPendWaiting && nowMs - mPendMs > kDoubleClickMs + 50.0)
        {
            mPendWaiting = false;
            mPendAmt.animateTo(0.0, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); // the real point cross-fades in as it goes
            if (onCommand) onCommand("auto point add " + mPendAu + " --at " + beats(mPendAt) + " --value " + num(mPendValue));
        }
        mPendAmt.update(nowMs);
    }

    int Timeline::pointNear(const std::string &au, const Point &local) const
    {
        const auto it = mAutos.find(au);
        if (it == mAutos.end()) return -1;
        const auto pts = shownPoints(it->second);
        int best = -1;
        double bestD = kGrab;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const Point p = autoPointAt(au, (int)i);
            const double d = std::hypot(p.x - local.x, p.y - local.y);
            if (d <= bestD) { best = (int)i; bestD = d; }
        }
        return best;
    }

    void Timeline::paintCurve(IRenderTarget &t, const AutoLive &l, const std::vector<solaris::AutoPointModel> &pts, const Rect &row, double a,
                              bool handles) const
    {
        if (pts.empty() || a <= 0.001) return;
        const double W = width.value(), off = autoOffset(l); // R-AUTO-12: a loop's first cycle sits at its span's start
        auto beatToX = [this, off](double b) { return Timeline::beatToX(b + off); };
        const Color line = fade(palette::primary(), a);
        // the curve, by the engine's own keys (R-AUTO-10): before the first point the first value, after the last the last
        const std::vector<anim::Key> keys = keysOf(pts);
        t.beginPath();
        t.moveTo(kHeaderW, valueToY(l, pts.front().value, row));
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const double x = beatToX(pts[i].at), y = valueToY(l, pts[i].value, row);
            t.lineTo(x, y);
            if (i + 1 >= pts.size()) continue;
            const double x2 = beatToX(pts[i + 1].at);
            if (keys[i].out == anim::Side::Hold) t.lineTo(x2, y);
            else if ((keys[i].out != anim::Side::Linear || keys[i + 1].in != anim::Side::Linear) && x2 > kHeaderW && x < W)
            {
                const int steps = std::clamp((int)((x2 - x) / 2.5), 6, 480);
                for (int k = 1; k < steps; ++k)
                {
                    const double b = pts[i].at + (pts[i + 1].at - pts[i].at) * k / steps;
                    t.lineTo(beatToX(b), valueToY(l, anim::segment(keys[i], keys[i + 1], b), row));
                }
            }
        }
        t.lineTo(W, valueToY(l, pts.back().value, row));
        t.setStroke(line, 1.5);
        t.strokePath();
        if (!handles) return;
        // a bezier point's handles: a stem from the point to each end (cosmo's curve)
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (pts[i].shape != "bezier") continue;
            const double x = beatToX(pts[i].at), y = valueToY(l, pts[i].value, row);
            for (int side : {1, 2})
            {
                double hb = 0, hv = 0;
                if (!handleEnd(pts, (int)i, side, hb, hv)) continue;
                const double hx = beatToX(hb), hy = valueToY(l, hv, row);
                t.setStroke(fade(palette::primary(), a * 0.7), 1.0);
                t.beginPath(); t.moveTo(x, y); t.lineTo(hx, hy); t.strokePath();
                const bool held = mAutoDragging && mAutoPress == l.model.id && mAutoPoint == (int)i && (mAutoHandle == side || mAutoHandle == 3);
                drawCircle(t, hx, hy, kHandleDot + (held ? 1.0 : 0.0), Paint::filledStroked(fade(palette::primary(), a), fade(palette::popover(), a), 1.0));
            }
        }
        for (size_t i = 0; i < pts.size(); ++i)
        {
            const double x = beatToX(pts[i].at), y = valueToY(l, pts[i].value, row);
            if (x < kHeaderW - kHandle || x > W + kHandle) continue;
            const bool dragged = mAutoDragging && mAutoPress == l.model.id && mAutoPoint == (int)i && mAutoHandle == 0;
            drawCircle(t, x, y, kHandle + (dragged ? 1.0 : 0.0), Paint::filledStroked(fade(palette::popover(), a), line, 1.5));
        }
    }

    void Timeline::paintAutomation(IRenderTarget &t) const
    {
        for (const auto &row : mRowMotion.rows())
        {
            if (row.data.automation.empty()) continue;
            const auto it = mAutos.find(row.data.automation);
            if (it == mAutos.end()) continue;
            const AutoLive &l = it->second;
            const Rect r{0.0, top() + row.liveY() - mScrollY.value(), width.value(), kRowH};
            const double a = row.liveAlpha();
            if (r.bottom() < top() || r.y > height.value() || a <= 0.001) continue;
            // the range's two ends, faint
            t.setStroke(fade(palette::whiteAlpha(0.06), a), 1.0);
            for (double y : {r.y + kPad, r.bottom() - kPad})
            {
                t.beginPath(); t.moveTo(kHeaderW, std::round(y) + 0.5); t.lineTo(width.value(), std::round(y) + 0.5); t.strokePath();
            }
            const std::vector<solaris::AutoPointModel> pts = drawnPoints(l); // a held point or handle: the pointer's
            const bool held = mAutoDragging && mAutoPress == l.model.id;
            const double f = l.t.value();
            const bool loops = l.model.loop > 0;
            if (loops)
            {
                // R-AUTO-12: the whole of it faint (its repeats, and where it rests outside its span), its span's brace;
                // then its FIRST cycle bright — the one that is edited — clipped to that cycle
                paintLoop(t, l, pts, r, a);
                const double x0 = beatToX(autoOffset(l)), x1 = beatToX(autoOffset(l) + l.model.loop);
                t.save();
                t.clipRect(x0 - kHandle - 1.5, r.y, std::max(0.0, x1 - x0) + 2.0 * kHandle + 3.0, r.h);
            }
            if (!held && f < 1.0 && !sameShapes(l.from, l.to))
            {
                // a point more or fewer, or a shape changed: the two curves cross-fade (handles with them)
                paintCurve(t, l, l.from, r, a * (1.0 - f), false);
                paintCurve(t, l, l.to, r, a * f, true);
            }
            else paintCurve(t, l, pts, r, a, true);
            if (loops) t.restore();
            // a click's point, drawn at once while it waits out a double-click (R-AUTO-11)
            if (mPendAu == l.model.id && mPendAmt.value() > 0.001)
            {
                const double pa = a * mPendAmt.value();
                drawCircle(t, beatToX(mPendAt + autoOffset(l)), valueToY(l, mPendValue, r), kHandle,
                           Paint::filledStroked(fade(palette::popover(), pa), fade(palette::primary(), pa), 1.5));
            }
        }
    }

    bool Timeline::autoGesture(const Gesture &g, const Point &local)
    {
        const bool inRows = local.x >= kHeaderW && local.y >= top();
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mAutoPress.clear();
            mAutoPoint = -1;
            mAutoDragging = false;
            mAutoHandle = 0;
            mSpanAu.clear();
            mSpanDragging = false;
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty()) return false;
            if (const int grip = spanGripAt(au, local); grip >= 0)
            {
                // R-AUTO-12: a span's end taken — the pointer has it until it is let go, then ONE line
                double a = 0, b = 0;
                spanOf(mAutos.find(au)->second, a, b);
                mSpanAu = au;
                mSpanEnd = grip;
                mSpanLive = grip == 0 ? a : b;
                return true;
            }
            mAutoPress = au;
            // a handle's end, unless the pointer is right on a point (R-AUTO-10); Alt on a point pulls handles out
            int side = 0;
            const int h = handleNear(au, local, side), p = pointNear(au, local);
            const Point pp = p >= 0 ? autoPointAt(au, p) : Point{-1e9, -1e9};
            const bool onPoint = p >= 0 && std::hypot(pp.x - local.x, pp.y - local.y) <= kHandle + 1.0;
            if (h >= 0 && !onPoint) { mAutoPoint = h; mAutoHandle = side; }
            else { mAutoPoint = p; mAutoHandle = p >= 0 && g.alt ? 3 : 0; }
            const auto it = mAutos.find(au);
            if (mAutoPoint >= 0 && it != mAutos.end() && mAutoPoint < (int)it->second.to.size()) mAutoHeld = it->second.to[(size_t)mAutoPoint];
            return true;
        }
        case Gesture::Type::DragStart:
            if (!mSpanAu.empty()) mSpanDragging = true;
            if (!mSpanAu.empty() || mAutoPress.empty() || mAutoPoint < 0) return !mAutoPress.empty() || !mSpanAu.empty();
            mAutoDragging = true;
            [[fallthrough]];
        case Gesture::Type::Drag:
        {
            if (mSpanDragging)
            {
                const auto it = mAutos.find(mSpanAu);
                if (it == mAutos.end()) return true;
                const auto &m = it->second.model;
                double a = 0, b = 0;
                spanOf(it->second, a, b);
                const double at = snap(xToBeat(local.x)); // the grid you see; never shorter than one cycle
                const double endNow = m.length > 0 ? m.at + m.length : b;
                mSpanLive = mSpanEnd == 0 ? std::clamp(at, 0.0, m.length > 0 ? endNow - m.loop : 1e9) : std::max(at, a + m.loop);
                return true;
            }
            if (!mAutoDragging) return !mAutoPress.empty();
            if (mAutoHandle != 0)
            {
                dragHandle(local, g.alt); // the pointer is the animation
                return true;
            }
            const auto it = mAutos.find(mAutoPress);
            if (it == mAutos.end()) return true;
            mAutoAt = snap(xToBeat(local.x)) - autoOffset(it->second); // the grid you see (R-UI-10), in the curve's own beats
            if (it->second.model.loop > 0) mAutoAt = std::clamp(mAutoAt, 0.0, it->second.model.loop); // a loop's points stay in its cycle
            mAutoValue = yToValue(it->second, local.y, autoRowRect(mAutoPress));
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (!mSpanAu.empty())
            {
                // R-AUTO-12: ONE line for the grip's whole gesture; the start keeps the end where it was
                const auto it = mAutos.find(mSpanAu);
                std::string line;
                if (mSpanDragging && it != mAutos.end())
                {
                    AutoLive &l = it->second;
                    const auto &m = l.model;
                    if (mSpanEnd == 0 && std::fabs(mSpanLive - m.at) > 1e-9)
                    {
                        line = "set " + m.id + ".at=" + beats(mSpanLive);
                        if (m.length > 0) line += " " + m.id + ".length=" + beats(m.at + m.length - mSpanLive);
                        l.spanA.set(mSpanLive); // where it was let go — the pointer was the animation
                        l.spanALast = mSpanLive;
                    }
                    else if (mSpanEnd == 1)
                    {
                        const double len = mSpanLive - m.at;
                        if (std::fabs(len - m.length) > 1e-9) line = "set " + m.id + ".length=" + beats(len);
                        l.spanB.set(mSpanLive);
                        l.spanBLast = mSpanLive;
                    }
                }
                mSpanAu.clear();
                mSpanDragging = false;
                if (!line.empty() && onCommand) onCommand(line);
                return true;
            }
            if (mAutoPress.empty()) return false;
            if (mAutoDragging && mAutoHandle != 0)
            {
                // ONE line for the whole handle gesture; it stays where it was let go — the numbers sent are the
                // numbers the model will hold, so nothing eases after the release
                const auto it = mAutos.find(mAutoPress);
                if (it != mAutos.end() && mAutoPoint >= 0 && mAutoPoint < (int)it->second.to.size() && onCommand)
                {
                    solaris::AutoPointModel h = mAutoHeld;
                    const std::string si = num(h.speedIn), ii = num(h.inflIn), so = num(h.speedOut), io = num(h.inflOut);
                    h.shape = "bezier";
                    h.speedIn = parsed(si);
                    h.inflIn = parsed(ii);
                    h.speedOut = parsed(so);
                    h.inflOut = parsed(io);
                    AutoLive &l = it->second;
                    const std::string line = "auto point shape " + mAutoPress + " --at " + beats(l.to[(size_t)mAutoPoint].at) + " --shape bezier --speed-in " + si +
                                             " --influence-in " + ii + " --speed-out " + so + " --influence-out " + io;
                    l.from = l.to;
                    l.from[(size_t)mAutoPoint] = h;
                    l.to = l.from;
                    l.t.set(1.0);
                    mAutoDragging = false;
                    mAutoHandle = 0;
                    mAutoPress.clear();
                    onCommand(line);
                    return true;
                }
            }
            else if (mAutoDragging)
            {
                const auto it = mAutos.find(mAutoPress);
                const auto pts = it != mAutos.end() ? it->second.to : std::vector<solaris::AutoPointModel>{};
                if (mAutoPoint >= 0 && mAutoPoint < (int)pts.size() && onCommand)
                {
                    // it stays where it was let go: the drawn curve starts from there
                    AutoLive &l = it->second;
                    l.from = l.to;
                    l.from[(size_t)mAutoPoint].at = mAutoAt;
                    l.from[(size_t)mAutoPoint].value = mAutoValue;
                    std::stable_sort(l.from.begin(), l.from.end(), [](const auto &x, const auto &y) { return x.at < y.at; });
                    l.to = l.from;
                    const std::string line = "auto point move " + mAutoPress + " --at " + beats(pts[(size_t)mAutoPoint].at) + " --to " + beats(mAutoAt) +
                                             " --value " + num(mAutoValue);
                    mAutoDragging = false;
                    mAutoPress.clear();
                    onCommand(line);
                    return true;
                }
            }
            mAutoDragging = false;
            mAutoHandle = 0;
            const bool was = !mAutoPress.empty();
            mAutoPress.clear();
            return was;
        }
        case Gesture::Type::Click:
        {
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty()) return false;
            if (pointNear(au, local) >= 0) return true; // a click on a point selects nothing, adds nothing
            int side = 0;
            if (handleNear(au, local, side) >= 0) return true; // nor on a handle
            const auto it = mAutos.find(au);
            if (spanGripAt(au, local) >= 0) return true; // a click on a span's grip adds nothing (R-AUTO-12)
            double at = snap(xToBeat(local.x)) - autoOffset(it->second); // the grid you see (R-UI-10), in the curve's own beats
            if (it->second.model.loop > 0 && at >= it->second.model.loop) at = std::fmod(at, it->second.model.loop); // a repeat: its cycle
            const double v = yToValue(it->second, local.y, autoRowRect(au));
            // a double-click here opens the window (R-AUTO-11): the point waits it out, drawn at once (R2), sent by advanceAuto
            mPendAu = au;
            mPendAt = at;
            mPendValue = v;
            mPendMs = mNowMs;
            mPendWaiting = true;
            mPendAmt.animateTo(1.0, motion::kHoverMs, Easing::EaseOutCubic, mNowMs);
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            if (local.x < kHeaderW && local.y >= top())
            {
                // an automation row's HEADER: its window (R-AUTO-11)
                const std::string au = autoAt(local.y);
                if (au.empty()) return false;
                if (onOpenAutomation) onOpenAutomation(au);
                return true;
            }
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty()) return false;
            const int i = pointNear(au, local);
            const auto it = mAutos.find(au);
            if (i >= 0)
            {
                // ON a point: it goes (R-AUTO-6)
                if (it != mAutos.end() && i < (int)it->second.to.size() && onCommand)
                    onCommand("auto point delete " + au + " --at " + beats(it->second.to[(size_t)i].at));
                return true;
            }
            int side = 0;
            if (handleNear(au, local, side) >= 0) return true;
            // on the curve away from a point: the automation's window — the first click's point never lands
            if (mPendWaiting && mPendAu == au)
            {
                mPendWaiting = false;
                mPendAmt.animateTo(0.0, motion::kHoverMs, Easing::EaseOutCubic, mNowMs);
            }
            if (onOpenAutomation) onOpenAutomation(au);
            return true;
        }
        case Gesture::Type::RightClick:
        {
            if (!inRows) return false;
            const std::string au = autoAt(local.y);
            if (au.empty() || !onMenu) return !au.empty();
            const int i = pointNear(au, local);
            const auto it = mAutos.find(au);
            std::vector<cosmo_v2::ContextMenu::Item> items;
            if (i >= 0 && it != mAutos.end() && i < (int)it->second.to.size())
            {
                const std::string at = beats(it->second.to[(size_t)i].at), now = it->second.to[(size_t)i].shape;
                static const char *kShapes[][2] = {{"linear", N_("Linear")}, {"hold", N_("Hold")}, {"smooth", N_("Smooth")}, {"bezier", N_("Bezier")}};
                for (const auto &shape : kShapes)
                {
                    const char *s = shape[0]; // the grammar's word, sent; its name, said
                    std::string label = say(shape[1]);
                    if (now == s) label += "  \xC2\xB7 " + say("now");
                    items.push_back({label, [this, au, at, s] { if (onCommand) onCommand("auto point shape " + au + " --at " + at + " --shape " + s); }});
                }
                items.push_back({say("Delete Point"), [this, au, at] { if (onCommand) onCommand("auto point delete " + au + " --at " + at); }});
            }
            items.push_back({say("Copy ID"), [this, au] { if (onCopy) onCopy(au); }});                 // R-UI-11
            items.push_back({say("Copy as Formula"), [this, au] { if (onCopy) onCopy("=" + au); }});   // what a formula reads it by
            items.push_back({say("Delete Automation"), [this, au] { if (onCommand) onCommand("auto delete " + au + " --unbind"); }});
            onMenu(items, g.pos);
            return true;
        }
        default:
            return !mAutoPress.empty();
        }
    }
}
}
