/*
 *  interstellar_v1 — VersionSwitcher: `‹ main ▾ ›`, the version you are editing, as CHROME (R-UI-4).
 *
 *  Which timeline (= version, R-VER) is open is as present a fact as which project is, so it sits
 *  in the top bar beside the project name, one click from anywhere in Edit. ‹ and › step through
 *  the versions in tree order; the body opens a dropdown listing every version indented by its
 *  `depth`, with the version actions under it: New version…, Pin/Unpin colour, Freeze/Thaw cut,
 *  Rebase. A PINNED or FROZEN version shows a lock glyph and the commit (or "frozen"), because
 *  that is the state people forget they are in (ui-brief §3).
 *
 *  The dropdown:
 *   * opens and closes EASED (160 ms, `openAmount()` is the live value) — height reveal + fade;
 *   * is placed against the ROOT (gotcha 16): left-aligned to the chrome but clamped inside the
 *     window, and capped at the room below the bar — the version list then SCROLLS (EasedScroll,
 *     clamped both ends, bar only when there is more), while the actions stay pinned under it;
 *   * is drawn in the overlay pass so it sits over the tab content, and while it is open this
 *     segment captures every point — a click outside closes it rather than falling through.
 *
 *  Every choice leaves as a command line: `timeline open <tl>`, `timeline pin|unpin|freeze|thaw|
 *  rebase <tl>`. "New version…" asks the App for a name (NamePrompt), which sends
 *  `timeline new <name> --base <tl>`.
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar_v1
{
    class VersionSwitcher : public artboard::Segment
    {
    public:
        static constexpr double kHeight = 21.0;
        static constexpr double kArrowW = 20.0;
        static constexpr double kRowH = 24.375;     // u(7.5): a version row, a little taller than a tool row
        static constexpr double kMenuW = 280.0;
        static constexpr int kActionCount = 4;
        enum Action { NewVersion = 0, Pin = 1, Freeze = 2, Rebase = 3 };

        VersionSwitcher();

        void bind(const interstellar::AppModel &m);
        /** The window size in this segment's coordinate space (the edit root's), for placement. */
        void setRootSize(double w, double h) { mRootW = w; mRootH = h; }

        void open() { mWantOpen = true; }
        void close() { mWantOpen = false; }
        void toggle() { mWantOpen = !mWantOpen; }
        bool isOpen() const { return mWantOpen; }
        /** The LIVE eased open amount 0..1 — a test reads this to tell an eased open from a snap. */
        double openAmount() const { return mOpenAmt.value(); }
        /** The LIVE eased chrome cross-fade after a version switch (1 = settled). */
        double swapAmount() const { return mSwap.value(); }

        // Geometry a test or a shot must aim at (local coords of this segment).
        artboard::Rect prevRect() const { return artboard::Rect{0, 0, kArrowW, kHeight}; }
        artboard::Rect nextRect() const { return artboard::Rect{width.value() - kArrowW, 0, kArrowW, kHeight}; }
        artboard::Rect bodyRect() const { return artboard::Rect{kArrowW, 0, width.value() - 2 * kArrowW, kHeight}; }
        /** The whole dropdown card at full open, placed against the root. */
        artboard::Rect menuRect() const;
        artboard::Rect versionRowRect(int i) const;   // includes the live list scroll
        artboard::Rect actionRect(int i) const;
        const EasedScroll &listScroll() const { return mScroll; }

        std::function<void(const std::string &line)> onCommand;
        std::function<void()> onNewVersion;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override;

    private:
        struct Layout { double x, y, w, headerH, listTop, listH, dividerY, actionsTop, total; };
        Layout menuLayout() const;
        int currentIndex() const;
        bool actionEnabled(int a) const;
        std::string actionLabel(int a) const;
        int hoverIdAt(const artboard::Point &local) const;   // 0 prev, 1 body, 2 next, 10+a action, 100+i row
        void stepVersion(int dir);
        void emit(const std::string &line) { if (onCommand) onCommand(line); }

        void paintBody(artboard::IRenderTarget &t, const interstellar::TimelineModel *tl, double alpha) const;

        std::vector<interstellar::TimelineModel> mTimelines;
        std::string mCurrent;
        /** The chrome's previous label, kept while a version switch cross-fades the body (R1). */
        interstellar::TimelineModel mPrevShown;
        bool mHasPrev = false, mSwapPending = false;
        std::string mShownId;
        artboard::AnimatedProperty mSwap{1.0};
        double mRootW = 1440.0, mRootH = 900.0;
        bool mWantOpen = false, mAppliedOpen = false;
        artboard::AnimatedProperty mOpenAmt{0.0};
        EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
    };
}
}
