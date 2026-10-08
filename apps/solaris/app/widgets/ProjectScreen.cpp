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
        std::string beats(double b)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%g", std::round(b * 960.0) / 960.0);
            return buf;
        }
    }

    ProjectScreen::ProjectScreen()
    {
        clipToBounds = true;
        mBar = std::make_shared<SongBar>();
        mBrowser = std::make_shared<Browser>();
        mTimeline = std::make_shared<Timeline>();
        addChild(mTimeline);
        addChild(mBrowser);
        addChild(mBar);

        mBrowser->onCommand = [this](const std::string &l) { if (onCommand) onCommand(l); };
        mTimeline->onCommand = [this](const std::string &l) { if (onCommand) onCommand(l); };
        mBrowser->onDragMove = [this](const Browser::Item &it, Point world) {
            double beat = 0;
            int row = -1;
            const bool over = overLanes(world, beat, row);
            const bool newLane = row >= mTimeline->rowCount();
            mTimeline->setDropHint(over && it.kind != "effect", beat, row, it.label + (newLane ? "  \xC2\xB7  new lane" : ""));
            mGhostWanted = true;
            mGhostAt = toLocal(world);
            mGhostLabel = it.label;
        };
        mBrowser->onDrop = [this](const Browser::Item &it, Point world) {
            mTimeline->setDropHint(false);
            mGhostWanted = false;
            double beat = 0;
            int row = -1;
            if (overLanes(world, beat, row)) place(it, beat, row);
        };
        mBrowser->onActivate = [this](const Browser::Item &it) { place(it, std::floor(mPosition * 4.0) / 4.0, mTimeline->rowCount()); };
    }

    bool ProjectScreen::overLanes(const Point &world, double &beat, int &row) const
    {
        const Point p = mTimeline->toLocal(world);
        if (p.x < Timeline::kHeaderW || p.y < Timeline::kRulerH || p.x > mTimeline->width.value() || p.y > mTimeline->height.value()) return false;
        beat = std::max(0.0, std::round(mTimeline->xToBeat(p.x) * 4.0) / 4.0);
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
        mBar->bind(m);
        mBrowser->bind(m);
        mTimeline->bind(m);
    }

    void ProjectScreen::layout()
    {
        const double W = width.value(), H = height.value(), top = SongBar::kHeight;
        mBar->x.set(0); mBar->y.set(0); mBar->width.set(W);
        mBrowser->x.set(0); mBrowser->y.set(top); mBrowser->width.set(Browser::kWidth); mBrowser->height.set(std::max(0.0, H - top));
        mTimeline->x.set(Browser::kWidth); mTimeline->y.set(top);
        mTimeline->width.set(std::max(0.0, W - Browser::kWidth));    // the flexible column last (§4)
        mTimeline->height.set(std::max(0.0, H - top));
        mBrowser->layout();
        mTimeline->layout();
    }

    void ProjectScreen::advance(double nowMs)
    {
        mNowMs = nowMs;
        const double want = mGhostWanted ? 1.0 : 0.0;
        if (std::fabs(mGhost.value() - want) > 1e-6 && !mGhost.isAnimating()) mGhost.animateTo(want, motion::kHoverMs, Easing::EaseOutCubic, nowMs);
        mGhost.update(nowMs);
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
