/*
 *  Arstro ImageProcessing Library
 *
 *  The multi-pass GPU pipeline's kernels (desktop GL 4.3 compute), one GLSL program per pass. Each
 *  reproduces a CPU stage step for step (R-GPU-2: the CPU is the reference): the same formulas, the
 *  same edge rules, the same constants — and where the CPU derives a table (a tone curve, the mixer's
 *  per-hue curves) the backend uploads the CPU's own table rather than rebuilding it. The transfer
 *  functions here are the closed forms the CPU's tables are built from (their error is ~1.8e-5, far
 *  under one 8-bit code value).
 *
 *  Passes and the stages they reproduce (canonical order, EditEngine::buildPipeline):
 *    point_pre   Exposure · Contrast · ToneRegions · WhiteBalance (fused: each is per pixel)
 *    curve       ToneCurve (master then per channel, optional log domain)
 *    lum         the luminance plane and its sRGB encode (Texture and Clarity's input)
 *    blur_h/v    the exact separable Gaussian of spatial::gaussianBlurPlane (clamped edges)
 *    box_h/v     one box pass of spatial::fastBlurPlane's cascade (clamped edges)
 *    local       Texture / Clarity's combine (Clarity with its midtone gate)
 *    vibrance    Vibrance (HSL saturation and vibrance)
 *    mix_weights ColorMixer: the per-hue adjustment, chroma-gated, into three planes
 *    mix_apply   ColorMixer: the adjustment applied (the spread's max-magnitude rule)
 *    grading     ColorGrading (three wheels, balance, hue remap)
 *    encode      the sRGB encode of the colour channels
 */
#pragma once
#include <string>

namespace arstro
{
namespace glpipe
{
    inline const char *header()
    {
        return R"GLSL(#version 430
layout(local_size_x = 256) in;
uniform int uW;
uniform int uH;
uniform int uCh;
float srgbE(float v) {
    if (v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    if (v <= 0.0031308) return 12.92 * v;
    return 1.055 * pow(v, 1.0 / 2.4) - 0.055;
}
float srgbD(float v) {
    if (v <= 0.0) return 0.0;
    if (v >= 1.0) return 1.0;
    if (v <= 0.04045) return v / 12.92;
    return pow((v + 0.055) / 1.055, 2.4);
}
float lumOf(float r, float g, float b) { return 0.2126 * r + 0.7152 * g + 0.0722 * b; }
float smooth01(float t) { t = clamp(t, 0.0, 1.0); return t * t * (3.0 - 2.0 * t); }
vec3 rgbToHsl(vec3 c) {
    float mx = max(c.r, max(c.g, c.b)), mn = min(c.r, min(c.g, c.b)), d = mx - mn;
    float l = (mx + mn) * 0.5;
    if (d <= 1e-9) return vec3(0.0, 0.0, l);
    float s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
    float hh;
    if (mx == c.r) hh = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
    else if (mx == c.g) hh = (c.b - c.r) / d + 2.0;
    else hh = (c.r - c.g) / d + 4.0;
    float h = hh * 60.0;
    if (h >= 360.0) h -= 360.0;
    return vec3(h, s, l);
}
float hue2rgb(float p, float q, float t) {
    if (t < 0.0) t += 1.0;
    if (t > 1.0) t -= 1.0;
    if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
    if (t < 1.0 / 2.0) return q;
    if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
    return p;
}
vec3 hslToRgb(float h, float s, float l) {
    if (s <= 1e-9) return vec3(l);
    float hn = h / 360.0;
    float q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
    float p = 2.0 * l - q;
    return vec3(hue2rgb(p, q, hn + 1.0 / 3.0), hue2rgb(p, q, hn), hue2rgb(p, q, hn - 1.0 / 3.0));
}
float hueDelta(float a, float b) { return mod(b - a + 540.0, 360.0) - 180.0; }
)GLSL";
    }

    // image → image passes read binding 0 and write binding 1
    inline std::string imagePass(const char *body)
    {
        return std::string(header()) + R"GLSL(
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 1) writeonly buffer Dst { float dst[]; };
)GLSL" + body;
    }

    inline std::string pointPre()
    {
        return imagePass(R"GLSL(
uniform float uExp;
uniform float uSlope;
uniform float uPivot;
uniform vec4  uRegions;   // highlights, shadows, whites, blacks — already /100
uniform vec3  uWb;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    float v[4];
    for (int c = 0; c < uCh; ++c) v[c] = src[b + uint(c)];
    for (int c = 0; c < cc; ++c) v[c] = v[c] * uExp;                         // Exposure
    for (int c = 0; c < cc; ++c) v[c] = (v[c] - uPivot) * uSlope + uPivot;   // Contrast
    {   // ToneRegions
        float r = v[0], g = cc >= 3 ? v[1] : v[0], bl = cc >= 3 ? v[2] : v[0];
        float L = lumOf(r, g, bl);
        float wShadow = 1.0 - smooth01(L / 0.5);
        float wHigh = smooth01((L - 0.5) / 0.5);
        float wBlack = 1.0 - smooth01(L / 0.28);
        float wWhite = smooth01((L - 0.72) / 0.28);
        float delta = 0.5 * (uRegions.y * wShadow + uRegions.x * wHigh + uRegions.z * wWhite + uRegions.w * wBlack);
        for (int c = 0; c < cc; ++c) v[c] = v[c] + delta;
    }
    if (cc >= 3) { v[0] *= uWb.x; v[1] *= uWb.y; v[2] *= uWb.z; }           // WhiteBalance
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = v[c];
}
)GLSL");
    }

    inline std::string curve()
    {
        return imagePass(R"GLSL(
layout(std430, binding = 3) readonly buffer Lut { float lut[]; };   // master [0,1024), then R, G, B
uniform int uLog;
float sampleLut(int base, float d) {
    if (!(d > 0.0)) d = 0.0; else if (d > 1.0) d = 1.0;
    float f = d * 1023.0;
    int i = int(f);
    if (i < 0) i = 0;
    if (i >= 1023) return lut[base + 1023];
    float fr = f - float(i);
    return lut[base + i] * (1.0 - fr) + lut[base + i + 1] * fr;
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    for (int c = 0; c < uCh; ++c) {
        float x = src[b + uint(c)];
        if (c < cc) {
            float d = uLog != 0 ? srgbE(x) : x;
            float y = sampleLut(0, d);
            y = sampleLut(1024 * (1 + (c < 3 ? c : 2)), y);
            x = uLog != 0 ? srgbD(y) : y;
        }
        dst[b + uint(c)] = x;
    }
}
)GLSL");
    }

    inline std::string lum()
    {
        return std::string(header()) + R"GLSL(
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 2) writeonly buffer L { float lplane[]; };
layout(std430, binding = 4) writeonly buffer P { float pplane[]; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    float r = src[b], g = uCh >= 3 ? src[b + 1u] : r, bl = uCh >= 3 ? src[b + 2u] : r;
    float L = lumOf(r, g, bl);
    lplane[i] = L;
    pplane[i] = srgbE(L);
}
)GLSL";
    }

    // plane → plane passes: binding 5 in, binding 6 out
    inline std::string planePass(const char *body)
    {
        return std::string(header()) + R"GLSL(
layout(std430, binding = 5) readonly buffer PIn { float pin_[]; };
layout(std430, binding = 6) writeonly buffer POut { float pout_[]; };
uniform int uInOff;    // a plane inside a larger buffer (the mixer's three planes share one)
uniform int uOutOff;
#define pin(i) pin_[uInOff + (i)]
)GLSL" + body;
    }

    inline std::string gaussH()
    {
        return planePass(R"GLSL(
layout(std430, binding = 7) readonly buffer K { float k[]; };
uniform int uR;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float acc = 0.0;
    for (int t = -uR; t <= uR; ++t) { int xx = clamp(x + t, 0, uW - 1); acc += pin(y * uW + xx) * k[t + uR]; }
    pout_[uint(uOutOff) + i] = acc;
}
)GLSL");
    }

    inline std::string gaussV()
    {
        return planePass(R"GLSL(
layout(std430, binding = 7) readonly buffer K { float k[]; };
uniform int uR;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float acc = 0.0;
    for (int t = -uR; t <= uR; ++t) { int yy = clamp(y + t, 0, uH - 1); acc += pin(yy * uW + x) * k[t + uR]; }
    pout_[uint(uOutOff) + i] = acc;
}
)GLSL");
    }

    inline std::string boxH()
    {
        return planePass(R"GLSL(
uniform int uR;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float acc = 0.0;
    for (int t = -uR; t <= uR; ++t) acc += pin(y * uW + clamp(x + t, 0, uW - 1));
    pout_[uint(uOutOff) + i] = acc / float(2 * uR + 1);
}
)GLSL");
    }

    inline std::string boxV()
    {
        return planePass(R"GLSL(
uniform int uR;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float acc = 0.0;
    for (int t = -uR; t <= uR; ++t) acc += pin(clamp(y + t, 0, uH - 1) * uW + x);
    pout_[uint(uOutOff) + i] = acc / float(2 * uR + 1);
}
)GLSL");
    }

    inline std::string local()
    {
        return imagePass(R"GLSL(
layout(std430, binding = 2) readonly buffer L { float lplane[]; };
layout(std430, binding = 4) readonly buffer P { float pplane[]; };
layout(std430, binding = 5) readonly buffer PB { float pblur[]; };
uniform float uK;
uniform int uMidGate;     // Clarity spares shadows and highlights; Texture does not
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    float p = pplane[i];
    float gate = uMidGate != 0 ? 1.0 - abs(2.0 * p - 1.0) : 1.0;
    float newP = clamp(p + uK * (p - pblur[i]) * gate, 0.0, 1.0);
    float L = lplane[i];
    float ratio = srgbD(newP) / (L > 1e-4 ? L : 1e-4);
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = c < cc ? src[b + uint(c)] * ratio : src[b + uint(c)];
}
)GLSL");
    }

    inline std::string vibrance()
    {
        return imagePass(R"GLSL(
uniform float uVib;
uniform float uSat;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    if (uCh < 3) { for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = src[b + uint(c)]; return; }
    vec3 hsl = rgbToHsl(vec3(src[b], src[b + 1u], src[b + 2u]));
    float s = hsl.y * (1.0 + uSat);
    s = s + uVib * (1.0 - s);
    s = clamp(s, 0.0, 1.0);
    vec3 o = hslToRgb(hsl.x, s, hsl.z);
    dst[b] = o.r; dst[b + 1u] = o.g; dst[b + 2u] = o.b;
    for (int c = 3; c < uCh; ++c) dst[b + uint(c)] = src[b + uint(c)];
}
)GLSL");
    }

    inline std::string mixWeights()
    {
        return std::string(header()) + R"GLSL(
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 3) readonly buffer Lut { float lut[]; };   // hue, sat, lum: 256 each
layout(std430, binding = 8) writeonly buffer M { float mplane[]; };   // three planes, back to back
uniform ivec3 uFlat;
float sampleCyclic(int c, float hue) {
    float f = hue / 360.0 * 256.0;
    int i = int(f);
    i = ((i % 256) + 256) % 256;
    int j = (i + 1) % 256;
    float fr = f - floor(f);
    return lut[c * 256 + i] * (1.0 - fr) + lut[c * 256 + j] * fr;
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(uW * uH);
    if (i >= n) return;
    uint b = i * uint(uCh);
    vec3 hsl = rgbToHsl(vec3(src[b], src[b + 1u], src[b + 2u]));
    float chroma = hsl.y * (1.0 - abs(2.0 * hsl.z - 1.0));
    float w = clamp((chroma - 0.010) / (0.040 - 0.010), 0.0, 1.0);
    w = w * w * (3.0 - 2.0 * w);
    mplane[i] = uFlat.x != 0 ? 0.0 : sampleCyclic(0, hsl.x) * w;
    mplane[n + i] = uFlat.y != 0 ? 0.0 : sampleCyclic(1, hsl.x) * w;
    mplane[2u * n + i] = uFlat.z != 0 ? 0.0 : sampleCyclic(2, hsl.x) * w;
}
)GLSL";
    }

    inline std::string mixApply()
    {
        return imagePass(R"GLSL(
layout(std430, binding = 8) readonly buffer M { float mplane[]; };    // own adjustments
layout(std430, binding = 9) readonly buffer MB { float mblur[]; };    // blurred (spread), or the same
uniform int uSpread;
float shoulder(float x) {
    if (x <= 0.75) return x;
    float headroom = 0.25;
    return 0.75 + headroom * (1.0 - exp(-(x - 0.75) / headroom));
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(uW * uH);
    if (i >= n) return;
    uint b = i * uint(uCh);
    float adj[3];
    for (int c = 0; c < 3; ++c) {
        float own = mplane[uint(c) * n + i];
        float nb = mblur[uint(c) * n + i];
        adj[c] = uSpread != 0 && abs(nb) > abs(own) ? nb : own;
    }
    float r = src[b], g = src[b + 1u], bl = src[b + 2u];
    if (adj[0] != 0.0 || adj[1] != 0.0) {
        vec3 hsl = rgbToHsl(vec3(r, g, bl));
        float h = hsl.x + adj[0] * 180.0;
        float s = hsl.y * (1.0 + adj[1]);
        if (h < 0.0) h += 360.0;
        if (h >= 360.0) h -= 360.0;
        s = clamp(s, 0.0, 1.0);
        vec3 o = hslToRgb(h, s, hsl.z);
        r = o.r; g = o.g; bl = o.b;
    }
    if (adj[2] != 0.0) {
        float gain = exp2(adj[2] * 1.5);
        float v = max(r, max(g, bl));
        float k = 1.0, d = 0.0;
        if (v > 0.0) {
            float base = shoulder(v);
            if (base > 0.0) {
                float wanted = v * gain;
                float landed = shoulder(wanted) / base * v;
                float ceil1 = v > 1.0 ? v : 1.0;
                if (landed > ceil1) landed = ceil1;
                k = landed / v;
                if (wanted > landed && landed > 0.0) { d = 1.0 - 1.0 / (wanted / landed); if (d > 1.0) d = 1.0; }
            }
        }
        if (d > 0.0) {
            float nv = v * k;
            r = nv * (r / v * (1.0 - d) + d);
            g = nv * (g / v * (1.0 - d) + d);
            bl = nv * (bl / v * (1.0 - d) + d);
        } else { r *= k; g *= k; bl *= k; }
    }
    dst[b] = r; dst[b + 1u] = g; dst[b + 2u] = bl;
    for (int c = 3; c < uCh; ++c) dst[b + uint(c)] = src[b + uint(c)];
}
)GLSL");
    }

    inline std::string grading()
    {
        return imagePass(R"GLSL(
uniform vec3 uHue;      // shadows, midtones, highlights (degrees)
uniform vec3 uSat;      // /100
uniform vec3 uLum;      // /100
uniform float uBalance; // /100
uniform int uRemap;
uniform vec4 uRemapP;   // src hue, range, dst hue, strength
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    if (uCh < 3) { for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = src[b + uint(c)]; return; }
    float r = src[b], g = src[b + 1u], bl = src[b + 2u];
    float L = lumOf(r, g, bl);
    float pivot = 0.5 + uBalance * 0.25;
    float wHigh = smooth01((L - pivot) / 0.5);
    float wShadow = 1.0 - smooth01(L / (pivot * 2.0));
    float wMid = max(0.0, 1.0 - wHigh - wShadow);
    float rw[3] = float[3](wShadow, wMid, wHigh);
    for (int region = 0; region < 3; ++region) {
        float w = rw[region], sat = uSat[region], lum = uLum[region];
        if (sat > 1e-4) {
            vec3 t = hslToRgb(uHue[region], sat, 0.5);
            r += (t.r - 0.5) * w * sat; g += (t.g - 0.5) * w * sat; bl += (t.b - 0.5) * w * sat;
        }
        float dl = lum * w * 0.3;
        r += dl; g += dl; bl += dl;
    }
    if (uRemap != 0 && uRemapP.y > 1e-3) {
        vec3 hsl = rgbToHsl(vec3(r, g, bl));
        float dist = abs(hueDelta(hsl.x, uRemapP.x));
        if (dist < uRemapP.y) {
            float w = 1.0 - dist / uRemapP.y;
            float h = hsl.x + w * uRemapP.w * hueDelta(hsl.x, uRemapP.z);
            if (h < 0.0) h += 360.0;
            if (h >= 360.0) h -= 360.0;
            vec3 o = hslToRgb(h, hsl.y, hsl.z);
            r = o.r; g = o.g; bl = o.b;
        }
    }
    dst[b] = r; dst[b + 1u] = g; dst[b + 2u] = bl;
    for (int c = 3; c < uCh; ++c) dst[b + uint(c)] = src[b + uint(c)];
}
)GLSL");
    }

    // ── GPU-B: the spatial and geometric stages ─────────────────────────────────────────────

    // Image::sampleBilinear on the source buffer, for channel c (clamped to the valid range)
    inline const char *bilinear()
    {
        return R"GLSL(
float sampleB(float fx, float fy, int c) {
    fx = clamp(fx, 0.0, float(uW - 1));
    fy = clamp(fy, 0.0, float(uH - 1));
    int x0 = int(fx), y0 = int(fy);
    int x1 = x0 < uW - 1 ? x0 + 1 : x0, y1 = y0 < uH - 1 ? y0 + 1 : y0;
    float tx = fx - float(x0), ty = fy - float(y0);
    float p00 = src[(y0 * uW + x0) * uCh + c], p10 = src[(y0 * uW + x1) * uCh + c];
    float p01 = src[(y1 * uW + x0) * uCh + c], p11 = src[(y1 * uW + x1) * uCh + c];
    float top = p00 + (p10 - p00) * tx, bot = p01 + (p11 - p01) * tx;
    return top + (bot - top) * ty;
}
)GLSL";
    }

    // Rotate's free angle (after the quarter turns, done on the CPU): the same inverse map
    inline std::string rotate()
    {
        return imagePass((std::string(bilinear()) + R"GLSL(
uniform float uCos;
uniform float uSin;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float cx = float(uW) * 0.5, cy = float(uH) * 0.5;
    float dx = float(x) + 0.5 - cx, dy = float(y) + 0.5 - cy;
    float sx = uCos * dx + uSin * dy + cx - 0.5;
    float sy = -uSin * dx + uCos * dy + cy - 0.5;
    uint b = i * uint(uCh);
    bool outside = sx < 0.0 || sy < 0.0 || sx > float(uW - 1) || sy > float(uH - 1);
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = outside ? 0.0 : sampleB(sx, sy, c);
}
)GLSL").c_str());
    }

    // LensCorrection: radial distortion, chromatic aberration (opposed R/B scales), vignette
    inline std::string lens()
    {
        return imagePass((std::string(bilinear()) + R"GLSL(
uniform float uKDist;
uniform float uKCA;
uniform float uVig;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float cx = float(uW - 1) * 0.5, cy = float(uH - 1) * 0.5;
    float maxR = sqrt(cx * cx + cy * cy);
    float invMaxR = maxR > 0.0 ? 1.0 / maxR : 0.0;
    float dx = float(x) - cx, dy = float(y) - cy;
    float r = sqrt(dx * dx + dy * dy) * invMaxR;
    float f = 1.0 + uKDist * r * r;
    float fr = f * (1.0 - uKCA * r), fb = f * (1.0 + uKCA * r);
    int cc = uCh >= 3 ? 3 : uCh;
    uint b = i * uint(uCh);
    float o[4];
    if (cc >= 3) {
        o[0] = sampleB(cx + dx * fr, cy + dy * fr, 0);
        o[1] = sampleB(cx + dx * f, cy + dy * f, 1);
        o[2] = sampleB(cx + dx * fb, cy + dy * fb, 2);
    } else o[0] = sampleB(cx + dx * f, cy + dy * f, 0);
    for (int c = cc; c < uCh; ++c) o[c] = sampleB(cx + dx * f, cy + dy * f, c);
    if (uVig != 0.0) { float m = 1.0 + uVig * r * r; for (int c = 0; c < cc; ++c) o[c] *= m; }
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = o[c];
}
)GLSL").c_str());
    }

    // NoiseReduction, colour: the three chroma planes (channel minus luminance) into one buffer
    inline std::string nrChroma()
    {
        return std::string(header()) + R"GLSL(
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 8) writeonly buffer M { float mplane[]; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(uW * uH);
    if (i >= n) return;
    uint b = i * uint(uCh);
    float L = lumOf(src[b], src[b + 1u], src[b + 2u]);
    for (int c = 0; c < 3; ++c) mplane[uint(c) * n + i] = src[b + uint(c)] - L;
}
)GLSL";
    }

    inline std::string nrChromaApply()
    {
        return imagePass(R"GLSL(
layout(std430, binding = 9) readonly buffer MB { float mblur[]; };
uniform float uAmt;
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint n = uint(uW * uH);
    if (i >= n) return;
    uint b = i * uint(uCh);
    float L = lumOf(src[b], src[b + 1u], src[b + 2u]);
    for (int c = 0; c < uCh; ++c) {
        if (c < 3) { float ch = src[b + uint(c)] - L; dst[b + uint(c)] = L + ch + (mblur[uint(c) * n + i] - ch) * uAmt; }
        else dst[b + uint(c)] = src[b + uint(c)];
    }
}
)GLSL");
    }

    // NoiseReduction, luminance: the bilateral on the encoded luminance (radius ≤ 4), as a ratio
    inline std::string nrLum()
    {
        return imagePass(R"GLSL(
layout(std430, binding = 2) readonly buffer L { float lplane[]; };
layout(std430, binding = 4) readonly buffer P { float pplane[]; };
uniform int uRad;
uniform float uInvS2;
uniform float uInvR2;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float center = pplane[i];
    float acc = 0.0, wsum = 0.0;
    for (int dy = -uRad; dy <= uRad; ++dy) {
        int yy = clamp(y + dy, 0, uH - 1);
        for (int dx = -uRad; dx <= uRad; ++dx) {
            int xx = clamp(x + dx, 0, uW - 1);
            float pv = pplane[yy * uW + xx];
            float dr = pv - center;
            float ws = exp(-float(dx * dx + dy * dy) * uInvS2 - dr * dr * uInvR2);
            acc += pv * ws; wsum += ws;
        }
    }
    float newP = wsum > 0.0 ? acc / wsum : center;
    float newL = srgbD(clamp(newP, 0.0, 1.0));
    float L2 = lplane[i];
    float ratio = newL / (L2 > 1e-4 ? L2 : 1e-4);
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = c < cc ? src[b + uint(c)] * ratio : src[b + uint(c)];
}
)GLSL");
    }

    // Dehaze's atmospheric light: each group's max of the darkest channel (the CPU finishes the max)
    inline std::string dehazeReduce()
    {
        return std::string(header()) + R"GLSL(
layout(std430, binding = 0) readonly buffer Src { float src[]; };
layout(std430, binding = 10) writeonly buffer Part { float part[]; };
shared float best[256];
void main() {
    uint i = gl_GlobalInvocationID.x;
    uint l = gl_LocalInvocationID.x;
    float v = -1e30;
    if (i < uint(uW * uH)) {
        uint b = i * uint(uCh);
        int cc = uCh >= 3 ? 3 : uCh;
        float mn = src[b];
        for (int c = 1; c < cc; ++c) mn = min(mn, src[b + uint(c)]);
        v = mn;
    }
    best[l] = v;
    barrier();
    for (uint s = 128u; s > 0u; s >>= 1u) { if (l < s) best[l] = max(best[l], best[l + s]); barrier(); }
    if (l == 0u) part[gl_WorkGroupID.x] = best[0];
}
)GLSL";
    }

    inline std::string dehaze()
    {
        return imagePass(R"GLSL(
uniform float uA;
uniform float uAmount;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    if (uAmount > 0.0) {
        float omega = 0.95 * uAmount;
        float mn = src[b];
        for (int c = 1; c < cc; ++c) mn = min(mn, src[b + uint(c)]);
        float t = max(0.1, 1.0 - omega * (mn / uA));
        for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = c < cc ? (src[b + uint(c)] - uA) / t + uA : src[b + uint(c)];
    } else {
        float k = -uAmount * 0.6;
        for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = c < cc ? src[b + uint(c)] + (uA - src[b + uint(c)]) * k : src[b + uint(c)];
    }
}
)GLSL");
    }

    // Sharpen: |P − blur(P)| into a plane (the masking's edge strength)
    inline std::string sharpenDetail()
    {
        return std::string(header()) + R"GLSL(
layout(std430, binding = 4) readonly buffer P { float pplane[]; };
layout(std430, binding = 5) readonly buffer PB { float pblur[]; };
layout(std430, binding = 6) writeonly buffer D { float dplane[]; };
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    dplane[i] = abs(pplane[i] - pblur[i]);
}
)GLSL";
    }

    inline std::string sharpen()
    {
        return imagePass(R"GLSL(
layout(std430, binding = 2) readonly buffer L { float lplane[]; };
layout(std430, binding = 4) readonly buffer P { float pplane[]; };
layout(std430, binding = 5) readonly buffer PB { float pblur[]; };
layout(std430, binding = 6) readonly buffer DB { float dblur[]; };
uniform float uAmount;
uniform float uMasking;
float smoothstep01(float lo, float hi, float x) {
    if (hi <= lo) return x >= hi ? 1.0 : 0.0;
    float t = clamp((x - lo) / (hi - lo), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    float mask = 1.0;
    if (uMasking > 0.0) mask = (1.0 - uMasking) + uMasking * smoothstep01(0.0, uMasking * 0.12 + 1e-4, dblur[i]);
    float newP = clamp(pplane[i] + uAmount * (pplane[i] - pblur[i]) * mask, 0.0, 1.0);
    float L = lplane[i];
    float ratio = srgbD(newP) / (L > 1e-4 ? L : 1e-4);
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = c < cc ? src[b + uint(c)] * ratio : src[b + uint(c)];
}
)GLSL");
    }

    // Grain: the CPU's own hashed value noise, two octaves, gated to the midtones
    inline std::string grain()
    {
        return imagePass(R"GLSL(
uniform float uCell;
uniform float uStrength;
uniform uint uSeed;
float cellNoise(int ix, int iy, uint seed) {
    uint h = uint(ix) * 374761393u + uint(iy) * 668265263u + seed * 362437u;
    h = (h ^ (h >> 13u)) * 1274126177u;
    return (float(h & 0xffffu) / 65535.0) * 2.0 - 1.0;
}
float fadeq(float t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
float valueNoise(float fx, float fy, uint seed) {
    int x0 = int(floor(fx)), y0 = int(floor(fy));
    float tx = fadeq(fx - float(x0)), ty = fadeq(fy - float(y0));
    float n00 = cellNoise(x0, y0, seed), n10 = cellNoise(x0 + 1, y0, seed);
    float n01 = cellNoise(x0, y0 + 1, seed), n11 = cellNoise(x0 + 1, y0 + 1, seed);
    float a = n00 + (n10 - n00) * tx, b = n01 + (n11 - n01) * tx;
    return a + (b - a) * ty;
}
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    int x = int(i) % uW, y = int(i) / uW;
    float fx = float(x) / uCell, fy = float(y) / uCell;
    float n = valueNoise(fx, fy, uSeed) * 0.65 + valueNoise(fx * 2.3 + 11.3, fy * 2.3 + 7.7, uSeed * 9176u + 1u) * 0.35;
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    float L = cc >= 3 ? lumOf(src[b], src[b + 1u], src[b + 2u]) : src[b];
    float midW = 4.0 * L * (1.0 - L);
    float delta = n * uStrength * (midW < 0.0 ? 0.0 : midW);
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = c < cc ? src[b + uint(c)] + delta : src[b + uint(c)];
}
)GLSL");
    }

    inline std::string encode()
    {
        return imagePass(R"GLSL(
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= uint(uW * uH)) return;
    uint b = i * uint(uCh);
    int cc = uCh >= 3 ? 3 : uCh;
    for (int c = 0; c < uCh; ++c) dst[b + uint(c)] = c < cc ? srgbE(src[b + uint(c)]) : src[b + uint(c)];
}
)GLSL");
    }
}
}
