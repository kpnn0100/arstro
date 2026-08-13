/*
 *  cosmo_v2 by arstro — MixerPanel: a Hue/Sat/Lum picker above a per-channel
 *  cyclic hue CURVE editor (task point 10: "Mixer need to use curve to edit,
 *  refer from original cosmo"). This replaces the earlier 8-slider band control
 *  -- the engine already models the mixer as three continuous cyclic curves
 *  (EditParams::mixer[3], hue 0..360 -> y in [-1,1]), so a curve editor is the
 *  faithful surface, matching cosmo's own MixerPanel + HueCurveEditor.
 *
 *  The Hue/Sat/Lum picker is the animated SegmentedControl (its highlight
 *  slides between the three channels).
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "SegmentedControl.h"
#include "HueCurveEditor.h"
#include "IconButton.h"
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
        MixerPanel();

        /** Refresh all three channels' curves and show the active channel. */
        void setMixer(const std::array<std::vector<CurvePoint>, 3> &mixer);
        // The effective (group-stacked) mixer curves, drawn faint behind each editor.
        void setReference(const std::array<std::vector<CurvePoint>, 3> &refs);
        void layout();

        /** channel: 0=Hue, 1=Sat, 2=Lum; pts = bezier control points for that channel. */
        std::function<void(int channel, std::vector<CurvePoint>)> onCurveChange;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void showChannel(int channel);

        std::shared_ptr<SegmentedControl> mSubTabs;
        std::array<std::shared_ptr<HueCurveEditor>, 3> mEditors;
        std::shared_ptr<IconButton> mResetBtn;
        int mChannel = 0;
    };
}
}
