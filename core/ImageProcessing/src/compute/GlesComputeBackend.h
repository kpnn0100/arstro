/*
 *  Arstro ImageProcessing Library
 *
 *  GlesComputeBackend: the OpenGL ES 3.1 sibling of GlComputeBackend, for Android and for
 *  any GLES-only platform — notably ARM Linux boards whose vendor driver speaks only GLES
 *  (RK3588 / Mali-G610 under libmali, R-GPU-7). Same accepted subset and per-pixel math
 *  (shared via GlComputeShared.h) — only the EGL context and the shader header
 *  (`#version 310 es` + precision) differ. The display is found headlessly (Mesa
 *  surfaceless -> default display -> GBM over a DRM node), the context is surfaceless
 *  where offered else a 1x1 pbuffer, and the GL entry points are loaded through
 *  eglGetProcAddress so the build links only EGL. Compiled when the build defines
 *  ARSTRO_GLES_COMPUTE; otherwise this file is empty and the factory does without it.
 *  The CPU pipeline stays the reference + guaranteed fallback.
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
