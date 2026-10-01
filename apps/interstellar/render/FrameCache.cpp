/*
 *  interstellar_render — FrameCache implementation. See FrameCache.h for why it is shaped this way.
 *
 *  The index is keyed on the Key struct itself, not on a string built from it: the first version
 *  concatenated path|frame|hash|level into a fresh std::string on every lookup, an allocation per
 *  `get` on the scrub path for no gain in safety (equality still compares every field, so a hash
 *  collision costs a probe, never a wrong picture).
 */
#include "FrameCache.h"
#include <functional>
#include <iterator>
#include <utility>

namespace arstro
{
namespace interstellar
{
namespace render
{
    std::size_t FrameCache::KeyHash::operator()(const Key &k) const
    {
        // boost-style combine; the path dominates and std::hash of it is already well mixed.
        std::size_t h = std::hash<std::string>{}(k.source);
        auto mix = [&h](uint64_t v) { h ^= std::hash<uint64_t>{}(v) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); };
        mix((uint64_t)k.sourceFrame);
        mix(k.paramHash);
        mix((uint64_t)(int64_t)k.level);
        return h;
    }

    void FrameCache::setCapBytes(std::size_t bytes)
    {
        std::lock_guard<std::mutex> lk(mMu);
        mCap = bytes;
        evictToLocked(mCap);
    }

    std::size_t FrameCache::capBytes() const
    {
        std::lock_guard<std::mutex> lk(mMu);
        return mCap;
    }

    bool FrameCache::get(const Key &k, Raster &out)
    {
        std::lock_guard<std::mutex> lk(mMu);
        auto it = mIndex.find(k);
        if (it == mIndex.end())
        {
            ++mMisses;
            return false;
        }
        mLru.splice(mLru.begin(), mLru, it->second);
        out = it->second->frame;   // a copy, under the lock — see the header
        ++mHits;
        return true;
    }

    void FrameCache::put(const Key &k, const Raster &frame)
    {
        if (frame.empty()) return;
        // The copy is made before the lock is taken, so a put of a 33 MB frame does not stall a
        // concurrent get for the length of a memcpy.
        List node;
        node.push_back(Entry{k, frame, frame.bytes()});

        std::lock_guard<std::mutex> lk(mMu);
        auto existing = mIndex.find(k);
        if (existing != mIndex.end())
        {
            mBytes -= existing->second->bytes;
            mLru.erase(existing->second);
            mIndex.erase(existing);
        }
        if (node.front().bytes > mCap) return;
        mLru.splice(mLru.begin(), node);
        mIndex.emplace(k, mLru.begin());
        mBytes += mLru.front().bytes;
        evictToLocked(mCap);
    }

    void FrameCache::evictToLocked(std::size_t target)
    {
        while (mBytes > target && !mLru.empty())
        {
            auto last = std::prev(mLru.end());
            mBytes -= last->bytes;
            mIndex.erase(last->key);
            mLru.erase(last);
            ++mEvictions;
        }
    }

    void FrameCache::invalidate(const std::string &source, long long fromFrame, long long toFrame)
    {
        std::lock_guard<std::mutex> lk(mMu);
        for (auto it = mLru.begin(); it != mLru.end();)
        {
            if (it->key.sourceFrame >= fromFrame && it->key.sourceFrame <= toFrame && it->key.source == source)
            {
                mBytes -= it->bytes;
                mIndex.erase(it->key);
                it = mLru.erase(it);
                continue;
            }
            ++it;
        }
    }

    void FrameCache::clear()
    {
        std::lock_guard<std::mutex> lk(mMu);
        mLru.clear();
        mIndex.clear();
        mBytes = 0;
    }

    std::size_t FrameCache::residentBytes() const { std::lock_guard<std::mutex> lk(mMu); return mBytes; }
    std::size_t FrameCache::entries() const { std::lock_guard<std::mutex> lk(mMu); return mIndex.size(); }
    uint64_t FrameCache::hits() const { std::lock_guard<std::mutex> lk(mMu); return mHits; }
    uint64_t FrameCache::misses() const { std::lock_guard<std::mutex> lk(mMu); return mMisses; }
    uint64_t FrameCache::evictions() const { std::lock_guard<std::mutex> lk(mMu); return mEvictions; }

    FrameCache::Stats FrameCache::stats() const
    {
        std::lock_guard<std::mutex> lk(mMu);
        Stats s;
        s.residentBytes = mBytes;
        s.entries = mIndex.size();
        s.hits = mHits;
        s.misses = mMisses;
        s.evictions = mEvictions;
        return s;
    }
}
}
}
