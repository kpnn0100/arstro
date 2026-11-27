/*
 *  arstro.clock — the smallest real app on the stack: the time, large, and a stopwatch.
 *
 *  It exists to prove the SDK end to end (a tree, a tap, an animation that keeps running, Back
 *  left to the shell) and to be the second card in recents. It is also the example to copy.
 */
#include "App.h"
#include "Widgets.h"
#include "GeneratedIcons.h"
#include <cstdio>
#include <ctime>

using namespace arstro;
using namespace arstro::kit;

class ClockRoot : public artboard::Segment
{
public:
    std::string time, date, stopwatch;
    double insetTop = 0;

protected:
    void onPaint(artboard::IRenderTarget &t) const override
    {
        const double w = width.value(), h = height.value();
        fillRounded(t, {0, 0, w, h}, 0, surface::appBackground());
        const double size = std::min(120.0, h * 0.24);
        drawLabel(t, time, 0, h * 0.34, w, size, font::sans(), palette::foreground(), Align::Center, -0.02 * size);
        drawLabel(t, date, 0, h * 0.34 + size * 0.62 + 8.0, w, type::body(), font::sans(), palette::secondaryForeground(), Align::Center);
        drawLabel(t, stopwatch, 0, h * 0.66, w, type::headline(), font::mono(), palette::foreground(), Align::Center);
    }
};

class ClockApp : public app::App
{
public:
    void create(app::Context &ctx) override
    {
        mRoot = std::make_shared<ClockRoot>();
        mButton = std::make_shared<TextButton>("Start");
        mButton->onTap = [this, &ctx] {
            // A running stopwatch changes on its own, so it asks for every frame; stopped, the
            // app draws nothing and costs nothing.
            mRunning = !mRunning;
            if (mRunning) mStartMs = ctx.nowMs - mElapsedMs;
            mButton->setLabel(mRunning ? "Stop" : "Start");
            ctx.setContinuous(mRunning);
        };
        mRoot->addChild(mButton);
    }
    artboard::Segment &root() override { return *mRoot; }

    void layout(app::Context &ctx) override
    {
        if (mRunning) mElapsedMs = ctx.nowMs - mStartMs;
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
        localtime_r(&now, &tm);
        char b[64];
        std::strftime(b, sizeof b, "%H:%M", &tm);
        mRoot->time = b;
        std::strftime(b, sizeof b, "%A, %e %B", &tm);
        mRoot->date = b;
        const long cs = (long)(mElapsedMs / 10.0);
        std::snprintf(b, sizeof b, "%02ld:%02ld.%02ld", cs / 6000, (cs / 100) % 60, cs % 100);
        mRoot->stopwatch = b;
        mRoot->insetTop = ctx.insetTop;
        const double bw = 120.0, bh = metric::touchTarget();
        mButton->x.set((ctx.width - bw) * 0.5);
        mButton->y.set(ctx.height * 0.66 + 36.0);
        mButton->width.set(bw);
        mButton->height.set(bh);
    }

private:
    std::shared_ptr<ClockRoot> mRoot;
    std::shared_ptr<TextButton> mButton;
    bool mRunning = false;
    double mStartMs = 0, mElapsedMs = 0;
};

int main()
{
    ClockApp app;
    return app::run(app, "arstro.clock");
}
