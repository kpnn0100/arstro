/*
 *  interstellar_core — FrameCache: a byte-capped LRU of finished layer frames (R-PLAY-3).
 *
 *  Without it a scrub is a re-decode per pointer move, which is the difference between an editor
 *  and a demo. With it, the same frame is produced once.
 *
 *  **The key is what makes it safe.** `(layer, sourceFrame, paramHash, level)` — and `paramHash`
 *  covers every value that fed the layer, not the parameter struct it came from. A key that
 *  hashed only the struct would miss a change to a SHAPE that a link maps into it, and serve a
 *  stale frame — which is indistinguishable from a rendering bug, and therefore the worst outcome
 *  a cache can have.
 *
 *  **Capped in BYTES, never in entries.** A 4K RGBA frame is 33 MB and a 1280-edge proxy is 3.5 MB,
 *  so "sixty frames" is either 200 MB or 2 GB depending on something the cache does not control.
 *  Every cap in this design is in bytes for the same reason cosmo's pixel pools are (R-MEM).
 *
 *  Its counters are published in the model (R-NFR-5), because "memory is bounded" and "the cache
 *  works" are both claims this project has learned not to make without a number a front end can
 *  read back (R-CPU-4).
 */
#pragma once
#include "Composite.h"
#include <cstdint>
#include <list>
#include <string>
#include <unordered_map>

namespace arstro
{
namespace interstellar
{
    class FrameCache
    {
    public:
        struct Key
        {
            std::string source;        // the media path — a frame belongs to its file, not its clip
            long long sourceFrame = 0;
            uint64_t paramHash = 0;
            int level = 0;
            bool operator==(const Key &o) const
            {
                return sourceFrame == o.sourceFrame && paramHash == o.paramHash &&
                       level == o.level && source == o.source;
            }
        };

        explicit FrameCache(size_t capBytes = 512ull * 1024 * 1024) : mCap(capBytes) {}

        void setCapBytes(size_t bytes);
        /** Copies into `out` on a hit and promotes the entry. A reference would dangle the moment
         *  the next `put` evicted it, and the caller composites afterwards. */
        bool get(const Key &k, Raster &out);
        void put(const Key &k, const Raster &frame);
        /** Drop everything for one source over a frame RANGE, so a cut at 40 s does not throw away
         *  the frames around 4 s. A whole-cache flush per edit turns a scrub into a re-render. */
        void invalidateSource(const std::string &source, long long fromFrame, long long toFrame);
        void clear();

        size_t residentBytes() const { return mBytes; }
        size_t entries() const { return mIndex.size(); }
        unsigned hits() const { return mHits; }
        unsigned misses() const { return mMisses; }
        unsigned evictions() const { return mEvictions; }

    private:
        struct Entry { Key key; Raster frame; size_t bytes = 0; };
        void evictTo(size_t target);

        std::list<Entry> mLru;      // front = most recently used
        struct KeyHash { size_t operator()(const Key &k) const; };
        std::unordered_map<std::string, std::list<Entry>::iterator> mIndex;
        static std::string indexKey(const Key &k);

        size_t mCap;
        size_t mBytes = 0;
        unsigned mHits = 0, mMisses = 0, mEvictions = 0;
    };
}
}
