#include "OmpPin.h"

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

        const char *gWhich = nullptr;
        SetNumThreads gSet = resolve(gWhich);
    }

    void pinNestedOpenMPForThisThread()
    {
        if (gSet) gSet(1);   // per-thread ICV: this call binds THIS thread's teams only
    }

    const char *ompPinStatus()
    {
        if (!gSet) return "no OpenMP runtime loaded — no nested team to pin (R-CPU-2c inert here)";
#ifdef _WIN32
        return gWhich ? "nested OpenMP teams pinned to 1 per decode worker (via a loaded OpenMP DLL)"
                      : "nested OpenMP teams pinned to 1 per decode worker";
#else
        return "nested OpenMP teams pinned to 1 per decode worker (omp_set_num_threads via libgomp)";
#endif
    }
}
}
