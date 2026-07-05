/*
 *  cosmo_v2 by arstro — MixerPanel (App.tsx MixerPanel): a Hue/Sat/Lum 3-way
 *  picker above 8 per-color-band sliders (Red..Magenta). The real engine
 *  models the mixer as a continuous cyclic curve per channel
 *  (EditParams::mixer[3], hue 0..360 -> y in [-1,1]) rather than 8 discrete
 *  sliders, so each band slider reads/writes one control point on that curve
 *  at a fixed hue (evenly spaced 45 degrees apart: Red=0, Orange=45, ...,
 *  Magenta=315) -- the same simplified 8-band control surface Lightroom-
 *  style HSL panels put over a continuous curve.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "../../ImageProcessing/src/engine/EditParams.h"
#include "SegmentedControl.h"
#include "SliderRow.h"
#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class MixerPanel : public artboard::Segment
    {
    public:
        static constexpr int kBands = 8;

        MixerPanel();

        /** Refresh all three channels' curves; re-derives the 8 displayed
         *  values for whichever sub-tab is currently active. */
        void setMixer(const std::array<std::vector<std::pair<float, float>>, 3> &mixer);
        void layout();
        void scrollBy(double delta);

        /** channel: 0=Hue, 1=Sat, 2=Lum. hueDeg: the band's fixed position. */
        std::function<void(int channel, float hueDeg, float y)> onBandChange;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void refreshBandValues();  // push mMixer[mSubChannel] samples into the 8 rows

        std::shared_ptr<SegmentedControl> mSubTabs;
        std::vector<std::shared_ptr<SliderRow>> mBandRows;
        std::array<std::vector<std::pair<float, float>>, 3> mMixer;
        int mSubChannel = 0;
        double mScroll = 0.0;
        double mContentHeight = 0.0;
        double mHeaderY = 0.0;
    };
}
}
