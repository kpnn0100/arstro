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
        mGraph->setHeaderShown(false);   // the column lists the properties
        mGraph->onCommand = [this](const std::string &l) { return onCommand && onCommand(l); };
        mGraph->onKeyContext = [this](const std::string &a, double t, Point w) { if (onKeyContext) onKeyContext(a, t, w); };
        mGraph->onPlotContext = [this](double t, Point w) { if (onPlotContext) onPlotContext(t, w); };
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
        for (const auto &r : mRows)
            if (r.header && !mOpen.count(r.section))
            {
                mOpen[r.section].reset(new AnimatedProperty(r.folded ? 0.0 : 1.0));   // first placement: no travel
                mOpenApplied[r.section] = !r.folded;
            }
        if (mSelected.empty() || rowOf(mSelected) < 0) mSelected = mRows.size() > 1 ? mRows[1].address : std::string();
        mShownRows.erase(std::remove_if(mShownRows.begin(), mShownRows.end(), [&](const std::string &a) { return rowOf(a) < 0; }), mShownRows.end());
        if (std::find(mShownRows.begin(), mShownRows.end(), mSelected) == mShownRows.end()) mShownRows.push_back(mSelected);
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
        if (!add) mShownRows.clear();
        const auto it = std::find(mShownRows.begin(), mShownRows.end(), address);
        if (add && it != mShownRows.end() && mShownRows.size() > 1)
        {
            // Ctrl/Shift-click on a shown property hides it again
            mShownRows.erase(it);
            if (mSelected == address) mSelected = mShownRows.back();
            if (mModel) bindGraph(*mModel);
            return;
        }
        if (it == mShownRows.end()) mShownRows.push_back(address);
        mSelected = address;
        // bring it into view (content coords: the row's top without the scroll)
        mScroll.reveal(rowRect(i).y + mScroll.value() - kTopPad, kRowH);
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

    double KeyLane::rowH(int i) const
    {
        const Row &r = mRows[(size_t)i];
        return r.header ? kSectionH : kRowH * openOf(r.section);
    }

    double KeyLane::contentH() const
    {
        double h = 0;
        for (int i = 0; i < (int)mRows.size(); ++i) h += rowH(i);
        return h;
    }

    Rect KeyLane::rowRect(int i) const
    {
        double y = kTopPad - mScroll.value();
        for (int k = 0; k < i; ++k) y += rowH(k);
        return Rect{0.0, y, mColW, rowH(i)};
    }

    Rect KeyLane::diamondRect(int i) const
    {
        const Rect r = rowRect(i);
        return Rect{r.right() - kPadX - kDiamondW + 6.0, r.y, kDiamondW, r.h};
    }

    void KeyLane::layout()
    {
        mGraph->x.set(mColW);
        mGraph->y.set(0);
        mGraph->width.set(std::max(0.0, width.value() - mColW));
        mGraph->height.set(height.value());
        mGraph->setPlotSpan(mSpanX0 - mColW, mSpanX1 - mColW);
        mScroll.setExtent(kTopPad, std::max(0.0, height.value() - kTopPad), contentH());
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
            DiamondAnim &d = mDia[r.address];
            const double fill = r.state == 2 ? 1.0 : 0.0, line = r.state == 0 ? 0.0 : 1.0;
            if (d.fillL < 0) { d.fill.set(fill); d.line.set(line); d.fillL = fill; d.lineL = line; }   // first placement
            if (fill != d.fillL) { d.fill.animateTo(fill, 180.0, Easing::EaseOutCubic, nowMs); d.fillL = fill; }
            if (line != d.lineL) { d.line.animateTo(line, 180.0, Easing::EaseOutCubic, nowMs); d.lineL = line; }
            d.fill.update(nowMs);
            d.line.update(nowMs);
        }
        mScroll.advance(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        layout();
        Segment::advance(nowMs);
    }

    bool KeyLane::handleGesture(const Gesture &g, const Point &local)
    {
        if (local.x >= mColW) return Segment::handleGesture(g, local);
        auto rowAt = [&](const Point &p) {
            if (p.y < kTopPad) return -1;
            for (int i = 0; i < (int)mRows.size(); ++i)
                if (rowH(i) > 1.0 && rowRect(i).contains(p)) return i;
            return -1;
        };
        switch (g.type)
        {
            case Gesture::Type::Move: mHover.setHovered(rowAt(local)); return true;
            case Gesture::Type::Scroll: return mScroll.scrollBy(g.delta.y);
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
                    return true;
                }
                const bool onDiamond = diamondRect(i).contains(local);
                if (!onDiamond || std::find(mShownRows.begin(), mShownRows.end(), r.address) == mShownRows.end())
                    select(r.address, g.ctrl || g.shift);
                if (onDiamond && onCommand) onCommand(keys::toggle(r.address, r.state, mNow));
                return true;
            }
            default: return true;
        }
    }

    void KeyLane::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, mColW, h}, 0.0, Paint::filled(surface::trackHeaderBg()));
        glyph::line(t, 0, 0.5, w, 0.5, palette::border(), 1.0);
        glyph::line(t, mColW - 0.5, 0, mColW - 0.5, h, palette::border(), 1.0);
        if (!mHasClip) return;
        t.save();
        t.clipRect(0, kTopPad, mColW, std::max(0.0, h - kTopPad));
        for (int i = 0; i < (int)mRows.size(); ++i)
        {
            const Rect r = rowRect(i);
            if (r.h < 1.0 || r.bottom() < 0 || r.y > h) continue;
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
                t.setFill(palette::mutedForeground());
                t.drawText(row.label, kPadX + 10.0, textfit::baseline(r.y + r.h * 0.5, 8.5), 8.5, font::sansSemiBold(), 0.13 * 8.5);
                continue;
            }
            const double open = openOf(row.section);
            const bool sel = row.address == mSelected;
            const bool shownToo = !sel && std::find(mShownRows.begin(), mShownRows.end(), row.address) != mShownRows.end();
            t.pushLayer(open);
            if (sel) drawRoundedRect(t, Rect{r.x + 4.0, r.y + 1.0, r.w - 8.0, r.h - 2.0}, radius::control(), Paint::filled(palette::primaryAlpha(0.16)));
            else if (shownToo) drawRoundedRect(t, Rect{r.x + 4.0, r.y + 1.0, r.w - 8.0, r.h - 2.0}, radius::control(), Paint::filled(palette::whiteAlpha(0.06)));
            else if (mHover.amount(i) > 0.001) drawRoundedRect(t, Rect{r.x + 4.0, r.y + 1.0, r.w - 8.0, r.h - 2.0}, radius::control(), Paint::filled(palette::hoverWash(mHover.amount(i))));
            t.setFill(sel ? palette::foreground() : palette::mutedForeground());
            t.drawText(textfit::ellipsize(t, row.label, r.w - kPadX - kDiamondW - 4.0, 10.0, font::sans()), kPadX, textfit::baseline(r.y + r.h * 0.5, 10.0), 10.0, font::sans());
            const Rect d = diamondRect(i);
            const double cx = d.x + d.w * 0.5, cy = d.y + d.h * 0.5, rr = 4.0;
            auto diamond = [&] { t.beginPath(); t.moveTo(cx, cy - rr); t.lineTo(cx + rr, cy); t.lineTo(cx, cy + rr); t.lineTo(cx - rr, cy); t.closePath(); };
            const auto dit = mDia.find(row.address);
            const double fill = dit == mDia.end() ? (row.state == 2 ? 1.0 : 0.0) : dit->second.fill.value();
            const double line = dit == mDia.end() ? (row.state == 0 ? 0.0 : 1.0) : dit->second.line.value();
            if (fill > 0.01) { diamond(); t.setFill(palette::primaryAlpha(fill)); t.fillPath(); }
            diamond();
            t.setStroke(lerpColor(palette::whiteAlpha(0.22), palette::foreground(), line), 1.0);
            t.strokePath();
            t.popLayer();
        }
        t.restore();
        mScroll.drawBar(t, mColW);
    }
}
}
