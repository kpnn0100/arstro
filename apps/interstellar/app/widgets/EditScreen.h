/*
 *  interstellar_v1 — EditScreen: Screen::Edit — three tabs over ONE monitor (R-UI-3, ui-brief §3).
 *
 *      ┌ EditTopBar 29.25 ───────────────────────────────────────────────────────────┐
 *      │ left column  │            MONITOR (outside the tab host)      │ right column │
 *      │ (per tab)    ├────────────────────────────────────────────────┤ (per tab)    │
 *      │              │  Transport                                     │              │
 *      ├──────────────┴────────────────────────────────────────────────┴──────────────┤
 *      │ deck (per tab: filmstrip · timeline · render queue)                          │
 *      └──────────────────────────────────────────────────────────────────────────────┘
 *
 *  The monitor and the transport are children of THIS screen, siblings of the three tab pages —
 *  not children of any page — so a tab switch can neither move nor reload them. Every tab uses
 *  the same column widths and the same deck height (shell:: tokens), which is what "the monitor
 *  does not move" means in pixels. The pages are FadePages: a tab switch cross-fades them
 *  (200 ms) while the switcher's highlight travels (220 ms); nothing is rebuilt.
 *
 *  Layout is hand-written (cosmo's convention, design rule §4): a non-virtual `layout()` that is a
 *  pure function of the window size, called every frame, fixed columns first and the monitor (the
 *  flexible one) last with max(0, remainder). The window minimum is derived, not guessed:
 *  `minWidth()` = both columns + the monitor's floor.
 *
 *  Also here: the refused-command toast (the error/refused state — a line the service rejected is
 *  said, in destructive, over the monitor, and fades), cosmo's ConfirmDialog ("save changes?")
 *  and the NamePrompt ("New version…"), last in child order so they draw and hit-test on top.
 */
#pragma once
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../Theme.h"
#include "../AppHooks.h"
#include "EditTopBar.h"
#include "FadePage.h"
#include "Monitor.h"
#include "Transport.h"
#include "RackTree.h"
#include "GradeInspector.h"
#include "GradeDeck.h"
#include "SourceBin.h"
#include "Timeline.h"
#include "ClipInspector.h"
#include "ChecksPanel.h"
#include "OutputSpec.h"
#include "RenderQueue.h"
#include "NamePrompt.h"
#include "../../../cosmo/widgets/ConfirmDialog.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class EditScreen : public artboard::Segment
    {
    public:
        enum Tab { Grade = 0, Cut = 1, Deliver = 2 };

        EditScreen();

        void bind(const interstellar::AppModel &m, bool interacting, double nowMs);
        void layout();

        /** Presentation state: which tab is open. Records intent; the cross-fade starts in advance. */
        void setTab(int tab);
        int tab() const { return mTab; }
        /** The LIVE eased opacity of a tab page, and the live switcher highlight position. */
        double tabFade(int tab) const { return mPages[tab]->fadeValue(); }
        double tabHighlight() const { return mTopBar->tabs()->highlightPos(); }

        void showRefusal(const std::string &message);
        /** The same chip, saying an action FINISHED (a ✓, no red) — "frame copied" (R-UI-11). */
        void showNotice(const std::string &message);
        bool toastIsError() const { return mToastError; }
        double toastAmount() const { return mToastAmt.value(); }

        static double minWidth() { return shell::leftW() + shell::rightW() + shell::minMonitorW(); }
        static double minHeight() { return shell::topBarH() + shell::minMonitorH() + shell::transportH() + 176.0; }

        // the parts, for the App's wiring and for tests
        std::shared_ptr<EditTopBar> topBar() { return mTopBar; }
        std::shared_ptr<Monitor> monitor() { return mMonitor; }
        std::shared_ptr<Transport> transport() { return mTransport; }
        std::shared_ptr<FadePage> page(int i) { return mPages[i]; }
        std::shared_ptr<RackTree> rackTree() { return mRack; }
        std::shared_ptr<GradeInspector> gradeInspector() { return mGradeInspector; }
        std::shared_ptr<GradeDeck> gradeDeck() { return mGradeDeck; }
        std::shared_ptr<SourceBin> sourceBin() { return mBin; }
        std::shared_ptr<Timeline> timeline() { return mTimeline; }
        std::shared_ptr<ClipInspector> clipInspector() { return mClipInspector; }
        std::shared_ptr<ChecksPanel> checks() { return mChecks; }
        std::shared_ptr<OutputSpec> outputSpec() { return mOutput; }
        std::shared_ptr<RenderQueue> renderQueue() { return mQueue; }
        std::shared_ptr<NamePrompt> namePrompt() { return mNamePrompt; }
        /** Cosmo's right-click menu, over everything in Edit (R-UI-9). */
        std::shared_ptr<cosmo_v2::ContextMenu> contextMenu() { return mContextMenu; }
        /** Right-click on a rack node — rack tree row or filmstrip cell — at a point in this
         *  screen's coordinates. The App fills the menu. */
        std::function<void(int rackIndex, artboard::Point at)> onRackContext;
        /** The capture button (transport or, in Grade, the monitor caption) at a WORLD rect. */
        std::function<void(artboard::Rect)> onCapture;
        /** The transport's live eased presence (0 in Grade … 1) — what a test reads. */
        double transportAmount() const { return mTransportAmt.value(); }
        std::shared_ptr<cosmo_v2::ConfirmDialog> confirm() { return mConfirm; }

        /** Every widget's intent funnels here, as a text line; the App dispatches it. */
        std::function<bool(const std::string &line)> onCommand;
        std::function<void()> onAddFootage;
        std::function<void()> onHome;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;

    private:
        bool emit(const std::string &line) { return onCommand ? onCommand(line) : false; }

        std::shared_ptr<EditTopBar> mTopBar;
        std::shared_ptr<Monitor> mMonitor;
        std::shared_ptr<Transport> mTransport;
        std::shared_ptr<FadePage> mPages[3];
        std::shared_ptr<RackTree> mRack;
        std::shared_ptr<GradeInspector> mGradeInspector;
        std::shared_ptr<GradeDeck> mGradeDeck;
        std::shared_ptr<SourceBin> mBin;
        std::shared_ptr<Timeline> mTimeline;
        std::shared_ptr<ClipInspector> mClipInspector;
        std::shared_ptr<ChecksPanel> mChecks;
        std::shared_ptr<OutputSpec> mOutput;
        std::shared_ptr<RenderQueue> mQueue;
        std::shared_ptr<NamePrompt> mNamePrompt;
        std::shared_ptr<cosmo_v2::ContextMenu> mContextMenu;
        std::shared_ptr<cosmo_v2::ConfirmDialog> mConfirm;
        int mTab = Grade;
        std::string mCurrentTimeline;

        std::string mToast;
        bool mToastError = true;
        bool mToastPending = false;
        double mToastShownAt = -1.0, mNowMs = 0.0;
        bool mToastClosing = false;
        artboard::AnimatedProperty mToastAmt{0.0};
        /** 1 in Cut and Deliver, 0 in Grade: the transport fades out and the monitor takes its room
         *  as one eased value (R-UI-3, amended). Intent from setTab, the tween from advance. */
        artboard::AnimatedProperty mTransportAmt{0.0};
        bool mTransportInit = false;
        double mTransportTarget = 0.0, mTransportApplied = 0.0;
    };
}
}
