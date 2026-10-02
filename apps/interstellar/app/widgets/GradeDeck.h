/*
 *  interstellar_v1 — GradeDeck: the Grade tab's deck — a filmstrip of rack sources, and the
 *  REFERENCE-FRAME selector under the selected video source (ui-brief §3, R-RACK-3).
 *
 *  The filmstrip is cosmo's own `Filmstrip` (R-UI-5): one cell per rack node in tree order — group
 *  chips with their child count, source cells with a thumbnail, cosmo's spinner cell while a
 *  source is still decoding, its sliding selection ring — so selecting in the deck and in the rack
 *  tree are the same act (`rack select <bind>`). An OFFLINE source gets a destructive "offline"
 *  chip over its cell, because cosmo's strip has no word for missing media and a dark plate alone
 *  would read as "still loading".
 *
 *  Under it, the frame selector — "which frame Cosmo grades" a video source on (R-RACK-3, amended
 *  2026-10-02). It is a fast-seek SLIDER over the WHOLE source (`rack[].mediaDuration`), whose track
 *  is a strip of frames (thumbnails through the optional hook, plates without it) and whose thumb is
 *  the accent marker at the reference frame. Dragging follows the pointer (direct manipulation) and
 *  PREVIEWS: `onPreview(bind, t)` per frame crossed, which the app shows graded in the monitor —
 *  nothing is committed. Release sends `rack frame <bind> --at <t>` and ends the preview. ‹ › beside
 *  the timecode step ONE frame at the source's own rate (`rack[].mediaFps`) and commit at once —
 *  the exact frame. Changing it alters no parameter. A model change of the frame eases the marker
 *  there (220 ms). When the deck is too short for a strip it degrades to a slim scrub track. For a
 *  still, a group, or nothing selected, the selector fades out and a sentence says why.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "ImageSlot.h"
#include "../../../cosmo/widgets/Filmstrip.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class GradeDeck : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 29.25;
        static constexpr double kBandLineH = 22.75;   // u(7): the label line over the strip
        static constexpr double kMaxStripH = 64.0;
        static constexpr double kMinStripH = 26.0;

        GradeDeck();

        void bind(const interstellar::AppModel &m);
        /** The host has new stills: the next bind re-asks for the cells and strip frames it is
         *  missing, and the late ones fade in. */
        void thumbnailsArrived() { mStructureKey.clear(); mFramesRetry = true; }
        void layout();

        std::shared_ptr<cosmo_v2::Filmstrip> filmstrip() { return mStrip; }
        /** The scrub surface: the frame strip, or the slim track when there is no room for one. */
        artboard::Rect frameTrackRect() const;
        bool hasFrameStrip() const;
        double frameToX(double t) const;
        double shownFrame() const { return mShownFrame.value(); }
        double selectorAmount() const { return mSelectorAmt.value(); }
        double sourceDuration() const { return mSourceDur; }
        double sourceFps() const { return mSrcFps; }
        bool previewing() const { return mDragging; }
        /** The frame-step buttons beside the timecode (dir -1 = back, +1 = forward), local. Known
         *  after the first paint (they sit after measured text). */
        artboard::Rect stepRect(int dir) const { return mStepRect[dir < 0 ? 0 : 1]; }

        std::function<void(const std::string &line)> onCommand;
        /** The slider is being dragged: show `bind` at source time `t` (t < 0 = the drag ended —
         *  back to the committed reference frame). Presentation only; nothing is dispatched. */
        std::function<void(const std::string &bind, double t)> onPreview;
        /** Right-click on filmstrip cell `rackIndex` at a WORLD point. */
        std::function<void(int rackIndex, artboard::Point local)> onContext;
        std::function<bool(const std::string &, double, int, interstellar::Raster &)> thumbnail;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double bandTop() const { return kHeaderH + cosmo_v2::Filmstrip::kHeight + 4.0; }
        void emit(const std::string &line) { if (onCommand) onCommand(line); }
        void refreshFrames();

        std::shared_ptr<cosmo_v2::Filmstrip> mStrip;
        std::vector<interstellar::RackNodeModel> mRack;
        std::string mStructureKey;
        int mSelected = -1;
        double mFps = 24.0;
        double mSrcFps = 24.0;                       // the selected source's own rate: one step
        double snapToFrame(double t) const;
        void step(int dir);
        void preview(double t);
        double mSourceDur = 1.0;
        bool mSelVideo = false;
        std::string mReason, mSelMedia, mSelName;
        // the frame strip's thumbnails, keyed by (media, cell count, length)
        std::vector<ImageSlot> mFrames;
        std::vector<std::unique_ptr<artboard::AnimatedProperty>> mFrameAlpha;   // per strip frame
        std::vector<bool> mFrameFade;
        bool mFramesRetry = false;
        std::string mFramesKey, mFramesMedia;
        bool mStripFadePending = false;
        artboard::AnimatedProperty mStripFade{1.0};   // a different source's frames fade in
        double mFrameTarget = 0.0, mFrameLast = -1.0;
        artboard::AnimatedProperty mShownFrame{0.0};
        bool mSelectorWanted = false, mSelectorApplied = false, mSelectorInit = false;
        artboard::AnimatedProperty mSelectorAmt{0.0};
        bool mDragging = false;
        artboard::AnimatedProperty mTrackHover{0.0};
        bool mTrackHovered = false, mTrackHoverApplied = false;
        cosmo_v2::HoverFade mStepHover;                 // ‹ = 0, › = 1
        mutable artboard::Rect mStepRect[2];
    };
}
}
