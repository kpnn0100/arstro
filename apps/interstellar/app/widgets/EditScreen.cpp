#include "EditScreen.h"
#include "CommandLine.h"
#include "TextFit.h"
#include "Glyphs.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;

    namespace
    {
        constexpr double kToastHoldMs = 2600.0;   // a hold, not a tween (design rule §2.5)
    }

    EditScreen::EditScreen()
    {
        clipToBounds = true;
        // the monitor + transport: siblings of the pages, never inside one (R-UI-3)
        mMonitor = std::make_shared<Monitor>();
        addChild(mMonitor);
        mMonitor->onCapture = [this](Rect r) { if (onCapture) onCapture(r); };
        mTransport = std::make_shared<Transport>();
        mTransport->onCommand = [this](const std::string &l) { emit(l); };
        mTransport->onCapture = [this](Rect r) { if (onCapture) onCapture(r); };
        addChild(mTransport);

        for (int i = 0; i < 3; ++i)
        {
            mPages[i] = std::make_shared<FadePage>();
            addChild(mPages[i]);
        }
        auto fwd = [this](const std::string &l) { emit(l); };
        // Grade
        mRack = std::make_shared<RackTree>();
        mRack->onCommand = fwd;
        mRack->onAddFootage = [this] { if (onAddFootage) onAddFootage(); };
        mRack->onContext = [this](int i, Point p) { if (onRackContext) onRackContext(i, p); };
        mGradeInspector = std::make_shared<GradeInspector>();
        mGradeInspector->onCommand = fwd;
        mGradeDeck = std::make_shared<GradeDeck>();
        mGradeDeck->onCommand = fwd;
        mGradeDeck->onContext = [this](int i, Point p) { if (onRackContext) onRackContext(i, p); };
        // browsing groups (R-UI-12): the tree and the strip open the same group, either way round
        mRack->onOpenGroup = [this](const std::string &ro) { mGradeDeck->openGroup(ro); };
        mGradeDeck->onNavigate = [this](const std::string &ro) { mRack->setOpen(ro, true); };
        mPages[Grade]->addChild(mRack);
        mPages[Grade]->addChild(mGradeInspector);
        mPages[Grade]->addChild(mGradeDeck);
        // Cut
        mBin = std::make_shared<SourceBin>();
        mBin->onCommand = fwd;
        mBin->onAddFootage = [this] { if (onAddFootage) onAddFootage(); };
        mBin->onContext = [this](int i, Point p) { if (onRackContext) onRackContext(i, p); };
        mBin->onOpen = [this](const interstellar::RackNodeModel &n) { emit("source view " + cmd::quote(n.bindName)); };   // R-EDT-1
        mTimeline = std::make_shared<Timeline>();
        mTimeline->onKeyContext = [this](const std::string &a, double t, Point w) { if (onKeyContext) onKeyContext(a, t, w); };
        mTimeline->onKeyPlotContext = [this](double t, Point w) { if (onKeyPlotContext) onKeyPlotContext(t, w); };
        mTimeline->onCommand = [this](const std::string &l) { return emit(l); };
        mTimeline->onClipContext = [this](const std::string &id, Point w) { if (onClipContext) onClipContext(id, w); };
        mTimeline->onLaneContext = [this](const std::string &trk, double t, Point w) { if (onLaneContext) onLaneContext(trk, t, w); };
        // a source dragged out of the bin is the timeline's to place (R-UI-14)
        mBin->onDragSource = [this](const interstellar::RackNodeModel &n, Point w, SourceBin::DragPhase ph) {
            const std::string label = n.cosmoName.empty() ? n.bindName : n.cosmoName;
            const double dur = n.mediaDuration > 0 ? n.mediaDuration : (n.video ? 0.0 : 5.0);
            if (ph == SourceBin::DragPhase::Move) mTimeline->dropHover(label, dur, w);
            else if (ph == SourceBin::DragPhase::Cancel) mTimeline->dropCancel();
            else
            {
                std::string trk;
                double at = 0;
                if (mTimeline->dropAt(w, trk, at) && onDropSource) onDropSource(n.bindName, trk, at);
            }
        };
        mClipInspector = std::make_shared<ClipInspector>();
        mClipInspector->onCommand = fwd;
        mPages[Cut]->addChild(mBin);
        mPages[Cut]->addChild(mClipInspector);
        mPages[Cut]->addChild(mTimeline);
        // Deliver
        mChecks = std::make_shared<ChecksPanel>();
        mOutput = std::make_shared<OutputSpec>();
        mOutput->onCommand = fwd;
        mQueue = std::make_shared<RenderQueue>();
        mPages[Deliver]->addChild(mChecks);
        mPages[Deliver]->addChild(mOutput);
        mPages[Deliver]->addChild(mQueue);

        // the chrome LAST among the body, so its open dropdown hit-tests above the pages
        mTopBar = std::make_shared<EditTopBar>();
        mTopBar->tabs()->onSelect = [this](int i) { setTab(i); };
        mTopBar->versions()->onCommand = fwd;
        mTopBar->onSave = [this] { emit("project save"); };
        mTopBar->onHome = [this] { if (onHome) onHome(); };
        addChild(mTopBar);

        // modals, on top of everything
        // cosmo's right-click menu: above the pages and the chrome, below the modals
        mContextMenu = std::make_shared<cosmo_v2::ContextMenu>();
        addChild(mContextMenu);
        mNamePrompt = std::make_shared<NamePrompt>();
        addChild(mNamePrompt);
        mConfirm = std::make_shared<cosmo_v2::ConfirmDialog>(palette::primary());
        addChild(mConfirm);
        mTopBar->versions()->onNewVersion = [this] {
            mNamePrompt->show("New version", "A derived timeline: its base, plus your changes. Letters, digits and _.",
                              "", "Create",
                              [this](const std::string &typed) {
                                  // Timeline names are bind names: derive a legal one from what was typed.
                                  const std::string name = cmd::bindName(typed);
                                  if (name.empty()) return;
                                  // the base is the version being edited when the name is confirmed
                                  std::string line = "timeline new " + cmd::quote(name);
                                  if (!mCurrentTimeline.empty()) line += " --base " + cmd::quote(mCurrentTimeline);
                                  emit(line);
                              });
        };
        for (int i = 0; i < 3; ++i) mPages[i]->setShownImmediate(i == mTab);
    }

    void EditScreen::setTab(int tab)
    {
        if (tab < 0 || tab > 2 || tab == mTab) return;
        mTab = tab;
        for (int i = 0; i < 3; ++i) mPages[i]->setShown(i == tab);
        mTopBar->tabs()->setSelected(tab);
        mTopBar->versions()->close();
        mTransportTarget = tab == Grade ? 0.0 : 1.0;   // the tween starts in advance (a setter has no clock)
        mMonitor->setCaptureShown(tab == Grade);
    }

    void EditScreen::bind(const interstellar::AppModel &m, bool interacting, double nowMs)
    {
        mCurrentTimeline = m.currentTimeline;
        mTopBar->bind(m);
        mTransport->bind(m);
        mRack->bind(m);
        mGradeInspector->bind(m, interacting);
        mGradeDeck->bind(m);
        mBin->bind(m);
        mTimeline->bind(m);
        mClipInspector->bind(m);
        mChecks->bind(m);
        mOutput->bind(m);
        mQueue->bind(m, nowMs);
    }

    void EditScreen::layout()
    {
        const double W = width.value(), H = height.value();
        const double top = shell::topBarH();
        const double deckH = shell::deckH(H);
        const double deckY = H - deckH;
        const double lw = shell::leftW(), rw = shell::rightW();
        const double colH = std::max(0.0, deckY - top);

        mContextMenu->x.set(0); mContextMenu->y.set(0); mContextMenu->width.set(W); mContextMenu->height.set(H);
        mTopBar->x.set(0); mTopBar->y.set(0); mTopBar->width.set(W); mTopBar->height.set(top);
        mTopBar->setRootSize(W, H);
        mTopBar->layout();

        // the flexible column last, from what the fixed ones left (design rule §4.6)
        const double monW = std::max(0.0, W - lw - rw);
        // Grade has no transport (R-UI-3, amended): the monitor takes its room, through the live
        // eased amount so the seam never tears.
        const double ta = mTransportAmt.value();
        const double monH = std::max(0.0, colH - shell::transportH() * ta);
        mMonitor->x.set(lw); mMonitor->y.set(top); mMonitor->width.set(monW); mMonitor->height.set(monH);
        mTransport->x.set(lw); mTransport->y.set(top + monH); mTransport->width.set(monW); mTransport->height.set(shell::transportH());
        mTransport->opacity.set(ta);
        mTransport->visible = ta > 0.001;   // culled: hidden widgets take no input

        for (int i = 0; i < 3; ++i)
        {
            mPages[i]->x.set(0); mPages[i]->y.set(top); mPages[i]->width.set(W); mPages[i]->height.set(std::max(0.0, H - top));
        }
        auto place = [&](Segment &s, double x, double y, double w, double h) { s.x.set(x); s.y.set(y); s.width.set(w); s.height.set(h); };
        const double pageDeckY = deckY - top;
        place(*mRack, 0, 0, lw, colH);
        place(*mGradeInspector, W - rw, 0, rw, colH);
        place(*mGradeDeck, 0, pageDeckY, W, deckH);
        mGradeInspector->layout();
        mGradeDeck->layout();
        place(*mBin, 0, 0, lw, colH);
        place(*mClipInspector, W - rw, 0, rw, colH);
        place(*mTimeline, 0, pageDeckY, W, deckH);
        mClipInspector->layout();
        place(*mChecks, 0, 0, lw, colH);
        place(*mOutput, W - rw, 0, rw, colH);
        place(*mQueue, 0, pageDeckY, W, deckH);
        mOutput->layout();

        place(*mNamePrompt, 0, 0, W, H);
        mNamePrompt->layout();
        place(*mConfirm, 0, 0, W, H);
    }

    void EditScreen::showRefusal(const std::string &message)
    {
        mToast = message;
        mToastError = true;
        mToastPending = true;
    }

    void EditScreen::showNotice(const std::string &message)
    {
        mToast = message;
        mToastError = false;
        mToastPending = true;
    }

    void EditScreen::advance(double nowMs)
    {
        mNowMs = nowMs;
        if (mToastPending)
        {
            mToastAmt.animateTo(1.0, motion::kModalOpenMs, Easing::EaseOutCubic, nowMs);
            mToastShownAt = nowMs;
            mToastClosing = false;
            mToastPending = false;
        }
        if (mToastShownAt >= 0.0 && !mToastClosing && nowMs - mToastShownAt > kToastHoldMs)
        {
            mToastAmt.animateTo(0.0, motion::kProgressOutMs, Easing::EaseOutCubic, nowMs);
            mToastClosing = true;
        }
        mToastAmt.update(nowMs);
        const double tt = mTab == Grade ? 0.0 : 1.0;
        if (!mTransportInit) { mTransportAmt.set(tt); mTransportTarget = mTransportApplied = tt; mTransportInit = true; mMonitor->setCaptureShown(mTab == Grade); }
        else if (mTransportApplied != mTransportTarget)
        {
            mTransportApplied = mTransportTarget;
            mTransportAmt.animateTo(mTransportTarget, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
        }
        mTransportAmt.update(nowMs);
        Segment::advance(nowMs);
    }

    void EditScreen::onPaint(IRenderTarget &t) const
    {
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(palette::background()));
    }

    void EditScreen::onOverlay(IRenderTarget &t) const
    {
        const double a = mToastAmt.value();
        if (a <= 0.001 || mToast.empty()) return;
        // the refused/error state, said in words over the monitor, never silently dropped — or,
        // without the red, a notice that an action finished
        const Rect mon{mMonitor->x.value(), mMonitor->y.value(), mMonitor->width.value(), mMonitor->height.value()};
        const std::string msg = textfit::ellipsize(t, mToast, std::max(0.0, mon.w - 80.0), 11.0, font::sans());
        const double tw = t.measureText(msg, 11.0, font::sans());
        const Rect chip{mon.x + (mon.w - tw - 40.0) * 0.5, mon.bottom() - 44.0 + 6.0 * (1.0 - a), tw + 40.0, 28.0};
        Color bg = palette::popover(); bg.a *= a;
        Color bd = mToastError ? palette::destructive() : palette::border(); bd.a *= (mToastError ? 0.8 : 1.0) * a;
        drawRoundedRect(t, chip, radius::control(), Paint::filledStroked(bg, bd, 1.0));
        Color ic = mToastError ? palette::destructive() : palette::success(); ic.a *= a;
        if (mToastError) glyph::warn(t, Rect{chip.x + 12.0, chip.y + 8.5, 11.0, 11.0}, ic);
        else glyph::check(t, Rect{chip.x + 12.0, chip.y + 8.5, 11.0, 11.0}, ic);
        Color fc = palette::foreground(); fc.a *= a;
        t.setFill(fc);
        t.drawText(msg, chip.x + 30.0, textfit::baseline(chip.y + chip.h * 0.5, 11.0), 11.0, font::sans());
    }
}
}
