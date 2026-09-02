#include "Segmenter.h"
#include "../base/ColorSpace.h"
#include "../base/Parallel.h"
#include "../base/Spatial.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace arstro
{
    namespace
    {
        inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }

        inline float smoothstep(float e0, float e1, float x)
        {
            if (e1 <= e0) return x >= e1 ? 1.f : 0.f;
            const float t = clampf((x - e0) / (e1 - e0), 0.f, 1.f);
            return t * t * (3.f - 2.f * t);
        }

        /** 1 inside a band, falling smoothly to 0 over `soft` on either side. Every criterion
         *  below is one of these or a pair of them, which is the whole shape of the model: soft
         *  membership of a range, multiplied together. A hard range would put a visible step in
         *  the mask wherever a pixel crossed it. */
        inline float band(float x, float lo, float hi, float soft)
        {
            return smoothstep(lo - soft, lo, x) * (1.f - smoothstep(hi, hi + soft, x));
        }

        /** The same, on the hue circle, so a band that straddles 0/360 is not two bands. */
        inline float hueBand(float h, float lo, float hi, float soft)
        {
            if (lo <= hi) return band(h, lo, hi, soft);
            return std::max(band(h, lo, 360.f + hi, soft), band(h + 360.f, lo, 360.f + hi, soft));
        }

        struct Features
        {
            float h = 0, s = 0, l = 0;   // display-referred HSL (R-AISEG-3)
            float r = 0, g = 0, b = 0;   // display-referred RGB
        };

        /** One pixel's colour features, gamma-encoded as it reads (R-AISEG-3). `srgbEncode` is
         *  table-driven, so this is three LUT lookups rather than three `pow`s. */
        inline Features featuresOf(const Pixel *px)
        {
            Features f;
            const Pixel r = color::srgbEncode(px[0]);
            const Pixel g = color::srgbEncode(px[1]);
            const Pixel b = color::srgbEncode(px[2]);
            Pixel h, s, l;
            color::rgbToHsl(r, g, b, h, s, l);
            f.h = (float)h; f.s = (float)s; f.l = (float)l;
            f.r = (float)r; f.g = (float)g; f.b = (float)b;
            return f;
        }

        // ── the five class models ──────────────────────────────────────────────────────────
        //
        // Each returns 0..1 for one pixel from its colour, where it sits in the frame, and how
        // much local structure surrounds it. `ny` is 0 at the top. `structure` is |L - blur(L)|
        // in display units: near 0 on a clear sky, large in hair.

        float scoreSky(const Features &f, float ny, float structure)
        {
            // Two skies, and they look nothing alike. A BLUE sky is a hue band with real
            // saturation; an OVERCAST or blown one is bright and nearly neutral, which the blue
            // test would refuse outright — and "the sky is white today" is not an unusual photo.
            const float blue = hueBand(f.h, 185.f, 260.f, 25.f) * smoothstep(0.10f, 0.28f, f.s);
            const float pale = smoothstep(0.62f, 0.86f, f.l) * (1.f - smoothstep(0.05f, 0.16f, f.s));
            float colour = std::max(blue, pale * 0.85f);

            // Where it is. A prior, not a rule: a sky reflected in a window at the bottom of the
            // frame is still a sky, so the floor is 0.25 rather than 0 — the classifier is made
            // to hesitate there, not forbidden to answer.
            const float place = 0.25f + 0.75f * (1.f - smoothstep(0.35f, 0.80f, ny));

            // A sky is smooth. This is what separates it from a blue jacket, and it is the
            // single most useful feature of the three.
            const float smoothness = 1.f - smoothstep(0.020f, 0.075f, structure);
            return colour * place * (0.35f + 0.65f * smoothness);
        }

        float scoreSkin(const Features &f, float, float structure)
        {
            // The classical skin locus: orange-red hue, moderate saturation, mid lightness, and
            // R > G > B. The ordering test is what keeps a terracotta wall from scoring as well
            // as a face; it is soft rather than boolean so the mask has no step in it.
            const float hue = hueBand(f.h, 5.f, 45.f, 14.f);
            const float sat = band(f.s, 0.14f, 0.62f, 0.10f);
            const float lum = band(f.l, 0.22f, 0.86f, 0.10f);
            const float order = smoothstep(0.0f, 0.05f, f.r - f.g) * smoothstep(0.0f, 0.03f, f.g - f.b);
            // Skin is smoother than what surrounds it in almost every portrait. A light touch:
            // enough to prefer a cheek over a tweed jacket of the same colour, not enough to
            // refuse a beard.
            const float smoothness = 1.f - 0.45f * smoothstep(0.030f, 0.110f, structure);
            return hue * sat * lum * order * smoothness;
        }

        float scoreFoliage(const Features &f, float, float)
        {
            // Green, and green is not ambiguous. Deliberately no position and no structure term:
            // foliage is at the top of a landscape and the bottom of a portrait, and it is
            // smooth in a lawn and violent in a hedge, so both would only add noise.
            const float hue = hueBand(f.h, 62.f, 168.f, 22.f);
            const float sat = smoothstep(0.12f, 0.26f, f.s);
            const float lum = band(f.l, 0.05f, 0.88f, 0.08f);
            return hue * sat * lum;
        }

        float scoreWater(const Features &f, float ny, float structure)
        {
            // Water shares its whole colour signature with sky — it is usually a picture OF the
            // sky — so the two are separated by where they are and by structure. The position
            // prior is the mirror of the sky's and it is doing most of the work here; the
            // structure term is deliberately lax, because water has ripples and sky does not.
            const float blue = hueBand(f.h, 170.f, 255.f, 30.f) * smoothstep(0.06f, 0.22f, f.s);
            const float dark = band(f.l, 0.06f, 0.72f, 0.14f);
            const float place = 0.20f + 0.80f * smoothstep(0.30f, 0.70f, ny);
            const float texture = 1.f - 0.35f * smoothstep(0.10f, 0.25f, structure);
            return blue * dark * place * texture;
        }

        float scoreHair(const Features &f, float, float structure)
        {
            // The weakest of the five, and the requirement says so out loud (R-AISEG-2). Hair is
            // dark, not vividly saturated, and full of fine structure — which is also a fair
            // description of a wool coat. Structure is the dominant term rather than a modifier,
            // because without it this is just "shadows".
            const float dark = 1.f - smoothstep(0.18f, 0.62f, f.l);
            const float muted = 1.f - smoothstep(0.30f, 0.58f, f.s);
            const float detail = smoothstep(0.018f, 0.080f, structure);
            return dark * muted * detail;
        }

        float scoreOf(SemanticSubject subject, const Features &f, float ny, float structure)
        {
            switch (subject)
            {
            case SemanticSubject::Sky: return scoreSky(f, ny, structure);
            case SemanticSubject::Skin: return scoreSkin(f, ny, structure);
            case SemanticSubject::Foliage: return scoreFoliage(f, ny, structure);
            case SemanticSubject::Water: return scoreWater(f, ny, structure);
            case SemanticSubject::Hair: return scoreHair(f, ny, structure);
            default: return 0.f;
            }
        }
    }

    const char *semanticSubjectName(SemanticSubject s)
    {
        switch (s)
        {
        case SemanticSubject::Sky: return "sky";
        case SemanticSubject::Skin: return "skin";
        case SemanticSubject::Foliage: return "foliage";
        case SemanticSubject::Water: return "water";
        case SemanticSubject::Hair: return "hair";
        default: return "sky";
        }
    }

    bool parseSemanticSubject(const std::string &text, SemanticSubject &out)
    {
        if (text.empty()) return false;
        std::string k;
        k.reserve(text.size());
        for (char c : text)
            k += (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
        for (int i = 0; i < (int)SemanticSubject::Count; ++i)
            if (k == semanticSubjectName((SemanticSubject)i)) { out = (SemanticSubject)i; return true; }
        // A bare number too — the project file writes one, and a script that pasted a value out
        // of a file should not be refused for it. Strictly all-digits, so `skyy` is an error
        // rather than a silent 0, which is the whole reason this returns a bool.
        for (char c : k)
            if (c < '0' || c > '9') return false;
        const int n = std::atoi(k.c_str());
        if (n < 0 || n >= (int)SemanticSubject::Count) return false;
        out = (SemanticSubject)n;
        return true;
    }

    namespace segment
    {
        namespace
        {
            /** Box-average `img` down by an integer factor, in linear light — which is the only
             *  place averaging pixels is physically meaningful, and the reason the engine works
             *  in linear light at all. */
            Image boxDownscale(const Image &img, int f)
            {
                const int w = img.width(), h = img.height(), ch = img.channels();
                const int aw = std::max(1, w / f), ah = std::max(1, h / f);
                Image out(aw, ah, 3, ColorSpace::LinearSRGB);
                par::parallelFor(ah, [&](int y0, int y1) {
                    for (int y = y0; y < y1; ++y)
                        for (int x = 0; x < aw; ++x)
                        {
                            double acc[3] = {0, 0, 0};
                            int count = 0;
                            for (int sy = y * f; sy < std::min(h, (y + 1) * f); ++sy)
                                for (int sx = x * f; sx < std::min(w, (x + 1) * f); ++sx)
                                {
                                    const Pixel *px = img.data() + ((std::size_t)sy * w + sx) * ch;
                                    acc[0] += px[0]; acc[1] += px[1]; acc[2] += px[2];
                                    ++count;
                                }
                            const double inv = count ? 1.0 / count : 0.0;
                            for (int c = 0; c < 3; ++c) out.at(x, y, c) = (Pixel)(acc[c] * inv);
                        }
                });
                return out;
            }

            /** Bilinear upsample of a scalar plane back to the render's size. Bilinear and not
             *  nearest because the plane is a COVERAGE: a nearest-neighbour edge would put visible
             *  steps into the mask at exactly the boundary the classifier worked to soften. */
            void upsamplePlane(const std::vector<Pixel> &src, int sw, int sh,
                               std::vector<Pixel> &dst, int dw, int dh)
            {
                dst.assign((std::size_t)dw * dh, (Pixel)0);
                if (sw <= 0 || sh <= 0) return;
                par::parallelFor(dh, [&](int y0, int y1) {
                    for (int y = y0; y < y1; ++y)
                    {
                        const float fy = clampf(((y + 0.5f) * sh / (float)dh) - 0.5f, 0.f, (float)(sh - 1));
                        const int iy = (int)fy, jy = std::min(sh - 1, iy + 1);
                        const float ty = fy - iy;
                        for (int x = 0; x < dw; ++x)
                        {
                            const float fx = clampf(((x + 0.5f) * sw / (float)dw) - 0.5f, 0.f, (float)(sw - 1));
                            const int ix = (int)fx, jx = std::min(sw - 1, ix + 1);
                            const float tx = fx - ix;
                            const float a = (float)src[(std::size_t)iy * sw + ix] * (1 - tx) +
                                            (float)src[(std::size_t)iy * sw + jx] * tx;
                            const float b = (float)src[(std::size_t)jy * sw + ix] * (1 - tx) +
                                            (float)src[(std::size_t)jy * sw + jx] * tx;
                            dst[(std::size_t)y * dw + x] = (Pixel)(a * (1 - ty) + b * ty);
                        }
                    }
                });
            }

            void analyse(const Image &img, SemanticSubject subject, float sensitivity,
                         std::vector<Pixel> &out);
        }

        void builtinCoverage(const Image &img, SemanticSubject subject, float sensitivity,
                             std::vector<Pixel> &out)
        {
            const int w = img.width(), h = img.height(), ch = img.channels();
            if (w <= 0 || h <= 0 || ch < 3) { out.assign((std::size_t)std::max(0, w * h), (Pixel)0); return; }
            sensitivity = clampf(sensitivity, 0.f, 1.f);

            // Decide the regions ONCE, at a bounded resolution, and stretch the answer back
            // (kAnalysisEdge). Not only cheaper: it is what makes a preview's mask and an
            // export's mask the same mask rather than two similar ones.
            const int f = std::max(1, (std::min(w, h) + kAnalysisEdge - 1) / kAnalysisEdge);
            if (f > 1)
            {
                const Image small = boxDownscale(img, f);
                std::vector<Pixel> smallCov;
                analyse(small, subject, sensitivity, smallCov);
                upsamplePlane(smallCov, small.width(), small.height(), out, w, h);
                return;
            }
            analyse(img, subject, sensitivity, out);
        }

        namespace
        {
        void analyse(const Image &img, SemanticSubject subject, float sensitivity,
                     std::vector<Pixel> &out)
        {
            const int w = img.width(), h = img.height(), ch = img.channels();
            const std::size_t n = (std::size_t)w * h;
            out.assign(n, (Pixel)0);

            // ── local structure ──
            // Display-referred luminance minus a blurred copy of itself: how much detail sits
            // around this pixel. Hair is found by it and sky by its absence, so it is a feature
            // of the model rather than a refinement of it.
            std::vector<Pixel> luma(n), lumaBlur;
            const Pixel *src = img.data();
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const Pixel *px = src + ((std::size_t)y * w + x) * ch;
                        luma[(std::size_t)y * w + x] =
                            color::srgbEncode(color::luminance(px[0], px[1], px[2]));
                    }
            });
            const float structSigma = std::max(1.0f, kStructureFraction * (float)std::min(w, h));
            spatial::fastBlurPlane(luma, lumaBlur, w, h, structSigma);

            // ── the per-pixel score ──
            std::vector<Pixel> score(n);
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                {
                    const float ny = (y + 0.5f) / (float)h;
                    for (int x = 0; x < w; ++x)
                    {
                        const std::size_t i = (std::size_t)y * w + x;
                        const float structure = std::fabs((float)luma[i] - (float)lumaBlur[i]);
                        score[i] = (Pixel)clampf(
                            scoreOf(subject, featuresOf(src + i * ch), ny, structure), 0.f, 1.f);
                    }
                }
            });

            // ── regularise, THEN threshold (R-AISEG-4) ──
            // In this order and not the other: thresholding first would decide each pixel alone
            // and then blur the decision, which turns a speckled score into a speckled mask with
            // soft edges on every speck. Softening the SCORE lets a pixel's neighbours outvote it,
            // which is the whole point — a lone sky-coloured pixel inside a roof stays a roof.
            std::vector<Pixel> smooth;
            const float regSigma = kRegulariseFraction * (float)std::min(w, h);
            if (regSigma >= 0.5f) spatial::fastBlurPlane(score, smooth, w, h, regSigma);
            else smooth = score;

            // Sensitivity IS the threshold (R-AISEG-5): 0 takes only what the model is sure of,
            // 1 takes anything it suspects, 0.5 is the default. The band around it is what keeps
            // the mask's edge soft without a second control.
            const float t = 0.80f - 0.70f * sensitivity;
            const float bandHalf = 0.16f;
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const std::size_t i = (std::size_t)y * w + x;
                        out[i] = (Pixel)smoothstep(t - bandHalf, t + bandHalf, (float)smooth[i]);
                    }
            });
        }
        }   // anonymous namespace
    }
}
