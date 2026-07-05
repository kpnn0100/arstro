#include "PresetTree.h"
#include "Icons.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

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
    }

    int PresetTree::rowIndexAt(double localY) const
    {
        const int idx = (int)((localY + mScroll) / kRowH);
        return (idx >= 0 && idx < (int)mRows.size()) ? idx : -1;
    }

    void PresetTree::scrollBy(double delta)
    {
        const double contentH = (double)mRows.size() * kRowH;
        const double viewH = height.value();
        const double maxScroll = std::max(0.0, contentH - viewH);
        mScroll = std::min(maxScroll, std::max(0.0, mScroll - delta));
    }

    bool PresetTree::handleGesture(const Gesture &g, const Point &local)
    {
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
        for (size_t i = 0; i < mRows.size(); ++i)
        {
            const Row &r = mRows[i];
            const double y = (double)i * kRowH - mScroll;
            if (y + kRowH < 0 || y > height.value()) continue;  // cheap offscreen skip

            const bool selected = !r.folder && r.relPath == mSelected;
            if (selected)
                drawRoundedRect(t, Rect{0, y, w, kRowH}, 0.0,
                                Paint::filled(Color{palette::primary().r, palette::primary().g, palette::primary().b, 0.10}));

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
            t.drawText(r.name, labelX, baseline, 11.0, fam);
        }
    }
}
}
