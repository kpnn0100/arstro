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
 *  The strip shows ONE LEVEL of the tree at a time, cosmo's way (R-UI-12): the top, or the open
 *  group's direct members — a group is a folder chip with its count, and double-clicking it drills
 *  in. Cosmo's own `Breadcrumb` in the header names the path ("All sources › Day exteriors", plus the
 *  selected source as cosmo does) and a click on a crumb goes back up. A level change is eased:
 *  the strip fades out, swaps its cells, and fades in sliding from the side it came from; the
 *  breadcrumb cross-fades between two instances. When the Grade target moves to a node another
 *  level holds, the strip follows it there. Which level is shown is presentation, never a command.
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
#include "../../../cosmo/widgets/Breadcrumb.h"
#include "NodeGraph.h"
#include <algorithm>
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
        /** Drill the strip into group `rackObj` ("" = the top). Records intent; advance eases it. */
        void openGroup(const std::string &rackObj);
        /** The level being shown (its group's #rackobj id, "" = top) and the one asked for. */
        const std::string &shownLevel() const { return mLevel; }
        const std::string &wantedLevel() const { return mLevelWanted; }
        double levelAmount() const { return mLevelFade.value(); }
        /** Strip cell ↔ rack index at the shown level (-1 when not there). */
        int rackIndexOfCell(int cell) const { return cell >= 0 && cell < (int)mCellRack.size() ? mCellRack[(size_t)cell] : -1; }
        int cellOfRack(int rackIndex) const;
        /** The breadcrumb now in front, and the path it was given. */
        std::shared_ptr<cosmo_v2::Breadcrumb> breadcrumb() { return mCrumb[mCrumbFront]; }
        const std::vector<std::string> &crumbPath() const { return mCrumbPath; }
        /** The strip drilled into a group by itself (a double-click): the tree opens it too. */
        std::function<void(const std::string &rackObj)> onNavigate;
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
        // ── R-CLR-4: the stills gallery, in place of the sources ──
        /** The deck's three views: 0 the sources, 1 the stills (R-CLR-4), 2 the node graph (R-CLR-3).
         *  Intent only — they cross-fade, the header's underline travels. */
        void setView(int v) { mViewWanted = std::clamp(v, 0, 2); }
        int view() const { return mViewWanted; }
        double viewAmount(int v) const { return v >= 0 && v < 3 ? mViewAmt[v].value() : 0.0; }
        artboard::Rect viewTabRect(int v) const { return v >= 0 && v < 3 ? mViewTab[v] : artboard::Rect{0, 0, 0, 0}; }
        void showStills(bool on) { setView(on ? 1 : 0); }
        bool stillsShown() const { return mViewWanted == 1; }
        double stillsAmount() const { return mViewAmt[1].value(); }
        artboard::Rect stillsChipRect() const { return mViewTab[1]; }
        std::shared_ptr<NodeGraph> nodeGraph() { return mGraph; }
        std::shared_ptr<cosmo_v2::Filmstrip> stillsStrip() { return mStills; }
        std::string stillOfCell(int cell) const { return cell >= 0 && cell < (int)mStillList.size() ? mStillList[(size_t)cell].id : std::string(); }
        std::function<bool(const std::string &stillId, interstellar::Raster &out)> stillPicture;
        std::function<void(const std::string &stillId, artboard::Point world)> onStillContext;
        std::function<void(const std::string &stillId)> onStillActivate;


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
        void rebuildCells();                          // the shown level's cells, selection and path
        void bindFrame(const interstellar::AppModel &m);   // the selected source's reference frame

        std::string mLevel, mLevelWanted;             // group #rackobj ids; "" = the top
        int mLevelIdx = -1;                           // the shown group's rack index this bind
        std::vector<int> mCellRack;                   // cell → rack index
        bool mLevelInit = false, mLevelSwapping = false;
        double mLevelDir = 1.0;                       // +1 drilled in (slides from the right), -1 up
        artboard::AnimatedProperty mLevelFade{1.0};
        std::string mSelKeyLast;                      // the Grade target the strip last followed
        std::shared_ptr<cosmo_v2::Breadcrumb> mCrumb[2];
        std::vector<std::string> mCrumbChain[2];      // each crumb's level ids: [0] = "" (the top)
        int mCrumbFront = 0;
        std::vector<std::string> mCrumbPath, mCrumbPathShown, mCrumbChainNext;
        bool mCrumbInit = false;
        mutable double mHeaderRight = 84.0;           // where "SOURCES n" ends — measured in paint

        std::shared_ptr<cosmo_v2::Filmstrip> mStrip;
        std::shared_ptr<cosmo_v2::Filmstrip> mStills;   // R-CLR-4
        std::vector<interstellar::StillModel> mStillList;
        std::string mStillsKey;
        std::shared_ptr<NodeGraph> mGraph;              // R-CLR-3
        int mViewWanted = 0, mViewApplied = 0;
        artboard::AnimatedProperty mViewAmt[3] = {artboard::AnimatedProperty{1.0}, artboard::AnimatedProperty{0.0}, artboard::AnimatedProperty{0.0}};
        artboard::AnimatedProperty mTabX{0.0}, mTabW{0.0};
        double mTabTX = -1.0, mTabTW = -1.0;          // where the underline is going
        bool mTabPlaced = false;
        mutable artboard::Rect mViewTab[3];
        cosmo_v2::HoverFade mTabHover;
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
