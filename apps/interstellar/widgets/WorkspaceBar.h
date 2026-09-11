/*
 *  interstellar/widgets — WorkspaceBar: the shell chrome and the four-workspace switcher (R-UI-1).
 *
 *  The highlight TRAVELS between segments over 220 ms — cosmo's segmented-control duration — and
 *  it is a single eased value, so a test can read it mid-travel and tell a tween from a snap.
 *  The wordmark obeys R-G-2a: letter-spacing -0.03 * size, the accent dot at the MEASURED end of
 *  the word (not an estimated one — an estimate detached cosmo's dot the moment the typeface
 *  changed).
 */
#pragma once
#include "../Theme.h"
#include "../core/service/AppModel.h"
#include <functional>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class WorkspaceBar : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 29.25;   // cosmo's TopBar height — one shell rhythm

        WorkspaceBar();

        void setWorkspace(interstellar::Workspace w);
        void setProjectName(const std::string &n) { mProject = n; }
        void setDirty(bool d) { mDirty = d; }

        /** The live eased highlight position, in SEGMENT INDEX units (0..3). A test asserts this
         *  differs from the target mid-travel, which is the only assertion that can tell an eased
         *  implementation from a snapping one. */
        double highlightIndex() const { return mHighlight.value(); }
        artboard::Rect segmentRect(int index) const;

        std::function<void(interstellar::Workspace)> onWorkspace;

        void layout();
        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        static constexpr int kCount = 4;
        static const char *label(int i);

        interstellar::Workspace mWorkspace = interstellar::Workspace::Cut;
        int mTarget = 1;
        artboard::Property mHighlight{1.0};
        std::string mProject;
        bool mDirty = false;
        double mLastMs = 0;
        mutable double mSegW = 56.0;   // measured in paint, read by segmentRect
    };
}
}
