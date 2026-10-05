#include "PluginList.h"
#include "CommandLine.h"
#include "Glyphs.h"
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
        constexpr double kSwitchW = 24.0, kSwitchH = 13.0;
        constexpr double kBottomPad = 4.875;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string keyOf(const std::string &id) { return id.empty() ? std::string("cosmo") : id; }
    }

    PluginList::PluginList() { clipToBounds = true; }

    void PluginList::bind(const interstellar::AppModel &m)
    {
        mItems.clear();
        mHasTarget = m.hasGradeTarget && m.selectedRack >= 0 && m.selectedRack < (int)m.rack.size();
        std::string node;
        if (mHasTarget)
        {
            const auto &n = m.rack[(size_t)m.selectedRack];
            if (n.bindName != mBind) mSelected.clear();   // another node: back to its Cosmo
            mBind = n.bindName;
            node = n.rackObj;
            Item cosmo;
            cosmo.label = "Cosmo";
            cosmo.sub = n.group ? "colour \xC2\xB7 group" : "colour";
            cosmo.enabled = !n.bypass;
            cosmo.mix = n.weight;
            mItems.push_back(cosmo);
            for (const auto &e : m.effects)
                if (e.node == node)
                {
                    Item it;
                    it.id = e.id;
                    it.label = e.label;
                    it.enabled = e.enabled;
                    it.mix = e.mix;
                    if (!e.params.empty())
                    {
                        const auto &p = e.params[0];
                        it.sub = cmd::num(std::round(p.value * (p.unit.empty() ? 100.0 : 1.0))) + (p.unit.empty() ? "%" : " " + p.unit);
                    }
                    mItems.push_back(it);
                }
        }
        else
            mBind.clear();
        bool still = mSelected.empty();
        for (const auto &it : mItems) still = still || it.id == mSelected;
        if (!still) select("");   // the selected effect went away
        std::vector<std::pair<std::string, Item>> items;
        for (const auto &it : mItems) items.emplace_back(keyOf(it.id), it);
        mRows.sync(items, kRowH);
        int sel = -1;
        for (int i = 0; i < (int)mItems.size(); ++i) if (mItems[(size_t)i].id == mSelected) sel = i;
        mSel.setHovered(sel);
        mHeightTarget = kHeaderH + (double)mItems.size() * kRowH + kBottomPad;
    }

    void PluginList::select(const std::string &id)
    {
        if (id == mSelected) return;
        mSelected = id;
        int sel = -1;
        for (int i = 0; i < (int)mItems.size(); ++i) if (mItems[(size_t)i].id == mSelected) sel = i;
        mSel.setHovered(sel);
        if (onSelect) onSelect(id);
    }

    Rect PluginList::rowRect(int i) const
    {
        const auto *r = mRows.byIndex(i);
        return Rect{0, kHeaderH + (r ? r->liveY() : i * kRowH), width.value(), kRowH};
    }
    Rect PluginList::switchRect(int i) const
    {
        const Rect r = rowRect(i);
        return Rect{kPadX, r.y + (kRowH - kSwitchH) * 0.5, kSwitchW, kSwitchH};
    }
    Rect PluginList::removeRect(int i) const
    {
        const Rect r = rowRect(i);
        return Rect{r.right() - kPadX - 16.0, r.y + (kRowH - 16.0) * 0.5, 16.0, 16.0};
    }
    Rect PluginList::addRect() const { return Rect{width.value() - kPadX - 44.0, (kHeaderH - 18.0) * 0.5, 44.0, 18.0}; }

    double PluginList::switchAmount(int i) const
    {
        if (i < 0 || i >= (int)mItems.size()) return 0.0;
        const auto it = mKnob.find(keyOf(mItems[(size_t)i].id));
        return it == mKnob.end() ? (mItems[(size_t)i].enabled ? 1.0 : 0.0) : it->second.value();
    }

    int PluginList::rowAt(const Point &p) const
    {
        for (int i = 0; i < (int)mItems.size(); ++i)
            if (rowRect(i).contains(p)) return i;
        return -1;
    }

    bool PluginList::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            if (addRect().contains(local)) { mHover.setHovered(1); return true; }
            const int i = rowAt(local);
            mHover.setHovered(i >= 0 ? 10 + i : -1);
            return true;
        }
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::Click:
        {
            if (mHasTarget && addRect().contains(local))
            {
                const Point o = worldTransform().apply(Point{addRect().x, addRect().y});
                if (onAdd) onAdd(Rect{o.x, o.y, addRect().w, addRect().h});
                return true;
            }
            const int i = rowAt(local);
            if (i < 0) return true;
            const Item &it = mItems[(size_t)i];
            if (switchRect(i).contains(local))
            {
                // the switch: Cosmo's is the node's bypass, an effect's is its own `enabled`
                if (it.id.empty()) emit("set " + cmd::quote(mBind + ".bypass=" + (it.enabled ? "1" : "0")));
                else emit("set " + cmd::quote(it.id + ".enabled=" + (it.enabled ? "0" : "1")));
                return true;
            }
            if (!it.id.empty() && removeRect(i).contains(local))
            {
                emit("effect remove " + cmd::quote(it.id));
                return true;
            }
            select(it.id);
            return true;
        }
        case Gesture::Type::RightClick:
        {
            const int i = rowAt(local);
            if (i >= 0 && onRowContext) onRowContext(mItems[(size_t)i].id, worldTransform().apply(local));
            return true;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void PluginList::advance(double nowMs)
    {
        mRows.advance(nowMs);
        for (const auto &it : mItems)
        {
            const std::string k = keyOf(it.id);
            auto &knob = mKnob[k];
            auto ap = mKnobApplied.find(k);
            if (ap == mKnobApplied.end()) { knob.set(it.enabled ? 1.0 : 0.0); mKnobApplied[k] = it.enabled; }
            else if (ap->second != it.enabled)
            {
                knob.animateTo(it.enabled ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
                ap->second = it.enabled;
            }
        }
        for (auto &kv : mKnob) kv.second.update(nowMs);
        if (mHeightApplied < 0) { mHeight.set(mHeightTarget); mHeightApplied = mHeightTarget; }
        else if (mHeightTarget != mHeightApplied)
        {
            mHeight.animateTo(mHeightTarget, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
            mHeightApplied = mHeightTarget;
        }
        mHeight.update(nowMs);
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        mSel.advance(nowMs, motion::kSelectMs);
        Segment::advance(nowMs);
    }

    void PluginList::onPaint(IRenderTarget &t) const
    {
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::card()));
        glyph::line(t, 0, h - 0.5, w, h - 0.5, palette::border(), 1.0);
        // header: IMAGE PROCESSING ……… + Add
        const double hy = kHeaderH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText("IMAGE PROCESSING", kPadX, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
        if (mHasTarget)
        {
            const Rect ar = addRect();
            const double hv = mHover.amount(1);
            drawRoundedRect(t, ar, radius::control(), Paint::filledStroked(palette::hoverWash(hv), lerpColor(palette::border(), palette::primary(), 0.4 + 0.6 * hv), 1.0));
            glyph::plus(t, Rect{ar.x + 5.0, ar.y + 4.0, 10.0, 10.0}, lerpColor(palette::mutedForeground(), palette::foreground(), hv));
            t.setFill(lerpColor(palette::mutedForeground(), palette::foreground(), hv));
            t.drawText("Add", ar.x + 18.0, textfit::baseline(ar.y + ar.h * 0.5, 10.0), 10.0, font::sans());
        }
        else
        {
            t.setFill(fade(palette::mutedForeground(), 0.7));
            t.drawText("select a source or a group", kPadX + 112.0, textfit::baseline(hy, 9.5), 9.5, font::sans());
        }
        t.save();
        t.clipRect(0, kHeaderH, w, std::max(0.0, h - kHeaderH));
        for (const auto &row : mRows.rows())
        {
            const double a = row.liveAlpha();
            if (a <= 0.001) continue;
            const Item &it = row.data;
            const int i = row.index;
            const double top = kHeaderH + row.liveY();
            const double cy = top + kRowH * 0.5;
            const double sel = i >= 0 ? mSel.amount(i) : 0.0, hv = i >= 0 ? mHover.amount(10 + i) : 0.0;
            if (sel > 0.001)
            {
                drawRoundedRect(t, Rect{3.0, top + 1.0, w - 6.0, kRowH - 2.0}, radius::control(), Paint::filled(palette::primaryAlpha(0.14 * sel * a)));
                drawRoundedRect(t, Rect{0, top + 5.0, 2.0, kRowH - 10.0}, 0.0, Paint::filled(palette::primaryAlpha(sel * a)));
            }
            if (hv > 0.001) drawRoundedRect(t, Rect{3.0, top + 1.0, w - 6.0, kRowH - 2.0}, radius::control(), Paint::filled(palette::hoverWash(hv * a)));
            // the switch: a pill track, the knob sliding with the eased amount
            const auto kn = mKnob.find(keyOf(it.id));
            const double on = kn == mKnob.end() ? (it.enabled ? 1.0 : 0.0) : kn->second.value();
            const Rect sr{kPadX, cy - kSwitchH * 0.5, kSwitchW, kSwitchH};
            drawRoundedRect(t, sr, radius::pill(), Paint::filled(fade(lerpColor(palette::switchBackground(), palette::primary(), on), a)));
            drawCircle(t, sr.x + 6.5 + (kSwitchW - 13.0) * on, cy, 4.5, Paint::filled(fade(palette::white(), a)));
            // name, dimmed when off; the mix (or, on hover, the ×) on the right
            const double content = a * (0.45 + 0.55 * on);
            const double nx = sr.right() + 9.0;
            const bool canRemove = !it.id.empty();
            const double right = w - kPadX - (canRemove ? 22.0 : 0.0) - 34.0;
            t.setFill(fade(it.id.empty() ? palette::foreground() : palette::foreground(), content));
            const std::string nm = textfit::ellipsize(t, it.label, std::max(0.0, right - nx), 10.5, it.id.empty() ? font::sansMedium() : font::sans());
            t.drawText(nm, nx, textfit::baseline(cy, 10.5), 10.5, it.id.empty() ? font::sansMedium() : font::sans());
            if (!it.sub.empty())
            {
                const double sx = nx + t.measureText(nm, 10.5, it.id.empty() ? font::sansMedium() : font::sans()) + 7.0;
                if (right - sx > 12.0)
                {
                    t.setFill(fade(palette::mutedForeground(), content * 0.9));
                    t.drawText(textfit::ellipsize(t, it.sub, right - sx, 9.0, font::sans()), sx, textfit::baseline(cy, 9.0), 9.0, font::sans());
                }
            }
            const std::string mix = cmd::num(std::round(it.mix * 100.0)) + "%";
            const double mw = t.measureText(mix, 9.0, font::mono());
            const double mx = w - kPadX - (canRemove ? 22.0 : 0.0) - mw;
            t.setFill(fade(palette::mutedForeground(), content));
            t.drawText(mix, mx, textfit::baseline(cy, 9.0), 9.0, font::mono());
            if (canRemove)
            {
                const Rect rr{w - kPadX - 16.0, cy - 8.0, 16.0, 16.0};
                const Color xc = fade(lerpColor(palette::mutedForeground(), palette::foreground(), hv), a * (0.35 + 0.65 * hv));
                glyph::line(t, rr.x + 4.5, rr.y + 4.5, rr.right() - 4.5, rr.bottom() - 4.5, xc, 1.2);
                glyph::line(t, rr.right() - 4.5, rr.y + 4.5, rr.x + 4.5, rr.bottom() - 4.5, xc, 1.2);
            }
        }
        t.restore();
    }
}
}
