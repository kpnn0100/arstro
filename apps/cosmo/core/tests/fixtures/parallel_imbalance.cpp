/*
 *  T1 fixture: does `par::parallelFor` absorb an uneven workload, or does it finish at
 *  the speed of its slowest share?
 *
 *  The reason parallelFor grew a pool and many-chunks-per-thread is big.LITTLE. On an
 *  RK3588 (4xA76 + 4xA55) or an Allwinner A733-class part, an A55 takes 2-3x as long as
 *  an A76 for the same slice — and parallelFor BLOCKS until every slice is done, so with
 *  EQUAL static slices every parallel pass in the library ran at little-core speed.
 *
 *  That cannot be measured on a symmetric desktop. But asymmetric CORES and asymmetric
 *  WORK are the same scheduling problem, and asymmetric work can be created anywhere: this
 *  gives the first 1/T of the range 3x the cost of the rest, which is exactly what one
 *  slow core out of T looks like to the scheduler. So the number below predicts the SBC
 *  behaviour on hardware you already have.
 *
 *      ideal    = total work / threads                  (perfect balance)
 *      static   = the cost of the single worst equal slice   (the old floor)
 *
 *  A dynamic split should land near `ideal`; a static one cannot beat `static`.
 *
 *  Build:
 *      g++ -O2 -std=c++17 -DARSTRO_ENABLE_THREADS -I core/ImageProcessing/src \
 *          -o /tmp/imbalance apps/cosmo/core/tests/fixtures/parallel_imbalance.cpp \
 *          -lpthread
 *      /tmp/imbalance [rows] [threads...]
 */
#include "base/Parallel.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace arstro;
using Clock = std::chrono::steady_clock;

static double ms(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

namespace
{
    // Real arithmetic, not a sleep: a sleeping "core" would let the OS run something else
    // and would not reproduce the contention a busy core creates.
    volatile double sink = 0.0;

    inline void burn(int reps)
    {
        double acc = 0.0;
        for (int r = 0; r < reps; ++r)
            for (int k = 0; k < 20000; ++k)   // sized so one run is tens of ms, not noise
                acc += (double)k * 1.0000001 + acc * 1e-12;
        sink = acc;
    }

    /** Exactly what parallelFor USED to do: spawn `t` fresh threads, one EQUAL slice
     *  each, join. Run for real rather than computed, so the comparison is a measurement
     *  of two implementations and not of one implementation against arithmetic. */
    template <class Fn>
    void staticEqualSplit(int count, int t, Fn fn)
    {
        if (t <= 1 || count < 2 * t) { fn(0, count); return; }
        const int chunk = (count + t - 1) / t;
        std::vector<std::thread> pool;
        pool.reserve(t - 1);
        for (int k = 1; k < t; ++k)
        {
            const int b = k * chunk, e = std::min(count, b + chunk);
            if (b < e) pool.emplace_back([&fn, b, e] { fn(b, e); });
        }
        fn(0, std::min(count, chunk));
        for (auto &th : pool) th.join();
    }
}

int main(int argc, char **argv)
{
    const int rows = argc > 1 ? std::atoi(argv[1]) : 1066;   // a 1600 px preview's height
    std::vector<int> threadCounts;
    for (int i = 2; i < argc; ++i) threadCounts.push_back(std::atoi(argv[i]));
    if (threadCounts.empty()) threadCounts = {2, 4, 8, 16, 24};

    std::printf("parallelFor imbalance — %d rows, the first 1/T of them 3x the cost\n", rows);
    std::printf("(3x on 1/T of the range == what ONE slow core out of T looks like)\n\n");
    std::printf("  %-8s %10s %10s %10s   %s\n", "threads", "pool", "ideal", "equal", "gain");
    std::printf("  %-8s %10s %10s %10s\n", "", "(new)", "(perfect)", "(old, run)");

    for (int t : threadCounts)
    {
        par::setThreads(t);
        const int heavyRows = rows / t;          // the "slow core's" share
        auto costOf = [&](int row) { return row < heavyRows ? 3 : 1; };

        long long totalUnits = 0;
        for (int r = 0; r < rows; ++r) totalUnits += costOf(r);

        // Calibrate: what does one unit cost on this machine, serially?
        par::setThreads(1);
        const auto c0 = Clock::now();
        for (int r = 0; r < rows; ++r) burn(costOf(r));
        const double serialMs = ms(c0, Clock::now());
        const double perUnit = serialMs / (double)totalUnits;

        // The two bounds. `static` is the old implementation's floor: equal slices, so the
        // slice holding the expensive rows decides when everybody is finished.
        const double idealMs = perUnit * (double)totalUnits / (double)t;
        long long worstSlice = 0;
        for (int k = 0; k < t; ++k)
        {
            const long long b = (long long)k * rows / t, e = (long long)(k + 1) * rows / t;
            long long u = 0;
            for (long long r = b; r < e; ++r) u += costOf((int)r);
            if (u > worstSlice) worstSlice = u;
        }
        const double staticPredictedMs = perUnit * (double)worstSlice;

        // ...and the same thing MEASURED, by actually running the old scheme.
        double staticMs = 1e18;
        for (int trial = 0; trial < 5; ++trial)
        {
            const auto s0 = Clock::now();
            staticEqualSplit(rows, t, [&](int b, int e) {
                for (int r = b; r < e; ++r) burn(costOf(r));
            });
            const double m = ms(s0, Clock::now());
            if (m < staticMs) staticMs = m;
        }
        (void)staticPredictedMs;

        par::setThreads(t);
        double best = 1e18;
        for (int trial = 0; trial < 5; ++trial)     // best-of, to cut scheduler noise
        {
            const auto t0 = Clock::now();
            par::parallelFor(rows, [&](int b, int e) {
                for (int r = b; r < e; ++r) burn(costOf(r));
            });
            const double m = ms(t0, Clock::now());
            if (m < best) best = m;
        }

        std::printf("  %-8d %9.2fms %9.2fms %9.2fms   %.2fx faster than the equal split\n",
                    t, best, idealMs, staticMs, staticMs / best);
    }
    par::setThreads(0);
    return 0;
}
