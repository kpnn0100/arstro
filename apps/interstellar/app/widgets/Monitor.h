/*
 *  interstellar_v1 — Monitor: the one picture, outside the tab host (R-UI-3, ui-brief §3).
 *
 *  ONE widget, ONE frame, every tab: the monitor is a sibling of the tab pages, not a child of
 *  any of them, so a tab switch can neither move it nor reload it — "a frame that looks different
 *  in two tabs is a defect". It shows the composited frame the App fetched through
 *  `AppHooks::renderFrame`, letterboxed on the canvas surface, with a mono timecode chip and the
 *  version it is showing.
 *
 *  Image ids belong to the render target that issued them, so the monitor registers its frame with
 *  the PERSISTENT target it is drawn into and RELEASES the id it replaces — a monitor that only
 *  ever registered would leak one frame per playhead step. If it is drawn into a different target
 *  (a test's RecordingTarget, then Cairo) it re-registers there.
 *
 *  Motion. A new picture that is a CONTENT change — a grade edit landed, a version switched —
 *  cross-dissolves over 160 ms, LINEAR (a dissolve must be a linear alpha ramp). Successive frames
 *  of playback or of a scrub are NOT content changes (gotcha 10: a motion is not a dissolve), so
 *  while playing or scrubbing the new frame replaces the old directly — the video is its own
 *  animation. The three states (frame / "decoding" / "no clip at the playhead") cross-fade.
 */
#pragma once
#include "../Theme.h"
#include "../../core/Raster.h"
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class Monitor : public artboard::Segment
    {
    public:
        enum class State { Frame = 0, Loading = 1, Empty = 2 };

        Monitor();

        /** A new picture. `dissolve` = this is a content change (not a playback step). */
        void setFrame(const interstellar::Raster &r, bool dissolve);
        void setState(State s) { mState = s; }
        State state() const { return mState; }
        void setTimecode(const std::string &tc) { mTimecode = tc; }
        void setCaption(const std::string &c) { mCaption = c; }
        void setProxyEdge(int edge) { mProxyEdge = edge; }

        /** The long-edge proxy size this monitor would ask for, from its drawn size. */
        int wantedProxyEdge() const;
        /** Where the frame is letterboxed inside the monitor (local coords). */
        artboard::Rect frameRect() const;
        /** The LIVE eased amount of each state's layer (0..1). */
        double stateAmount(State s) const { return mStateAmt[(int)s].value(); }
        double dissolveAmount() const { return mDissolve.value(); }
        int imageId() const { return mCurId; }
        int frameWidth() const { return mW; }
        int frameHeight() const { return mH; }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;

    private:
        void syncImages(artboard::IRenderTarget &t) const;

        State mState = State::Empty;
        bool mStateInit = false;
        State mStateApplied = State::Empty;
        artboard::AnimatedProperty mStateAmt[3];
        std::string mTimecode, mCaption;
        int mProxyEdge = 720;

        // pixels (owned copy) and the two image ids a dissolve needs
        std::vector<uint8_t> mPixels;
        int mW = 0, mH = 0;
        mutable bool mDirty = false;
        mutable int mCurId = 0, mPrevId = 0, mPrevW = 0, mPrevH = 0;
        mutable artboard::IRenderTarget *mOwner = nullptr;
        bool mDissolveWanted = false;
        artboard::AnimatedProperty mDissolve{1.0};   // 1 = current fully shown
        double mPhaseMs = 0.0;                       // the decoding spinner's clock
    };
}
}
