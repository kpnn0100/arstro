/*
 *  Arstrobench by arstro — the shape every measured workload returns.
 *
 *  One type, one place where the score is computed (R-SCORE-1: score = 1/seconds),
 *  so the UI renders image and signal results through identical code and knows
 *  nothing about which engine produced them.
 *
 *  UI-free: no Artboard, no GTK, no OS calls.
 */
#pragma once
#include <string>

namespace arstro
{
namespace arstrobench
{
    struct WorkloadResult
    {
        double seconds = 0.0;    ///< fastest timed pass (R-SCORE-3), processing only (R-SCORE-2)
        double score = 0.0;      ///< 1 / seconds — "workloads per second"
        double checksum = 0.0;   ///< folded output; keeps the work from being elided (R-SCORE-6)
        std::string detail;      ///< one line describing what was measured, for the card
        bool ok = false;         ///< false when the workload could not run at all

        /** The one definition of the score. A non-positive time cannot be inverted, so it
         *  yields a not-ok result rather than an infinity the UI would have to special-case. */
        static WorkloadResult fromSeconds(double seconds)
        {
            WorkloadResult r;
            if (seconds > 0.0)
            {
                r.seconds = seconds;
                r.score = 1.0 / seconds;
                r.ok = true;
            }
            return r;
        }
    };
}
}
