/*
 *  Arstro ImageProcessing Library
 *
 *  Shared, GL-flavour-independent pieces of the OpenGL compute backends: the accepted-
 *  edit predicate (`computeSupports`) and the compute-shader body (`shaderBody`, the GLSL
 *  after the `#version` line). The desktop backend (GlComputeBackend.cpp, `#version 430`)
 *  and the GLES 3.1 backend (GlesComputeBackend.cpp, `#version 310 es` + precision) both
 *  use these so the accepted subset and the per-pixel math can never drift between them.
 *
 *  Platform-free (only EditParams). No GL/EGL includes here.
 */
#pragma once
#include "../base/CurvePoint.h"
#include "../engine/EditParams.h"
#include <vector>

namespace arstro
{
namespace glcompute
{
    // The compute-shader body: exposure -> contrast -> white balance (per colour channel,
    // linear) then the sRGB encode, reproducing the CPU point ops (Exposure/Contrast/
    // WhiteBalance) and color::srgbEncode. Writes BOTH the linear result (pre-curve/pre-
    // mixer histogram taps, equal to the final image while the later stages are identity)
    // and the encoded result. Concatenate after a version+precision header per GL flavour.
    inline const char *shaderBody()
    {
        return R"GLSL(
layout(local_size_x = 256) in;
layout(std430, binding = 0) readonly  buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Lin { float lin[]; };
layout(std430, binding = 2) writeonly buffer Enc { float enc[]; };
uniform int   uCount;
uniform int   uCh;
uniform int   uColorCh;
uniform float uExp;
uniform float uSlope;
uniform float uPivot;
uniform vec3  uWb;
float srgb(float v) {
    if (v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    if (v <= 0.0031308) return 12.92 * v;
    return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uCount)) return;
    uint base = i * uint(uCh);
    for (int c = 0; c < uCh; ++c) {
        float x = src[base + uint(c)];
        if (c < uColorCh) {              // colour channels: exposure, contrast, WB
            x = x * uExp;
            x = (x - uPivot) * uSlope + uPivot;
            if (uColorCh >= 3) x = x * uWb[c];   // WB is a no-op on grayscale (matches CPU)
        }
        lin[base + uint(c)] = x;
        enc[base + uint(c)] = (c < uColorCh) ? srgb(x) : x;  // alpha passes through un-encoded
    }
}
)GLSL";
    }

    // True only when the edit is entirely within the GPU-ported subset: exposure /
    // contrast / temperature / tint may vary; EVERY other stage must be at its default
    // (identity), so a decline -> CPU covers the rest.
    inline bool computeSupports(const EditParams &p)
    {
        const bool toneRegionsId = p.highlights == 0 && p.shadows == 0 && p.whites == 0 && p.blacks == 0;
        const bool presenceId = p.vibrance == 0 && p.saturation == 0 && p.texture == 0 && p.clarity == 0;
        const bool effectsId = p.dehaze == 0 && p.grainAmount == 0;
        const bool detailId = p.sharpenAmount == 0 && p.nrLuminance == 0 && p.nrColor == 0;
        const bool lensId = p.lensDistortion == 0 && p.lensCA == 0 && p.lensVignette == 0;
        const bool geomId = p.rotation == 0 && p.quarterTurns == 0 &&
                            p.cropX == 0 && p.cropY == 0 && p.cropW == 1 && p.cropH == 1;
        auto curveIsId = [](const std::vector<CurvePoint> &c)
        {
            return c.size() == 2 && c[0].x == 0 && c[0].y == 0 && c[1].x == 1 && c[1].y == 1;
        };
        const bool curveId = curveIsId(p.curve) && curveIsId(p.curveChannel[0]) &&
                             curveIsId(p.curveChannel[1]) && curveIsId(p.curveChannel[2]);
        const bool mixerId = p.mixer[0].empty() && p.mixer[1].empty() && p.mixer[2].empty();
        bool gradeId = p.balance == 0 && !p.remapEnable;
        for (int r = 0; r < 3 && gradeId; ++r)
            gradeId = p.grade[r].hue == 0 && p.grade[r].sat == 0 && p.grade[r].lum == 0;
        return toneRegionsId && presenceId && effectsId && detailId && lensId && geomId &&
               curveId && mixerId && gradeId && p.masks.empty();
    }
}  // namespace glcompute
}  // namespace arstro
