/*
 *  Arstrobench by arstro — the machine the score was measured on (R-SYS).
 *
 *  A score without a machine attached is not comparable to anything, so every result
 *  carries the chip, the memory and the operating system. Read through the platform's
 *  own API/filesystem, never by spawning a tool (R-SYS-3).
 *
 *  UI-free: no Artboard, no GTK.
 */
#pragma once
#include <cstdint>
#include <string>

namespace arstro
{
namespace arstrobench
{
    struct SystemInfo
    {
        std::string chip = "Unknown";  ///< CPU model string
        int threads = 0;               ///< hardware threads (0 = could not determine)
        uint64_t ramBytes = 0;         ///< total physical memory
        std::string os = "Unknown";    ///< human-readable OS name + version

        /** Query this machine. Never throws; unknown fields keep their "Unknown"/0
         *  default so the UI always has a row to draw (R-SYS-2). */
        static SystemInfo query();

        /** "AMD Ryzen 9 5900X  ·  24 threads" — the thread count is part of the chip's
         *  identity for a benchmark, so it is folded into that one row. */
        std::string chipText() const;
        /** "31.3 GB", or "Unknown" when the query failed. */
        std::string ramText() const;
    };
}
}
