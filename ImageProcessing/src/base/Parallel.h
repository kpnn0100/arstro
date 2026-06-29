/*
 *  Arstro ImageProcessing Library
 *
 *  Parallel: data-parallel hardware acceleration for the pixel pipeline. par::parallelFor
 *  splits a row range across CPU cores so per-pixel/per-row work (the engine's hot loops)
 *  uses all available hardware. Pure compute, no UI. Enabled when ARSTRO_ENABLE_THREADS is
 *  defined (native, with -pthread); otherwise it runs serially (e.g. the single-threaded
 *  web build). Thread count is settable (0 = auto = hardware_concurrency).
 *
 *  Correctness contract: the callback for a chunk [begin,end) must touch only its own rows
 *  of the OUTPUT and read-only shared state — every parallelized loop here is row-
 *  independent, so serial and parallel results are byte-identical.
 */
#pragma once
#include <algorithm>
#ifdef ARSTRO_ENABLE_THREADS
#include <thread>
#include <vector>
#endif

namespace arstro
{
namespace par
{
    inline int &threadsRef() { static int n = 0; return n; }  // 0 = auto
    inline void setThreads(int n) { threadsRef() = n < 0 ? 0 : n; }

    inline int threads()
    {
        const int n = threadsRef();
        if (n > 0) return n;
#ifdef ARSTRO_ENABLE_THREADS
        const unsigned hc = std::thread::hardware_concurrency();
        return hc ? (int)hc : 4;
#else
        return 1;
#endif
    }

    /** Run fn(begin,end) over contiguous chunks covering [0,count); blocks until done. */
    template <class Fn>
    inline void parallelFor(int count, Fn fn)
    {
        if (count <= 0) return;
#ifdef ARSTRO_ENABLE_THREADS
        int t = threads();
        if (t <= 1 || count < 2 * t) { fn(0, count); return; }
        const int chunk = (count + t - 1) / t;
        std::vector<std::thread> pool;
        pool.reserve(t - 1);
        for (int k = 1; k < t; ++k)
        {
            const int b = k * chunk, e = std::min(count, b + chunk);
            if (b < e) pool.emplace_back([&fn, b, e] { fn(b, e); });
        }
        fn(0, std::min(count, chunk));  // this thread takes chunk 0
        for (auto &th : pool) th.join();
#else
        fn(0, count);
#endif
    }
}
}
