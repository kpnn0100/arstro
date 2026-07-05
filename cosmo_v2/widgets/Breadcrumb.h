/*
 *  cosmo_v2 by arstro — Breadcrumb: the group navigation path strip between
 *  the photo stage and the filmstrip (App.tsx: `flex items-center gap-1 px-3
 *  py-1.5 border-t border-b border-border bg-[#161616]`). Every crumb except
 *  the last is muted and clickable (navigates up); the last is bright and
 *  inert -- matching the Figma mock, which puts the selected image's own
 *  filename as the trailing crumb (not just the group path), so the caller
 *  is expected to append it when an image (not a group) is selected.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class Breadcrumb : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 22.75;  // 10px*~1.3 line height + 2*py-1.5(4.875)

        Breadcrumb();

        void setPath(std::vector<std::string> crumbs);
        std::function<void(int)> onCrumbClick;  // index into the crumbs passed to setPath (never the last)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        struct Span { double x = 0, w = 0; };
        std::vector<Span> computeSpans() const;

        std::vector<std::string> mCrumbs;
    };
}
}
