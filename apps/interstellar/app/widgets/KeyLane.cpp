#include "KeyLane.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kDiamondW = 16.0;
        constexpr double kModeW = 34.0;      // OFFSET at 7 px, tracked, and its pad
        constexpr double kAddW = 84.0;       // ＋ Animate…
        constexpr double kGapT = 1e-3;       // a dragged key stays this far from its neighbours (the file's ms)
        Color fade(Color c, double a) { c.a *= a; return c; }
        double spanDur(const interstellar::ClipModel &c) { return c.duration > 0 ? c.duration : (c.out - c.in) / std::max(1e-6, c.speed); }
    }

    KeyLane::KeyLane()
    {
        clipToBounds = true;
        mGraph = std::make_shared<KeyGraph>();
        mGraph->setHeaderShown(false);   // the lanes name the properties
        mGraph->onCommand = [this](const std::string &l) { return onCommand && onCommand(l); };
        mGraph->onKeyContext = [this](const std::string &a, double t, Point w) { if (onKeyContext) onKeyContext(a, t, w); };
        mGraph->onPlotContext = [this](double t, Point w) { if (onPlotContext) onPlotContext(t, w); };
        mGraph->visible = false;
        addChild(mGraph);
    }

    void KeyLane::rebuild(const interstellar::AppModel &m)
    {
        // one lane per marked property of the project, under every clip of its object in this timeline
        std::vector<Row> fresh;
        for (const auto &a : m.anims)
        {
            Row r;
            r.prop = keys::describe(m, a);
            r.mode = a.mode.empty() ? std::string("fixed") : a.mode;
            for (const auto &k : a.keys) r.keys.push_back(k.t);
            std::sort(r.keys.begin(), r.keys.end());
            std::string src = a.node;   // a source's clock: every clip of it (an effect's: its node's)
            if (a.owner == "effect")
                for (const auto &e : m.effects)
                    if (e.id == a.node) src = e.node;
            for (const auto &c : m.clips)
            {
                if (c.audio || c.nested) continue;
                if (a.owner == "clip" ? c.id == a.node : c.src == src) r.spans.push_back(Span{c.id, c.at, spanDur(c), c.in, c.out, c.speed});
            }
            std::sort(r.spans.begin(), r.spans.end(), [](const Span &p, const Span &q) { return p.at < q.at; });
            char buf[48];
            std::snprintf(buf, sizeof buf, "%05d|%04d|%04d|", r.prop.objectOrder, r.prop.groupOrder, r.prop.labelOrder);
            r.sortKey = buf + r.prop.address;
            fresh.push_back(r);
        }
        // an unmarked lane eases out where it was, then goes
        for (auto &old : mRows)
        {
            bool still = false;
            for (const auto &f : fresh) still = still || f.prop.address == old.prop.address;
            const auto it = mPresent.find(old.prop.address);
            if (!still && it != mPresent.end() && it->second && it->second->value() > 0.001)
            {
                old.gone = true;
                fresh.push_back(old);
            }
        }
        std::stable_sort(fresh.begin(), fresh.end(), [](const Row &p, const Row &q) { return p.sortKey < q.sortKey; });
        mRows = std::move(fresh);
        for (const auto &r : mRows)
            if (!mPresent[r.prop.address])
            {
                // the first model places the lanes; a lane marked later eases in from nothing
                mPresent[r.prop.address].reset(new AnimatedProperty(mEverBound ? 0.0 : 1.0));
                mPresentApplied[r.prop.address] = !mEverBound;
            }
        mEverBound = true;
        // an unmarked property has no lane, so no curve open under it
        auto live = [&](const std::string &a) { const int i = rowOf(a); return i >= 0 && !mRows[(size_t)i].gone; };
        if (!mSelected.empty() && !live(mSelected)) mSelected.clear();
        mShownRows.erase(std::remove_if(mShownRows.begin(), mShownRows.end(), [&](const std::string &a) { return !live(a); }), mShownRows.end());
        if (mSelected.empty()) mShownRows.clear();
        else if (std::find(mShownRows.begin(), mShownRows.end(), mSelected) == mShownRows.end()) mShownRows.push_back(mSelected);
        // a property chosen as it was marked: its lane exists from this model on (once — a refused mark never comes)
        if (!mPendingSelect.empty())
        {
            const std::string a = mPendingSelect;
            mPendingSelect.clear();
            if (live(a)) select(a);
        }
    }

    int KeyLane::graphSpan(int i) const
    {
        // the clip under the playhead, else the chosen clip, else the first
        if (i < 0 || i >= (int)mRows.size() || mRows[(size_t)i].spans.empty()) return -1;
        const auto &sp = mRows[(size_t)i].spans;
        for (int s = 0; s < (int)sp.size(); ++s)
            if (mPlayhead >= sp[(size_t)s].at - 1e-9 && mPlayhead < sp[(size_t)s].at + sp[(size_t)s].dur - 1e-9) return s;
        if (mModel)
            for (int s = 0; s < (int)sp.size(); ++s)
                if (sp[(size_t)s].clip == mModel->selectedClip) return s;
        return 0;
    }

    void KeyLane::bindGraph(const interstellar::AppModel &m)
    {
        std::vector<std::string> ids;
        for (const auto &addr : mShownRows)
        {
            const int i = rowOf(addr);
            if (i < 0) continue;
            if (const interstellar::AnimModel *a = keys::animOf(m, mRows[(size_t)i].prop.node, mRows[(size_t)i].prop.key)) ids.push_back(a->id);
        }
        const int front = rowOf(mSelected), s = graphSpan(front);
        if (s < 0) { mGraph->bind(m, {}, 0.0, 1.0); return; }
        const Span &sp = mRows[(size_t)front].spans[(size_t)s];
        const double n = nowOf(front);
        mGraph->setNow(n >= 0 ? n : sp.in);   // the playhead on the curve's clock — not Grade's reference frame
        mGraph->setFront(mSelected);
        mGraph->bind(m, ids, sp.in, sp.out);
    }

    void KeyLane::bind(const interstellar::AppModel &m)
    {
        mModel = &m;   // the service's model outlives every frame: a selection rebinds from it
        rebuild(m);
        bindGraph(m);
    }

    void KeyLane::select(const std::string &address, bool add)
    {
        const int i = rowOf(address);
        if (i < 0) { mPendingSelect = address; return; }   // just marked: the next model brings its lane
        if (mRows[(size_t)i].gone) return;
        const auto it = std::find(mShownRows.begin(), mShownRows.end(), address);
        if (add && it != mShownRows.end() && mShownRows.size() > 1)
        {
            // Ctrl/Shift-click on a shown property hides it again
            mShownRows.erase(it);
            if (mSelected == address) mSelected = mShownRows.back();
            if (mModel) bindGraph(*mModel);
            return;
        }
        if (!add) mShownRows.clear();
        if (std::find(mShownRows.begin(), mShownRows.end(), address) == mShownRows.end()) mShownRows.push_back(address);
        mSelected = address;
        // bring the lane and its curve into view — re-aimed every frame while the lanes and bands move
        mScroll.reveal(rowTop(i) - kHeadH, kRowH + kGraphH);
        mRevealFront = true;
        if (mModel) bindGraph(*mModel);
    }

    void KeyLane::closeCurve()
    {
        mSelected.clear();
        mShownRows.clear();
        if (mModel) bindGraph(*mModel);
    }

    int KeyLane::rowOf(const std::string &address) const
    {
        for (int i = 0; i < (int)mRows.size(); ++i)
            if (mRows[(size_t)i].prop.address == address) return i;
        return -1;
    }

    int KeyLane::markedCount() const
    {
        int n = 0;
        for (const auto &r : mRows) n += r.gone ? 0 : 1;
        return n;
    }

    double KeyLane::rowAmount(int i) const
    {
        if (i < 0 || i >= (int)mRows.size()) return 0.0;
        const auto it = mPresent.find(mRows[(size_t)i].prop.address);
        return it == mPresent.end() || !it->second ? (mRows[(size_t)i].gone ? 0.0 : 1.0) : it->second->value();
    }

    double KeyLane::diamondFill(int i) const
    {
        const auto it = mDia.find(mRows[(size_t)i].prop.address);
        return it == mDia.end() ? 0.0 : it->second.fill.value();
    }

    double KeyLane::bandAmount(int i) const
    {
        if (i < 0 || i >= (int)mRows.size()) return 0.0;
        const auto it = mBand.find(mRows[(size_t)i].prop.address);
        return it == mBand.end() || !it->second ? 0.0 : it->second->value();
    }

    double KeyLane::nowOf(int i) const
    {
        if (i < 0 || i >= (int)mRows.size()) return -1.0;
        for (const auto &s : mRows[(size_t)i].spans)
            if (mPlayhead >= s.at - 1e-9 && mPlayhead < s.at + s.dur - 1e-9) return std::clamp(s.in + (mPlayhead - s.at) * s.speed, s.in, s.out);
        return -1.0;
    }

    double KeyLane::hintAmount() const
    {
        double none = 1.0;
        for (int i = 0; i < (int)mRows.size(); ++i) none = std::min(none, 1.0 - rowAmount(i));
        return none;
    }

    double KeyLane::rowH(int i) const { return kRowH * rowAmount(i); }
    double KeyLane::bandH(int i) const { return kGraphH * bandAmount(i) * rowAmount(i); }

    double KeyLane::rowTop(int i) const
    {
        double y = kHeadH + kPad + kHintH * hintAmount();
        for (int k = 0; k < i; ++k) y += rowH(k) + bandH(k);
        return y;
    }

    double KeyLane::contentH() const
    {
        double h = kPad + kHintH * hintAmount();
        for (int i = 0; i < (int)mRows.size(); ++i) h += rowH(i) + bandH(i);
        return h + kPad;
    }

    Rect KeyLane::headerRect() const { return Rect{0.0, 0.0, width.value(), kHeadH}; }
    Rect KeyLane::foldRect() const { return Rect{0.0, 0.0, mColW, kHeadH}; }
    Rect KeyLane::addRect() const { return Rect{mColW + kPadX - 4.0, 3.0, kAddW, kHeadH - 6.0}; }
    Rect KeyLane::rowRect(int i) const { return Rect{0.0, rowTop(i) - mScroll.value(), mColW, rowH(i)}; }

    Rect KeyLane::bandRect(int i) const
    {
        const Rect r = rowRect(i);
        return Rect{0.0, r.bottom(), width.value(), bandH(i)};
    }

    Rect KeyLane::diamondRect(int i) const
    {
        const Rect r = rowRect(i);
        const double top = r.bottom() - kRowH;   // the lane's full line, revealed from its bottom
        return Rect{mColW - kPadX - kDiamondW + 4.0, top, kDiamondW, kRowH * 0.5};
    }

    Rect KeyLane::modeRect(int i) const
    {
        const Rect r = rowRect(i);
        const double cy = r.bottom() - kRowH * 0.3;
        return Rect{mColW - kPadX - kModeW, cy - 6.0, kModeW, 12.0};
    }

    Point KeyLane::keyPoint(int i, int k, int s) const
    {
        const Rect r = rowRect(i);
        const Row &row = mRows[(size_t)i];
        double t = row.keys[(size_t)k];
        if (mKeyDrag.row == i && mKeyDrag.key == k && mKeyDrag.moved) { t = mKeyDrag.t; if (s < 0) s = mKeyDrag.span; }   // the pointer is the animation
        if (s < 0)
            for (int q = 0; q < (int)row.spans.size() && s < 0; ++q)
                if (t >= row.spans[(size_t)q].in - 1e-9 && t <= row.spans[(size_t)q].out + 1e-9) s = q;
        const double xx = s >= 0 && s < (int)row.spans.size() ? keyX(row.spans[(size_t)s], t) : mColW;
        return Point{xx, r.bottom() - kRowH * 0.5};
    }

    int KeyLane::spanAt(int i, double xx) const
    {
        const auto &sp = mRows[(size_t)i].spans;
        for (int s = 0; s < (int)sp.size(); ++s)
            if (xx >= xOf(sp[(size_t)s].at) && xx <= xOf(sp[(size_t)s].at + sp[(size_t)s].dur)) return s;
        return -1;
    }

    int KeyLane::keyAt(const Point &p, int &row, int &span) const
    {
        row = span = -1;
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Row &r = mRows[(size_t)i];
            if (rowH(i) < 2.0 || r.gone) continue;
            const Rect rr = rowRect(i);
            if (p.y < rr.y || p.y >= rr.bottom()) continue;
            row = i;
            // a source's key shows under every clip of it: the one under the pointer is the one meant
            for (int s = 0; s < (int)r.spans.size(); ++s)
                for (int k = (int)r.keys.size() - 1; k >= 0; --k)
                {
                    const Span &sp = r.spans[(size_t)s];
                    if (r.keys[(size_t)k] < sp.in - 1e-9 || r.keys[(size_t)k] > sp.out + 1e-9) continue;
                    if (std::fabs(keyX(sp, r.keys[(size_t)k]) - p.x) <= kKeyR + 3.0) { span = s; return k; }
                }
            return -1;
        }
        return -1;
    }

    void KeyLane::layout()
    {
        // the front property's curve sits in its band, its plot exactly the clip it is drawn over
        const int front = rowOf(mSelected), s = graphSpan(front);
        const Rect band = front >= 0 ? bandRect(front) : Rect{0, 0, 0, 0};
        mGraph->x.set(mColW);
        mGraph->y.set(band.y);
        mGraph->width.set(std::max(0.0, width.value() - mColW));
        mGraph->height.set(std::max(0.0, band.h));
        mGraph->opacity.set(std::clamp(band.h / kGraphH, 0.0, 1.0));
        mGraph->visible = s >= 0 && band.h > 1.0 && band.bottom() > kHeadH && band.y < height.value();
        if (s >= 0)
        {
            const Span &sp = mRows[(size_t)front].spans[(size_t)s];
            mGraph->setPlotSpan(xOf(sp.at) - mColW, xOf(sp.at + sp.dur) - mColW);
        }
        mScroll.setExtent(kHeadH, std::max(0.0, height.value() - kHeadH), contentH());
    }

    void KeyLane::advance(double nowMs)
    {
        bool moving = false;
        for (const auto &r : mRows)
        {
            const std::string &a = r.prop.address;
            // a lane eases in when its property is marked, out when it is unmarked
            auto &present = mPresent[a];
            const bool shown = !r.gone;
            if (!present) { present.reset(new AnimatedProperty(shown ? 1.0 : 0.0)); mPresentApplied[a] = shown; }
            else if (mPresentApplied[a] != shown)
            {
                present->animateTo(shown ? 1.0 : 0.0, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                mPresentApplied[a] = shown;
            }
            present->update(nowMs);
            // a curve band opens under the front lane and closes under the one that stops being it
            auto &band = mBand[a];
            const bool want = a == mSelected;
            if (!band) { band.reset(new AnimatedProperty(want ? 1.0 : 0.0)); mBandApplied[a] = want; }   // first placement
            else if (mBandApplied[a] != want)
            {
                band->animateTo(want ? 1.0 : 0.0, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                mBandApplied[a] = want;
            }
            band->update(nowMs);
            moving = moving || present->isAnimating() || band->isAnimating();
            // its diamond: an outline while the playhead is over a clip of it (it can key), filled on a key
            const int i = (int)(&r - &mRows[0]);
            const double n = nowOf(i);
            bool here = false;
            for (const double k : r.keys) here = here || (n >= 0 && std::fabs(k - n) < 5e-4);
            DiamondAnim &d = mDia[a];
            const double fill = here ? 1.0 : 0.0, line = n >= 0 ? 1.0 : 0.0;
            if (d.fillL < 0) { d.fill.set(fill); d.line.set(line); d.fillL = fill; d.lineL = line; }   // first placement
            if (fill != d.fillL) { d.fill.animateTo(fill, 180.0, Easing::EaseOutCubic, nowMs); d.fillL = fill; }
            if (line != d.lineL) { d.line.animateTo(line, 180.0, Easing::EaseOutCubic, nowMs); d.lineL = line; }
            d.fill.update(nowMs);
            d.line.update(nowMs);
        }
        // a lane that has finished easing out is gone
        std::set<std::string> done;
        for (int i = 0; i < (int)mRows.size(); ++i)
            if (mRows[(size_t)i].gone && rowAmount(i) < 0.001) done.insert(mRows[(size_t)i].prop.address);
        if (!done.empty())
            mRows.erase(std::remove_if(mRows.begin(), mRows.end(), [&](const Row &r) { return done.count(r.prop.address) > 0; }), mRows.end());
        if (mRevealFront)
        {
            const int front = rowOf(mSelected);
            if (front >= 0) mScroll.reveal(rowTop(front) - kHeadH, kRowH + kGraphH);
            mRevealFront = moving && front >= 0;
        }
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        layout();
        Segment::advance(nowMs);
    }

    bool KeyLane::handleGesture(const Gesture &g, const Point &local)
    {
        auto rowAt = [&](const Point &p) {
            if (p.y < kHeadH) return -1;
            for (int i = 0; i < (int)mRows.size(); ++i)
                if (!mRows[(size_t)i].gone && rowH(i) > 1.0 && p.y >= rowRect(i).y && p.y < rowRect(i).bottom()) return i;
            return -1;
        };
        const bool column = local.x < mColW;
        switch (g.type)
        {
            case Gesture::Type::Move:
            {
                const int i = rowAt(local);
                int slot = -1;
                if (addRect().contains(local)) slot = 0;
                else if (foldRect().contains(local)) slot = 1;
                else if (i >= 0) slot = modeRect(i).contains(local) ? 3 + 2 * i : 2 + 2 * i;
                mHover.setHovered(slot);
                return true;
            }
            case Gesture::Type::Scroll: return mScroll.scrollBy(g.delta.y);   // nothing to scroll: the timeline's tracks take it
            case Gesture::Type::Down:
            {
                // a keyframe on its lane: the press is the start of a move in time
                int row = -1, span = -1;
                const int k = column ? -1 : keyAt(local, row, span);
                mKeyDrag = KeyDrag{};
                if (k >= 0)
                {
                    mKeyDrag.address = mRows[(size_t)row].prop.address;
                    mKeyDrag.row = row;
                    mKeyDrag.key = k;
                    mKeyDrag.span = span;
                    mKeyDrag.from = mKeyDrag.t = mRows[(size_t)row].keys[(size_t)k];
                }
                return true;
            }
            case Gesture::Type::DragStart:
            case Gesture::Type::Drag:
            {
                if (mKeyDrag.row < 0 || mKeyDrag.row >= (int)mRows.size()) return true;
                const Row &r = mRows[(size_t)mKeyDrag.row];
                if (mKeyDrag.key >= (int)r.keys.size() || mKeyDrag.span < 0 || mKeyDrag.span >= (int)r.spans.size()) return true;
                const Span &sp = r.spans[(size_t)mKeyDrag.span];
                // on the clip it was grabbed in, between its neighbours, on the file's millisecond grid
                const double lo = mKeyDrag.key > 0 ? r.keys[(size_t)mKeyDrag.key - 1] + kGapT : -1e9;
                const double hi = mKeyDrag.key + 1 < (int)r.keys.size() ? r.keys[(size_t)mKeyDrag.key + 1] - kGapT : 1e9;
                const double t = sp.in + (tOf(local.x) - sp.at) * sp.speed;
                mKeyDrag.t = std::clamp(std::round(t * 1000.0) / 1000.0, lo, hi);
                mKeyDrag.moved = mKeyDrag.moved || std::fabs(mKeyDrag.t - mKeyDrag.from) >= 5e-4;
                return true;
            }
            case Gesture::Type::Up:
            case Gesture::Type::Drop:
            {
                const KeyDrag d = mKeyDrag;
                mKeyDrag = KeyDrag{};
                if (d.row >= 0 && d.moved && std::fabs(d.t - d.from) >= 5e-4 && onCommand)
                    onCommand("key set " + cmd::quote(d.address) + " --at " + cmd::num(d.from) + " --to " + cmd::num(d.t));
                return true;
            }
            case Gesture::Type::DoubleClick:
            {
                // on a lane, inside a clip of its object: a key there, on that clip's clock
                const int i = rowAt(local);
                if (column || i < 0 || !onCommand) return true;
                const int s = spanAt(i, local.x);
                if (s < 0) return true;
                const Span &sp = mRows[(size_t)i].spans[(size_t)s];
                const double t = std::clamp(std::round((sp.in + (tOf(local.x) - sp.at) * sp.speed) * 1000.0) / 1000.0, sp.in, sp.out);
                onCommand("key add " + cmd::quote(mRows[(size_t)i].prop.address) + " --at " + cmd::num(t));
                return true;
            }
            case Gesture::Type::RightClick:
            {
                int row = -1, span = -1;
                const int k = column ? -1 : keyAt(local, row, span);
                const Point w = worldTransform().apply(local);
                if (k >= 0) { if (onKeyContext) onKeyContext(mRows[(size_t)row].prop.address, mRows[(size_t)row].keys[(size_t)k], w); }
                else if (const int i = rowAt(local); i >= 0 && onRowContext) onRowContext(mRows[(size_t)i].prop.address, w);
                return true;
            }
            case Gesture::Type::Click:
            {
                if (addRect().contains(local))
                {
                    // Animate…: the host offers what is not marked yet, under the button
                    if (onAddMenu)
                    {
                        const Rect b = addRect();
                        const Point o = worldTransform().apply(Point{b.x, b.y});
                        onAddMenu(Rect{o.x, o.y, b.w, b.h});
                    }
                    return true;
                }
                if (foldRect().contains(local)) { if (onToggle) onToggle(); return true; }
                const int i = rowAt(local);
                if (i < 0) return true;
                Row &r = mRows[(size_t)i];
                if (column && diamondRect(i).contains(local))
                {
                    // keys at the playhead — only over a clip of its object: there is no other clock to key on
                    const double n = nowOf(i);
                    if (n >= 0 && onCommand)
                    {
                        bool here = false;
                        for (const double k : r.keys) here = here || std::fabs(k - n) < 5e-4;
                        onCommand(keys::toggle(r.prop.address, here ? 2 : 1, n));
                    }
                    return true;
                }
                if (column && modeRect(i).contains(local))
                {
                    // R-ANIM-10: the other mode, the picture held (a shape or a clip's own is fixed only)
                    if (!r.prop.fixedOnly && onCommand)
                        onCommand("key mode " + cmd::quote(r.prop.address) + (r.mode == "offset" ? " fixed" : " offset"));
                    return true;
                }
                // a key on the lane: its curve opens with that key chosen
                int row = -1, span = -1;
                const int k = column ? -1 : keyAt(local, row, span);
                if (k >= 0)
                {
                    if (mSelected != r.prop.address || g.ctrl || g.shift) select(r.prop.address, g.ctrl || g.shift);
                    const auto &sh = mShownRows;
                    const int curve = (int)(std::find(sh.begin(), sh.end(), r.prop.address) - sh.begin());
                    if (curve < mGraph->curveCount()) mGraph->selectKey(curve, k, false);
                    return true;
                }
                // the lane itself: its curve opens under it — or, the front one clicked again, closes
                if (!g.ctrl && !g.shift && r.prop.address == mSelected) closeCurve();
                else select(r.prop.address, g.ctrl || g.shift);
                return true;
            }
            default: return true;
        }
    }

    void KeyLane::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        t.save();
        t.clipRect(0, 0, w, h);   // its own paint too, not only its children
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(surface::laneBg()));
        drawRoundedRect(t, Rect{0, 0, mColW, h}, 0.0, Paint::filled(surface::trackHeaderBg()));
        glyph::line(t, mColW - 0.5, 0, mColW - 0.5, h, palette::border(), 1.0);
        // ── the header: ▾ ANIMATION n, and ＋ Animate… ──
        {
            drawRoundedRect(t, headerRect(), 0.0, Paint::filled(palette::card()));
            glyph::line(t, 0, 0.5, w, 0.5, palette::border(), 1.0);
            glyph::line(t, 0, kHeadH - 0.5, w, kHeadH - 0.5, palette::border(), 1.0);
            const double cy = kHeadH * 0.5, hv = mHover.amount(1);
            if (hv > 0.001) drawRoundedRect(t, Rect{2.0, 2.0, mColW - 4.0, kHeadH - 4.0}, radius::control(), Paint::filled(palette::hoverWash(hv)));
            {
                // the fold's chevron turns with the section: ▸ folded, ▾ open
                const double a = (1.0 - std::clamp(mOpen, 0.0, 1.0)) * -1.5707963, cx = kPadX + 3.5;
                auto pt = [&](double px, double py) { return Point{cx + px * std::cos(a) - py * std::sin(a), cy + px * std::sin(a) + py * std::cos(a)}; };
                const Point p0 = pt(-3.5, -1.75), p1 = pt(3.5, -1.75), p2 = pt(0.0, 2.75);
                t.beginPath(); t.moveTo(p0.x, p0.y); t.lineTo(p1.x, p1.y); t.lineTo(p2.x, p2.y); t.closePath();
                t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), 0.5 + 0.5 * hv));
                t.fillPath();
            }
            const double lx = kPadX + 12.0, track = 9.0 * 0.13;
            const std::string count = std::to_string(markedCount());
            const double cw = textfit::width(t, count, 9.0, font::mono());
            const std::string title = textfit::ellipsize(t, "ANIMATION", mColW - lx - cw - kPadX - 6.0, 9.0, font::sansSemiBold(), track);
            t.setFill(palette::foreground());
            t.drawText(title, lx, textfit::baseline(cy, 9.0), 9.0, font::sansSemiBold(), track);
            t.setFill(palette::mutedForeground());
            t.drawText(count, mColW - kPadX - cw, textfit::baseline(cy, 9.0), 9.0, font::mono());
            const Rect b = addRect();
            const double av = mHover.amount(0);
            drawRoundedRect(t, b, radius::control(), Paint::filledStroked(lerpColor(palette::secondary(), palette::primaryAlpha(0.22), av), palette::border(), 1.0));
            glyph::plus(t, Rect{b.x + 6.0, b.y + (b.h - 8.0) * 0.5, 8.0, 8.0}, lerpColor(palette::mutedForeground(), palette::primary(), av));
            t.setFill(lerpColor(palette::foreground(), palette::primary(), av));
            t.drawText(textfit::ellipsize(t, "Animate\xE2\x80\xA6", b.w - 22.0, 10.0, font::sans()), b.x + 18.0, textfit::baseline(b.y + b.h * 0.5, 10.0), 10.0, font::sans());
        }
        t.save();
        t.clipRect(0, kHeadH, w, std::max(0.0, h - kHeadH));
        // while nothing is marked: what this section is for
        if (const double none = hintAmount(); none > 0.001)
        {
            const double y = kHeadH + kPad - mScroll.value() + kHintH * 0.5;
            t.setFill(fade(palette::mutedForeground(), none));
            t.drawText(textfit::ellipsize(t, "Nothing is animated \xE2\x80\x94 mark a property in Grade (its diamond) or Animate\xE2\x80\xA6", w - mColW - 2 * kPadX, 10.0, font::sans()),
                       mColW + kPadX, textfit::baseline(y, 10.0), 10.0, font::sans());
        }
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Rect r = rowRect(i);
            const Rect band = bandRect(i);
            if ((r.h < 0.5 && band.h < 0.5) || std::max(r.bottom(), band.bottom()) < kHeadH || r.y > h) continue;
            const Row &row = mRows[(size_t)i];
            const double present = rowAmount(i);
            const bool sel = row.prop.address == mSelected;
            const bool shownToo = !sel && std::find(mShownRows.begin(), mShownRows.end(), row.prop.address) != mShownRows.end();
            t.save();
            t.clipRect(0, r.y, w, r.h + band.h);   // a lane easing in or out is cut, never squashed
            t.pushLayer(present * present);
            const Rect line{0.0, r.bottom() - kRowH, w, kRowH};   // the lane's full line, revealed from its bottom
            const Rect cell{line.x + 4.0, line.y + 1.0, mColW - 8.0, line.h - 2.0};
            if (sel) drawRoundedRect(t, cell, radius::control(), Paint::filled(palette::primaryAlpha(0.16)));
            else if (shownToo) drawRoundedRect(t, cell, radius::control(), Paint::filled(palette::whiteAlpha(0.06)));
            else if (const double hv = std::max(mHover.amount(2 + 2 * i), mHover.amount(3 + 2 * i)); hv > 0.001)
                drawRoundedRect(t, cell, radius::control(), Paint::filled(palette::hoverWash(hv)));
            // ── its name on two lines: the property, then "object · group"; its diamond and its mode ──
            const double y1 = line.y + kRowH * 0.3, y2 = line.y + kRowH * 0.7;
            t.setFill(palette::foreground());
            t.drawText(textfit::ellipsize(t, row.prop.label, mColW - 2 * kPadX - kDiamondW - 2.0, 10.0, font::sans()), kPadX, textfit::baseline(y1, 10.0), 10.0, font::sans());
            t.setFill(palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, row.prop.object + " \xC2\xB7 " + row.prop.group, mColW - 2 * kPadX - kModeW - 4.0, 9.0, font::sans()),
                       kPadX, textfit::baseline(y2, 9.0), 9.0, font::sans());
            {
                const Rect d = diamondRect(i);
                const double cx = d.x + d.w * 0.5, cyy = d.y + d.h * 0.5 + 1.0, rr = 4.0;
                auto diamond = [&] { t.beginPath(); t.moveTo(cx, cyy - rr); t.lineTo(cx + rr, cyy); t.lineTo(cx, cyy + rr); t.lineTo(cx - rr, cyy); t.closePath(); };
                const auto dit = mDia.find(row.prop.address);
                const double fill = dit == mDia.end() ? 0.0 : dit->second.fill.value();
                const double ln = dit == mDia.end() ? 0.0 : dit->second.line.value();
                if (fill > 0.01) { diamond(); t.setFill(palette::primaryAlpha(fill)); t.fillPath(); }
                diamond();
                t.setStroke(lerpColor(palette::whiteAlpha(0.22), palette::foreground(), ln), 1.0);
                t.strokePath();
            }
            {
                // OFFSET rides on Grade's value (the accent); FIXED is the value (quiet) — R-ANIM-10
                const Rect m = modeRect(i);
                const bool offset = row.mode == "offset";
                const double hv = row.prop.fixedOnly ? 0.0 : mHover.amount(3 + 2 * i);
                drawRoundedRect(t, m, radius::hairline(), Paint::filledStroked(lerpColor(offset ? palette::primaryAlpha(0.16) : palette::secondary(), palette::primaryAlpha(0.3), hv),
                                                                               offset ? palette::primaryAlpha(0.45) : palette::border(), 1.0));
                const std::string s = offset ? "OFFSET" : "FIXED";
                const double tr = 7.0 * 0.06, tw = textfit::width(t, s, 7.0, font::sansSemiBold(), tr);
                t.setFill(offset ? palette::primary() : fade(palette::mutedForeground(), row.prop.fixedOnly ? 0.7 : 1.0));
                t.drawText(s, m.x + (m.w - tw) * 0.5, textfit::baseline(m.y + m.h * 0.5, 7.0), 7.0, font::sansSemiBold(), tr);
            }
            // ── its lane: every clip of its object, the keys under the frames they key ──
            t.save();
            t.clipRect(mColW, line.y, std::max(0.0, w - mColW), line.h);
            if (sel) drawRoundedRect(t, Rect{mColW, line.y, w - mColW, line.h}, 0.0, Paint::filled(palette::primaryAlpha(0.06)));
            const double cy = line.y + line.h * 0.5;
            if (row.spans.empty())
            {
                t.setFill(fade(palette::mutedForeground(), 0.8));
                t.drawText(textfit::ellipsize(t, "not in this timeline", w - mColW - 2 * kPadX, 9.0, font::sans()), mColW + kPadX, textfit::baseline(cy, 9.0), 9.0, font::sans());
            }
            for (int s = 0; s < (int)row.spans.size(); ++s)
            {
                const Span &sp = row.spans[(size_t)s];
                const double x0 = xOf(sp.at), x1 = xOf(sp.at + sp.dur);
                if (x1 < mColW || x0 > w) continue;
                drawRoundedRect(t, Rect{x0, line.y + 3.0, std::max(1.0, x1 - x0), line.h - 6.0}, radius::control(), Paint::filled(palette::whiteAlpha(0.04)));
                double first = -1, last = -1;
                for (const double k : row.keys)
                    if (k >= sp.in - 1e-9 && k <= sp.out + 1e-9) { if (first < 0) first = k; last = k; }
                if (first >= 0 && last > first) glyph::line(t, keyX(sp, first), cy, keyX(sp, last), cy, palette::whiteAlpha(sel ? 0.32 : 0.18), 1.0);
                for (int k = 0; k < (int)row.keys.size(); ++k)
                {
                    const bool dragged = mKeyDrag.row == i && mKeyDrag.key == k && mKeyDrag.moved;
                    if (dragged && s != mKeyDrag.span) continue;   // a dragged key moves on the clip it was grabbed in
                    const double kt = dragged ? mKeyDrag.t : row.keys[(size_t)k];
                    if (kt < sp.in - 1e-9 || kt > sp.out + 1e-9) continue;
                    const double xx = keyX(sp, kt);
                    t.beginPath(); t.moveTo(xx, cy - kKeyR); t.lineTo(xx + kKeyR, cy); t.lineTo(xx, cy + kKeyR); t.lineTo(xx - kKeyR, cy); t.closePath();
                    t.setFill(sel || shownToo ? palette::primary() : palette::foreground());
                    t.fillPath();
                }
            }
            t.restore();
            glyph::line(t, mColW, line.bottom() - 0.5, w, line.bottom() - 0.5, palette::whiteAlpha(0.04), 1.0);
            // ── its curve, when open: the column beside it says whose ──
            if (band.h > 0.5)
            {
                drawRoundedRect(t, Rect{0, band.y, mColW, band.h}, 0.0, Paint::filled(surface::trackHeaderBg()));
                const double a = std::clamp(band.h / kGraphH, 0.0, 1.0);
                glyph::line(t, kPadX + 2.0, band.y, kPadX + 2.0, band.y + std::max(0.0, band.h - 8.0), fade(palette::primary(), 0.6 * a), 1.0);
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, row.mode == "offset" ? "curve (offset)" : "curve", mColW - kPadX * 2 - 8.0, 9.0, font::sans()), kPadX + 8.0,
                           textfit::baseline(band.y + 10.0, 9.0), 9.0, font::sans());
                if (row.spans.empty())
                {
                    t.setFill(fade(palette::mutedForeground(), 0.8 * a));
                    t.drawText(textfit::ellipsize(t, "no clip of it in this timeline to draw the curve over", w - mColW - 2 * kPadX, 9.0, font::sans()), mColW + kPadX,
                               textfit::baseline(band.y + band.h * 0.5, 9.0), 9.0, font::sans());
                }
                glyph::line(t, 0, band.bottom() - 0.5, w, band.bottom() - 0.5, palette::border(), 1.0);
            }
            t.popLayer();
            t.restore();
        }
        // the playhead runs through the lanes as it runs through the tracks
        if (const double px = xOf(mPlayhead); px >= mColW && px <= w)
        {
            t.save();
            t.clipRect(mColW, kHeadH, w - mColW, h - kHeadH);
            glyph::line(t, px, kHeadH, px, h, surface::playhead(), 1.0);
            t.restore();
        }
        t.restore();
        mScroll.drawBar(t, w - 2.0);
        t.restore();
    }
}
}
