/*
 *  Arstro ImageProcessing Library
 *
 *  GlComputeBackend: a concrete IComputeBackend that runs the per-pixel colour/tone
 *  point ops on the GPU via OpenGL 4.3 compute shaders over a headless context (no
 *  visible window/swapchain needed). Compiled only when the umbrella defines
 *  ARSTRO_GL_COMPUTE; otherwise this file is empty and the factory in
 *  ComputeBackend.cpp returns nullptr (CPU-only).
 *
 *  `createGlComputeAccelerator()` has TWO implementations behind the SAME macro,
 *  selected by the source file that is actually compiled for the target OS (both
 *  share the accepted subset and per-pixel GLSL from GlComputeShared.h, so they
 *  can never drift from each other):
 *    - GlComputeBackend.cpp (this pair's .cpp, `!_WIN32`) — Linux/AMD (Mesa
 *      radeonsi), also Intel/NVIDIA GL and Mesa llvmpipe (software) as a fallback.
 *      Context: a surfaceless EGL context.
 *    - WglComputeBackend.cpp (`_WIN32`) — native Windows, any vendor's desktop GL
 *      ICD (NVIDIA/AMD/Intel). Context: WGL over a hidden message-only window
 *      (Windows has no EGL by default, so this is the native substitute for the
 *      "headless GL context" that EGL's surfaceless extension gives Linux).
 *  Both accelerate the subset they can reproduce faithfully (exposure + contrast +
 *  white balance + the sRGB encode) and DECLINE (return false -> CPU) any edit
 *  outside that subset; the abstraction is designed to grow stage-by-stage.
 */
#pragma once
#include "ComputeBackend.h"
#include <memory>

namespace arstro
{
#ifdef ARSTRO_GL_COMPUTE
    /** The OpenGL 4.3 compute accelerator (or nullptr if the toolchain is present
     *  but a context can't be created — callers should still null-check). */
    std::unique_ptr<IComputeBackend> createGlComputeAccelerator();
#endif
}
