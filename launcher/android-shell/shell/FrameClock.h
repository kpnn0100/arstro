/*
 *  arstro-android-shell — FrameClock (M1.3).
 *
 *  The ONE shared ~60Hz tick for the whole shell. Every surface registers a per-frame
 *  callback; the clock runs a single GLib timeout, computes a monotonic `nowMs`, and
 *  calls each callback. Centralising the tick (rather than one timer per surface) keeps
 *  every surface's animation clock coherent and makes "idle = zero redraws" a property
 *  of what the callbacks do (they queue a draw only when their surface is dirty), not of
 *  how many timers are running.
 *
 *  `tick(nowMs)` runs one frame synchronously with a caller-supplied time — used by the
 *  headless tests to drive frames deterministically without a GLib main loop.
 */
#pragma once
#include <functional>
#include <vector>

namespace arstro
{
namespace androidshell
{
    class FrameClock
    {
    public:
        using Tick = std::function<void(double nowMs)>;

        // Register a per-frame callback (typically a SurfaceHost::frameTick bound to `this`).
        void add(Tick tick) { mTicks.push_back(std::move(tick)); }
        int count() const { return static_cast<int>(mTicks.size()); }

        // Start the GLib ~60Hz timeout (needs a running GTK/GLib main loop). nowMs passed to
        // callbacks is milliseconds since start(), from the monotonic clock.
        void start(int intervalMs = 16);
        void stop();

        // Run one frame synchronously at time `nowMs` (tests / headless). Calls every callback.
        void tick(double nowMs);

    private:
        static int onTimeout(void *self);  // GSourceFunc trampoline (returns G_SOURCE_CONTINUE)

        std::vector<Tick> mTicks;
        unsigned int mSourceId = 0;
        long long mStartUs = 0;
    };

} // namespace androidshell
} // namespace arstro
