#include "ComputeBackend.h"
#include "GlComputeBackend.h"

namespace arstro
{
    // The ONE place a concrete backend is registered — keeping GPU support a
    // per-platform add-on that touches neither the engine nor the UI (Open/Closed).
    // Today: the OpenGL 4.3 compute backend on platforms that build it
    // (ARSTRO_GL_COMPUTE — Linux/AMD/Intel/NVIDIA via Mesa/desktop GL, with an
    // llvmpipe software fallback); nullptr (CPU reference path) otherwise. Other
    // APIs (Vulkan/Metal/D3D/WebGPU) can be selected here per platform later.
    std::unique_ptr<IComputeBackend> createComputeAccelerator()
    {
#ifdef ARSTRO_GL_COMPUTE
        return createGlComputeAccelerator();
#else
        return nullptr;
#endif
    }
}
