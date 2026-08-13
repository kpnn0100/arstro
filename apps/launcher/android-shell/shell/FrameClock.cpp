/*
 *  arstro-android-shell — FrameClock implementation (M1.3). See FrameClock.h.
 */
#include "FrameClock.h"
#include <glib.h>

namespace arstro
{
namespace androidshell
{
    void FrameClock::tick(double nowMs)
    {
        for (auto &t : mTicks)
            if (t) t(nowMs);
    }

    int FrameClock::onTimeout(void *self)
    {
        auto *clock = static_cast<FrameClock *>(self);
        const double nowMs = (g_get_monotonic_time() - clock->mStartUs) / 1000.0;
        clock->tick(nowMs);
        return G_SOURCE_CONTINUE;
    }

    void FrameClock::start(int intervalMs)
    {
        if (mSourceId != 0) return;  // already running
        mStartUs = g_get_monotonic_time();
        mSourceId = g_timeout_add(static_cast<guint>(intervalMs),
                                  reinterpret_cast<GSourceFunc>(&FrameClock::onTimeout), this);
    }

    void FrameClock::stop()
    {
        if (mSourceId != 0)
        {
            g_source_remove(mSourceId);
            mSourceId = 0;
        }
    }

} // namespace androidshell
} // namespace arstro
