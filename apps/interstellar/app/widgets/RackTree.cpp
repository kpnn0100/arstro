#include "RackTree.h"
#include "CommandLine.h"
#include "Glyphs.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/Icons.h"
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
        constexpr double kGlyph = 12.0;
        constexpr double kChevron = 12.0;   // the open/shut column every row reserves, so names align
        constexpr double kBypassBox = 20.0;
        constexpr double kNamePx = 11.0;
        constexpr double kMetaPx = 9.0;
        Color fade(Color c, double a) { c.a *= a; return c; }
        std::string usesLabel(int n) { return n == 0 ? "unused" : (n == 1 ? "1 clip" : std::to_string(n) + " clips"); }
        // the empty state's add chip, centred in the viewport
        Rect emptyChip(const Rect &vp) { return Rect{vp.x + (vp.w - 116.0) * 0.5, vp.y + vp.h * 0.42 + 14.0, 116.0, 24.0}; }
    }

    RackTree::RackTree() { clipToBounds = true; }

    void RackTree::bind(const interstellar::AppModel &m)
    {
        mRack = m.rack;
        mSelected = m.selectedRack;
        mFps = m.fps > 0 ? m.fps : 24.0;
        // The Grade target moved somewhere a shut group hides: open its ancestors (once per move, so
        // the user can still shut the group around the selection afterwards).
        const std::string selKey = mSelected >= 0 && mSelected < (int)m.rack.size() ? m.rack[(size_t)mSelected].rackObj : std::string();
        if (selKey != mRevealedFor)
        {
            mRevealedFor = selKey;
            for (int p = mSelected >= 0 && mSelected < (int)m.rack.size() ? m.rack[(size_t)mSelected].parent : -1;
                 p >= 0 && p < (int)m.rack.size(); p = m.rack[(size_t)p].parent)
                mOpen.insert(m.rack[(size_t)p].rackObj);
        }
        syncRows();
        for (const auto &n : m.rack)
        {
            const std::string key = n.rackObj.empty() ? n.bindName : n.rackObj;
            auto &st = mStates[key];
            st.want[0] = n.bypass; st.want[1] = n.pending; st.want[2] = n.failed; st.want[3] = n.overridden;
            st.want[4] = n.selected;
            st.want[6] = !n.group && n.sharesMedia > 0;
        }
        mSel.setHovered(mSelected);
        mBound = true;
    }

    bool RackTree::rowVisible(int i) const
    {
        if (i < 0 || i >= (int)mRack.size()) return false;
        for (int p = mRack[(size_t)i].parent; p >= 0 && p < (int)mRack.size(); p = mRack[(size_t)p].parent)
            if (!mOpen.count(mRack[(size_t)p].rackObj)) return false;
        return true;
    }

    bool RackTree::isOpen(int i) const { return i >= 0 && i < (int)mRack.size() && mOpen.count(mRack[(size_t)i].rackObj) > 0; }

    double RackTree::openAmount(int i) const
    {
        if (i < 0 || i >= (int)mRack.size()) return 0.0;
        const RowState *st = stateFor(mRack[(size_t)i]);
        return st ? st->open.value() : (isOpen(i) ? 1.0 : 0.0);
    }

    void RackTree::setOpen(const std::string &rackObj, bool open)
    {
        if (open) mOpen.insert(rackObj); else mOpen.erase(rackObj);
        syncRows();   // members fade in / out and the rows below travel — AnimatedRows eases it
    }

    /** The visible rows, in tree order, keyed by #rackobj — a shut group's members leave as ghosts. */
    void RackTree::syncRows()
    {
        std::vector<std::pair<std::string, RowItem>> items;
        items.reserve(mRack.size());
        for (int i = 0; i < (int)mRack.size(); ++i)
        {
            const auto &n = mRack[(size_t)i];
            if (n.group) mStates[n.rackObj.empty() ? n.bindName : n.rackObj].want[5] = mOpen.count(n.rackObj) > 0;
            if (rowVisible(i)) items.emplace_back(n.rackObj.empty() ? n.bindName : n.rackObj, RowItem{i, n});
        }
        mRows.sync(items, kRowH);
    }

    Rect RackTree::viewport() const { return Rect{0, kHeaderH, width.value(), std::max(0.0, height.value() - kHeaderH)}; }

    double RackTree::rowTopLocal(int i) const
    {
        for (const auto &r : mRows.rows())
            if (r.index >= 0 && r.data.rack == i) return kHeaderH + r.liveY() - mScroll.value();
        return -1e6;   // inside a shut group: not in the tree
    }

    Rect RackTree::rowRect(int i) const { return Rect{0, rowTopLocal(i), width.value(), kRowH}; }

    namespace
    {
        // Row-relative geometry from a row's TOP, so a fading ghost row (no index any more)
        // draws its controls exactly where its live self had them.
        Rect bypassAt(double w, double top) { return Rect{w - kPadX - kBypassBox, top + (RackTree::kRowH - kBypassBox) * 0.5, kBypassBox, kBypassBox}; }
    }

    Rect RackTree::bypassRect(int i) const { return bypassAt(width.value(), rowTopLocal(i)); }

    Rect RackTree::chevronRect(int i) const
    {
        if (i < 0 || i >= (int)mRack.size() || !mRack[(size_t)i].group) return Rect{0, 0, 0, 0};
        const double top = rowTopLocal(i);
        return Rect{kPadX + mRack[(size_t)i].depth * space::indent() - 2.0, top + 11.5 - 8.0, kChevron + 4.0, 16.0};
    }

    Rect RackTree::addRect() const { return Rect{width.value() - kPadX - 20.0, (kHeaderH - 20.0) * 0.5, 20.0, 20.0}; }

    const RackTree::RowState *RackTree::stateFor(const interstellar::RackNodeModel &n) const
    {
        auto it = mStates.find(n.rackObj.empty() ? n.bindName : n.rackObj);
        return it == mStates.end() ? nullptr : &it->second;
    }

    double RackTree::bypassAmount(int i) const
    {
        if (i < 0 || i >= (int)mRack.size()) return 0.0;
        const RowState *st = stateFor(mRack[i]);
        return st ? st->bypass.value() : (mRack[i].bypass ? 1.0 : 0.0);
    }


    int RackTree::rowAt(const Point &p) const
    {
        if (!viewport().contains(p)) return -1;
        for (const auto &r : mRows.rows())
            if (r.index >= 0 && rowRect(r.data.rack).contains(p)) return r.data.rack;
        return -1;
    }

    bool RackTree::handleGesture(const Gesture &g, const Point &local)
    {
        switch (g.type)
        {
        case Gesture::Type::Move:
        {
            if (addRect().contains(local)) { mHover.setHovered(0); return true; }
            if (mRack.empty() && emptyChip(viewport()).contains(local)) { mHover.setHovered(1); return true; }
            const int i = rowAt(local);
            mHover.setHovered(i >= 0 ? (mRack[(size_t)i].group && chevronRect(i).contains(local) ? 1000 + i : 10 + i) : -1);
            return true;
        }
        case Gesture::Type::RightClick:
        {
            const int i = rowAt(local);
            if (i >= 0 && onContext) onContext(i, worldTransform().apply(local));
            return true;
        }
        case Gesture::Type::Down:
            return true;
        case Gesture::Type::DoubleClick:
        {
            // a group row: open it here and in the SOURCES strip (R-UI-12); the first click selected it
            const int i = rowAt(local);
            if (i >= 0 && mRack[(size_t)i].group && !chevronRect(i).contains(local) && !bypassRect(i).contains(local))
            {
                setOpen(mRack[(size_t)i].rackObj, true);
                if (onOpenGroup) onOpenGroup(mRack[(size_t)i].rackObj);
            }
            return true;
        }
        case Gesture::Type::Scroll:
            return mScroll.scrollBy(g.delta.y);   // false = nothing to scroll: let it bubble
        case Gesture::Type::Click:
        {
            if (addRect().contains(local) || (mRack.empty() && emptyChip(viewport()).contains(local)))
            {
                if (onAddFootage) onAddFootage();
                return true;
            }
            const int i = rowAt(local);
            if (i < 0) return true;
            const auto &n = mRack[i];
            if (n.group && chevronRect(i).contains(local))
            {
                setOpen(n.rackObj, !mOpen.count(n.rackObj));   // presentation: no command
                return true;
            }
            if (bypassRect(i).contains(local))
            {
                emit("set " + cmd::quote(n.bindName + ".bypass=" + (n.bypass ? "0" : "1")));
                return true;
            }
            const auto ovr = mOvrRects.find(i);
            if (n.overridden && ovr != mOvrRects.end() && ovr->second.contains(local))
            {
                emit("revert " + cmd::quote(n.bindName));   // drop this version's overrides on the node
                return true;
            }
            // Cosmo's selection: Shift-click a range, Ctrl-click to toggle, a plain click selects one.
            if (g.shift) emit("rack select " + cmd::quote(n.bindName) + " --range");
            else if (g.ctrl) emit("rack select " + cmd::quote(n.bindName) + " --add");
            else
            {
                int selectedCount = 0;
                for (const auto &r : mRack) selectedCount += r.selected;
                if (i != mSelected || selectedCount > 1) emit("rack select " + cmd::quote(n.bindName));
            }
            return true;
        }
        default:
            break;
        }
        return Segment::handleGesture(g, local);
    }

    void RackTree::advance(double nowMs)
    {
        mPhaseMs = nowMs;
        mRows.advance(nowMs);
        mScroll.setExtent(kHeaderH, std::max(0.0, height.value() - kHeaderH), (double)mRows.count() * kRowH);
        mScroll.advance(nowMs);
        for (auto &kv : mStates)
        {
            RowState &st = kv.second;
            artboard::AnimatedProperty *props[RowState::kN] = {&st.bypass, &st.pending, &st.failed, &st.ovr, &st.selected, &st.open, &st.shared};
            for (int k = 0; k < RowState::kN; ++k)
            {
                if (!st.init) props[k]->set(st.want[k] ? 1.0 : 0.0);
                else if (st.want[k] != st.applied[k])
                    props[k]->animateTo(st.want[k] ? 1.0 : 0.0, motion::kSelectMs, Easing::EaseOutCubic, nowMs);
                st.applied[k] = st.want[k];
                props[k]->update(nowMs);
            }
            st.init = true;
        }
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);
        mSel.advance(nowMs, motion::kSelectMs);
        Segment::advance(nowMs);
    }

    void RackTree::onPaint(IRenderTarget &t) const
    {
        mOvrRects.clear();
        const double w = width.value(), h = height.value();
        drawRoundedRect(t, Rect{0, 0, w, h}, 0.0, Paint::filled(palette::leftRailBg()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(w - 0.5, 0); t.lineTo(w - 0.5, h); t.strokePath();

        // header: RACK · n, rule, add button (a section header is a ROW — gotcha 13)
        const double hy = kHeaderH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText("RACK", kPadX, textfit::baseline(hy, 9.0), 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const double labelW = t.measureText("RACK", 9.0, font::sansSemiBold(), 0.13 * 9.0);
        const std::string count = std::to_string(mRack.size());
        t.setFill(fade(palette::mutedForeground(), 0.7));
        t.drawText(count, kPadX + labelW + 6.0, textfit::baseline(hy, 9.0), 9.0, font::mono());
        const double ruleX0 = kPadX + labelW + 6.0 + t.measureText(count, 9.0, font::mono()) + 6.5;
        const Rect ar = addRect();
        if (ar.x - 6.5 > ruleX0)
        {
            t.setStroke(palette::border(), 1.0);
            t.beginPath(); t.moveTo(ruleX0, hy); t.lineTo(ar.x - 6.5, hy); t.strokePath();
        }
        {
            const double hv = mHover.amount(0);
            if (hv > 0.001) drawRoundedRect(t, ar, radius::control(), Paint::filled(palette::hoverWash(hv)));
            glyph::plus(t, Rect{ar.x + 5, ar.y + 5, 10, 10}, lerpColor(palette::mutedForeground(), palette::foreground(), 0.7 * hv));
        }

        const Rect vp = viewport();
        t.save();
        t.clipRect(vp.x, vp.y, vp.w, vp.h);
        if (mRack.empty() && mRows.rows().empty())
        {
            const std::string s = "no footage yet \xE2\x80\x94 add some";
            const double tw = t.measureText(s, 11.0, font::sans());
            t.setFill(palette::mutedForeground());
            t.drawText(s, vp.x + (vp.w - tw) * 0.5, vp.y + vp.h * 0.42, 11.0, font::sans());
            const Rect chip = emptyChip(vp);
            const double hv = mHover.amount(1);
            drawRoundedRect(t, chip, radius::control(),
                            Paint::filledStroked(palette::primaryAlpha(0.10 * hv), lerpColor(palette::border(), palette::primary(), 0.5 + 0.5 * hv), 1.0));
            const std::string cl = "Add footage\xE2\x80\xA6";
            const double cw = t.measureText(cl, 11.0, font::sans());
            t.setFill(lerpColor(palette::foreground(), palette::white(), hv));
            t.drawText(cl, chip.x + (chip.w - cw) * 0.5, textfit::baseline(chip.y + chip.h * 0.5, 11.0), 11.0, font::sans());
        }

        for (const auto &row : mRows.rows())
        {
            const auto &n = row.data.n;
            const double a = row.liveAlpha();
            if (a <= 0.001) continue;
            if (!mScroll.bandVisible(row.liveY(), kRowH)) continue;   // the same test that culls input
            const double top = kHeaderH + row.liveY() - mScroll.value();
            const int i = row.index >= 0 ? row.data.rack : -1;   // a ghost has no index any more
            const double sel = i >= 0 ? mSel.amount(i) : 0.0;
            const double hv = i >= 0 ? mHover.amount(10 + i) : 0.0;
            if (sel > 0.001)
            {
                drawRoundedRect(t, Rect{0, top, w - 1.0, kRowH}, 0.0, Paint::filled(palette::primaryAlpha(0.14 * sel * a)));
                drawRoundedRect(t, Rect{0, top + 4, 2.0, kRowH - 8}, 0.0, Paint::filled(palette::primaryAlpha(sel * a)));
            }
            if (hv > 0.001) drawRoundedRect(t, Rect{0, top, w - 1.0, kRowH}, 0.0, Paint::filled(palette::hoverWash(hv * a)));

            const RowState *st = stateFor(n);
            // In the selection (not the Grade target): a lighter wash, eased like every state.
            const double ms = st ? st->selected.value() * (1.0 - sel) : 0.0;
            if (ms > 0.001) drawRoundedRect(t, Rect{0, top, w - 1.0, kRowH}, 0.0, Paint::filled(palette::primaryAlpha(0.08 * ms * a)));
            const double by = st ? st->bypass.value() : (n.bypass ? 1.0 : 0.0);
            const double pe = st ? st->pending.value() : (n.pending ? 1.0 : 0.0);
            const double fa = st ? st->failed.value() : (n.failed ? 1.0 : 0.0);
            const double ov = st ? st->ovr.value() : (n.overridden ? 1.0 : 0.0);
            const double content = a * (1.0 - 0.55 * by);
            const double cx0 = kPadX + n.depth * space::indent();
            const double x0 = cx0 + kChevron;
            const double l1 = top + 11.5, l2 = top + 23.0;
            for (int d = 0; d < n.depth; ++d)   // depth guides, under each ancestor's folder
            {
                const double gx = kPadX + d * space::indent() + kChevron + 5.5;
                glyph::line(t, gx, top, gx, top + kRowH, fade(palette::border(), a), 1.0);
            }
            if (n.group)
            {
                // the open/shut chevron TURNS with the eased amount: › shut, ⌄ open
                const double op = st ? st->open.value() : (mOpen.count(n.rackObj) ? 1.0 : 0.0);
                const double hvc = i >= 0 ? mHover.amount(1000 + i) : 0.0;
                const double ang = op * 1.5707963267948966, ca = std::cos(ang), sa = std::sin(ang);
                const double ccx = cx0 + kChevron * 0.5 - 1.0, ccy = l1;
                auto pt = [&](double px, double py) { return Point{ccx + px * ca - py * sa, ccy + px * sa + py * ca}; };
                const Point p0 = pt(-1.75, -3.5), p1 = pt(1.75, 0.0), p2 = pt(-1.75, 3.5);
                t.setStroke(fade(lerpColor(palette::mutedForeground(), palette::foreground(), hvc), content), 1.2);
                t.beginPath(); t.moveTo(p0.x, p0.y); t.lineTo(p1.x, p1.y); t.lineTo(p2.x, p2.y); t.strokePath();
            }
            // the glyph cross-fades between its states: decoding (spinner), offline (warning), the node
            const Rect gb{x0, l1 - kGlyph * 0.5, kGlyph, kGlyph};
            const double normal = (1.0 - pe) * (1.0 - fa);
            const Color gc = sel > 0.5 ? palette::primary() : palette::mutedForeground();
            if (pe > 0.001) glyph::spinner(t, gb.x + gb.w * 0.5, gb.y + gb.h * 0.5, 4.5, mPhaseMs, content * pe);
            if (fa > 0.001) glyph::warn(t, gb, fade(palette::destructive(), content * fa));
            if (normal > 0.001)
            {
                const Color nc = fade(gc, content * normal);
                if (n.group) cosmo_v2::icon::folder(t, gb, nc);
                else if (n.video) glyph::film(t, gb, nc);
                else cosmo_v2::icon::image(t, gb, nc);
            }

            // line one: name … [OVR] — the badge's room opens with its eased amount
            const Rect br = bypassAt(w, top);
            double right = w - kPadX - kBypassBox - 6.0;
            if (ov > 0.001)
            {
                const double bw = t.measureText("OVR", 7.5, font::sansSemiBold()) + 8.0;
                const Rect badge{right - bw, l1 - 6.5, bw, 13.0};
                if (n.overridden && i >= 0) mOvrRects[i] = badge;
                drawRoundedRect(t, badge, radius::hairline(), Paint::filledStroked(palette::primaryAlpha(0.12 * content * ov), palette::primaryAlpha(0.55 * content * ov), 1.0));
                t.setFill(fade(palette::primary(), content * ov));
                t.drawText("OVR", badge.x + 4.0, textfit::baseline(l1, 7.5), 7.5, font::sansSemiBold());
                right -= (bw + 5.0) * ov;
            }
            // a variant and its original share one file (R-RACK-5): a muted SHARED badge, eased
            const double shd = st ? st->shared.value() : (n.sharesMedia > 0 ? 1.0 : 0.0);
            if (shd > 0.001)
            {
                const double bw = t.measureText("SHARED", 7.5, font::sansSemiBold()) + 8.0;
                const Rect badge{right - bw, l1 - 6.5, bw, 13.0};
                drawRoundedRect(t, badge, radius::hairline(), Paint::stroked(fade(palette::border(), 2.0 * content * shd), 1.0));
                t.setFill(fade(palette::mutedForeground(), content * shd));
                t.drawText("SHARED", badge.x + 4.0, textfit::baseline(l1, 7.5), 7.5, font::sansSemiBold());
                right -= (bw + 5.0) * shd;
            }
            const std::string label = n.cosmoName.empty() ? n.bindName : n.cosmoName;
            const char *fam = n.group ? font::sansMedium() : font::sans();
            const std::string nm = textfit::ellipsize(t, label, right - (x0 + 18.0), kNamePx, fam);
            t.setFill(fade(lerpColor(palette::foreground(), palette::destructive(), fa), content));
            t.drawText(nm, x0 + 18.0, textfit::baseline(l1, kNamePx), kNamePx, fam);

            // line two: what it spells and its uses — or the reason it has neither
            const double usesRight = w - kPadX - kBypassBox - 8.0;
            if (fa > 0.001)
            {
                const std::string s = textfit::ellipsize(t, "offline \xE2\x80\x94 media missing", usesRight - (x0 + 18.0), kMetaPx, font::sans());
                t.setFill(fade(palette::destructive(), a * fa));
                t.drawText(s, x0 + 18.0, textfit::baseline(l2, kMetaPx), kMetaPx, font::sans());
            }
            if (fa < 0.999)
            {
                const double la = content * (1.0 - fa);
                const std::string uses = n.group ? std::string() : usesLabel(n.usedBy);
                double usesW = 0.0;
                if (!uses.empty())
                {
                    usesW = t.measureText(uses, kMetaPx, font::sans());
                    t.setFill(fade(palette::mutedForeground(), la));
                    t.drawText(uses, usesRight - usesW, textfit::baseline(l2, kMetaPx), kMetaPx, font::sans());
                }
                const double room = usesRight - usesW - 8.0 - (x0 + 18.0);
                if (pe > 0.001)
                {
                    t.setFill(fade(palette::mutedForeground(), la * pe));
                    t.drawText(textfit::ellipsize(t, "decoding\xE2\x80\xA6", room, kMetaPx, font::sans()), x0 + 18.0, textfit::baseline(l2, kMetaPx), kMetaPx, font::sans());
                }
                if (pe < 0.999)
                {
                    t.setFill(fade(palette::mutedForeground(), la * (1.0 - pe)));
                    t.drawText(textfit::ellipsize(t, n.bindName, room, kMetaPx, font::mono()), x0 + 18.0, textfit::baseline(l2, kMetaPx), kMetaPx, font::mono());
                }
            }

            // bypass toggle (full strength even when the row is dimmed: it is how you undo it)
            if (hv > 0.001) drawRoundedRect(t, br, radius::control(), Paint::filled(palette::hoverWash(hv * a * (1.0 - by))));
            if (by > 0.001) drawRoundedRect(t, br, radius::control(), Paint::filled(palette::primaryAlpha(0.14 * a * by)));
            const Color idle = lerpColor(fade(palette::mutedForeground(), 0.55), palette::foreground(), 0.6 * hv);
            cosmo_v2::icon::ban(t, Rect{br.x + 5, br.y + 5, 10, 10}, fade(lerpColor(idle, palette::primary(), by), a), 1.1);
        }
        t.restore();
        mScroll.drawBar(t, w - 2.0);
    }
}
}
