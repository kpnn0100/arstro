#include "Thumbnailer.h"
#include "HostFrameSource.h"
#include <algorithm>
#include <chrono>
#include <cmath>

namespace arstro
{
namespace interstellar_host
{
    void boxDownscale(const interstellar::Raster &in, int edge, interstellar::Raster &out)
    {
        const int longEdge = std::max(in.width, in.height);
        if (edge <= 0 || longEdge <= edge) { out = in; return; }
        const double s = (double)edge / longEdge;
        const int ow = std::max(1, (int)std::lround(in.width * s));
        const int oh = std::max(1, (int)std::lround(in.height * s));
        out.allocate(ow, oh);
        for (int y = 0; y < oh; ++y)
        {
            const int y0 = y * in.height / oh, y1 = std::max(y0 + 1, (y + 1) * in.height / oh);
            for (int x = 0; x < ow; ++x)
            {
                const int x0 = x * in.width / ow, x1 = std::max(x0 + 1, (x + 1) * in.width / ow);
                unsigned sum[4] = {0, 0, 0, 0};
                for (int yy = y0; yy < y1; ++yy)
                {
                    const uint8_t *p = in.rgba.data() + ((size_t)yy * in.width + x0) * 4;
                    for (int xx = x0; xx < x1; ++xx, p += 4)
                        for (int c = 0; c < 4; ++c) sum[c] += p[c];
                }
                const unsigned n = (unsigned)((y1 - y0) * (x1 - x0));
                uint8_t *o = out.rgba.data() + ((size_t)y * ow + x) * 4;
                for (int c = 0; c < 4; ++c) o[c] = (uint8_t)((sum[c] + n / 2) / n);
            }
        }
    }

    Thumbnailer::Thumbnailer(Mode mode) : mMode(mode)
    {
        if (mMode == Mode::Async) mThread = std::thread([this] { worker(); });
    }

    Thumbnailer::~Thumbnailer()
    {
        {
            std::lock_guard<std::mutex> l(mMu);
            mStop = true;
        }
        mCv.notify_all();
        if (mThread.joinable()) mThread.join();
    }

    void Thumbnailer::clear()
    {
        std::lock_guard<std::mutex> l(mMu);
        mCache.clear();
    }

    bool Thumbnailer::decode(const Key &k, interstellar::Raster &out)
    {
        const std::string &path = std::get<0>(k);
        auto it = mSources.find(path);
        if (it == mSources.end())
        {
            if (mSources.size() >= 6) mSources.erase(mSources.begin());   // a bound on open decoders
            it = mSources.emplace(path, std::unique_ptr<interstellar::IFrameSource>(new HostFrameSource())).first;
            interstellar::IFrameSource::Info info;
            if (!it->second->open(path, info) || !info.valid()) { mSources.erase(it); return false; }
            mInfo[path] = info;
        }
        const interstellar::IFrameSource::Info &info = mInfo[path];
        const double t = std::get<1>(k) / 1000.0;
        const long long frame = info.frames <= 1 ? 0 : (long long)std::llround(t * (info.fps > 0 ? info.fps : 24.0));
        interstellar::Raster full;
        if (!it->second->frameAt(std::clamp<long long>(frame, 0, std::max<long long>(0, info.frames - 1)), full)) return false;
        boxDownscale(full, std::get<2>(k), out);
        return !out.empty();
    }

    bool Thumbnailer::get(const std::string &mediaPath, double t, int edge, interstellar::Raster &out)
    {
        const Key key(mediaPath, (long long)std::llround(t * 1000.0), edge);
        if (mMode == Mode::Sync)
        {
            auto c = mCache.find(key);
            if (c == mCache.end())
            {
                interstellar::Raster r;
                decode(key, r);
                c = mCache.emplace(key, std::move(r)).first;
            }
            out = c->second;
            return !out.empty();
        }
        std::lock_guard<std::mutex> l(mMu);
        const auto c = mCache.find(key);
        if (c != mCache.end()) { out = c->second; return !out.empty(); }
        if (mQueued.insert(key).second)
        {
            mQueue.push_back(key);
            mCv.notify_one();
        }
        return false;
    }

    void Thumbnailer::worker()
    {
        for (;;)
        {
            Key k;
            {
                std::unique_lock<std::mutex> l(mMu);
                mBusy = false;
                if (mQueue.empty()) mIdleCv.notify_all();
                mCv.wait(l, [this] { return mStop || !mQueue.empty(); });
                if (mStop) return;
                k = mQueue.front();
                mQueue.pop_front();
                mBusy = true;
            }
            interstellar::Raster r;
            decode(k, r);   // off the lock: the UI thread keeps answering from the cache
            {
                std::lock_guard<std::mutex> l(mMu);
                mCache[k] = std::move(r);
                mQueued.erase(k);
            }
            mEpoch.fetch_add(1);
        }
    }

    bool Thumbnailer::waitIdle(int ms)
    {
        if (mMode == Mode::Sync) return true;
        std::unique_lock<std::mutex> l(mMu);
        return mIdleCv.wait_for(l, std::chrono::milliseconds(ms), [this] { return mQueue.empty() && !mBusy; });
    }
}
}
