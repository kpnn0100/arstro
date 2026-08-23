/*
 *  Cosmo by arstro — how big the engine's pixel caches may be, on THIS machine (R-MEM-1/5).
 *
 *  `EditEngine` caps its two pixel pools in bytes, and the numbers it defaults to (800 MB of
 *  full-resolution sources, 1 GB of preview proxies) were literals chosen blind. That is how
 *  D-44 happened: a 26 MB proxy against a 1 GB cap holds 37 photos, so browsing a 120-photo
 *  project re-decoded on 8 hops in 10 — about a second each — while the machine it ran on had
 *  27.7 GB and was using under 1 GB of it.
 *
 *  A cache sized by a constant is either wasteful on a workstation or fatal on a laptop, and the
 *  same constant cannot be both. So the host asks the OS how much memory exists and hands the
 *  engine a share of it. Host layer because that is the only layer allowed a platform call —
 *  `cosmo_core` and `arstro_image` carry no OS code — and `setMemoryCaps` already exists as the
 *  seam, so this adds a policy, not a mechanism.
 *
 *  The split favours PROXIES over sources, and by a lot, because they are what browsing touches:
 *  a proxy is ~14x smaller than the source it came from, so the same bytes buy fourteen times as
 *  many photos you can step onto instantly. Full-resolution sources are needed only by export and
 *  by the slot being rendered, and R-MEM-2 re-decodes one when it is not there.
 */
#pragma once
#include <algorithm>
#include <cstddef>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace arstro
{
namespace cosmo_v2
{
    /** Total physical RAM in bytes, or 0 when the platform will not say. */
    inline size_t physicalMemoryBytes()
    {
#ifdef _WIN32
        MEMORYSTATUSEX st;
        st.dwLength = sizeof(st);
        if (GlobalMemoryStatusEx(&st)) return (size_t)st.ullTotalPhys;
        return 0;
#else
        const long pages = sysconf(_SC_PHYS_PAGES);
        const long pageSize = sysconf(_SC_PAGE_SIZE);
        if (pages > 0 && pageSize > 0) return (size_t)pages * (size_t)pageSize;
        return 0;
#endif
    }

    struct PixelCaps
    {
        size_t sourceBytes = 0;
        size_t proxyBytes = 0;
    };

    /** The share of the machine cosmo's pixel caches may hold.
     *
     *  ~8% for sources and ~20% for proxies, floored so a small machine still caches something
     *  and capped so a very large one does not simply keep everything forever (an unbounded
     *  cache is what R-MEM exists to stop, and "the machine is big" is not a reason to stop
     *  bounding it). On a 27.7 GB box that is 2.2 GB of sources and 5.5 GB of proxies — the
     *  latter holds ~210 previews at 1600 px, so a 120-photo rack fits entirely and D-44's
     *  re-decodes disappear. On an 8 GB laptop it is 0.6 GB / 1.6 GB, which still holds ~60.
     *
     *  When the platform will not report its memory, fall back to the engine's own defaults
     *  rather than guessing high — the failure mode of guessing high is the swapping that
     *  started all of this. */
    inline PixelCaps pixelCapsFor(size_t physicalBytes)
    {
        constexpr size_t kMB = 1024ull * 1024ull;
        PixelCaps c;
        if (physicalBytes == 0) { c.sourceBytes = 800 * kMB; c.proxyBytes = 1024 * kMB; return c; }
        c.sourceBytes = std::min<size_t>(std::max<size_t>(physicalBytes / 12, 256 * kMB), 4096 * kMB);
        c.proxyBytes = std::min<size_t>(std::max<size_t>(physicalBytes / 5, 512 * kMB), 8192 * kMB);
        return c;
    }

    inline PixelCaps pixelCapsForThisMachine() { return pixelCapsFor(physicalMemoryBytes()); }
}
}
