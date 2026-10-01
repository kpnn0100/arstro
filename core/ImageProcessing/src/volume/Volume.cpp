/*
 *  Arstro ImageProcessing Library
 *
 *  CachedVolume: the lazy volume over the host's decoder. See Volume.h for the contract and the
 *  reasons behind its shape; this file is the bookkeeping that makes three promises true:
 *
 *    1. a frame inside the window being materialised is never evicted — the only eviction is
 *       of frames OUTSIDE the requested [t0, t1], decided before any decode starts;
 *    2. a forward walk decodes each source frame once, in ascending order — missing frames are
 *       fetched in ascending t, and a frame shared with the previous window is kept;
 *    3. after window() returns, residency is exactly the window — so it is bounded by the
 *       footprint and the cap, and is independent of clip length.
 */
#include "Volume.h"
#include <utility>

namespace arstro
{
    CachedVolume::CachedVolume(VolumeExtent ext, Provider p, std::size_t capBytes)
        : mExt(ext), mProvider(std::move(p)), mCap(capBytes)
    {
        mResident.reserve(VolumeView::kMaxWindow);
        mSpare.reserve(VolumeView::kMaxWindow);
    }

    std::size_t CachedVolume::frameBytes() const
    {
        if (mExt.width <= 0 || mExt.height <= 0) return 0;
        return (std::size_t)mExt.width * (std::size_t)mExt.height * VolumeView::kChannels;
    }

    std::size_t CachedVolume::residentBytes() const
    {
        std::size_t n = 0;
        for (const Entry &e : mResident) n += e.frame.rgba.capacity();
        return n;
    }

    void CachedVolume::clear()
    {
        mResident.clear();
        mSpare.clear();
    }

    CachedVolume::Entry *CachedVolume::find(long long t)
    {
        for (Entry &e : mResident)
            if (e.t == t) return &e;
        return nullptr;
    }

    bool CachedVolume::window(long long t0, long long t1, VolumeView &out)
    {
        // The previous view ends here whatever happens next (the lifetime rule), so a caller that
        // ignores a false return reads an empty view rather than pointers we are about to reuse.
        out = VolumeView{};

        const std::size_t fb = frameBytes();
        if (!mProvider || fb == 0) return false;
        if (t0 < 0 || t1 < t0 || t1 >= mExt.frames) return false;
        const long long n = t1 - t0 + 1;
        if (n > VolumeView::kMaxWindow) return false;
        if ((std::size_t)n * fb > mCap) return false;

        // Split the resident set: frames inside [t0, t1] stay where they are (their pointers do
        // not move — std::vector's buffer survives the Entry being moved), frames outside donate
        // their buffers. This is the ONLY eviction, and it happens before the first decode, so
        // nothing a decode does can push out a frame of this window.
        std::size_t keep = 0;
        for (std::size_t i = 0; i < mResident.size(); ++i)
        {
            if (mResident[i].t >= t0 && mResident[i].t <= t1)
            {
                if (keep != i) mResident[keep] = std::move(mResident[i]);
                ++keep;
            }
            else
                mSpare.push_back(std::move(mResident[i].frame));
        }
        mResident.resize(keep);

        // Ascending t: the decoder is sequential, and any other order is a seek.
        for (long long t = t0; t <= t1; ++t)
        {
            if (find(t)) continue;
            Entry e;
            e.t = t;
            if (!mSpare.empty())
            {
                e.frame = std::move(mSpare.back());
                mSpare.pop_back();
            }
            ++mDecodes;
            const bool ok = mProvider(t, e.frame) && e.frame.width == mExt.width &&
                            e.frame.height == mExt.height && e.frame.rgba.size() == fb;
            if (!ok)
            {
                // Keep what is valid; the half-built window is simply not handed out.
                mSpare.clear();
                return false;
            }
            mResident.push_back(std::move(e));
        }
        // Buffers still spare mean the window shrank. Releasing them is what makes residency
        // exactly the window rather than the high-water mark of every window so far.
        mSpare.clear();

        out.frames = (int)n;
        out.width = mExt.width;
        out.height = mExt.height;
        out.t0 = t0;
        out.rowStride = (std::ptrdiff_t)mExt.width * VolumeView::kChannels;
        // Every t in [t0, t1] is resident exactly once, so one pass places every pointer.
        for (const Entry &e : mResident) out.frame[e.t - t0] = e.frame.rgba.data();
        return true;
    }
}
