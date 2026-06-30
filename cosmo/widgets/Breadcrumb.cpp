#include "Breadcrumb.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace { constexpr double kSize = 11.0, kChar = 6.2, kSepW = 14.0; }

    Breadcrumb::Breadcrumb(const Color &accent) : mAccent(accent) {}

    void Breadcrumb::setPath(std::vector<std::string> names) { mPath = std::move(names); }

    void Breadcrumb::onPaint(IRenderTarget &t) const
    {
        mEnds.assign(mPath.size(), 0.0);
        double x = 0.0;
        const double y = height.value() * 0.5 + kSize * 0.4;
        for (size_t i = 0; i < mPath.size(); ++i)
        {
            const bool last = i + 1 == mPath.size();
            t.setFill(last ? palette::ink() : palette::muted());
            t.drawText(mPath[i], x, y, kSize);
            x += (double)mPath[i].size() * kChar + 4.0;
            mEnds[i] = x;
            if (!last)
            {
                t.setFill(palette::faint());
                t.drawText("/", x, y, kSize);
                x += kSepW;
            }
        }
    }

    bool Breadcrumb::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type != Gesture::Type::Click) return Segment::handleGesture(g, local);
        for (size_t i = 0; i < mEnds.size(); ++i)
            if (local.x <= mEnds[i]) { if (onNavigate) onNavigate((int)i); return true; }
        return true;
    }
}
}
