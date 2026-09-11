#include "FrameCache.h"
#include <algorithm>

namespace arstro
{
namespace interstellar
{
    std::string FrameCache::indexKey(const Key &k)
    {
        // One string key rather than a hashed struct: the source path is already a string, and a
        // collision here would serve the wrong picture. Cheap next to a decode.
        return k.source + '|' + std::to_string(k.sourceFrame) + '|' + std::to_string(k.paramHash) +
               '|' + std::to_string(k.level);
    }

    size_t FrameCache::KeyHash::operator()(const Key &k) const
    {
        return std::hash<std::string>{}(indexKey(k));
    }

    void FrameCache::setCapBytes(size_t bytes)
    {
        mCap = bytes;
        evictTo(mCap);
    }

    bool FrameCache::get(const Key &k, Raster &out)
    {
        auto it = mIndex.find(indexKey(k));
        if (it == mIndex.end()) { ++mMisses; return false; }
        // Promote, then COPY: the caller composites after this returns, and a reference would
        // dangle the moment the next put() evicted the entry.
        mLru.splice(mLru.begin(), mLru, it->second);
        out = it->second->frame;
        ++mHits;
        return true;
    }

    void FrameCache::put(const Key &k, const Raster &frame)
    {
        if (frame.empty()) return;
        const std::string ik = indexKey(k);
        auto existing = mIndex.find(ik);
        if (existing != mIndex.end())
        {
            mBytes -= existing->second->bytes;
            mLru.erase(existing->second);
            mIndex.erase(existing);
        }
        Entry e;
        e.key = k;
        e.frame = frame;
        e.bytes = frame.rgba.size();
        // A single frame larger than the whole cap is not cached rather than emptying the cache
        // to hold one thing.
        if (e.bytes > mCap) return;
        mLru.push_front(std::move(e));
        mIndex[ik] = mLru.begin();
        mBytes += mLru.begin()->bytes;
        evictTo(mCap);
    }

    void FrameCache::evictTo(size_t target)
    {
        while (mBytes > target && !mLru.empty())
        {
            auto last = std::prev(mLru.end());
            mBytes -= last->bytes;
            mIndex.erase(indexKey(last->key));
            mLru.erase(last);
            ++mEvictions;
        }
    }

    void FrameCache::invalidateSource(const std::string &source, long long from, long long to)
    {
        for (auto it = mLru.begin(); it != mLru.end();)
        {
            if (it->key.source == source && it->key.sourceFrame >= from && it->key.sourceFrame <= to)
            {
                mBytes -= it->bytes;
                mIndex.erase(indexKey(it->key));
                it = mLru.erase(it);
                continue;
            }
            ++it;
        }
    }

    void FrameCache::clear()
    {
        mLru.clear();
        mIndex.clear();
        mBytes = 0;
    }
}
}
