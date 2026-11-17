#include "PresetTree.h"
#include "TextFit.h"
#include "Icons.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kScrollMs = 180.0; constexpr double kSelMs = 140.0; }

    PresetTree::PresetTree() { clipToBounds = true; }

    void PresetTree::setRoots(std::vector<cosmo::PresetNode> roots)
    {
        mRoots = std::move(roots);
        rebuildRows();
    }

    double PresetTree::indentFor(int depth) const
    {
        if (depth <= 0) return 6.5;               // px-2
        return 19.5 + (depth - 1) * 13.0;          // pl-6, +13px per extra level beyond Figma's spec
    }

    void PresetTree::rebuildRows()
    {
        mRows.clear();
        std::function<void(const std::vector<cosmo::PresetNode> &, int)> walk =
            [&](const std::vector<cosmo::PresetNode> &nodes, int depth) {
            for (const auto &n : nodes)
            {
                const bool expanded = std::find(mExpanded.begin(), mExpanded.end(), n.relPath) != mExpanded.end();
                mRows.push_back({depth, n.folder, n.name, n.relPath, expanded});
                if (n.folder && expanded) walk(n.kids, depth + 1);
            }
        };
        walk(mRoots, 0);

        // A collapse can shrink the content below the current target — re-clamp so
        // the scroll never eases into empty space past the end.
        const double maxScroll = std::max(0.0, (double)mRows.size() * kRowH - height.value());
        if (mScrollTarget > maxScroll) { mScrollTarget = maxScroll; mScrollDirty = true; }
    }

    int PresetTree::rowIndexAt(double localY) const
    {
        const int idx = (int)((localY + mScroll.value()) / kRowH);
        return (idx >= 0 && idx < (int)mRows.size()) ? idx : -1;
    }

    void PresetTree::scrollBy(double delta)
    {
        const double contentH = (double)mRows.size() * kRowH;
        const double viewH = height.value();
        const double maxScroll = std::max(0.0, contentH - viewH);
        // Move the TARGET; advance() eases mScroll toward it so wheel scrolls glide
        // instead of jumping (R-G-1). Accumulating on the target lets fast repeats stack.
        mScrollTarget = std::min(maxScroll, std::max(0.0, mScrollTarget - delta));
        mScrollDirty = true;
    }

    void PresetTree::advance(double nowMs)
    {
        if (mScrollDirty) { mScroll.animateTo(mScrollTarget, kScrollMs, Easing::EaseOutCubic, nowMs); mScrollDirty = false; }
        mScroll.update(nowMs);

        // Row hover: drop it when this widget no longer owns hover; each row
        // cross-fades independently (R-G-3).
        if (!isHovered()) mHover.clear();
        mHover.advance(nowMs);

        // Fade the selection fill in when the selected preset changes.
        if (mSelected != mSelPrev)
        {
            mSelPrev = mSelected;
            mSelAmt.set(0.0);
            mSelAmt.animateTo(1.0, kSelMs, Easing::EaseOutCubic, nowMs);
        }
        mSelAmt.update(nowMs);

        // R-UX-7: the empty-state sentence fades with the list's emptiness. The first frame
        // SETS it (there is nothing to travel from), every later change eases.
        const bool empty = mRows.empty();
        if (!mEmptyInit) { mEmptyInit = true; mEmptyShown = empty; mEmptyAmt.set(empty ? 1.0 : 0.0); }
        else if (empty != mEmptyShown)
        {
            mEmptyShown = empty;
            mEmptyAmt.animateTo(empty ? 1.0 : 0.0, 180.0, Easing::EaseOutCubic, nowMs);
        }
        mEmptyAmt.update(nowMs);

        Segment::advance(nowMs);
    }

    bool PresetTree::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Move) { mHover.setHovered(rowIndexAt(local.y)); return true; }  // track hovered row

        const int row = rowIndexAt(local.y);
        if (row < 0) return Segment::handleGesture(g, local);
        const Row &r = mRows[row];

        if (g.type == Gesture::Type::Click)
        {
            if (r.folder)
            {
                auto it = std::find(mExpanded.begin(), mExpanded.end(), r.relPath);
                if (it != mExpanded.end()) mExpanded.erase(it);
                else mExpanded.push_back(r.relPath);
                rebuildRows();
            }
            return true;
        }
        if (g.type == Gesture::Type::DoubleClick)
        {
            if (!r.folder)
            {
                mSelected = r.relPath;
                if (onApply) onApply(r.relPath);
            }
            return true;
        }
        if (g.type == Gesture::Type::RightClick)
        {
            if (!r.folder && onContext) onContext(r.relPath, g.pos.x, g.pos.y);
            return true;
        }
        return Segment::handleGesture(g, local);
    }

    void PresetTree::onPaint(IRenderTarget &t) const
    {
        const double w = width.value();
        const double scroll = mScroll.value();
        const double sel = mSelAmt.value();
        for (size_t i = 0; i < mRows.size(); ++i)
        {
            const Row &r = mRows[i];
            const double y = (double)i * kRowH - scroll;
            if (y + kRowH < 0 || y > height.value()) continue;  // cheap offscreen skip

            const double hv = mHover.amount((int)i);  // per-row hover wash cross-fades (R-G-3)
            if (hv > 0.001)
                drawRoundedRect(t, Rect{0, y, w, kRowH}, 0.0, Paint::filled(palette::whiteAlpha(0.06 * hv)));

            const bool selected = !r.folder && r.relPath == mSelected;
            if (selected)
                drawRoundedRect(t, Rect{0, y, w, kRowH}, 0.0,
                                Paint::filled(Color{palette::primary().r, palette::primary().g, palette::primary().b, 0.10 * sel}));

            const double indent = indentFor(r.depth);
            const double chevronCx = indent + 9.75 * 0.5;
            const double chevronCy = y + kRowH * 0.5;
            if (r.folder)
            {
                const Rect chevronBox{chevronCx - 4.5, chevronCy - 4.5, 9.0, 9.0};
                if (r.expanded) icon::chevronDown(t, chevronBox, palette::mutedForeground(), 1.2);
                else icon::chevronRight(t, chevronBox, palette::mutedForeground(), 1.2);
            }

            const double labelX = indent + 9.75 + 4.875;
            const double baseline = y + kRowH * 0.5 + 11.0 * 0.35;
            const Color labelColor = r.folder
                ? Color{palette::foreground().r, palette::foreground().g, palette::foreground().b, 0.65}
                : (selected ? palette::primary() : palette::mutedForeground());
            const char *fam = r.folder ? font::sansMedium() : font::sans();
            t.setFill(labelColor);
            // R-UX-6: ellipsized to the rail's width less the indent and px-2, with real
            // metrics - a long preset name used to run to the rail's edge and be cut there.
            t.drawText(fitTextEnd(t, r.name, w - labelX - kPadRight, 11.0, fam), labelX, baseline, 11.0, fam);
        }

        // R-UX-7: an empty rail says so, and how to fill it - a blank column reads as "broken".
        // Eased in and out (mEmptyAmt), and faded with the rail's own width so a closing rail
        // does not cut the sentence at its edge.
        const double ea = mEmptyAmt.value() * std::clamp((w - 120.0) / 60.0, 0.0, 1.0);
        if (ea > 0.004)
        {
            const double x = 9.75, y0 = 9.75;
            t.setFill(Color{palette::foreground().r, palette::foreground().g, palette::foreground().b, 0.65 * ea});
            t.drawText("No presets yet", x, y0 + 11.0 * 0.85, 11.0, font::sansMedium());
            t.setFill(Color{palette::mutedForeground().r, palette::mutedForeground().g, palette::mutedForeground().b, ea});
            static const char *const kLines[2] = {"Save preset keeps the current", "look here, to use on any photo."};
            for (int i = 0; i < 2; ++i)
                t.drawText(fitTextEnd(t, kLines[i], w - 2 * x, 10.0, font::sans()), x, y0 + 30.0 + i * 14.0, 10.0, font::sans());
        }
    }
}
}
