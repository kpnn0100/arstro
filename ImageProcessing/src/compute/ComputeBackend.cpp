#include "ComputeBackend.h"
#include "GlComputeBackend.h"
#include "GlesComputeBackend.h"

namespace arstro
{
    // The ONE place a concrete backend is registered — keeping GPU support a
    // per-platform add-on that touches neither the engine nor the UI (Open/Closed).
    // Today: the OpenGL 4.3 compute backend on desktop platforms that build it
    // (ARSTRO_GL_COMPUTE — Linux/AMD/Intel/NVIDIA via Mesa/desktop GL, with an
    // llvmpipe software fallback), or the OpenGL ES 3.1 backend on Android
    // (ARSTRO_GLES_COMPUTE); nullptr (CPU reference path) otherwise. Other APIs
    // (Vulkan/Metal/D3D/WebGPU) can be selected here per platform later. Desktop GL is
    // preferred when both are somehow built (e.g. the unit-test build enables both to
    // exercise each backend directly).
    std::unique_ptr<IComputeBackend> createComputeAccelerator()
    {
#if defined(ARSTRO_GL_COMPUTE)
        return createGlComputeAccelerator();
#elif defined(ARSTRO_GLES_COMPUTE)
        return createGlesComputeAccelerator();
#else
        return nullptr;
#endif
    }
}
