#include "ProjectScreen.h"
#include "../../../interstellar/app/widgets/TextFit.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace solaris_ui
{
    using namespace artboard;
    namespace textfit = interstellar_v1::textfit;

    namespace
    {
        std::string q(const std::string &s) { return s.find_first_of(" \t\"") == std::string::npos && !s.empty() ? s : "\"" + s + "\""; }
        std::string beats(double b) { return Timeline::beatText(b); } // to the tick
    }

    ProjectScreen::ProjectScreen()
    {
        clipToBounds = true;
        mBar = std::make_shared<SongBar>();
        mBrowser = std::make_shared<Browser>();
        mTimeline = std::make_shared<Timeline>();
        mDock = std::make_shared<MixerDock>();
        addChild(mTimeline);
        addChild(mDock);
        addChild(mBrowser);
        mWindows = std::make_shared<WindowLayer>();
        addChild(mWindows); // over the lanes, the dock and the browser; under the song bar and its menus
        addChild(mBar);
        mWindows->onCommand = [this](const std::string &l) { return onCommand ? onCommand(l) : false; };
        mWindows->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { if (onMenu) onMenu(std::move(items), world); };
        mWindows->onRename = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
            if (onRename) onRename(cur, world, std::move(done));
        };
        // R-UI-11: one clipboard (the host's) and one eased Show IDs for every widget
        auto copy = [this](const std::string &text) { if (onCopy) onCopy(text); };
        auto ids = [this] { return mIds.value(); };
        mWindows->onCopy = copy;
        mWindows->idsAmount = ids;
        mDock->onCopy = copy;
        mDock->idsAmount = ids;
        mTimeline->onCopy = copy;
        mTimeline->idsAmount = ids;
        mDock->onOpenDevice = [this](const std::string &dv) { mWindows->openDevice(dv); };
        mDock->isDeviceOpen = [this](const std::string &dv) { return mWindows->isOpen("dev:" + dv); };

        mBrowser->onCommand = [this](const std::string &l) { if (onCommand) onCommand(l); };
        mTimeline->onCommand = [this](const std::string &l) { if (onCommand) onCommand(l); };
        mTimeline->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { if (onMenu) onMenu(std::move(items), world); };
        mTimeline->onOpenPattern = [this](const std::string &pt) { mWindows->openRoll(pt); };
        mTimeline->onOpenAutomation = [this](const std::string &au) { mWindows->openAutomation(au); }; // R-AUTO-11
        mDock->onCommand = [this](const std::string &l) { return onCommand ? onCommand(l) : false; };
        mDock->onMenu = [this](std::vector<cosmo_v2::ContextMenu::Item> items, Point world) { if (onMenu) onMenu(std::move(items), world); };
        mDock->onRename = [this](const std::string &cur, Point world, std::function<void(const std::string &)> done) {
            if (onRename) onRename(cur, world, std::move(done));
        };
        mDock->onToggle = [this] { mDockOpen = !mDockOpen; };
        mDock->onResize = [this](double worldY) {
            // direct manipulation: the edge follows the pointer, and that is where it stays
            const double h = height.value() - toLocal(Point{0.0, worldY}).y;
            mDockOpen = true;
            mDockWant = std::max(MixerDock::kTabsH + 97.5, h);
            mDockLast = dockTarget();
            mDockH.set(mDockLast);
        };
        mBrowser->onDragMove = [this](const Browser::Item &it, Point world) {
            double beat = 0;
            int row = -1;
            // over a sampler's window, a sample loads into it (R-EDM-8): the window lights, the lanes do not
            const std::string sampler = it.kind == "audio" ? mWindows->samplerAt(world) : std::string();
            mWindows->setSampleHint(sampler);
            const bool over = sampler.empty() && overLanes(world, beat, row);
            const bool newLane = row >= mTimeline->rowCount();
            mTimeline->setDropHint(over && it.kind != "effect", beat, row, it.label + (newLane ? "  \xC2\xB7  new lane" : ""));
            mGhostWanted = true;
            mGhostAt = toLocal(world);
            mGhostLabel = it.label;
        };
        mBrowser->onDrop = [this](const Browser::Item &it, Point world) {
            mTimeline->setDropHint(false);
            mWindows->setSampleHint(std::string());
            mGhostWanted = false;
            double beat = 0;
            int row = -1;
            const std::string sampler = it.kind == "audio" ? mWindows->samplerAt(world) : std::string();
            if (!sampler.empty()) { if (onCommand) onCommand("set " + sampler + ".sample=" + q(it.value)); } // ONE line
            else if (overLanes(world, beat, row)) place(it, beat, row);
        };
        mBrowser->onActivate = [this](const Browser::Item &it) { place(it, std::floor(mPosition * 4.0) / 4.0, mTimeline->rowCount()); };
    }

    bool ProjectScreen::overLanes(const Point &world, double &beat, int &row) const
    {
        const Point p = mTimeline->toLocal(world);
        if (p.x < Timeline::kHeaderW || p.y < Timeline::kRulerH || p.x > mTimeline->width.value() || p.y > mTimeline->height.value()) return false;
        beat = mTimeline->snap(mTimeline->xToBeat(p.x)); // the lanes' one grid (R-UI-10): the hint and the drop agree
        row = mTimeline->rowAt(p.y);
        return row >= 0;
    }

    bool ProjectScreen::place(const Browser::Item &it, double beat, int row)
    {
        if (!onCommand) return false;
        const std::string lane = row < mTimeline->rowCount() ? mTimeline->rowLane(row) : std::string();
        const std::string onLane = lane.empty() ? std::string() : " --lane " + q(lane);
        if (it.kind == "audio") return onCommand("clip add --src " + q(it.value) + " --at " + beats(beat) + onLane);
        if (it.kind == "instrument") return onCommand("clip add --instrument " + it.value + " --at " + beats(beat) + " --length 4" + onLane);
        if (it.kind == "effect" && onNotice) onNotice("An effect goes on a mixer strip \xE2\x80\x94 drop it in the mixer.");
        return false;
    }

    void ProjectScreen::bind(const solaris::AppModel &m)
    {
        mPosition = m.transport.position;
        mIdsWant = m.settings.showIds;
        mBar->bind(m);
        mBrowser->bind(m);
        mTimeline->bind(m);
        mDock->bind(m, mInteracting);
        mWindows->bind(m, mInteracting);
    }

    double ProjectScreen::dockTarget() const
    {
        const double avail = std::max(0.0, height.value() - SongBar::kHeight);
        if (!mDockOpen) return MixerDock::kTabsH;
        // the dock gives way before the lanes do (R4)
        return std::max(MixerDock::kTabsH, std::min(mDockWant, avail - kLanesFloor));
    }

    void ProjectScreen::layout()
    {
        const double W = width.value(), H = height.value(), top = SongBar::kHeight;
        mBar->x.set(0); mBar->y.set(0); mBar->width.set(W);
        mBar->layout();
        // the browser folds by its WIDTH (View › Browser): everything right of it reads the live width
        const double bw = mBrowserW.value();
        mBrowser->x.set(bw - Browser::kWidth); mBrowser->y.set(top); mBrowser->width.set(Browser::kWidth); mBrowser->height.set(std::max(0.0, H - top));
        mBrowser->opacity.set(std::clamp(bw / Browser::kWidth, 0.0, 1.0));
        const double rw = std::max(0.0, W - bw), dockH = std::min(mDockH.value(), std::max(0.0, H - top));
        mTimeline->x.set(bw); mTimeline->y.set(top);
        mTimeline->width.set(rw);                                    // the flexible column last (§4)
        mTimeline->height.set(std::max(0.0, H - top - dockH));       // from the LIVE dock height: the seam never tears
        mDock->x.set(bw); mDock->y.set(top + std::max(0.0, H - top - dockH));
        mDock->width.set(rw);
        mDock->height.set(dockH);
        mWindows->x.set(0.0); mWindows->y.set(top);
        mWindows->width.set(W); mWindows->height.set(std::max(0.0, H - top));
        mBrowser->layout();
        mTimeline->layout();
        mDock->layout();
        mWindows->layout();
    }

    void ProjectScreen::advance(double nowMs)
    {
        mNowMs = nowMs;
        const double want = mGhostWanted ? 1.0 : 0.0;
        if (std::fabs(mGhost.value() - want) > 1e-6 && !mGhost.isAnimating()) mGhost.animateTo(want, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
        mGhost.update(nowMs);
        // the dock's height: toggled, or the window changed — eased; dragged — already set
        const double dt = dockTarget();
        if (!mDockInit) { mDockH.set(dt); mDockLast = dt; mDockInit = true; }
        else if (dt != mDockLast) { mDockH.animateTo(dt, motion::kSlideMs, Easing::EaseOutCubic, nowMs); mDockLast = dt; }
        mDockH.update(nowMs);
        const double bwWant = mBrowserOpen ? Browser::kWidth : 0.0;
        if (!mBrowserInit) { mBrowserW.set(bwWant); mBrowserLast = mBrowserOpen; mBrowserInit = true; }
        else if (mBrowserOpen != mBrowserLast) { mBrowserW.animateTo(bwWant, motion::kSlideMs, Easing::EaseOutCubic, nowMs); mBrowserLast = mBrowserOpen; }
        mBrowserW.update(nowMs);
        // Show IDs: the ids fade in and out together, wherever they are drawn (§1: a setting is not exempt)
        if (!mIdsInit) { mIds.set(mIdsWant ? 1.0 : 0.0); mIdsLast = mIdsWant; mIdsInit = true; }
        else if (mIdsWant != mIdsLast) { mIds.animateTo(mIdsWant ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs); mIdsLast = mIdsWant; }
        mIds.update(nowMs);
        Segment::advance(nowMs);
    }

    void ProjectScreen::onPaint(IRenderTarget &t) const
    {
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(surface::stageBg()));
    }

    void ProjectScreen::onOverlay(IRenderTarget &t) const
    {
        // the dragged item, over everything: it follows the pointer exactly (direct manipulation)
        const double a = mGhost.value();
        if (a <= 0.001 || mGhostLabel.empty()) return;
        const std::string s = textfit::ellipsize(t, mGhostLabel, 220.0, 11.0, font::sans());
        const double w = t.measureText(s, 11.0, font::sans()) + 20.0;
        const Rect r{mGhostAt.x + 12.0, mGhostAt.y + 10.0, w, 24.0};
        Color bg = palette::popover(), bd = palette::primary(), fg = palette::foreground();
        bg.a *= 0.95 * a; bd.a *= a; fg.a *= a;
        drawRoundedRect(t, r, radius::control(), Paint::filledStroked(bg, bd, 1.0));
        t.setFill(fg);
        t.drawText(s, r.x + 10.0, textfit::baseline(r.y + r.h * 0.5, 11.0), 11.0, font::sans());
    }
}
}
