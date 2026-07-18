#include "ComputeBackend.h"

namespace arstro
{
    // No concrete GPU backend is built yet, so the engine runs on the CPU reference
    // path (R-GPU-2). Each platform will return its own backend from here later,
    // guarded by the platform macro (see the header for the shape). Keeping this the
    // ONE place a concrete backend is registered is what makes GPU support a
    // per-platform add-on that touches neither the engine nor the UI (Open/Closed).
    std::unique_ptr<IComputeBackend> createComputeAccelerator()
    {
        return nullptr;
    }
}
