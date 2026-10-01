/*
 *  interstellar_v1 — ClipInspector: the Cut tab's right column — the selected clip, in numbers.
 *
 *  Cuts only, no colour (R-TL-2: a clip carries no colour of any spelling), on cosmo's card
 *  surface with cosmo's section-header rows: what the clip is (name, source, track, and its
 *  PROVENANCE as a badge in the same vocabulary the timeline draws — inherited, overridden,
 *  dangling-with-its-reason), then its placement in mono (at, in, out, duration, speed), then its
 *  mix (opacity, gain). Two actions, cosmo `PillButton`s: Split at playhead
 *  (`clip split <clip> --at <t>`) and Delete (`clip delete <clip>`).
 *
 *  With nothing selected it says so, in words. A selection change cross-fades the values rather
 *  than re-seating them in one frame.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "../../../cosmo/widgets/PillButton.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class ClipInspector : public artboard::Segment
    {
    public:
        ClipInspector();
        void bind(const interstellar::AppModel &m);
        void layout();

        bool hasClip() const { return mHas; }
        std::shared_ptr<cosmo_v2::PillButton> splitButton() { return mSplit; }
        std::shared_ptr<cosmo_v2::PillButton> deleteButton() { return mDelete; }
        double contentAmount() const { return mContent.value(); }

        std::function<void(const std::string &line)> onCommand;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        interstellar::ClipModel mClip;
        std::string mTrackName;
        bool mHas = false, mHasApplied = false, mInit = false;
        std::string mLastId;
        bool mSwapPending = false;
        double mFps = 24.0, mPlayhead = 0.0;
        artboard::AnimatedProperty mContent{0.0}, mSwap{1.0};
        std::shared_ptr<cosmo_v2::PillButton> mSplit, mDelete;
    };
}
}
