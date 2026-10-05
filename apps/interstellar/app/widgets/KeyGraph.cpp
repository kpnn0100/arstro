#include "KeyGraph.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;       // u(3): the panel padding
        constexpr double kPadY = 6.5;
        constexpr double kChipH = 16.25;     // u(5)
        constexpr double kKeyR = 4.5;        // a key diamond's half-diagonal
        constexpr double kHit = 7.0;         // pointer slop around a key or a handle
        constexpr double kGapT = 1e-3;       // a dragged key stays this far from its neighbours (the file's ms)
        Color fade(Color c, double a) { c.a *= a; return c; }

        anim::Key fromModel(const interstellar::KeyframeModel &k)
        {
            anim::Key x;
            x.t = k.t;
            x.v = k.v;
            anim::parseSide(k.in, x.in);
            anim::parseSide(k.out, x.out);
            x.speedIn = k.speedIn;
            x.speedOut = k.speedOut;
            x.inflIn = k.inflIn;
            x.inflOut = k.inflOut;
            return x;
        }
    }

    KeyGraph::KeyGraph() { clipToBounds = true; }

    std::vector<anim::Key> KeyGraph::keysOf(int curve) const
    {
        std::vector<anim::Key> v;
        if (curve < 0 || curve >= (int)mCurves.size()) return v;
        for (const auto &k : mCurves[(size_t)curve].keys) v.push_back(fromModel(k));
        return v;
    }

    const std::vector<anim::Key> &KeyGraph::liveKeys() const
    {
        if (mDrag.kind != 0) return mDrag.keys;
        mModelKeys = keysOf(mSel);
        return mModelKeys;
    }

    void KeyGraph::bind(const interstellar::AppModel &m, const std::vector<std::string> &animIds, double t0, double t1)
    {
        const std::string was = selectedAddress();
        mIds = animIds;
        mCurves.clear();
        for (const auto &id : animIds)
            for (const auto &a : m.anims)
                if (a.id == id) mCurves.push_back(a);
        mT0 = t0;
        mT1 = t1 > t0 ? t1 : t0 + 1.0;
        // keep the curve the user picked when it is still there
        mSel = 0;
        for (int i = 0; i < (int)mCurves.size(); ++i)
            if (mCurves[(size_t)i].address == was) mSel = i;
        mNow = mSel < (int)mCurves.size() ? mCurves[(size_t)mSel].now : 0.0;
        if (mSelKey >= (int)(mSel < (int)mCurves.size() ? mCurves[(size_t)mSel].keys.size() : 0)) mSelKey = -1;
        if (mDrag.kind == 0) fitRange(false);
    }

    std::string KeyGraph::label(int i) const
    {
        const auto &a = mCurves[(size_t)i];
        const auto dot = a.key.rfind('.');
        const std::string tail = dot == std::string::npos ? a.key : a.key.substr(dot + 1);
        return a.owner == "effect" ? a.nodeBind + " " + tail : a.owner == "clip" && a.key.rfind("geom.", 0) == 0 ? a.key.substr(5) : tail;
    }

    void KeyGraph::fitRange(bool place)
    {
        if (mSel < 0 || mSel >= (int)mCurves.size()) return;
        const auto ks = keysOf(mSel);
        if (ks.empty()) return;
        // the keys and whatever the curve does between them (an overshooting bezier)
        double lo = ks.front().v, hi = lo;
        for (int i = 0; i <= 64; ++i)
        {
            const double v = anim::eval(ks, mT0 + (mT1 - mT0) * i / 64.0);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
        }
        for (const auto &k : ks) { lo = std::min(lo, k.v); hi = std::max(hi, k.v); }
        const auto &a = mCurves[(size_t)mSel];
        const double minSpan = std::max(1e-3, (a.max - a.min) * 0.04);
        if (hi - lo < minSpan) { const double c = 0.5 * (hi + lo); lo = c - minSpan * 0.5; hi = c + minSpan * 0.5; }
        const double pad = (hi - lo) * 0.18;
        mLoT = lo - pad;
        mHiT = hi + pad;
        if (place) { mLo.set(mLoT); mHi.set(mHiT); mLoL = mLoT; mHiL = mHiT; mRangePlaced = true; }
    }

    void KeyGraph::advance(double nowMs)
    {
        const std::string id = mSel < (int)mCurves.size() ? mCurves[(size_t)mSel].id : std::string();
        if (!mRangePlaced && !id.empty()) fitRange(true);   // first placement: nowhere to travel from
        if (id != mShownId)
        {
            // another curve: cross-fade the plot, and its range travels with it
            if (!mShownId.empty()) { mSwitch.set(0.0); mSwitch.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); }
            mShownId = id;
        }
        if (mLoT != mLoL || mHiT != mHiL)
        {
            mLo.animateTo(mLoT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mHi.animateTo(mHiT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
            mLoL = mLoT;
            mHiL = mHiT;
        }
        mLo.update(nowMs);
        mHi.update(nowMs);
        mSwitch.update(nowMs);
        mChipHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    Rect KeyGraph::plotRect() const
    {
        const double x0 = mSpanX0 >= 0 ? mSpanX0 : kPadX, x1 = mSpanX0 >= 0 ? mSpanX1 : width.value() - kPadX;
        return Rect{x0, mHeaderH + kPadY, std::max(0.0, x1 - x0), std::max(0.0, height.value() - mHeaderH - 2 * kPadY)};
    }

    Rect KeyGraph::chipRect(int i) const
    {
        // fixed-width chips in a row: their text is ellipsized at paint, where text can be measured
        const double w = 92.0, gap = 4.875;
        return Rect{kPadX + i * (w + gap), (kHeaderH - kChipH) * 0.5, w, kChipH};
    }

    double KeyGraph::xAt(double t) const
    {
        const Rect p = plotRect();
        return p.x + (t - mT0) / (mT1 - mT0) * p.w;
    }
    double KeyGraph::timeAtX(double x) const
    {
        const Rect p = plotRect();
        return p.w > 0 ? mT0 + (x - p.x) / p.w * (mT1 - mT0) : mT0;
    }
    double KeyGraph::yAt(double v) const
    {
        const Rect p = plotRect();
        const double lo = mLo.value(), hi = mHi.value();
        return p.bottom() - (hi > lo ? (v - lo) / (hi - lo) : 0.5) * p.h;
    }

    Point KeyGraph::keyPoint(int key) const
    {
        const auto &ks = liveKeys();
        if (key < 0 || key >= (int)ks.size()) return Point{-1, -1};
        return Point{xAt(ks[(size_t)key].t), yAt(ks[(size_t)key].v)};
    }

    Point KeyGraph::handlePoint(int key, bool out) const
    {
        const auto &ks = liveKeys();
        if (key < 0 || key >= (int)ks.size()) return Point{-1, -1};
        const anim::Key &k = ks[(size_t)key];
        if (out)
        {
            if (key + 1 >= (int)ks.size()) return keyPoint(key);
            const double dt = ks[(size_t)key + 1].t - k.t, f = anim::clampInfl(k.inflOut);
            return Point{xAt(k.t + f * dt), yAt(k.v + k.speedOut * f * dt)};
        }
        if (key == 0) return keyPoint(key);
        const double dt = k.t - ks[(size_t)key - 1].t, f = anim::clampInfl(k.inflIn);
        return Point{xAt(k.t - f * dt), yAt(k.v - k.speedIn * f * dt)};
    }

    int KeyGraph::keyAt(const Point &p) const
    {
        const auto &ks = liveKeys();
        for (int i = (int)ks.size() - 1; i >= 0; --i)
        {
            const Point k = keyPoint(i);
            if (std::fabs(p.x - k.x) <= kHit && std::fabs(p.y - k.y) <= kHit) return i;
        }
        return -1;
    }

    int KeyGraph::handleAt(const Point &p, bool &out) const
    {
        const auto &ks = liveKeys();
        if (mSelKey < 0 || mSelKey >= (int)ks.size()) return -1;
        for (bool o : {true, false})
        {
            const anim::Side s = o ? ks[(size_t)mSelKey].out : ks[(size_t)mSelKey].in;
            if (s != anim::Side::Bezier || (o && mSelKey + 1 >= (int)ks.size()) || (!o && mSelKey == 0)) continue;
            const Point h = handlePoint(mSelKey, o);
            if (std::hypot(p.x - h.x, p.y - h.y) <= kHit) { out = o; return mSelKey; }
        }
        return -1;
    }

    bool KeyGraph::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect plot = plotRect();
        switch (g.type)
        {
            case Gesture::Type::Move:
            {
                int hover = -1;
                for (int i = 0; i < (int)mCurves.size(); ++i) if (chipRect(i).contains(local)) hover = i;
                mChipHover.setHovered(hover);
                return true;
            }
            case Gesture::Type::Down:
            {
                for (int i = 0; i < (int)mCurves.size() && mHeaderH > 0; ++i)
                    if (chipRect(i).contains(local)) { if (i != mSel) { mSel = i; mSelKey = -1; fitRange(false); } return true; }
                if (!plot.contains(local) && local.y < plot.y - kHit) return true;
                bool out = false;
                const int h = handleAt(local, out);
                mDrag = Drag{};
                if (h >= 0) { mDrag.kind = out ? 3 : 2; mDrag.key = h; mDrag.keys = keysOf(mSel); return true; }
                const int k = keyAt(local);
                mSelKey = k;
                if (k >= 0) { mDrag.kind = 1; mDrag.key = k; mDrag.keys = keysOf(mSel); }
                return true;
            }
            case Gesture::Type::DragStart:
            case Gesture::Type::Drag:
            {
                if (mDrag.kind == 0 || mDrag.key < 0 || mDrag.key >= (int)mDrag.keys.size()) return true;
                mDrag.moved = true;
                auto &ks = mDrag.keys;
                anim::Key &k = ks[(size_t)mDrag.key];
                const auto &a = mCurves[(size_t)mSel];
                const double lo = mLo.value(), hi = mHi.value();
                const double v = lo + (plot.bottom() - local.y) / std::max(1.0, plot.h) * (hi - lo);
                const double t = timeAtX(local.x);
                if (mDrag.kind == 1)
                {
                    // the pointer is the animation; the key stays between its neighbours
                    const double tMin = mDrag.key > 0 ? ks[(size_t)mDrag.key - 1].t + kGapT : -1e9;
                    const double tMax = mDrag.key + 1 < (int)ks.size() ? ks[(size_t)mDrag.key + 1].t - kGapT : 1e9;
                    k.t = std::clamp(std::round(t * 1000.0) / 1000.0, tMin, tMax);
                    k.v = std::clamp(v, a.min, a.max);
                }
                else
                {
                    // a handle: its slope is the speed, its reach into the segment the influence
                    const bool out = mDrag.kind == 3;
                    const double dt = out ? ks[(size_t)mDrag.key + 1].t - k.t : k.t - ks[(size_t)mDrag.key - 1].t;
                    const double reach = out ? t - k.t : k.t - t;
                    const double infl = std::clamp(reach / std::max(1e-9, dt), 0.001, 1.0);
                    const double speed = (out ? v - k.v : k.v - v) / std::max(1e-9, infl * dt);
                    if (out) { k.speedOut = speed; k.inflOut = infl * 100.0; }
                    else { k.speedIn = speed; k.inflIn = infl * 100.0; }
                }
                return true;
            }
            case Gesture::Type::Up:
            case Gesture::Type::Drop:
            {
                if (mDrag.kind != 0 && mDrag.moved && onCommand && mSel < (int)mCurves.size())
                {
                    const auto &a = mCurves[(size_t)mSel];
                    const anim::Key &was = mCurves[(size_t)mSel].keys.size() > (size_t)mDrag.key ? fromModel(mCurves[(size_t)mSel].keys[(size_t)mDrag.key]) : anim::Key{};
                    const anim::Key &k = mDrag.keys[(size_t)mDrag.key];
                    std::string line = "key set " + cmd::quote(a.address) + " --at " + cmd::num(was.t);
                    if (mDrag.kind == 1)
                    {
                        if (std::fabs(k.t - was.t) >= 5e-4) line += " --to " + cmd::num(k.t);
                        line += " --value " + cmd::num(k.v);
                    }
                    else if (mDrag.kind == 3) line += " --speed-out " + cmd::num(k.speedOut) + " --influence-out " + cmd::num(k.inflOut);
                    else line += " --speed-in " + cmd::num(k.speedIn) + " --influence-in " + cmd::num(k.inflIn);
                    onCommand(line);
                }
                mDrag = Drag{};
                return true;
            }
            case Gesture::Type::DoubleClick:
            {
                if (!plot.contains(local) || keyAt(local) >= 0 || mSel >= (int)mCurves.size() || !onCommand) return true;
                const double t = std::clamp(std::round(timeAtX(local.x) * 1000.0) / 1000.0, mT0, mT1);
                onCommand("key add " + cmd::quote(mCurves[(size_t)mSel].address) + " --at " + cmd::num(t));
                return true;
            }
            case Gesture::Type::RightClick:
            {
                const int k = keyAt(local);
                if (k < 0 || mSel >= (int)mCurves.size()) return true;
                mSelKey = k;
                if (onKeyContext) onKeyContext(mCurves[(size_t)mSel].address, liveKeys()[(size_t)k].t, worldTransform().apply(local));
                return true;
            }
            default: return true;
        }
    }

    void KeyGraph::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::curvePlotBg()));
        if (mCurves.empty())
        {
            t.setFill(palette::mutedForeground());
            const std::string s = textfit::ellipsize(t, mEmptyText, w - 2 * kPadX, 10.0, font::sans());
            t.drawText(s, kPadX, h * 0.5 + 10.0 * 0.35, 10.0, font::sans());
            return;
        }
        // the header: one chip per curve
        for (int i = 0; i < (int)mCurves.size() && mHeaderH > 0; ++i)
        {
            const Rect c = chipRect(i);
            if (c.right() > w - kPadX) break;
            const bool sel = i == mSel;
            drawRoundedRect(t, c, radius::control(), Paint::filled(sel ? palette::primaryAlpha(0.22) : palette::hoverWash(mChipHover.amount(i))));
            t.setFill(sel ? palette::foreground() : palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, label(i), c.w - 9.75, 9.0, font::sans()), c.x + 4.875, c.y + c.h * 0.5 + 9.0 * 0.35, 9.0, font::sans());
        }
        if (mSel >= (int)mCurves.size()) return;
        const auto &a = mCurves[(size_t)mSel];
        const auto &ks = liveKeys();
        const Rect p = plotRect();
        const double sw = mSwitch.value();
        t.save();
        t.clipRect(p.x - kKeyR - 1, p.y - kKeyR - 1, p.w + 2 * kKeyR + 2, p.h + 2 * kKeyR + 2);
        // grid: quarters of the value axis, the window's ends
        for (int i = 0; i <= 4; ++i)
        {
            const double y = p.y + p.h * i / 4.0;
            glyph::line(t, p.x, y, p.right(), y, palette::whiteAlpha(i == 0 || i == 4 ? 0.06 : 0.035), 1.0);
        }
        // now: where Grade stands (the reference frame) or the playhead in the clip
        const double nx = xAt(mNow);
        if (nx >= p.x && nx <= p.right()) glyph::line(t, nx, p.y, nx, p.bottom(), palette::primaryAlpha(0.45), 1.0);
        // the curve — the render path's own function, sampled
        if (!ks.empty())
        {
            const int n = std::max(2, (int)(p.w / 2.0));
            t.beginPath();
            for (int i = 0; i <= n; ++i)
            {
                const double tt = mT0 + (mT1 - mT0) * i / n;
                const double y = yAt(anim::eval(ks, tt));
                if (i == 0) t.moveTo(p.x + p.w * i / n, y); else t.lineTo(p.x + p.w * i / n, y);
            }
            t.setStroke(palette::primaryAlpha(0.95 * sw), 1.5);
            t.strokePath();
        }
        // the selected key's handles, then every key
        if (mSelKey >= 0 && mSelKey < (int)ks.size())
            for (bool out : {false, true})
            {
                const anim::Side s = out ? ks[(size_t)mSelKey].out : ks[(size_t)mSelKey].in;
                if (s != anim::Side::Bezier || (out && mSelKey + 1 >= (int)ks.size()) || (!out && mSelKey == 0)) continue;
                const Point k = keyPoint(mSelKey), hp = handlePoint(mSelKey, out);
                glyph::line(t, k.x, k.y, hp.x, hp.y, palette::whiteAlpha(0.5 * sw), 1.0);
                drawRoundedRect(t, Rect{hp.x - 3.0, hp.y - 3.0, 6.0, 6.0}, radius::pill(), Paint::filled(palette::foreground()));
            }
        for (int i = 0; i < (int)ks.size(); ++i)
        {
            const Point k = keyPoint(i);
            const bool sel = i == mSelKey;
            t.beginPath();
            t.moveTo(k.x, k.y - kKeyR); t.lineTo(k.x + kKeyR, k.y); t.lineTo(k.x, k.y + kKeyR); t.lineTo(k.x - kKeyR, k.y); t.closePath();
            t.setFill(sel ? palette::primary() : palette::card());
            t.fillPath();
            t.beginPath();
            t.moveTo(k.x, k.y - kKeyR); t.lineTo(k.x + kKeyR, k.y); t.lineTo(k.x, k.y + kKeyR); t.lineTo(k.x - kKeyR, k.y); t.closePath();
            t.setStroke(sel ? palette::primary() : palette::foreground(), 1.0);
            t.strokePath();
        }
        t.restore();
        // the value axis ends, and the selected key in words
        char buf[96];
        t.setFill(fade(palette::mutedForeground(), 0.85));
        std::snprintf(buf, sizeof buf, "%.3g", mHi.value());
        t.drawText(buf, p.x + 3.0, p.y + 9.0, 8.5, font::mono());
        std::snprintf(buf, sizeof buf, "%.3g", mLo.value());
        t.drawText(buf, p.x + 3.0, p.bottom() - 3.0, 8.5, font::mono());
        if (mSelKey >= 0 && mSelKey < (int)ks.size())
        {
            const anim::Key &k = ks[(size_t)mSelKey];
            std::snprintf(buf, sizeof buf, "t %.3f s  \xC2\xB7  %.4g  \xC2\xB7  in %s  out %s", k.t, k.v, anim::sideName(k.in), anim::sideName(k.out));
            const double tw = textfit::width(t, buf, 9.0, font::mono());
            const double x = std::max(mHeaderH > 0 ? chipRect((int)mCurves.size()).x : kPadX, w - kPadX - tw);
            const double by = mHeaderH > 0 ? kHeaderH * 0.5 + 9.0 * 0.35 : p.y + 9.0;   // headerless: inside the plot's top
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, buf, w - kPadX - x, 9.0, font::mono()), x, by, 9.0, font::mono());
        }
        (void)a;
    }
}
}
