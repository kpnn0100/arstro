/*
 *  cosmo_v2 by arstro — CenterStage: photo canvas + breadcrumb + filmstrip
 *  stacked vertically (App.tsx: `flex flex-col flex-1 min-w-0 overflow-hidden`).
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "PhotoCanvas.h"
#include "Breadcrumb.h"
#include "Filmstrip.h"
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    class CenterStage : public artboard::Segment
    {
    public:
        CenterStage();

        std::shared_ptr<PhotoCanvas> photo() { return mPhoto; }
        std::shared_ptr<Breadcrumb> breadcrumb() { return mBreadcrumb; }
        std::shared_ptr<Filmstrip> filmstrip() { return mFilmstrip; }

        void layout();  // call after width/height changes

    private:
        std::shared_ptr<PhotoCanvas> mPhoto;
        std::shared_ptr<Breadcrumb> mBreadcrumb;
        std::shared_ptr<Filmstrip> mFilmstrip;
    };
}
}
