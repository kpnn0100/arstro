/*
 *  interstellar/widgets — Monitor: the composited frame at the playhead (R-UI-2).
 *
 *  ONE widget, shown by every workspace. It sits OUTSIDE the cross-fading workspace host on
 *  purpose: a frame that looks different in two workspaces is a defect, and putting it inside
 *  the host would make it two widgets with two states.
 *
 *  Two traps, both already paid for elsewhere in this repo:
 *
 *  * **a new image every frame must be registered with the PERSISTENT target and released**, or
 *    the target grows without bound. Cosmo's render target persists across frames for exactly
 *    this reason.
 *  * **a proxy-level change is not a content change.** Two resolutions of the same picture must
 *    not cross-dissolve — that reads as a double exposure (design-rule gotcha 10, "a zoom is not
 *    a content change"). So the fade is driven by the frame SEQUENCE, not by its size.
 */
#pragma once
#include "../Theme.h"
#include "../core/Composite.h"
#include <functional>

namespace arstro
{
namespace interstellar_v1
{
    class Monitor : public artboard::Segment
    {
    public:
        Monitor();

        /** Hand over a finished frame. Cheap to call with the same sequence number — it only
         *  re-uploads when the sequence actually advanced. */
        void setFrame(const interstellar::Raster &frame, unsigned seq);
        /** No clip at the playhead is a SENTENCE, not a black rectangle: "empty because nothing
         *  is here" and "empty because it has not loaded" ask the user different things
         *  (arstro.design.rule §7). */
        void setEmptyReason(const std::string &reason) { mEmptyReason = reason; }
        void setLoading(bool on) { mLoading = on; }
        void setTimecode(const std::string &tc) { mTimecode = tc; }
        void setProxyLevel(int level, int edge) { mLevel = level; mLevelEdge = edge; }

        /** The rect the frame is drawn into — PUBLISHED so a shot and a test can aim at it
         *  rather than hard-coding where they think it is (arstro.design.rule §5). */
        artboard::Rect frameRect() const;
        /** The live eased fade of the newest frame. Exposed because a test must be able to tell
         *  a tween from a snap, and a private eased value is an unverifiable one (§1). */
        double frameFade() const { return mFade.value(); }

        void layout();
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        mutable int mImageId = 0;
        mutable int mUploadedSeq = -1;
        interstellar::Raster mFrame;
        unsigned mSeq = 0;
        double mLastMs = 0;
        unsigned mFadeFromSeq = 0;
        artboard::Property mFade{1.0};
        std::string mEmptyReason = "no clip at the playhead";
        std::string mTimecode = "00:00:00:00";
        bool mLoading = false;
        int mLevel = 0, mLevelEdge = 0;
    };
}
}
