/*
 *  cosmo_v2 by arstro — PresetTree: the left-rail preset browser. Artboard has
 *  no tree control (see the Artboard capability survey), so this is composed
 *  from primitives directly, following the same "flatten visible rows" shape
 *  as cosmo's own PresetPanel/HistoryView but with new geometry/paint code
 *  matching the Figma spec (App.tsx's PresetTree function): a chevron +
 *  medium-weight 65%-opacity label for a folder row, an indented muted label
 *  for a leaf, primary text + a 10%-opacity primary fill across the row when
 *  selected. Single-click toggles a folder; double-click applies a preset
 *  (matches the design brief: "double-click applies one and highlights it").
 *
 *  Pixel conversion (13px root -> 0.25rem = 3.25px per spacing unit):
 *    row height ~20px (approximated: no text-measurement HAL primitive to
 *    derive an exact intrinsic height from py-[3px] + line-height)
 *    px-2 = 6.5px   pl-6 = 19.5px   w-3 (chevron slot) = 9.75px
 *    gap-1.5 = 4.875px
 *  Real preset folders can nest deeper than the Figma mock's 2 levels; depth
 *  >= 2 extrapolates the same per-level step (13px, pl-6 minus px-2).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../core/PresetLibrary.h"
#include "HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class PresetTree : public artboard::Segment
    {
    public:
        PresetTree();

        /** Rebuild rows from a freshly scanned tree, preserving which folders
         *  were expanded (by relPath) across the rebuild. */
        void setRoots(std::vector<cosmo::PresetNode> roots);
        void setSelected(const std::string &relPath) { mSelected = relPath; }
        void scrollBy(double delta);

        void advance(double nowMs) override;  // eases the scroll offset + hover/selection fades

        std::function<void(std::string)> onApply;                          // double-click a preset (relPath)
        std::function<void(std::string, double, double)> onContext;        // right-click a preset (relPath, x, y)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Row
        {
            int depth = 0;
            bool folder = false;
            std::string name, relPath;
            bool expanded = false;
        };
        static constexpr double kRowH = 20.0;

        void rebuildRows();
        int rowIndexAt(double localY) const;
        double indentFor(int depth) const;

        std::vector<cosmo::PresetNode> mRoots;
        std::vector<std::string> mExpanded;   // relPaths of expanded folders
        std::vector<Row> mRows;               // flattened, visible-only
        std::string mSelected;

        // Scroll offset eases toward mScrollTarget instead of jumping (R-G-1).
        // mScroll.value() is the live offset read while drawing/hit-testing.
        artboard::AnimatedProperty mScroll{0.0};
        double mScrollTarget = 0.0;
        bool mScrollDirty = false;

        // Row hover (self-drawn multi-region): each row cross-fades independently
        // via HoverFade (fade-out old, fade-in new on Move) — R-G-3.
        HoverFade mHover;

        // Selection-fill fade-in: mSelAmt eases 0->1 when mSelected changes.
        std::string mSelPrev;
        artboard::AnimatedProperty mSelAmt{0.0};
    };
}
}
