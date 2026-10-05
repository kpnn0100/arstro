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
        long long ms(double t) { return std::llround(t * 1000.0); }

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

    const std::vector<anim::Key> &KeyGraph::liveKeys(int curve) const
    {
        const auto it = mDrag.keys.find(curve);
        if (mDrag.kind != 0 && it != mDrag.keys.end()) return it->second;
        mModelKeys = keysOf(curve);
        return mModelKeys;
    }

    void KeyGraph::bind(const interstellar::AppModel &m, const std::vector<std::string> &animIds, double t0, double t1)
    {
        const std::string was = mFront.empty() ? selectedAddress() : mFront;
        mCurves.clear();
        for (const auto &id : animIds)
            for (const auto &a : m.anims)
                if (a.id == id) mCurves.push_back(a);
        mT0 = t0;
        mT1 = t1 > t0 ? t1 : t0 + 1.0;
        mSel = 0;
        for (int i = 0; i < (int)mCurves.size(); ++i)
            if (mCurves[(size_t)i].address == was) mSel = i;
        mNow = mHasNowOverride ? mNowOverride : mSel < (int)mCurves.size() ? mCurves[(size_t)mSel].now : 0.0;
        // the selection keeps only keys that still exist
        std::set<std::pair<std::string, long long>> keep;
        for (int c = 0; c < (int)mCurves.size(); ++c)
            for (const auto &k : mCurves[(size_t)c].keys)
                if (mSelection.count({mCurves[(size_t)c].address, ms(k.t)})) keep.insert({mCurves[(size_t)c].address, ms(k.t)});
        mSelection.swap(keep);
        if (mDrag.kind == 0)
            for (int c = 0; c < (int)mCurves.size(); ++c) fitRange(c);
    }

    void KeyGraph::setFront(const std::string &address)
    {
        mFront = address;
        for (int i = 0; i < (int)mCurves.size(); ++i)
            if (mCurves[(size_t)i].address == address) mSel = i;
    }

    std::string KeyGraph::label(int i) const
    {
        const auto &a = mCurves[(size_t)i];
        const auto dot = a.key.rfind('.');
        const std::string tail = dot == std::string::npos ? a.key : a.key.substr(dot + 1);
        return a.owner == "effect" ? a.nodeBind + " " + tail : a.owner == "clip" && a.key.rfind("geom.", 0) == 0 ? a.key.substr(5) : tail;
    }

    void KeyGraph::fitRange(int curve)
    {
        if (curve < 0 || curve >= (int)mCurves.size()) return;
        const auto &a = mCurves[(size_t)curve];
        Range &r = mRanges[a.id];
        double lo = -1.0, hi = 1.0;
        if (!a.shape)
        {
            const auto ks = keysOf(curve);
            if (ks.empty()) return;
            // the keys and whatever the curve does between them (an overshooting bezier)
            lo = hi = ks.front().v;
            for (int i = 0; i <= 64; ++i)
            {
                const double v = anim::eval(ks, mT0 + (mT1 - mT0) * i / 64.0);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            for (const auto &k : ks) { lo = std::min(lo, k.v); hi = std::max(hi, k.v); }
            const double minSpan = std::max(1e-3, (a.max - a.min) * 0.04);
            if (hi - lo < minSpan) { const double c = 0.5 * (hi + lo); lo = c - minSpan * 0.5; hi = c + minSpan * 0.5; }
            const double pad = (hi - lo) * 0.18;
            lo -= pad;
            hi += pad;
        }
        r.loT = lo;
        r.hiT = hi;
    }

    void KeyGraph::advance(double nowMs)
    {
        const std::string id = mSel < (int)mCurves.size() ? mCurves[(size_t)mSel].id : std::string();
        if (id != mShownId)
        {
            // another front curve: the plot cross-fades
            if (!mShownId.empty()) { mSwitch.set(0.0); mSwitch.animateTo(1.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); }
            mShownId = id;
        }
        for (auto &kv : mRanges)
        {
            Range &r = kv.second;
            if (!r.placed) { r.lo.set(r.loT); r.hi.set(r.hiT); r.loL = r.loT; r.hiL = r.hiT; r.placed = true; }   // first placement
            if (r.loT != r.loL || r.hiT != r.hiL)
            {
                r.lo.animateTo(r.loT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                r.hi.animateTo(r.hiT, motion::kCatchUpMs, Easing::EaseOutCubic, nowMs);
                r.loL = r.loT;
                r.hiL = r.hiT;
            }
            r.lo.update(nowMs);
            r.hi.update(nowMs);
        }
        mSwitch.update(nowMs);
        mChipHover.advance(nowMs);
        Segment::advance(nowMs);
    }

    double KeyGraph::rangeLo() const
    {
        const auto it = mSel < (int)mCurves.size() ? mRanges.find(mCurves[(size_t)mSel].id) : mRanges.end();
        return it == mRanges.end() ? 0.0 : it->second.lo.value();
    }
    double KeyGraph::rangeHi() const
    {
        const auto it = mSel < (int)mCurves.size() ? mRanges.find(mCurves[(size_t)mSel].id) : mRanges.end();
        return it == mRanges.end() ? 1.0 : it->second.hi.value();
    }
    double KeyGraph::rangeLoTarget() const
    {
        const auto it = mSel < (int)mCurves.size() ? mRanges.find(mCurves[(size_t)mSel].id) : mRanges.end();
        return it == mRanges.end() ? 0.0 : it->second.loT;
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

    int KeyGraph::shapeRank(int curve) const
    {
        int rank = 0, count = 0;
        for (int c = 0; c < (int)mCurves.size(); ++c)
            if (mCurves[(size_t)c].shape) { if (c < curve) ++rank; ++count; }
        return count ? rank : 0;
    }

    double KeyGraph::yOf(int curve, double v) const
    {
        const Rect p = plotRect();
        if (curve < 0 || curve >= (int)mCurves.size()) return p.y + p.h * 0.5;
        if (mCurves[(size_t)curve].shape)
        {
            // shapes: one row each, spread down the plot
            int count = 0;
            for (const auto &c : mCurves) count += c.shape;
            return p.y + p.h * (shapeRank(curve) + 1.0) / (count + 1.0);
        }
        const auto it = mRanges.find(mCurves[(size_t)curve].id);
        const double lo = it == mRanges.end() ? 0.0 : it->second.lo.value(), hi = it == mRanges.end() ? 1.0 : it->second.hi.value();
        return p.bottom() - (hi > lo ? (v - lo) / (hi - lo) : 0.5) * p.h;
    }

    Point KeyGraph::keyPointOf(int curve, int key) const
    {
        if (curve < 0 || curve >= (int)mCurves.size()) return Point{-1, -1};
        const auto &ks = liveKeys(curve);
        if (key < 0 || key >= (int)ks.size()) return Point{-1, -1};
        return Point{xAt(ks[(size_t)key].t), yOf(curve, ks[(size_t)key].v)};
    }

    Point KeyGraph::handlePoint(int key, bool out) const
    {
        const auto ks = liveKeys(mSel);
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

    std::pair<std::string, long long> KeyGraph::keyId(int curve, int key) const
    {
        return {mCurves[(size_t)curve].address, ms(mCurves[(size_t)curve].keys[(size_t)key].t)};
    }

    bool KeyGraph::isSelected(int curve, int key) const
    {
        if (curve < 0 || curve >= (int)mCurves.size() || key < 0 || key >= (int)mCurves[(size_t)curve].keys.size()) return false;
        return mSelection.count(keyId(curve, key)) > 0;
    }

    void KeyGraph::selectKey(int curve, int key, bool add)
    {
        if (curve < 0 || curve >= (int)mCurves.size() || key < 0 || key >= (int)mCurves[(size_t)curve].keys.size()) return;
        if (!add) mSelection.clear();
        mSelection.insert(keyId(curve, key));
    }

    int KeyGraph::selectedKey() const
    {
        if (mSelection.size() != 1 || mSel >= (int)mCurves.size()) return -1;
        for (int k = 0; k < (int)mCurves[(size_t)mSel].keys.size(); ++k)
            if (isSelected(mSel, k)) return k;
        return -1;
    }

    std::string KeyGraph::selectionList() const
    {
        std::string s;
        for (const auto &x : mSelection) s += (s.empty() ? "" : ",") + x.first + "@" + cmd::num(x.second / 1000.0);
        return s;
    }

    bool KeyGraph::keyAt(const Point &p, int &curve, int &key) const
    {
        // the front curve first, then the others
        std::vector<int> order{mSel};
        for (int c = 0; c < (int)mCurves.size(); ++c) if (c != mSel) order.push_back(c);
        for (int c : order)
        {
            if (c < 0 || c >= (int)mCurves.size()) continue;
            const auto &ks = liveKeys(c);
            for (int i = (int)ks.size() - 1; i >= 0; --i)
            {
                const Point k = keyPointOf(c, i);
                if (std::fabs(p.x - k.x) <= kHit && std::fabs(p.y - k.y) <= kHit) { curve = c; key = i; return true; }
            }
        }
        return false;
    }

    int KeyGraph::handleAt(const Point &p, bool &out) const
    {
        const int sk = selectedKey();
        const auto ks = liveKeys(mSel);
        if (sk < 0 || sk >= (int)ks.size() || shapeShown()) return -1;
        for (bool o : {true, false})
        {
            const anim::Side s = o ? ks[(size_t)sk].out : ks[(size_t)sk].in;
            if (s != anim::Side::Bezier || (o && sk + 1 >= (int)ks.size()) || (!o && sk == 0)) continue;
            const Point h = handlePoint(sk, o);
            if (std::hypot(p.x - h.x, p.y - h.y) <= kHit) { out = o; return sk; }
        }
        return -1;
    }

    Rect KeyGraph::boxRect() const
    {
        const double x0 = std::min(mDrag.start.x, mDrag.now.x), y0 = std::min(mDrag.start.y, mDrag.now.y);
        return Rect{x0, y0, std::fabs(mDrag.now.x - mDrag.start.x), std::fabs(mDrag.now.y - mDrag.start.y)};
    }

    bool KeyGraph::handleGesture(const Gesture &g, const Point &local)
    {
        const Rect plot = plotRect();
        switch (g.type)
        {
            case Gesture::Type::Move:
            {
                int hover = -1;
                for (int i = 0; i < (int)mCurves.size() && mHeaderH > 0; ++i) if (chipRect(i).contains(local)) hover = i;
                mChipHover.setHovered(hover);
                return true;
            }
            case Gesture::Type::Down:
            {
                for (int i = 0; i < (int)mCurves.size() && mHeaderH > 0; ++i)
                    if (chipRect(i).contains(local)) { mSel = i; mFront = mCurves[(size_t)i].address; return true; }
                if (mCurves.empty() || (!plot.contains(local) && local.y < plot.y - kHit)) return true;
                mDrag = Drag{};
                mDrag.start = mDrag.now = local;
                bool out = false;
                const int h = handleAt(local, out);
                if (h >= 0)
                {
                    mDrag.kind = out ? 3 : 2;
                    mDrag.curve = mSel;
                    mDrag.key = h;
                    mDrag.keys[mSel] = keysOf(mSel);
                    return true;
                }
                int c = -1, k = -1;
                if (keyAt(local, c, k))
                {
                    if (g.shift) { if (isSelected(c, k)) mSelection.erase(keyId(c, k)); else mSelection.insert(keyId(c, k)); }
                    else if (!isSelected(c, k)) selectKey(c, k, false);
                    mSel = c;
                    mFront = mCurves[(size_t)c].address;
                    mDrag.curve = c;
                    mDrag.key = k;
                    if (mSelection.size() > 1)
                    {
                        mDrag.kind = 4;   // the whole selection, in time
                        for (int cc = 0; cc < (int)mCurves.size(); ++cc) mDrag.keys[cc] = keysOf(cc);
                    }
                    else if (isSelected(c, k))
                    {
                        mDrag.kind = 1;
                        mDrag.keys[c] = keysOf(c);
                    }
                    return true;
                }
                // empty plot: a box selects
                if (!g.shift) mSelection.clear();
                mDrag.kind = 5;
                return true;
            }
            case Gesture::Type::DragStart:
            case Gesture::Type::Drag:
            {
                if (mDrag.kind == 0) return true;
                mDrag.now = local;
                mDrag.moved = true;
                if (mDrag.kind == 5) return true;   // the box follows the pointer
                if (mDrag.kind == 4)
                {
                    // every selected key moves by the same time: the preview, per curve
                    const double dt = std::round((timeAtX(local.x) - timeAtX(mDrag.start.x)) * 1000.0) / 1000.0;
                    mDrag.dt = dt;
                    for (int c = 0; c < (int)mCurves.size(); ++c)
                    {
                        auto ks = keysOf(c);
                        for (int i = 0; i < (int)ks.size(); ++i)
                            if (isSelected(c, i)) ks[(size_t)i].t += dt;
                        std::stable_sort(ks.begin(), ks.end(), [](const anim::Key &a, const anim::Key &b) { return a.t < b.t; });
                        mDrag.keys[c] = ks;
                    }
                    return true;
                }
                auto &ks = mDrag.keys[mDrag.curve];
                if (mDrag.key < 0 || mDrag.key >= (int)ks.size()) return true;
                anim::Key &k = ks[(size_t)mDrag.key];
                const auto &a = mCurves[(size_t)mDrag.curve];
                const auto rit = mRanges.find(a.id);
                const double lo = rit == mRanges.end() ? 0.0 : rit->second.lo.value(), hi = rit == mRanges.end() ? 1.0 : rit->second.hi.value();
                const double v = lo + (plot.bottom() - local.y) / std::max(1.0, plot.h) * (hi - lo);
                const double t = timeAtX(local.x);
                if (mDrag.kind == 1)
                {
                    // the pointer is the animation; the key stays between its neighbours
                    const double tMin = mDrag.key > 0 ? ks[(size_t)mDrag.key - 1].t + kGapT : -1e9;
                    const double tMax = mDrag.key + 1 < (int)ks.size() ? ks[(size_t)mDrag.key + 1].t - kGapT : 1e9;
                    k.t = std::clamp(std::round(t * 1000.0) / 1000.0, tMin, tMax);
                    if (!a.shape) k.v = std::clamp(v, a.min, a.max);   // a shape key moves in time only
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
                if (mDrag.kind == 5)
                {
                    // every key of every curve inside the box
                    const Rect b = boxRect();
                    if (mDrag.moved)
                        for (int c = 0; c < (int)mCurves.size(); ++c)
                            for (int i = 0; i < (int)mCurves[(size_t)c].keys.size(); ++i)
                                if (b.contains(keyPointOf(c, i))) mSelection.insert(keyId(c, i));
                }
                else if (mDrag.kind == 4 && mDrag.moved && std::fabs(mDrag.dt) >= 5e-4 && onCommand)
                    onCommand("key shift --keys " + cmd::quote(selectionList()) + " --by " + cmd::num(mDrag.dt));
                else if (mDrag.kind != 0 && mDrag.kind != 4 && mDrag.moved && onCommand && mDrag.curve < (int)mCurves.size())
                {
                    const auto &a = mCurves[(size_t)mDrag.curve];
                    const anim::Key was = fromModel(a.keys[(size_t)mDrag.key]);
                    const anim::Key &k = mDrag.keys[mDrag.curve][(size_t)mDrag.key];
                    std::string line = "key set " + cmd::quote(a.address) + " --at " + cmd::num(was.t);
                    if (mDrag.kind == 1)
                    {
                        if (std::fabs(k.t - was.t) >= 5e-4) line += " --to " + cmd::num(k.t);
                        if (!a.shape) line += " --value " + cmd::num(k.v);
                        else if (std::fabs(k.t - was.t) < 5e-4) line.clear();   // a shape key that did not move: nothing to say
                    }
                    else if (mDrag.kind == 3) line += " --speed-out " + cmd::num(k.speedOut) + " --influence-out " + cmd::num(k.inflOut);
                    else line += " --speed-in " + cmd::num(k.speedIn) + " --influence-in " + cmd::num(k.inflIn);
                    if (!line.empty())
                    {
                        if (mDrag.kind == 1 && std::fabs(k.t - was.t) >= 5e-4)   // the moved key stays selected where it lands
                            mSelection = {{a.address, ms(k.t)}};
                        onCommand(line);
                    }
                }
                mDrag = Drag{};
                return true;
            }
            case Gesture::Type::DoubleClick:
            {
                int c = -1, k = -1;
                if (!plot.contains(local) || keyAt(local, c, k) || mSel >= (int)mCurves.size() || !onCommand) return true;
                const double t = std::clamp(std::round(timeAtX(local.x) * 1000.0) / 1000.0, mT0, mT1);
                onCommand("key add " + cmd::quote(mCurves[(size_t)mSel].address) + " --at " + cmd::num(t));
                return true;
            }
            case Gesture::Type::RightClick:
            {
                int c = -1, k = -1;
                if (keyAt(local, c, k))
                {
                    if (!isSelected(c, k)) selectKey(c, k, false);
                    mSel = c;
                    mFront = mCurves[(size_t)c].address;
                    if (onKeyContext) onKeyContext(mCurves[(size_t)c].address, liveKeys(c)[(size_t)k].t, worldTransform().apply(local));
                }
                else if (plot.contains(local) && onPlotContext)
                    onPlotContext(std::round(timeAtX(local.x) * 1000.0) / 1000.0, worldTransform().apply(local));
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
        // now: the playhead in the clip
        const double nx = xAt(mNow);
        if (nx >= p.x && nx <= p.right()) glyph::line(t, nx, p.y, nx, p.bottom(), palette::primaryAlpha(0.45), 1.0);
        // every curve, the front one last (on top, brighter)
        std::vector<int> order;
        for (int c = 0; c < (int)mCurves.size(); ++c) if (c != mSel) order.push_back(c);
        order.push_back(mSel);
        for (int c : order)
        {
            const auto &ks = liveKeys(c);
            if (ks.empty()) continue;
            const bool front = c == mSel;
            const double a = front ? 0.95 * sw : 0.4;
            if (mCurves[(size_t)c].shape)
            {
                // a shape: its keys on one row, joined — what it is at a key is edited in Grade at that frame
                const double y = yOf(c, 0.0);
                glyph::line(t, xAt(ks.front().t), y, xAt(ks.back().t), y, palette::primaryAlpha(a), front ? 1.5 : 1.0);
                if (front)
                {
                    t.setFill(fade(palette::mutedForeground(), 0.85));
                    t.drawText(textfit::ellipsize(t, mCurves[(size_t)c].shapeNow, p.w - 8.0, 8.5, font::mono()), p.x + 3.0, p.bottom() - 3.0, 8.5, font::mono());
                }
            }
            else
            {
                const int n = std::max(2, (int)(p.w / 2.0));
                t.beginPath();
                for (int i = 0; i <= n; ++i)
                {
                    const double tt = mT0 + (mT1 - mT0) * i / n;
                    const double y = yOf(c, anim::eval(ks, tt));
                    if (i == 0) t.moveTo(p.x + p.w * i / n, y); else t.lineTo(p.x + p.w * i / n, y);
                }
                t.setStroke(front ? palette::primaryAlpha(a) : palette::whiteAlpha(a), front ? 1.5 : 1.0);
                t.strokePath();
            }
            // the front key's handles
            const int sk = front ? selectedKey() : -1;
            if (sk >= 0 && sk < (int)ks.size() && !mCurves[(size_t)c].shape)
                for (bool out : {false, true})
                {
                    const anim::Side s = out ? ks[(size_t)sk].out : ks[(size_t)sk].in;
                    if (s != anim::Side::Bezier || (out && sk + 1 >= (int)ks.size()) || (!out && sk == 0)) continue;
                    const Point k = keyPoint(sk), hp = handlePoint(sk, out);
                    glyph::line(t, k.x, k.y, hp.x, hp.y, palette::whiteAlpha(0.5 * sw), 1.0);
                    drawRoundedRect(t, Rect{hp.x - 3.0, hp.y - 3.0, 6.0, 6.0}, radius::pill(), Paint::filled(palette::foreground()));
                }
            for (int i = 0; i < (int)ks.size(); ++i)
            {
                const Point k = keyPointOf(c, i);
                const bool sel = i < (int)mCurves[(size_t)c].keys.size() && isSelected(c, i);
                auto diamond = [&] { t.beginPath(); t.moveTo(k.x, k.y - kKeyR); t.lineTo(k.x + kKeyR, k.y); t.lineTo(k.x, k.y + kKeyR); t.lineTo(k.x - kKeyR, k.y); t.closePath(); };
                diamond();
                t.setFill(sel ? palette::primary() : palette::card());
                t.fillPath();
                diamond();
                t.setStroke(sel ? palette::primary() : fade(palette::foreground(), front ? 1.0 : 0.55), 1.0);
                t.strokePath();
            }
        }
        // the box, while one is dragged
        if (mDrag.kind == 5 && mDrag.moved)
            drawRoundedRect(t, boxRect(), 0.0, Paint::filledStroked(palette::primaryAlpha(0.1), palette::primaryAlpha(0.7), 1.0));
        t.restore();
        // the front curve's value axis ends, and the selection in words
        char buf[120];
        if (!shapeShown())
        {
            t.setFill(fade(palette::mutedForeground(), 0.85));
            std::snprintf(buf, sizeof buf, "%.3g", rangeHi());
            t.drawText(buf, p.x + 3.0, p.y + 9.0, 8.5, font::mono());
            std::snprintf(buf, sizeof buf, "%.3g", rangeLo());
            t.drawText(buf, p.x + 3.0, p.bottom() - 3.0, 8.5, font::mono());
        }
        std::string words;
        const int sk = selectedKey();
        if (sk >= 0)
        {
            const auto &ks = liveKeys(mSel);
            if (sk < (int)ks.size())
            {
                const anim::Key &k = ks[(size_t)sk];
                if (shapeShown()) std::snprintf(buf, sizeof buf, "t %.3f s  \xC2\xB7  in %s  out %s", k.t, anim::sideName(k.in), anim::sideName(k.out));
                else std::snprintf(buf, sizeof buf, "t %.3f s  \xC2\xB7  %.4g  \xC2\xB7  in %s  out %s", k.t, k.v, anim::sideName(k.in), anim::sideName(k.out));
                words = buf;
            }
        }
        else if (mSelection.size() > 1)
            words = std::to_string(mSelection.size()) + " keys selected";
        if (!words.empty())
        {
            const double tw = textfit::width(t, words, 9.0, font::mono());
            const double x = std::max(mHeaderH > 0 ? chipRect((int)mCurves.size()).x : kPadX, w - kPadX - tw);
            const double by = mHeaderH > 0 ? kHeaderH * 0.5 + 9.0 * 0.35 : p.y + 9.0;   // headerless: inside the plot's top
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, words, w - kPadX - x, 9.0, font::mono()), x, by, 9.0, font::mono());
        }
    }
}
}
