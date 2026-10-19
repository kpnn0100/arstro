// Timeline — automation CLIPS (R-AUTO-14): an automation's curve drawn in each clip of it — its first pass bright with its
// points and handles, each repeat fainter with a seam at its start — and its points edited there, in the pass under the
// pointer (R-AUTO-6), its bezier handles (R-AUTO-10); every gesture one command, in the automation's own beats. The
// clip itself is moved, copied and resized as any clip (its name strip, its end); its window (R-AUTO-11) starts here too.
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
        constexpr double kPad = 2.5;       // the curve keeps clear of its area's edges
        constexpr double kNameH = 15.0;    // a clip's name strip, over its curve (paintClip draws the name at its top)
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

    bool Timeline::autoFrame(const std::string &ac, AutoFrame &f) const
    {
        const ClipLive *l = live(ac);
        if (!l || l->v.c.kind != "automation") return false;
        const auto it = mAutos.find(l->v.c.automation);
        if (it == mAutos.end() || !(it->second.model.length > 0)) return false;
        const Rect r = clipRect(ac); // as drawn: eased
        if (r.w <= 0 || r.h <= kNameH + 2.0 * kPad) return false;
        f.clip = l;
        f.autom = &it->second;
        f.area = Rect{r.x, r.y + kNameH, r.w, r.h - kNameH - 2.0};
        f.at = l->at.value();
        f.end = f.at + liveLength(*l);
        f.cycle = it->second.model.length;
        return true;
    }

    double Timeline::passAt(const AutoFrame &f, double x) const
    {
        const double passes = std::max(1.0, std::ceil((f.end - f.at) / f.cycle - 1e-9));
        return f.at + std::clamp(std::floor((xToBeat(x) - f.at) / f.cycle), 0.0, passes - 1.0) * f.cycle;
    }

    std::string Timeline::autoClipAt(const Point &local) const
    {
        for (auto it = mLive.rbegin(); it != mLive.rend(); ++it) // topmost (drawn last) first; a ghost takes no input
        {
            if (it->gone || it->v.c.kind != "automation") continue;
            AutoFrame f;
            if (!autoFrame(it->v.c.id, f)) continue;
            const Rect r = clipRect(it->v.c.id);
            if (f.area.contains(local) && local.x < r.right() - kEdgeGrip) return it->v.c.id; // its end: the clip's (R-CLIP-7)
        }
        return std::string();
    }

    Rect Timeline::autoClipRect(const std::string &ac) const
    {
        AutoFrame f;
        return autoFrame(ac, f) ? f.area : Rect{};
    }

    Point Timeline::autoPointAt(const std::string &ac, int i) const
    {
        AutoFrame f;
        if (!autoFrame(ac, f)) return Point{-1, -1};
        const auto pts = drawnPoints(*f.autom); // as DRAWN: a held point is the pointer's
        if (i < 0 || i >= (int)pts.size()) return Point{-1, -1};
        return Point{beatToX(f.at + pts[(size_t)i].at), valueToY(*f.autom, pts[(size_t)i].value, f.area)};
    }

    double Timeline::autoValueAt(const std::string &ac, double y) const
    {
        AutoFrame f;
        return autoFrame(ac, f) ? yToValue(*f.autom, y, f.area) : 0.0;
    }

    double Timeline::autoEase(const std::string &au) const
    {
        const auto it = mAutos.find(au);
        return it == mAutos.end() ? 1.0 : it->second.t.value();
    }

    Point Timeline::autoDragPoint() const
    {
        AutoFrame f;
        if (!mAutoDragging || mAutoHandle != 0 || !autoFrame(mAutoClip, f)) return Point{-1, -1}; // a handle held moves no point
        return Point{beatToX(mAutoPass + mAutoAt), valueToY(*f.autom, mAutoValue, f.area)};
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

    Point Timeline::autoHandleAt(const std::string &ac, int i, int side) const
    {
        AutoFrame f;
        if (!autoFrame(ac, f)) return Point{-1, -1};
        double b = 0, v = 0;
        if (!handleEnd(drawnPoints(*f.autom), i, side, b, v)) return Point{-1, -1};
        return Point{beatToX(f.at + b), valueToY(*f.autom, v, f.area)};
    }

    int Timeline::handleNear(const std::string &ac, const Point &local, int &side) const
    {
        AutoFrame f;
        if (!autoFrame(ac, f)) return -1;
        const auto pts = drawnPoints(*f.autom);
        const double pass = f.at; // its points are the first pass's: the one drawn with them, the one that is edited
        int best = -1;
        double bestD = kGrab;
        for (int i = 0; i < (int)pts.size(); ++i)
            for (int sd : {1, 2})
            {
                double b = 0, v = 0;
                if (!handleEnd(pts, i, sd, b, v)) continue;
                const double d = std::hypot(beatToX(pass + b) - local.x, valueToY(*f.autom, v, f.area) - local.y);
                if (d <= bestD) { best = i; bestD = d; side = sd; }
            }
        return best;
    }

    void Timeline::dragHandle(const Point &local, bool alt)
    {
        // cosmo's gesture over Interstellar's numbers (R-AUTO-10): a handle's end is its influence of the
        // segment on its side (time) along its speed (slope). Alt-pull: both sides the pointer's slope and
        // reach; a handle: its own side, the opposite mirrored (Alt: left alone — the symmetry breaks).
        AutoFrame f;
        if (!autoFrame(mAutoClip, f)) return;
        const auto &pts = f.autom->to;
        const int i = mAutoPoint;
        if (i < 0 || i >= (int)pts.size()) return;
        const auto &p = pts[(size_t)i];
        const double b = xToBeat(local.x) - mAutoPass, v = yToValue(*f.autom, local.y, f.area);
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

    void Timeline::advanceAuto(double nowMs)
    {
        // a click on a curve, once it is not a double-click, is the point it asked for (R-AUTO-6, R-AUTO-11)
        if (mPendWaiting && nowMs - mPendMs > kDoubleClickMs + 50.0)
        {
            mPendWaiting = false;
            mPendAmt.animateTo(0.0, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs); // the real point cross-fades in as it goes
            if (onCommand) onCommand("auto point add " + mPendAu + " --at " + beats(mPendAt) + " --value " + num(mPendValue));
        }
        mPendAmt.update(nowMs);
    }

    int Timeline::pointNear(const std::string &ac, const Point &local) const
    {
        AutoFrame f;
        if (!autoFrame(ac, f)) return -1;
        const auto pts = shownPoints(*f.autom);
        const double pass = f.at; // its points are the first pass's: the one drawn with them, the one that is edited
        int best = -1;
        double bestD = kGrab;
        for (size_t i = 0; i < pts.size(); ++i)
        {
            if (pts[i].at > f.cycle + 1e-9) continue; // past its length: silent, not drawn
            const double d = std::hypot(beatToX(pass + pts[i].at) - local.x, valueToY(*f.autom, pts[i].value, f.area) - local.y);
            if (d <= bestD) { best = (int)i; bestD = d; }
        }
        return best;
    }

    void Timeline::paintCurve(IRenderTarget &t, const AutoLive &l, const std::vector<solaris::AutoPointModel> &pts, const Rect &area, double a,
                              bool marks, double origin, double until) const
    {
        if (pts.empty() || a <= 0.001) return;
        const double L = l.model.length, cut = std::min(L, until - origin), W = width.value();
        std::vector<solaris::AutoPointModel> in; // its pass: the points within its length (a point past it is silent)
        for (const auto &p : pts)
            if (p.at <= L + 1e-9) in.push_back(p);
        if (in.empty() || cut <= 0) return;
        auto X = [this, origin](double b) { return Timeline::beatToX(origin + b); };
        const Color line = fade(palette::primary(), a);
        // the curve, by the engine's own keys (R-AUTO-10) — sampled every 2 px and at every point, so a hold steps where
        // it holds; before the first point the first value, after the last the last
        const std::vector<anim::Key> keys = keysOf(in);
        std::vector<double> at;
        const double step = std::max(1e-6, 2.0 / std::max(1e-6, mPpb.value()));
        const double b0 = std::max(0.0, xToBeat(kHeaderW) - origin), b1 = std::min(cut, xToBeat(W) - origin);
        for (double b = b0; b < b1; b += step) at.push_back(b);
        for (const auto &p : in)
            if (p.at >= b0 && p.at <= b1)
            {
                at.push_back(p.at);
                at.push_back(std::max(b0, p.at - 1e-6)); // just before it: a hold's step is upright
            }
        at.push_back(b1);
        std::sort(at.begin(), at.end());
        t.beginPath();
        bool first = true;
        for (double b : at)
        {
            const double y = valueToY(l, std::clamp(anim::eval(keys, b), l.model.min, l.model.max), area);
            if (first) t.moveTo(X(b), y);
            else t.lineTo(X(b), y);
            first = false;
        }
        t.setStroke(line, 1.5);
        t.strokePath();
        if (!marks) return;
        const bool pressed = std::fabs(origin - mAutoPass) < 1e-9; // the pass in hand
        // a bezier point's handles: a stem from the point to each end (cosmo's curve)
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i].shape != "bezier" || in[i].at > cut + 1e-9) continue;
            const double x = X(in[i].at), y = valueToY(l, in[i].value, area);
            for (int side : {1, 2})
            {
                double hb = 0, hv = 0;
                if (!handleEnd(in, (int)i, side, hb, hv)) continue;
                const double hx = X(hb), hy = valueToY(l, hv, area);
                t.setStroke(fade(palette::primary(), a * 0.7), 1.0);
                t.beginPath(); t.moveTo(x, y); t.lineTo(hx, hy); t.strokePath();
                const bool held = pressed && mAutoDragging && mAutoPress == l.model.id && mAutoPoint == (int)i && (mAutoHandle == side || mAutoHandle == 3);
                drawCircle(t, hx, hy, kHandleDot + (held ? 1.0 : 0.0), Paint::filledStroked(fade(palette::primary(), a), fade(palette::popover(), a), 1.0));
            }
        }
        for (size_t i = 0; i < in.size(); ++i)
        {
            if (in[i].at > cut + 1e-9) continue;
            const double x = X(in[i].at), y = valueToY(l, in[i].value, area);
            if (x < kHeaderW - kHandle || x > W + kHandle) continue;
            const bool dragged = pressed && mAutoDragging && mAutoPress == l.model.id && mAutoPoint == (int)i && mAutoHandle == 0;
            drawCircle(t, x, y, kHandle + (dragged ? 1.0 : 0.0), Paint::filledStroked(fade(palette::popover(), a), line, 1.5));
        }
    }

    void Timeline::paintAutoClip(IRenderTarget &t, const ClipView &v, const Rect &r, double a, double len) const
    {
        // R-AUTO-14: its automation's curve, pass after pass — the first bright, each repeat fainter with a seam at its
        // start (R-CLIP-7's look); a shape or a point more or fewer cross-fades the two curves (R-AUTO-6)
        const auto it = mAutos.find(v.c.automation);
        if (it == mAutos.end() || a <= 0.001) return;
        const AutoLive &l = it->second;
        const double L = l.model.length;
        if (!(L > 0) || r.h <= kNameH + 2.0 * kPad) return;
        const Rect area{r.x, r.y + kNameH, r.w, r.h - kNameH - 2.0};
        const double at = xToBeat(r.x), end = at + len, W = width.value();
        const std::vector<solaris::AutoPointModel> pts = drawnPoints(l); // a held point or handle: the pointer's
        const bool held = mAutoDragging && mAutoPress == l.model.id;
        const double f = l.t.value();
        t.save();
        t.clipRect(r.x, r.y, r.w, r.h);
        int k = 0;
        for (double p0 = at; p0 < end - 1e-9; p0 += L, ++k)
        {
            if (beatToX(p0) > W) break;
            if (beatToX(std::min(end, p0 + L)) < kHeaderW) continue;
            const double pa = a * (k == 0 ? 1.0 : 0.55);
            if (k > 0 && beatToX(p0 + L) - beatToX(p0) >= 6.0)
            {
                const double x = std::round(beatToX(p0)) + 0.5;
                t.setStroke(fade(palette::primary(), 0.5 * a), 1.0);
                t.beginPath(); t.moveTo(x, area.y); t.lineTo(x, area.bottom()); t.strokePath();
            }
            const double until = std::min(end, p0 + L);
            if (!held && f < 1.0 && !sameShapes(l.from, l.to))
            {
                paintCurve(t, l, l.from, area, pa * (1.0 - f), false, p0, until);
                paintCurve(t, l, l.to, area, pa * f, k == 0, p0, until);
            }
            else paintCurve(t, l, pts, area, pa, k == 0, p0, until); // its points: the first pass, the one that is edited
        }
        // a click's point, drawn at once while it waits out a double-click (R-AUTO-11)
        if (mPendClip == v.c.id && mPendAmt.value() > 0.001)
        {
            const double pa = a * mPendAmt.value();
            drawCircle(t, beatToX(mPendPass + mPendAt), valueToY(l, mPendValue, area), kHandle,
                       Paint::filledStroked(fade(palette::popover(), pa), fade(palette::primary(), pa), 1.5));
        }
        t.restore();
    }

    bool Timeline::autoGesture(const Gesture &g, const Point &local)
    {
        const bool inRows = local.x >= kHeaderW && local.y >= top();
        switch (g.type)
        {
        case Gesture::Type::Down:
        {
            mAutoPress.clear();
            mAutoClip.clear();
            mAutoPoint = -1;
            mAutoDragging = false;
            mAutoHandle = 0;
            if (!inRows) return false;
            const std::string ac = autoClipAt(local); // its curve; its name strip and its end are the clip's
            AutoFrame f;
            if (ac.empty() || !autoFrame(ac, f)) return false;
            mAutoClip = ac;
            mAutoPress = f.autom->model.id;
            mAutoPass = f.at; // a point or a handle is taken in the first pass

            // a handle's end, unless the pointer is right on a point (R-AUTO-10); Alt on a point pulls handles out
            int side = 0;
            const int h = handleNear(ac, local, side), p = pointNear(ac, local);
            bool onPoint = false;
            if (p >= 0)
            {
                const auto pts = shownPoints(*f.autom);
                onPoint = std::hypot(beatToX(mAutoPass + pts[(size_t)p].at) - local.x, valueToY(*f.autom, pts[(size_t)p].value, f.area) - local.y) <= kHandle + 1.0;
            }
            if (h >= 0 && !onPoint) { mAutoPoint = h; mAutoHandle = side; }
            else { mAutoPoint = p; mAutoHandle = p >= 0 && g.alt ? 3 : 0; }
            if (mAutoPoint >= 0 && mAutoPoint < (int)f.autom->to.size()) mAutoHeld = f.autom->to[(size_t)mAutoPoint];
            return true;
        }
        case Gesture::Type::DragStart:
            if (mAutoPress.empty() || mAutoPoint < 0) return !mAutoPress.empty();
            mAutoDragging = true;
            [[fallthrough]];
        case Gesture::Type::Drag:
        {
            if (!mAutoDragging) return !mAutoPress.empty();
            if (mAutoHandle != 0)
            {
                dragHandle(local, g.alt); // the pointer is the animation
                return true;
            }
            AutoFrame f;
            if (!autoFrame(mAutoClip, f)) return true;
            // the grid you see (R-UI-10), in the automation's own beats — within the pass it was taken in
            mAutoAt = std::clamp(snap(xToBeat(local.x)) - mAutoPass, 0.0, f.cycle);
            mAutoValue = yToValue(*f.autom, local.y, f.area);
            return true;
        }
        case Gesture::Type::Up:
        case Gesture::Type::Drop:
        {
            if (mAutoPress.empty()) return false;
            const auto it = mAutos.find(mAutoPress);
            if (mAutoDragging && mAutoHandle != 0)
            {
                // ONE line for the whole handle gesture; it stays where it was let go — the numbers sent are the
                // numbers the model will hold, so nothing eases after the release
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
                    mAutoClip.clear();
                    onCommand(line);
                    return true;
                }
            }
            else if (mAutoDragging)
            {
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
                    mAutoClip.clear();
                    onCommand(line);
                    return true;
                }
            }
            mAutoDragging = false;
            mAutoHandle = 0;
            mAutoPress.clear();
            mAutoClip.clear();
            return true;
        }
        case Gesture::Type::Click:
        {
            if (!inRows) return false;
            const std::string ac = autoClipAt(local);
            AutoFrame f;
            if (ac.empty() || !autoFrame(ac, f)) return false;
            if (pointNear(ac, local) >= 0) return true; // a click on a point selects nothing, adds nothing
            int side = 0;
            if (handleNear(ac, local, side) >= 0) return true; // nor on a handle
            // the grid you see (R-UI-10), in the automation's own beats: its place in the pass under the pointer
            const double pass = passAt(f, local.x);
            // a double-click here opens the window (R-AUTO-11): the point waits it out, drawn at once (R2), sent by advanceAuto
            mPendAu = f.autom->model.id;
            mPendClip = ac;
            mPendPass = pass;
            mPendAt = std::clamp(snap(xToBeat(local.x)) - pass, 0.0, f.cycle);
            mPendValue = yToValue(*f.autom, local.y, f.area);
            mPendMs = mNowMs;
            mPendWaiting = true;
            mPendAmt.animateTo(1.0, motion::kHoverMs, Easing::EaseOutCubic, mNowMs);
            return true;
        }
        case Gesture::Type::DoubleClick:
        {
            if (local.x < kHeaderW && local.y >= top())
            {
                // an automation's TRACK header: its window (R-AUTO-11)
                const int ri = rowAt(local.y);
                if (ri < 0 || ri >= (int)mRows.size() || mRows[(size_t)ri].automation.empty()) return false;
                if (onOpenAutomation) onOpenAutomation(mRows[(size_t)ri].automation);
                return true;
            }
            if (!inRows) return false;
            const std::string ac = autoClipAt(local);
            AutoFrame f;
            if (ac.empty() || !autoFrame(ac, f)) return false;
            const std::string au = f.autom->model.id;
            const int i = pointNear(ac, local);
            if (i >= 0)
            {
                // ON a point: it goes (R-AUTO-6)
                if (i < (int)f.autom->to.size() && onCommand) onCommand("auto point delete " + au + " --at " + beats(f.autom->to[(size_t)i].at));
                return true;
            }
            int side = 0;
            if (handleNear(ac, local, side) >= 0) return true;
            // on the curve away from a point: the automation's window — the first click's point never lands
            if (mPendWaiting && mPendClip == ac)
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
            const std::string ac = autoClipAt(local);
            AutoFrame f;
            if (ac.empty() || !autoFrame(ac, f)) return false;
            const int i = pointNear(ac, local);
            if (i < 0 || !onMenu || i >= (int)f.autom->to.size()) return false; // elsewhere in the clip: the clip's menu
            const std::string au = f.autom->model.id;
            const std::string at = beats(f.autom->to[(size_t)i].at), now = f.autom->to[(size_t)i].shape;
            std::vector<cosmo_v2::ContextMenu::Item> items;
            static const char *kShapes[][2] = {{"linear", N_("Linear")}, {"hold", N_("Hold")}, {"smooth", N_("Smooth")}, {"bezier", N_("Bezier")}};
            for (const auto &shape : kShapes)
            {
                const char *s = shape[0]; // the grammar's word, sent; its name, said
                std::string label = say(shape[1]);
                if (now == s) label += "  \xC2\xB7 " + say("now");
                items.push_back({label, [this, au, at, s] { if (onCommand) onCommand("auto point shape " + au + " --at " + at + " --shape " + s); }});
            }
            items.push_back({say("Delete Point"), [this, au, at] { if (onCommand) onCommand("auto point delete " + au + " --at " + at); }});
            onMenu(items, g.pos);
            return true;
        }
        default:
            return !mAutoPress.empty();
        }
    }
}
}
