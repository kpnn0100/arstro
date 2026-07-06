/*
 *  cosmo_v2 by arstro — EditStackTabs: the right column's 7-tab edit stack
 *  (Basic / Detail / Mask / Mixer / Curve / Grade / Xform) + swappable pages.
 *
 *  A cosmo_v2-local replacement for Artboard's TabView so the app-layer text
 *  metric (TextMetrics.h) can CENTRE each label — Artboard's HAL has no text
 *  measurement, so its TabView can only left-align. The active tab is filled
 *  with the SAME card surface as the panel body below it, so the highlighted
 *  tab blends seamlessly into the editing section (Figma's `bg-card` on both);
 *  idle tabs show the darker strip background. A 1.5px accent bar slides under
 *  the active tab when the selection changes.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class EditStackTabs : public artboard::Segment
    {
    public:
        EditStackTabs();

        double tabHeight = 27.0;
        std::function<void(int)> onChange;

        void addPage(const std::string &title, std::shared_ptr<artboard::Segment> page);
        int pageCount() const { return (int)mTitles.size(); }
        int selectedIndex() const { return mSelected; }
        void setSelectedIndex(int index);

        void layoutPages();  // position/size/visibility of pages (call after width/height)
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        std::vector<std::string> mTitles;
        std::vector<std::shared_ptr<artboard::Segment>> mPages;
        int mSelected = 0;
        artboard::AnimatedProperty mIndX, mIndW;  // sliding accent-bar position + width
        bool mInit = false, mPending = false;
    };
}
}
