/*
 *  Cosmo by arstro — GroupDeltaBar: a thin strip above the photo, shown only when
 *  the current image belongs to a group. The group sets a shared base look (edited
 *  in the normal panels); this bar adds a PER-IMAGE offset on top of it (exposure +
 *  white-balance nudge) so each shot in the group can be fine-tuned without leaving
 *  the group — the "double adjustment". It reports the offset; CosmoApp composes it.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo
{
    class GroupDeltaBar : public artboard::Segment
    {
    public:
        GroupDeltaBar(const artboard::Theme &theme, const artboard::Color &accent);

        std::function<void(double, double)> onChange;  // exposure delta (EV), temp delta (-100..100)

        void setValues(double exposure, double temp);   // no callback
        void layout(double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        artboard::Color mAccent;
        std::shared_ptr<artboard::Slider> mExp, mTemp;
        double mExpLabelX = 0, mTempLabelX = 0, mLabelY = 0;
    };
}
}
