#include "OmpPin.h"

#include <atomic>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace arstro
{
namespace cosmo_v2
{
    namespace
    {
        using SetNumThreads = void (*)(int);

        // Resolved once. Whichever OpenMP runtime LibRaw dragged in is already loaded by
        // the time we look, so this is a lookup in the process, not a load.
        SetNumThreads resolve(const char *&whichOut)
        {
#ifdef _WIN32
            // MSYS2/MinGW builds link libgomp; MSVC builds would use vcomp; Intel's
            // runtime turns up alongside some vendor SDKs. Try each by name.
            static const char *kDlls[] = {"libgomp-1.dll", "vcomp140.dll", "libiomp5md.dll"};
            for (const char *dll : kDlls)
                if (HMODULE h = GetModuleHandleA(dll))
                    if (FARPROC p = GetProcAddress(h, "omp_set_num_threads"))
                    {
                        whichOut = dll;
                        return reinterpret_cast<SetNumThreads>(p);
                    }
            whichOut = nullptr;
            return nullptr;
#else
            // RTLD_DEFAULT searches everything already loaded, which is exactly the
            // question being asked: did anything in this process bring OpenMP in?
            void *p = dlsym(RTLD_DEFAULT, "omp_set_num_threads");
            whichOut = p ? "libgomp" : nullptr;
            return reinterpret_cast<SetNumThreads>(p);
#endif
        }

        // R-CPU-2(c): "an explicit user value still wins". The previous version called
        // gSet(1) unconditionally, so a user who set OMP_NUM_THREADS=4 was silently
        // overridden to 1 — the requirement said otherwise, and this is the half of D-41
        // that is a promise broken rather than a promise unkept. Read here in the HOST
        // layer, where getenv is allowed; libgomp has already applied the same value in its
        // load-time constructor, so honouring it costs nothing and changes nothing.
        //
        // 0 means "the user said nothing", not "the user said zero" — the caller's teamSize
        // then decides.
        int resolveUserOverride()
        {
            if (const char *env = std::getenv("OMP_NUM_THREADS"))
            {
                // Only a plain positive integer. OMP_NUM_THREADS may be a comma-separated
                // list for nested levels, and second-guessing that is not this pin's job:
                // anything cosmo cannot read as one number is left to libgomp entirely.
                char *end = nullptr;
                const long n = std::strtol(env, &end, 10);
                if (end && *end == '\0' && n >= 1 && n < 1024) return (int)n;
            }
            return 0;
        }

        const char *gWhich = nullptr;
        SetNumThreads gSet = resolve(gWhich);
        const int gUserOverride = resolveUserOverride();
        std::atomic<int> gPinnedThreads{0};
    }

    void pinNestedOpenMPForThisThread(int teamSize)
    {
        // Counted once per thread, so ompPinnedThreadCount() answers "how many distinct
        // threads did this actually bind" rather than "how many times was it called" — the
        // first is the number R-CPU-4 wants read back, the second is noise. The count is
        // kept even when no runtime is loaded: "the pin ran on every decoding thread" is a
        // claim worth checking on the platforms where there is nothing to pin too, and it
        // is the only part of this mechanism that is verifiable there.
        thread_local bool counted = false;
        if (!counted)
        {
            counted = true;
            gPinnedThreads.fetch_add(1);
        }
        // The user's own value outranks whatever the caller worked out (R-CPU-2c). Not
        // re-read per call: libgomp parsed it once in a load-time constructor and so did we.
        const int n = gUserOverride > 0 ? gUserOverride : (teamSize < 1 ? 1 : teamSize);
        if (gSet) gSet(n);   // per-thread ICV: this call binds THIS thread's teams only
    }

    const char *ompPinStatus()
    {
        // Built once and kept, because callers take a const char *. It describes the
        // MECHANISM — whether the pin can act at all, and via what — and deliberately does
        // not quote a team size: the size is per call now, so a single number here would be
        // exactly the sort of unverifiable claim R-CPU-4 was amended to forbid. The number
        // that IS reported is ompPinnedThreadCount(), which is measured.
        static const std::string s = [] {
            if (!gSet) return std::string("no OpenMP runtime loaded — no nested team to pin "
                                          "(R-CPU-2c inert here)");
            std::string out = "nested OpenMP teams sized per decoding thread";
            if (gWhich) { out += " (via "; out += gWhich; out += ")"; }
            if (gUserOverride > 0)
                out += "; OMP_NUM_THREADS=" + std::to_string(gUserOverride) +
                       " is yours and wins (R-CPU-2c)";
            return out;
        }();
        return s.c_str();
    }

    int ompPinnedThreadCount() { return gPinnedThreads.load(); }
    int ompPinUserOverride() { return gUserOverride; }
}
}
