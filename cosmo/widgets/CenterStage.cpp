#include "CenterStage.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    CenterStage::CenterStage()
    {
        clipToBounds = true;
        mPhoto = std::make_shared<PhotoCanvas>();
        addChild(mPhoto);
        mBreadcrumb = std::make_shared<Breadcrumb>();
        addChild(mBreadcrumb);
        mFilmstrip = std::make_shared<Filmstrip>();
        addChild(mFilmstrip);
    }

    void CenterStage::layout()
    {
        const double w = width.value(), h = height.value();
        const double photoH = h - Breadcrumb::kHeight - Filmstrip::kHeight;

        mPhoto->x.set(0.0); mPhoto->y.set(0.0);
        mPhoto->width.set(w); mPhoto->height.set(std::max(0.0, photoH));
        mPhoto->layout();

        mBreadcrumb->x.set(0.0); mBreadcrumb->y.set(photoH);
        mBreadcrumb->width.set(w);

        mFilmstrip->x.set(0.0); mFilmstrip->y.set(photoH + Breadcrumb::kHeight);
        mFilmstrip->width.set(w);
    }
}
}
