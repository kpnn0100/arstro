/*
 *  Cosmo by arstro — Breadcrumb: the group navigation path (Root / Portraits /
 *  Beach). Each crumb is clickable to jump to that level; onNavigate(level) fires
 *  with the index in the path (0 = root).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class Breadcrumb : public artboard::Segment
    {
    public:
        explicit Breadcrumb(const artboard::Color &accent);
        void setPath(std::vector<std::string> names);
        std::function<void(int)> onNavigate;  // level index (0 = root)

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        artboard::Color mAccent;
        std::vector<std::string> mPath;
        mutable std::vector<double> mEnds;  // right-edge x of each crumb (for hit-testing)
    };
}
}
