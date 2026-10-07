#include "KeyLane.h"
#include "ColourKeys.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "KeyState.h"
#include "TextFit.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kDiamondW = 20.0;
        constexpr double kGapT = 1e-3;   // a dragged key stays this far from its neighbours (the file's ms)
        Color fade(Color c, double a) { c.a *= a; return c; }
        // the clip's own properties (R-ANIM-1), in the order an editor reads a transform
        // …and its speed (R-EDT-3): a ramp keyed on the same footage clock
        const char *const kClipKey[] = {"opacity", "geom.x", "geom.y", "geom.scale", "geom.rotation", "speed"};
        const char *const kClipLabel[] = {"Opacity", "Position X", "Position Y", "Scale", "Rotation", "Speed"};
        constexpr int kClipRows = (int)(sizeof kClipKey / sizeof kClipKey[0]);
    }

    KeyLane::KeyLane()
    {
        clipToBounds = true;
        mGraph = std::make_shared<KeyGraph>();
        mGraph->setHeaderShown(false);   // the rows name the properties
        mGraph->onCommand = [this](const std::string &l) { return onCommand && onCommand(l); };
        mGraph->onKeyContext = [this](const std::string &a, double t, Point w) { if (onKeyContext) onKeyContext(a, t, w); };
        mGraph->onPlotContext = [this](double t, Point w) { if (onPlotContext) onPlotContext(t, w); };
        mGraph->visible = false;
        addChild(mGraph);
    }

    void KeyLane::rebuild(const interstellar::AppModel &m)
    {
        mRows.clear();
        mHasClip = false;
        for (const auto &c : m.clips)
            if (c.id == m.selectedClip && !c.audio) { mClip = c; mHasClip = true; }
        if (!mHasClip) return;
        const auto &c = mClip;
        // the clip's own footage clock — and a source's time, inside this clip, is the same seconds
        mNow = std::clamp(c.in + (m.playhead - c.at) * c.speed, c.in, c.out);
        auto header = [&](const char *name) {
            Row r;
            r.section = name;
            r.label = name;
            r.header = true;
            r.folded = mFolded.count(name) > 0;
            mRows.push_back(r);
        };
        auto prop = [&](const char *section, const std::string &label, const std::string &node, const std::string &key,
                        const std::string &address) {
            Row r;
            r.section = section;
            r.label = label;
            r.node = node;
            r.key = key;
            r.address = address;
            r.state = keys::state(m, node, key, mNow);
            if (const interstellar::AnimModel *a = keys::animOf(m, node, key))
                for (const auto &k : a->keys) r.keys.push_back(k.t);
            std::sort(r.keys.begin(), r.keys.end());
            mRows.push_back(r);
        };
        const std::string clipName = c.name.empty() ? c.id : c.name;
        header("CLIP");
        for (int i = 0; i < kClipRows; ++i) prop("CLIP", kClipLabel[i], c.id, kClipKey[i], clipName + "." + kClipKey[i]);
        if (!c.srcName.empty())
        {
            header("GRADE");
            int n = 0;
            const ColourKey *ck = colourKeys(n);
            for (int i = 0; i < n; ++i) prop("GRADE", ck[i].label, c.src, ck[i].key, c.srcName + "." + ck[i].key);
            bool any = false;
            for (const auto &e : m.effects) any = any || e.node == c.src;
            if (any)
            {
                header("EFFECTS");
                for (const auto &e : m.effects)
                {
                    if (e.node != c.src) continue;
                    prop("EFFECTS", e.label + " \xC2\xB7 Mix", e.id, "mix", e.id + ".mix");
                    for (const auto &pm : e.params) prop("EFFECTS", e.label + " \xC2\xB7 " + pm.label, e.id, pm.key, e.id + "." + pm.key);
                }
            }
        }
        if (mFoldClip != c.id)
        {
            // a clip's track opens on what moves: a section starts open when something in it is animated
            // (CLIP too when nothing is yet) — a section the user opened or folded stays as they left it
            mFoldClip = c.id;
            std::map<std::string, bool> animated;
            bool any = false;
            for (const auto &r : mRows)
                if (!r.header && r.state > 0) { animated[r.section] = true; any = true; }
            for (auto &r : mRows)
                if (r.header && !mFoldChosen.count(r.section))
                {
                    const bool open = animated[r.section] || (!any && r.section == "CLIP");
                    if (open) mFolded.erase(r.section); else mFolded.insert(r.section);
                    r.folded = !open;
                }
        }
        for (const auto &r : mRows)
            if (r.header && !mOpen.count(r.section))
            {
                mOpen[r.section].reset(new AnimatedProperty(r.folded ? 0.0 : 1.0));   // first placement: no travel
                mOpenApplied[r.section] = !r.folded;
            }
        // a property that left (another clip chosen) closes its curve; none is opened for you
        if (!mSelected.empty() && rowOf(mSelected) < 0) mSelected.clear();
        mShownRows.erase(std::remove_if(mShownRows.begin(), mShownRows.end(), [&](const std::string &a) { return rowOf(a) < 0; }), mShownRows.end());
        if (mSelected.empty()) mShownRows.clear();
        else if (std::find(mShownRows.begin(), mShownRows.end(), mSelected) == mShownRows.end()) mShownRows.push_back(mSelected);
    }

    void KeyLane::bindGraph(const interstellar::AppModel &m)
    {
        if (!mHasClip) return;
        std::vector<std::string> ids;
        std::string label;
        if (const int i = rowOf(mSelected); i >= 0) label = mRows[(size_t)i].label;
        for (const auto &addr : mShownRows)
        {
            const int i = rowOf(addr);
            if (i < 0) continue;
            if (const interstellar::AnimModel *a = keys::animOf(m, mRows[(size_t)i].node, mRows[(size_t)i].key)) ids.push_back(a->id);
        }
        mGraph->setEmptyText(label + " is not animated \xE2\x80\x94 click \xE2\x97\x87 to key it at the playhead");
        mGraph->setNow(mNow);   // the playhead in the clip — not Grade's reference frame
        mGraph->setFront(mSelected);
        mGraph->bind(m, ids, mClip.in, mClip.out);
    }

    void KeyLane::bind(const interstellar::AppModel &m)
    {
        mModel = &m;   // the service's model outlives every frame: a fold or a selection rebuilds from it
        rebuild(m);
        bindGraph(m);
    }

    void KeyLane::select(const std::string &address, bool add)
    {
        const int i = rowOf(address);
        if (i < 0) return;
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
        mFolded.erase(mRows[(size_t)i].section);   // a chosen property's section opens (eased)
        // bring the row and its curve into view — re-aimed every frame while the bands open and close
        mScroll.reveal(rowTop(i) - kTopPad, kRowH + kGraphH);
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
            if (!mRows[(size_t)i].header && mRows[(size_t)i].address == address) return i;
        return -1;
    }

    int KeyLane::sectionRow(const std::string &section) const
    {
        for (int i = 0; i < (int)mRows.size(); ++i)
            if (mRows[(size_t)i].header && mRows[(size_t)i].section == section) return i;
        return -1;
    }

    double KeyLane::diamondFill(int i) const
    {
        const auto it = mDia.find(mRows[(size_t)i].address);
        return it == mDia.end() ? 0.0 : it->second.fill.value();
    }

    double KeyLane::openOf(const std::string &section) const
    {
        const auto it = mOpen.find(section);
        return it == mOpen.end() ? 1.0 : it->second->value();
    }

    double KeyLane::bandAmount(int i) const
    {
        if (i < 0 || i >= (int)mRows.size() || mRows[(size_t)i].header) return 0.0;
        const auto it = mBand.find(mRows[(size_t)i].address);
        return it == mBand.end() ? 0.0 : it->second->value();
    }

    double KeyLane::rowH(int i) const
    {
        const Row &r = mRows[(size_t)i];
        return r.header ? kSectionH : kRowH * openOf(r.section);
    }

    double KeyLane::bandH(int i) const
    {
        const Row &r = mRows[(size_t)i];
        return r.header ? 0.0 : kGraphH * bandAmount(i) * openOf(r.section);
    }

    double KeyLane::rowTop(int i) const
    {
        double y = kTopPad;
        for (int k = 0; k < i; ++k) y += rowH(k) + bandH(k);
        return y;
    }

    double KeyLane::contentH() const
    {
        double h = kTopPad;
        for (int i = 0; i < (int)mRows.size(); ++i) h += rowH(i) + bandH(i);
        return h;
    }

    Rect KeyLane::rowRect(int i) const { return Rect{0.0, rowTop(i) - mScroll.value() - mCut, mColW, rowH(i)}; }

    Rect KeyLane::bandRect(int i) const
    {
        const Rect r = rowRect(i);
        return Rect{0.0, r.bottom(), width.value(), bandH(i)};
    }

    Rect KeyLane::diamondRect(int i) const
    {
        const Rect r = rowRect(i);
        return Rect{r.right() - kPadX - kDiamondW + 6.0, r.y, kDiamondW, r.h};
    }

    double KeyLane::xOfTime(double t) const
    {
        const double span = mClip.out - mClip.in;
        return span > 1e-9 ? mSpanX0 + (t - mClip.in) / span * (mSpanX1 - mSpanX0) : mSpanX0;
    }

    double KeyLane::timeOfX(double x) const
    {
        const double w = mSpanX1 - mSpanX0;
        return w > 1e-9 ? mClip.in + (x - mSpanX0) / w * (mClip.out - mClip.in) : mClip.in;
    }

    Point KeyLane::keyPoint(int i, int k) const
    {
        const Rect r = rowRect(i);
        const Row &row = mRows[(size_t)i];
        double t = row.keys[(size_t)k];
        if (mKeyDrag.row == i && mKeyDrag.key == k && mKeyDrag.moved) t = mKeyDrag.t;   // the pointer is the animation
        return Point{xOfTime(t), r.y + r.h * 0.5};
    }

    int KeyLane::keyAt(const Point &p, int &row) const
    {
        row = -1;
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Row &r = mRows[(size_t)i];
            if (r.header || rowH(i) < 2.0) continue;
            const Rect rr = rowRect(i);
            if (p.y < rr.y || p.y >= rr.bottom()) continue;
            row = i;
            for (int k = (int)r.keys.size() - 1; k >= 0; --k)
                if (std::fabs(keyPoint(i, k).x - p.x) <= kKeyR + 3.0) return k;
            return -1;
        }
        return -1;
    }

    void KeyLane::layout()
    {
        // the front property's curve sits in its band, its plot exactly the clip's span
        const int front = rowOf(mSelected);
        const Rect band = front >= 0 ? bandRect(front) : Rect{0, 0, 0, 0};
        mGraph->x.set(mColW);
        mGraph->y.set(band.y);
        mGraph->width.set(std::max(0.0, width.value() - mColW));
        mGraph->height.set(std::max(0.0, band.h));
        mGraph->opacity.set(std::clamp(band.h / kGraphH, 0.0, 1.0));
        mGraph->visible = band.h > 1.0 && band.bottom() > 0.0 && band.y < height.value();
        mGraph->setPlotSpan(mSpanX0 - mColW, mSpanX1 - mColW);
        mScroll.setExtent(kTopPad, std::max(0.0, mBoxH - kTopPad), contentH() - kTopPad);
    }

    void KeyLane::advance(double nowMs)
    {
        for (auto &kv : mOpen)
        {
            const bool want = !mFolded.count(kv.first);
            if (mOpenApplied[kv.first] != want)
            {
                kv.second->animateTo(want ? 1.0 : 0.0, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                mOpenApplied[kv.first] = want;
            }
            kv.second->update(nowMs);
        }
        for (const auto &r : mRows)
        {
            if (r.header) continue;
            // a curve band opens under the front row and closes under the one that stops being it
            auto &band = mBand[r.address];
            const bool want = r.address == mSelected;
            if (!band) { band.reset(new AnimatedProperty(want ? 1.0 : 0.0)); mBandApplied[r.address] = want; }   // first placement
            else if (mBandApplied[r.address] != want)
            {
                band->animateTo(want ? 1.0 : 0.0, motion::kSlideMs, Easing::EaseOutCubic, nowMs);
                mBandApplied[r.address] = want;
            }
            band->update(nowMs);
            DiamondAnim &d = mDia[r.address];
            const double fill = r.state == 2 ? 1.0 : 0.0, line = r.state == 0 ? 0.0 : 1.0;
            if (d.fillL < 0) { d.fill.set(fill); d.line.set(line); d.fillL = fill; d.lineL = line; }   // first placement
            if (fill != d.fillL) { d.fill.animateTo(fill, 180.0, Easing::EaseOutCubic, nowMs); d.fillL = fill; }
            if (line != d.lineL) { d.line.animateTo(line, 180.0, Easing::EaseOutCubic, nowMs); d.lineL = line; }
            d.fill.update(nowMs);
            d.line.update(nowMs);
        }
        if (mRevealFront)
        {
            const int front = rowOf(mSelected);
            bool moving = false;
            for (const auto &kv : mBand) moving = moving || (kv.second && kv.second->isAnimating());
            for (const auto &kv : mOpen) moving = moving || kv.second->isAnimating();
            if (front >= 0) mScroll.reveal(rowTop(front) - kTopPad, kRowH + kGraphH);
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
            for (int i = 0; i < (int)mRows.size(); ++i)
                if (rowH(i) > 1.0 && p.y >= rowRect(i).y && p.y < rowRect(i).bottom()) return i;
            return -1;
        };
        const bool column = local.x < mColW;
        switch (g.type)
        {
            case Gesture::Type::Move: mHover.setHovered(rowAt(local)); return true;
            case Gesture::Type::Scroll: return mScroll.scrollBy(g.delta.y);   // nothing to scroll: the timeline's tracks take it
            case Gesture::Type::Down:
            {
                // a keyframe on its row: the press is the start of a move in time
                int row = -1;
                const int k = column ? -1 : keyAt(local, row);
                mKeyDrag = KeyDrag{};
                if (k >= 0) { mKeyDrag.address = mRows[(size_t)row].address; mKeyDrag.row = row; mKeyDrag.key = k; mKeyDrag.from = mKeyDrag.t = mRows[(size_t)row].keys[(size_t)k]; }
                return true;
            }
            case Gesture::Type::DragStart:
            case Gesture::Type::Drag:
            {
                if (mKeyDrag.row < 0 || mKeyDrag.row >= (int)mRows.size()) return true;
                const Row &r = mRows[(size_t)mKeyDrag.row];
                if (mKeyDrag.key >= (int)r.keys.size()) return true;
                // between its neighbours, on the file's millisecond grid
                const double lo = mKeyDrag.key > 0 ? r.keys[(size_t)mKeyDrag.key - 1] + kGapT : -1e9;
                const double hi = mKeyDrag.key + 1 < (int)r.keys.size() ? r.keys[(size_t)mKeyDrag.key + 1] - kGapT : 1e9;
                mKeyDrag.t = std::clamp(std::round(timeOfX(local.x) * 1000.0) / 1000.0, lo, hi);
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
                // on a row's lane, inside the clip: a key there
                const int i = rowAt(local);
                if (column || i < 0 || mRows[(size_t)i].header || local.x < mSpanX0 || local.x > mSpanX1 || !onCommand) return true;
                const double t = std::clamp(std::round(timeOfX(local.x) * 1000.0) / 1000.0, mClip.in, mClip.out);
                onCommand("key add " + cmd::quote(mRows[(size_t)i].address) + " --at " + cmd::num(t));
                return true;
            }
            case Gesture::Type::RightClick:
            {
                int row = -1;
                const int k = column ? -1 : keyAt(local, row);
                if (k >= 0 && onKeyContext) onKeyContext(mRows[(size_t)row].address, mRows[(size_t)row].keys[(size_t)k], worldTransform().apply(local));
                return true;
            }
            case Gesture::Type::Click:
            {
                const int i = rowAt(local);
                if (i < 0) return true;
                Row &r = mRows[(size_t)i];
                if (r.header)
                {
                    // fold or unfold: presentation only, eased in advance
                    if (mFolded.count(r.section)) mFolded.erase(r.section); else mFolded.insert(r.section);
                    r.folded = mFolded.count(r.section) > 0;
                    mFoldChosen.insert(r.section);
                    return true;
                }
                const bool onDiamond = column && diamondRect(i).contains(local);
                if (onDiamond)
                {
                    if (onCommand) onCommand(keys::toggle(r.address, r.state, mNow));
                    return true;
                }
                // a key on the lane: its curve opens with that key chosen
                int row = -1;
                const int k = column ? -1 : keyAt(local, row);
                if (k >= 0)
                {
                    if (mSelected != r.address || g.ctrl || g.shift) select(r.address, g.ctrl || g.shift);
                    const auto &sh = mShownRows;
                    const int curve = (int)(std::find(sh.begin(), sh.end(), r.address) - sh.begin());
                    if (curve < mGraph->curveCount()) mGraph->selectKey(curve, k, false);
                    return true;
                }
                // the row itself: its curve opens under it — or, the front one clicked again, closes
                if (!g.ctrl && !g.shift && r.address == mSelected) closeCurve();
                else select(r.address, g.ctrl || g.shift);
                return true;
            }
            default: return true;
        }
    }

    void KeyLane::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        t.save();
        t.clipRect(0, 0, w, h);   // its own paint too, not only its children: rows end where the next track begins
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(surface::laneBg()));
        drawRoundedRect(t, Rect{0, 0, mColW, h}, 0.0, Paint::filled(surface::trackHeaderBg()));
        glyph::line(t, mColW - 0.5, 0, mColW - 0.5, h, palette::border(), 1.0);
        if (!mHasClip) { t.restore(); return; }
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Rect r = rowRect(i);
            const Rect band = bandRect(i);
            if ((r.h < 1.0 && band.h < 1.0) || std::max(r.bottom(), band.bottom()) < 0 || r.y > h) continue;
            const Row &row = mRows[(size_t)i];
            if (row.header)
            {
                // ▸ / ▾ turns with the section's own eased amount
                const double open = openOf(row.section), cx = kPadX + 3.0, cy = r.y + r.h * 0.5;
                const double a = (1.0 - open) * -1.5707963;   // 0 = pointing down
                auto pt = [&](double x, double y) { return Point{cx + x * std::cos(a) - y * std::sin(a), cy + x * std::sin(a) + y * std::cos(a)}; };
                const Point p0 = pt(-3.0, -1.5), p1 = pt(3.0, -1.5), p2 = pt(0.0, 2.5);
                t.beginPath(); t.moveTo(p0.x, p0.y); t.lineTo(p1.x, p1.y); t.lineTo(p2.x, p2.y); t.closePath();
                t.setFill(palette::mutedForeground());
                t.fillPath();
                t.drawText(row.label, kPadX + 10.0, textfit::baseline(r.y + r.h * 0.5, 8.5), 8.5, font::sansSemiBold(), 0.13 * 8.5);
                glyph::line(t, mColW, r.bottom() - 0.5, w, r.bottom() - 0.5, palette::border(), 1.0);
                continue;
            }
            const double open = openOf(row.section);
            const bool sel = row.address == mSelected;
            const bool shownToo = !sel && std::find(mShownRows.begin(), mShownRows.end(), row.address) != mShownRows.end();
            t.pushLayer(open);
            // ── the name and the diamond that keys at the playhead ──
            if (sel) drawRoundedRect(t, Rect{r.x + 4.0, r.y + 1.0, r.w - 8.0, r.h - 2.0}, radius::control(), Paint::filled(palette::primaryAlpha(0.16)));
            else if (shownToo) drawRoundedRect(t, Rect{r.x + 4.0, r.y + 1.0, r.w - 8.0, r.h - 2.0}, radius::control(), Paint::filled(palette::whiteAlpha(0.06)));
            else if (mHover.amount(i) > 0.001) drawRoundedRect(t, Rect{r.x + 4.0, r.y + 1.0, r.w - 8.0, r.h - 2.0}, radius::control(), Paint::filled(palette::hoverWash(mHover.amount(i))));
            t.setFill(sel ? palette::foreground() : palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, row.label, r.w - kPadX - kDiamondW - 4.0, 10.0, font::sans()), kPadX, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::sans());
            const Rect d = diamondRect(i);
            {
                const double cx = d.x + d.w * 0.5, cy = d.y + d.h * 0.5, rr = 4.0;
                auto diamond = [&] { t.beginPath(); t.moveTo(cx, cy - rr); t.lineTo(cx + rr, cy); t.lineTo(cx, cy + rr); t.lineTo(cx - rr, cy); t.closePath(); };
                const auto dit = mDia.find(row.address);
                const double fill = dit == mDia.end() ? (row.state == 2 ? 1.0 : 0.0) : dit->second.fill.value();
                const double line = dit == mDia.end() ? (row.state == 0 ? 0.0 : 1.0) : dit->second.line.value();
                if (fill > 0.01) { diamond(); t.setFill(palette::primaryAlpha(fill)); t.fillPath(); }
                diamond();
                t.setStroke(lerpColor(palette::whiteAlpha(0.22), palette::foreground(), line), 1.0);
                t.strokePath();
            }
            // ── its lane: the clip's span, the keys under the frames they key ──
            t.save();
            t.clipRect(mColW, r.y, std::max(0.0, w - mColW), r.h);
            if (sel) drawRoundedRect(t, Rect{mColW, r.y, w - mColW, r.h}, 0.0, Paint::filled(palette::primaryAlpha(0.06)));
            if (mSpanX1 > mSpanX0) drawRoundedRect(t, Rect{mSpanX0, r.y + 1.0, mSpanX1 - mSpanX0, r.h - 2.0}, 0.0, Paint::filled(palette::whiteAlpha(0.035)));
            const double cy = r.y + r.h * 0.5;
            if (row.keys.size() > 1)
                glyph::line(t, keyPoint(i, 0).x, cy, keyPoint(i, (int)row.keys.size() - 1).x, cy, palette::whiteAlpha(sel ? 0.32 : 0.18), 1.0);
            for (int k = 0; k < (int)row.keys.size(); ++k)
            {
                const Point p = keyPoint(i, k);
                const bool inside = row.keys[(size_t)k] >= mClip.in - 1e-9 && row.keys[(size_t)k] <= mClip.out + 1e-9;   // a source key past this clip's frames
                t.beginPath(); t.moveTo(p.x, p.y - kKeyR); t.lineTo(p.x + kKeyR, p.y); t.lineTo(p.x, p.y + kKeyR); t.lineTo(p.x - kKeyR, p.y); t.closePath();
                t.setFill(fade(sel || shownToo ? palette::primary() : palette::foreground(), inside ? 1.0 : 0.35));
                t.fillPath();
            }
            t.restore();
            glyph::line(t, mColW, r.bottom() - 0.5, w, r.bottom() - 0.5, palette::whiteAlpha(0.04), 1.0);
            // ── its curve, when open: the column beside it says whose ──
            if (band.h > 0.5)
            {
                drawRoundedRect(t, Rect{0, band.y, mColW, band.h}, 0.0, Paint::filled(surface::trackHeaderBg()));
                const double a = std::clamp(band.h / kGraphH, 0.0, 1.0);
                glyph::line(t, kPadX + 2.0, band.y, kPadX + 2.0, band.y + std::max(0.0, band.h - 8.0), fade(palette::primary(), 0.6 * a), 1.0);
                t.setFill(fade(palette::mutedForeground(), a));
                t.drawText(textfit::ellipsize(t, "curve", mColW - kPadX * 2 - 8.0, 9.0, font::sans()), kPadX + 8.0, textfit::baseline(band.y + 10.0, 9.0), 9.0, font::sans());
                glyph::line(t, 0, band.bottom() - 0.5, w, band.bottom() - 0.5, palette::border(), 1.0);
            }
            t.popLayer();
        }
        // the playhead runs through the rows as it runs through the tracks
        if (mPlayheadX >= mColW && mPlayheadX <= w)
        {
            t.save();
            t.clipRect(mColW, 0, w - mColW, h);
            glyph::line(t, mPlayheadX, 0, mPlayheadX, h, surface::playhead(), 1.0);
            t.restore();
        }
        mScroll.drawBar(t, w - 2.0);
        t.restore();
    }
}
}
