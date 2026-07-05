/*
 *  cosmo_v2 by arstro — PhotoCanvas: the photo stage (App.tsx center stage's
 *  photo area). A #0A0A0A backdrop behind Artboard's ImageView (already does
 *  exactly the `object-contain` fit the design calls for), with the
 *  before/after pill switch overlaid bottom-center
 *  (`absolute bottom-4 left-1/2 -translate-x-1/2`, 13px from the bottom).
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "SegmentedControl.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    class PhotoCanvas : public artboard::Segment
    {
    public:
        PhotoCanvas();

        std::shared_ptr<artboard::ImageView> imageView() { return mImageView; }
        bool showAfter() const { return mPill->selected() == 1; }
        void setShowAfter(bool after) { mPill->setSelected(after ? 1 : 0); }
        std::function<void(bool)> onBeforeAfterChange;  // true = "after"

        void layout();  // call after width/height changes

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        std::shared_ptr<artboard::ImageView> mImageView;
        std::shared_ptr<SegmentedControl> mPill;
    };
}
}
