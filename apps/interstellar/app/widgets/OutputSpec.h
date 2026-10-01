/*
 *  interstellar_v1 — OutputSpec: the Deliver tab's right column — what to render, as what, where.
 *
 *  R-RENDER-1 says a render NAMES its timeline and never implies "the current one", so the first
 *  thing here is a timeline picker — every version, indented by depth as in the version switcher,
 *  with its lock when pinned/frozen — defaulting to the version you are editing but never tied to
 *  it. Then the format (cosmo's `SegmentedControl`: H.264 · ProRes · PNG seq), the output path (an
 *  artboard TextBox in cosmo's field style, defaulted from the project folder, the version and the
 *  format until you edit it), and the Render button:
 *
 *      render --timeline <tl> --out <path> --format h264|prores|png-seq
 *
 *  A chosen version with dangling deltas gets a destructive note under the picker — rendering it
 *  is allowed, but not silently. The picker scrolls once there are more versions than room.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include "../../../cosmo/widgets/SegmentedControl.h"
#include "../../../cosmo/widgets/PillButton.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class OutputSpec : public artboard::Segment
    {
    public:
        static constexpr double kRowH = 24.375;
        static constexpr int kMaxVisibleRows = 6;

        OutputSpec();
        void bind(const interstellar::AppModel &m);
        void layout();

        const std::string &renderTimeline() const { return mTimeline; }
        std::string format() const;
        std::string outPath() const { return mPath->text; }
        std::string renderLine() const;
        artboard::Rect timelineRowRect(int i) const;
        std::shared_ptr<cosmo_v2::PillButton> renderButton() { return mRender; }
        std::shared_ptr<cosmo_v2::SegmentedControl> formatPicker() { return mFormat; }
        std::shared_ptr<artboard::TextBox> pathField() { return mPath; }

        std::function<void(const std::string &line)> onCommand;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        double listTop() const;
        double listH() const;
        double formatTop() const;
        std::string defaultPath() const;
        void refreshDefaultPath();

        std::vector<interstellar::TimelineModel> mTimelines;
        std::string mTimeline, mCurrent, mProjectDir;
        std::shared_ptr<cosmo_v2::SegmentedControl> mFormat;
        std::shared_ptr<artboard::TextBox> mPath;
        std::shared_ptr<cosmo_v2::PillButton> mRender;
        std::string mLastDefault;
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover, mSel;
        int mFormatIndex = 0;
        int mNoteCount = 0;                               // the chosen version's dangling deltas, last seen
        bool mNoteWanted = false, mNoteApplied = false, mNoteInit = false;
        artboard::AnimatedProperty mNoteAmt{0.0};
    };
}
}
