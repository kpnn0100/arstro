/*
 *  Arstro ImageProcessing Library
 *
 *  GlComputeBackend: a concrete IComputeBackend that runs the per-pixel colour/tone
 *  point ops on the GPU via OpenGL 4.3 compute shaders over a surfaceless EGL
 *  context (no window/display needed — headless). Compiled only when the umbrella
 *  defines ARSTRO_GL_COMPUTE and links EGL/GL; otherwise this file is empty and the
 *  factory in ComputeBackend.cpp returns nullptr (CPU-only).
 *
 *  This is the Linux/AMD (Mesa radeonsi) backend — it also runs on Intel/NVIDIA GL
 *  and on Mesa llvmpipe (software) as a fallback. It accelerates the subset it can
 *  reproduce faithfully (exposure + contrast + white balance + the sRGB encode) and
 *  DECLINES (returns false -> CPU) any edit outside that subset; the abstraction is
 *  designed to grow stage-by-stage.
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
