/*
 *  interstellar_render — FrameCache: a byte-capped LRU of frames.
 *
 *  Without it a scrub is a re-decode and a re-grade per pointer move, which is the difference
 *  between an editor and a demo. With it, a frame is produced once.
 *
 *  **The key is what makes it safe.** `(media path, sourceFrame, paramHash, level)`. A frame
 *  belongs to its FILE, not its clip, so two clips cut from one file share frames; `paramHash`
 *  (ParamHash.h) covers the whole grade, so a changed grade can never be served a stale picture —
 *  a stale frame is indistinguishable from a rendering bug, the worst outcome a cache can have.
 *  `paramHash == kUngraded` (0) is the ungraded source frame, which is what the temporal volume
 *  works over (R-VOL-5); `hashParams` never returns 0, so the two can share one cache.
 *
 *  **Capped in BYTES, never in entries.** A 4K RGBA frame is 33 MB and a 1280-edge proxy 3.5 MB, so
 *  "sixty frames" is 200 MB or 2 GB depending on something the cache does not control.
 *
 *  `get` COPIES out. A reference would dangle at the next eviction, and the caller composites
 *  after the call returns — possibly while a decode-ahead thread is putting. Every call takes one
 *  mutex, so a playback thread and a prefetch thread can share an instance; the copy happens under
 *  it for the same dangling reason.
 */
#pragma once
#include "Raster.h"
#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace arstro
{
namespace interstellar
{
namespace render
{
    class FrameCache
    {
    public:
        static constexpr uint64_t kUngraded = 0;

        struct Key
        {
            std::string source;        // the media path
            long long sourceFrame = 0;
            uint64_t paramHash = kUngraded;
            int level = 0;             // proxy level; 0 = full resolution
            bool operator==(const Key &o) const
            {
                return sourceFrame == o.sourceFrame && paramHash == o.paramHash && level == o.level &&
                       source == o.source;
            }
        };

        struct Stats
        {
            std::size_t residentBytes = 0, entries = 0;
            uint64_t hits = 0, misses = 0, evictions = 0;
        };

        explicit FrameCache(std::size_t capBytes = 512ull * 1024 * 1024) : mCap(capBytes) {}

        /** Lowering the cap evicts down to it immediately. */
        void setCapBytes(std::size_t bytes);
        std::size_t capBytes() const;

        /** On a hit, copies the frame into `out`, promotes it, and returns true. */
        bool get(const Key &k, Raster &out);
        /** Insert or replace. A frame larger than the whole cap is NOT cached (and any older entry
         *  under the same key is dropped, since it is stale by definition): emptying the cache to
         *  hold one thing would throw away every frame a scrub is about to want. */
        void put(const Key &k, const Raster &frame);
        /** Drop one source's frames over [fromFrame, toFrame], inclusive. A range, not a flush: a
         *  trim at 40 s must not throw away the frames a scrub around 4 s is using. */
        void invalidate(const std::string &source, long long fromFrame, long long toFrame);
        void clear();

        std::size_t residentBytes() const;
        std::size_t entries() const;
        uint64_t hits() const;
        uint64_t misses() const;
        uint64_t evictions() const;
        /** One consistent snapshot of every counter, for a model that publishes them together. */
        Stats stats() const;

    private:
        struct Entry
        {
            Key key;
            Raster frame;
            std::size_t bytes = 0;
        };
        struct KeyHash
        {
            std::size_t operator()(const Key &k) const;
        };
        using List = std::list<Entry>;

        void evictToLocked(std::size_t target);

        mutable std::mutex mMu;
        List mLru;   // front = most recently used
        std::unordered_map<Key, List::iterator, KeyHash> mIndex;
        std::size_t mCap;
        std::size_t mBytes = 0;
        uint64_t mHits = 0, mMisses = 0, mEvictions = 0;
    };
}
}
}
