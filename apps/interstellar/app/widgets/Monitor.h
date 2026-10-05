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
 *
 *  Zoom (R-UI-13, cosmo's R-ZOOM): Ctrl + wheel zooms about the pointer, 1×–8×, in cosmo's 1.15×
 *  notches — EASED (220 ms), and anchored: the picture point under the pointer when the notch
 *  landed stays under it, because the view centre is RE-DERIVED every frame from the live zoom
 *  rather than set once (the timeline's rule). While zoomed, a drag pans (direct manipulation),
 *  clamped so the picture always covers the frame; a double-click eases back to fit. A chip names
 *  the magnification, fading with the live zoom. The proxy the monitor asks for grows with the
 *  target zoom (the service's preview cap still bounds it, R-SET-3).
 */
#pragma once
#include "../Theme.h"
#include "../../core/Raster.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "ImageSlot.h"
#include <functional>
#include <string>
#include <vector>

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
        const std::string &caption() const { return mCaption; }
        void setProxyEdge(int edge) { mProxyEdge = edge; }
        /** Grade has no transport (R-UI-3), so the capture button sits on the caption there
         *  (R-UI-11). Intent only — it fades. */
        void setCaptureShown(bool on) { mCaptureWanted = on; }
        double captureAmount() const { return mCaptureAmt.value(); }
        /** Where the caption's capture button was last painted (local); empty when hidden. */
        artboard::Rect captureRect() const { return mCaptureRect; }
        std::function<void(artboard::Rect world)> onCapture;

        /** The long-edge proxy size this monitor would ask for, from its drawn size and its zoom. */
        int wantedProxyEdge() const;
        /** Where the frame is letterboxed inside the monitor at 1× (local coords). */
        artboard::Rect frameRect() const;
        /** Where the picture is drawn NOW — the live zoom and pan applied (local; may overhang
         *  frameRect, which clips it). */
        artboard::Rect imageRect() const;
        /** Zoom by `factor` keeping the picture point under local `at` there. Intent only. */
        void zoomAbout(double factor, artboard::Point at);
        /** Back to fit (double-click, Workspace › Reset Workspace). Intent only — it eases. */
        void resetZoom();
        /** The clip warning (R-UI-15): `mask` is clipMaskOf(the frame shown); the overlay fades in and
         *  out with `setClipWarning`. Presentation only. */
        void setClipMask(const interstellar::Raster &mask) { mClipMask.set(mask); }
        void setClipWarning(bool on) { mClipWanted = on; }
        double clipAmount() const { return mClipAmt.value(); }
        double zoomLive() const { return mZoom.value(); }
        /** R-EDT-5: a multicam clip's angles as chips along the picture's foot; the active one is
         *  marked by a highlight that TRAVELS to it (220 ms) when the angle changes, and the bar fades
         *  with `shown`. A click asks for that angle (1-based). Intent only. */
        void setAngles(const std::vector<std::string> &names, int active, bool shown);
        std::function<void(int angle)> onAngle;
        /** Where angle `k` (1-based) was last painted (local); empty when hidden. */
        artboard::Rect angleRect(int k) const;
        double anglesAmount() const { return mAnglesAmt.value(); }
        /** The LIVE eased position of the highlight, in chips (0 = the first). */
        double angleHighlight() const { return mAngleSel.value(); }
        double zoomTarget() const { return mZoomTarget; }
        /** R-CLR-5: the wipe's divider over the picture (the service draws the two sides). It eases to
         *  `at` when the model moves it and follows the pointer exactly while dragged; `onWipe` asks
         *  for each new split. Intent only — it fades with `on`. */
        void setWipe(bool on, bool vertical, double at, const std::string &label);
        std::function<void(double at)> onWipe;
        double wipeAmount() const { return mWipeAmt.value(); }
        double wipeLive() const { return mWipeLive.value(); }
        bool wipeDragging() const { return mWipeDrag; }
        /** The divider's grip (local); empty when hidden. */
        artboard::Rect wipeGripRect() const;
        /** R-DLV-1: the caption under the playhead, over the picture's foot, as a render burns it (a
         *  plate to a line, sized to the picture). A change CROSS-FADES — the leaving words out while the
         *  new ones come in; empty fades it away. Lifted above the angle bar while that shows. */
        void setSubtitle(const std::string &text) { mSubWanted = text; }
        double subtitleAmount() const { return mSubAmt.value(); }
        double subtitleLeavingAmount() const { return mSubLeaveAmt.value(); }
        const std::string &subtitleText() const { return mSubShown; }
        bool panning() const { return mPanning; }
        static constexpr double kMaxZoom = 8.0;
        static constexpr double kZoomNotch = 1.15;   // cosmo's
        /** The LIVE eased amount of each state's layer (0..1). */
        double stateAmount(State s) const { return mStateAmt[(int)s].value(); }
        double dissolveAmount() const { return mDissolve.value(); }
        int imageId() const { return mCurId; }
        int frameWidth() const { return mW; }
        int frameHeight() const { return mH; }

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool hitTestSelf(const artboard::Point &p) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

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
        bool mCaptureWanted = false, mCaptureApplied = false, mCaptureInit = false;
        artboard::AnimatedProperty mCaptureAmt{0.0};
        cosmo_v2::HoverFade mCaptureHover;
        mutable artboard::Rect mCaptureRect{0, 0, 0, 0};
        // R-CLR-5: the wipe divider
        bool mWipeWanted = false, mWipeApplied = false, mWipeInit = false, mWipeVertical = true, mWipeDrag = false;
        double mWipeTarget = 0.5, mWipeLast = -1.0;
        std::string mWipeLabel;
        artboard::AnimatedProperty mWipeAmt{0.0}, mWipeLive{0.5};
        double wipeAtPoint(const artboard::Point &local) const;
        bool nearWipe(const artboard::Point &local) const;
        // R-EDT-5: the angle bar
        std::vector<std::string> mAngleNames;
        int mAngleActive = 0;
        bool mAnglesWanted = false, mAnglesApplied = false, mAnglesInit = false;
        artboard::AnimatedProperty mAnglesAmt{0.0}, mAngleSel{0.0};
        std::string mSubWanted, mSubShown, mSubLeaving;             // R-DLV-1
        artboard::AnimatedProperty mSubAmt{0.0}, mSubLeaveAmt{0.0};
        bool mSubInit = false;
        int mAngleSelApplied = -1;
        bool mAngleSnap = true;                       // a new set of angles places the highlight, it does not travel
        cosmo_v2::HoverFade mAngleHover;
        mutable std::vector<artboard::Rect> mAngleRects;
        int angleAt(const artboard::Point &local) const;   // 1-based, 0 = none
        ImageSlot mClipMask;
        bool mClipWanted = false, mClipApplied = false;
        artboard::AnimatedProperty mClipAmt{0.0};
        // zoom + pan: the centre is the picture point (0..1) at the frame's centre
        artboard::Point centreFor(double zoom) const;
        double mZoomTarget = 1.0, mZoomLast = 1.0;
        artboard::AnimatedProperty mZoom{1.0};
        bool mAnchored = false;                       // the centre follows the live zoom about an anchor
        artboard::Point mAnchorAt{0, 0}, mAnchorU{0.5, 0.5};
        artboard::Point mCentre{0.5, 0.5};
        bool mPanning = false;
        artboard::Point mPanLast{0, 0};
    };
}
}
