/*
 *  cosmo_v2 by arstro — CurvePanel (App.tsx CurvePanel): RGB/R/G/B channel
 *  picker + Reset button above a 232x164 draggable tone-curve plot. Genuinely
 *  interactive -- click-drag moves a point, click on empty curve space adds one,
 *  double-click removes one (not the endpoints).
 *
 *  Each channel has its OWN curve: the panel holds four independent point-sets
 *  (0=RGB master, 1=R, 2=G, 3=B), and the picker switches which one is shown and
 *  edited (instant swap, like MixerPanel). The RGB master maps EditParams::curve
 *  (applied to every channel); R/G/B map EditParams::curveChannel[0..2] (applied
 *  to their channel after the master). The plot is drawn in the active channel's
 *  colour. Edits emit (channel, points); a saved project restores all four.
 */
#pragma once
#include "../../Artboard/include/artboard/artboard.h"
#include "SegmentedControl.h"
#include "IconButton.h"
#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    class CurvePanel : public artboard::Segment
    {
    public:
        static constexpr double kPlotH = 164.0;  // plot width fills the panel (set in layout)
        using Points = std::vector<std::pair<float, float>>;

        CurvePanel();

        // Restore all four curves: RGB master + the R/G/B per-channel curves.
        void setCurves(const Points &master, const std::array<Points, 3> &channels);
        // Select which channel the plot shows/edits (0=RGB,1=R,2=G,3=B). The channel
        // picker drives this; also programmatically selectable.
        void showChannel(int channel);
        // Read a channel's current points (0=RGB,1=R,2=G,3=B).
        const Points &curveFor(int channel) const { return mCurves[channel]; }
        void layout();

        // (channel, points): channel 0 = RGB master, 1..3 = R/G/B.
        std::function<void(int, Points)> onCurveChange;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        artboard::Rect plotRect() const;
        int hitPoint(const artboard::Point &plotLocal) const;  // -1 if none within radius
        Points &active() { return mCurves[mChannel]; }
        const Points &active() const { return mCurves[mChannel]; }
        artboard::Color channelColor() const;  // accent for RGB, red/green/blue for R/G/B
        void emitChange();

        std::shared_ptr<SegmentedControl> mChannelPicker;
        std::shared_ptr<IconButton> mResetBtn;
        // 0 = RGB master, 1 = R, 2 = G, 3 = B; each defaults to identity.
        std::array<Points, 4> mCurves{{{{0, 0}, {1, 1}}, {{0, 0}, {1, 1}}, {{0, 0}, {1, 1}}, {{0, 0}, {1, 1}}}};
        int mChannel = 0;
        int mDragIndex = -1;
        double mPlotY = 0.0;
        double mPlotW = 232.0;  // set each layout() to the panel's inner width
    };
}
}
