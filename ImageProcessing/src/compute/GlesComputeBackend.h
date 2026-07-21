/*
 *  Arstro ImageProcessing Library
 *
 *  GlesComputeBackend: the OpenGL ES 3.1 sibling of GlComputeBackend, for Android (and
 *  any GLES-only platform). Same accepted subset and per-pixel math (shared via
 *  GlComputeShared.h) — only the EGL context (ES 3.1 over a 1x1 pbuffer) and the shader
 *  header (`#version 310 es` + precision) differ. Compiled when the build defines
 *  ARSTRO_GLES_COMPUTE and links EGL/GLESv3; otherwise this file is empty and the factory
 *  falls back to CPU. The CPU pipeline stays the reference + guaranteed fallback.
 */
#pragma once
#include "ComputeBackend.h"
#include <memory>

namespace arstro
{
#ifdef ARSTRO_GLES_COMPUTE
    /** The OpenGL ES 3.1 compute accelerator (or nullptr if a context can't be created —
     *  callers still null-check / the engine falls back to CPU). */
    std::unique_ptr<IComputeBackend> createGlesComputeAccelerator();
#endif
}
