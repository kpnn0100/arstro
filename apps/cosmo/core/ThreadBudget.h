/*
 *  Arstro cosmo_core — ThreadBudget: the ONE owner of the user's CPU budget.
 *
 *  R-SVC-10 exists because there used to be no owner. `AppSettings::workersFor()` is a
 *  pure function, and each consumer called it for itself: the decode pool converted the
 *  percentage in the GTK host, the engine's Auto thread count converted the same
 *  percentage in App. They run CONCURRENTLY — R-LOADPERF-3 streams decoded images into a
 *  live editor — so a load scheduled about twice what the user asked for (D-11): 13 of 24
 *  cores at a 25% budget, and 17 of 16 on a 16-core box at the default 50%.
 *
 *  The fix is not better arithmetic, it is single ownership. The percentage is converted
 *  ONCE into `total()`, and every consumer is handed a slice of that one number:
 *
 *      total       = clamp(round(cores * percent / 100), 1, cores)
 *      decode pool = clamp(total - kEngineFloor, 1, kMaxDecodeWorkers)   [reserved for a load]
 *      engine      = max(1, total - reserved)                            [what is left]
 *
 *  So the engine gets the WHOLE budget when nothing is loading and shrinks to what the
 *  load leaves while one is running, which is also why the split is a reservation rather
 *  than a fixed ratio. The floor is 1 on both sides: previews never stop entirely, and a
 *  load always makes progress (R-CPU-1's "never zero").
 *
 *  It owns the engine's thread count outright — `apply()` is the only place that calls
 *  `par::setThreads` for the budget. An explicit user CPU-threads choice (2/4/8) still
 *  wins for the engine per R-CPU-2b, and it is allowed to exceed the budget: it is a
 *  deliberate override, so the load logs both numbers rather than silently clamping one.
 *
 *  Peak concurrency is MEASURED, not assumed (R-CPU-4 as amended): producers announce
 *  themselves, so `peakDecode()` is the real high-water mark and a test can assert
 *  `peakDecode() + engineThreads() <= total()`. The previous version of this feature was
 *  "verified" by a log line nobody had ever seen.
 *
 *  No logging and no getenv: cosmo_core stays portable, so the host reads these numbers
 *  and prints them.
 */
#pragma once
#include <atomic>

namespace arstro
{
namespace cosmo
{
    class ThreadBudget
    {
    public:
        /** Beyond ~8 decode workers they contend for memory bandwidth instead of adding
         *  throughput (R-LOADPERF-1a / R-CPU-5), so the cap can bind before the budget. */
        static constexpr int kMaxDecodeWorkers = 8;
        /** The engine never drops to zero threads, even mid-load: an arriving photo still
         *  has to render a preview, and R-LOADPERF-3 shows the editor while the load runs. */
        static constexpr int kEngineFloor = 1;

        /** `cores <= 0` means "ask the machine" (hardware_concurrency, 4 if it cannot say). */
        explicit ThreadBudget(int percent = 50, int cores = 0);

        void setPercent(int percent);              // R-CPU-3: takes effect on the next load/render
        /** 0 = Auto (follow the budget); 2/4/8 = the user's explicit override (R-CPU-2b). */
        void setExplicitEngineThreads(int n);

        int percent() const { return mPercent; }
        int cores() const { return mCores; }
        int explicitEngineThreads() const { return mExplicit; }

        /** The one number every other number is a slice of. */
        int total() const;
        /** What `beginLoad()` would reserve, without reserving it. */
        int decodeWorkers() const;
        /** What the engine may use right now — the whole budget when idle, the remainder
         *  during a load, or the user's explicit override if they set one. */
        int engineThreads() const;

        /** Reserve the decode pool's share and apply the shrunken engine count. Returns
         *  the worker count to start. Not nestable — one load at a time (R-CPU-3). */
        int beginLoad();
        /** Release the reservation and give the engine the whole budget back. */
        void endLoad();
        bool loadActive() const { return mReserved.load() > 0; }

        /** Push `engineThreads()` into the engine. Called by the setters and by
         *  begin/endLoad; the host calls it once at startup. */
        void apply() const;

        // ── measured, not asserted (R-CPU-4 amended) ──
        /** Called by a decode producer around its work; keeps the high-water mark. */
        void producerEnter();
        void producerExit();
        int peakDecode() const { return mPeakDecode.load(); }
        void resetPeak() { mPeakDecode.store(0); }

    private:
        int mPercent = 50;
        int mCores = 4;
        int mExplicit = 0;                  // 0 = Auto
        std::atomic<int> mReserved{0};      // decode workers currently held by a load
        std::atomic<int> mActive{0};        // producers inside their work right now
        std::atomic<int> mPeakDecode{0};
    };
}
}
