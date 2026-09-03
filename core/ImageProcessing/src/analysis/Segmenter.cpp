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

        /** 1 inside a band, falling smoothly to 0 over `soft` on either side. The gate is built
         *  out of these rather than out of hard comparisons for one reason: a hard range puts a
         *  visible step into the mask wherever a pixel crosses it, and the histogram that reads
         *  the gate's output would inherit that step as a cliff in its own bins. */
        inline float band(float x, float lo, float hi, float soft)
        {
            return smoothstep(lo - soft, lo, x) * (1.f - smoothstep(hi, hi + soft, x));
        }

        /** The same, on the hue circle, so a band that straddles 0/360 is not two bands. Skin's
         *  does: it runs from just below red round to orange. */
        inline float hueBand(float h, float lo, float hi, float soft)
        {
            if (lo <= hi) return band(h, lo, hi, soft);
            return std::max(band(h, lo, 360.f + hi, soft), band(h + 360.f, lo, 360.f + hi, soft));
        }

        /** One pixel's colour, gamma-encoded as it reads (R-AISEG-3), in the space deepgaze
         *  works in. `srgbEncode` is table-driven, so this is three LUT lookups and not three
         *  `pow`s.
         *
         *  HSV and not HSL, because that is what deepgaze's bounds are stated in and converting
         *  the bounds instead of the pixels would be the same arithmetic done once per source
         *  file instead of once per pixel — with the difference that a reader could no longer
         *  check the constants against the reference. */
        struct Hsv
        {
            float h = 0;   // degrees
            float s = 0;   // 0..1
            float v = 0;   // 0..1
            float r = 0, g = 0, b = 0;   // display-referred, for the ordering test
        };

        inline Hsv hsvOf(const Pixel *px)
        {
            Hsv o;
            o.r = (float)color::srgbEncode(px[0]);
            o.g = (float)color::srgbEncode(px[1]);
            o.b = (float)color::srgbEncode(px[2]);
            const float mx = std::max(o.r, std::max(o.g, o.b));
            const float mn = std::min(o.r, std::min(o.g, o.b));
            const float d = mx - mn;
            o.v = mx;
            o.s = mx > 0.f ? d / mx : 0.f;
            if (d <= 0.f) { o.h = 0.f; return o; }
            if (mx == o.r)      o.h = 60.f * std::fmod((o.g - o.b) / d, 6.f);
            else if (mx == o.g) o.h = 60.f * (((o.b - o.r) / d) + 2.f);
            else                o.h = 60.f * (((o.r - o.g) / d) + 4.f);
            if (o.h < 0.f) o.h += 360.f;
            return o;
        }

        // ── the gate (R-AISEG-23 step 1) ────────────────────────────────────────────────────
        //
        // deepgaze's RangeColorDetector, with its published skin bounds, plus one addition: the
        // R > G > B ordering test. That is Kovac's rule and it costs two comparisons; it is what
        // keeps a neutral grey or a blue-grey that happens to sit in the hue band from seeding
        // the histogram, and the histogram is only as good as what seeds it.
        inline float skinGate(const Hsv &c)
        {
            using namespace segment;
            const float hue = hueBand(c.h, kSkinHueMin, kSkinHueMax, 10.f);
            const float sat = band(c.s, kSkinSatMin, kSkinSatMax, 0.06f);
            const float val = smoothstep(kSkinValMin - 0.06f, kSkinValMin, c.v);
            const float order = smoothstep(0.0f, 0.04f, c.r - c.g) * smoothstep(-0.01f, 0.02f, c.g - c.b);
            return hue * sat * val * order;
        }

        // ── the back-projection histogram (R-AISEG-23 step 2) ───────────────────────────────
        //
        // Hue x saturation, and nothing else, exactly as deepgaze: value is left out because it
        // is the axis lighting moves a subject along, and a model that has learned "this face is
        // this bright" declines the same face in its own shadow.
        //
        // 36 x 32 bins and not deepgaze's 180 x 256. deepgaze seeds from a hand-picked template
        // of a few thousand pixels at most and never normalises for sparsity; here the seed is
        // whatever the gate admitted, which on a landscape with one small face can be a few
        // thousand pixels spread over 46,080 bins — a histogram with one sample in most of its
        // occupied cells is noise wearing a model's clothes. Coarse bins plus the 3x3 smoothing
        // below is the same trade every practical back-projection makes.
        constexpr int kHueBins = 36;
        constexpr int kSatBins = 32;

        struct HsHistogram
        {
            float bin[kHueBins * kSatBins] = {0.f};

            /** Bilinear lookup, so the projection has no bin edges in it. A nearest-bin lookup
             *  puts a visible contour line across a cheek wherever the hue crosses 10 degrees,
             *  which then survives every later step and ends up in the traced boundary. */
            float at(float hueDeg, float sat) const
            {
                const float fh = clampf(hueDeg, 0.f, 359.999f) / 360.f * kHueBins - 0.5f;
                const float fs = clampf(sat, 0.f, 1.f) * kSatBins - 0.5f;
                const int h0 = (int)std::floor(fh), s0 = (int)std::floor(fs);
                const float th = fh - h0, ts = fs - s0;
                auto cell = [&](int hi, int si) -> float {
                    // Hue WRAPS and saturation clamps: the first is a circle, the second is not.
                    hi = ((hi % kHueBins) + kHueBins) % kHueBins;
                    si = si < 0 ? 0 : (si >= kSatBins ? kSatBins - 1 : si);
                    return bin[hi * kSatBins + si];
                };
                const float a = cell(h0, s0) * (1 - th) + cell(h0 + 1, s0) * th;
                const float b = cell(h0, s0 + 1) * (1 - th) + cell(h0 + 1, s0 + 1) * th;
                return a * (1 - ts) + b * ts;
            }
        };

        /** Smooth the histogram over its own 3x3 neighbourhood, wrapping in hue, then normalise
         *  so the most popular colour scores 1. Both halves matter: the smoothing is what lets a
         *  skin tone that fell one bin to the side of the mode still score well, and the
         *  normalisation is what makes the plane a likelihood the shared `scoreToCoverage` can
         *  threshold at a sensitivity that means the same thing it does for every other model. */
        void finishHistogram(HsHistogram &h)
        {
            HsHistogram s;
            for (int i = 0; i < kHueBins; ++i)
                for (int j = 0; j < kSatBins; ++j)
                {
                    float acc = 0.f;
                    for (int di = -1; di <= 1; ++di)
                        for (int dj = -1; dj <= 1; ++dj)
                        {
                            const int ii = ((i + di) % kHueBins + kHueBins) % kHueBins;
                            const int jj = j + dj;
                            if (jj < 0 || jj >= kSatBins) continue;
                            acc += h.bin[ii * kSatBins + jj];
                        }
                    s.bin[i * kSatBins + j] = acc;
                }
            float mx = 0.f;
            for (float v : s.bin) mx = std::max(mx, v);
            const float inv = mx > 0.f ? 1.f / mx : 0.f;
            for (int i = 0; i < kHueBins * kSatBins; ++i) h.bin[i] = s.bin[i] * inv;
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
        case SemanticSubject::Person: return "person";
        default: return "skin";
        }
    }

    bool parseSemanticSubject(const std::string &text, SemanticSubject &out)
    {
        if (text.empty()) return false;
        std::string k;
        k.reserve(text.size());
        for (char c : text)
            k += (char)((c >= 'A' && c <= 'Z') ? c - 'A' + 'a' : c);
        // Every name still parses, withdrawn ones included (R-AISEG-22). A script that says
        // `subject=sky` must not be silently read as skin; it parses, and then the detection
        // reports that nothing answers for it. Refusing it here would be the same information
        // delivered as a syntax error, which is a worse place for it.
        for (int i = 0; i < (int)SemanticSubject::Count; ++i)
            if (k == semanticSubjectName((SemanticSubject)i)) { out = (SemanticSubject)i; return true; }
        // A bare number too — the project file writes one, and a script that pasted a value out
        // of a file should not be refused for it. Strictly all-digits, so `skinn` is an error
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
        Image downscaleForAnalysis(const Image &img, int f)
        {
            const int w = img.width(), h = img.height(), ch = img.channels();
            if (f <= 1) return img;
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

        bool builtinHandles(SemanticSubject subject)
        {
            // Skin, and only Skin (R-AISEG-22). The other four are withdrawn rather than
            // deleted: their enum values are what project files store.
            return subject == SemanticSubject::Skin;
        }

        void scoreToCoverage(std::vector<Pixel> &score, int w, int h, float sensitivity)
        {
            if (w <= 0 || h <= 0 || score.size() < (std::size_t)w * h) return;
            sensitivity = clampf(sensitivity, 0.f, 1.f);
            // ── regularise, THEN threshold (R-AISEG-4) ──
            // In this order and not the other: thresholding first would decide each pixel alone
            // and then blur the decision, which turns a speckled score into a speckled mask with
            // soft edges on every speck. Softening the SCORE lets a pixel's neighbours outvote it.
            std::vector<Pixel> smooth;
            const float regSigma = kRegulariseFraction * (float)std::min(w, h);
            if (regSigma >= 0.5f) spatial::fastBlurPlane(score, smooth, w, h, regSigma);
            else smooth = score;

            // Sensitivity IS the threshold (R-AISEG-5): 0 takes only what the model is sure of,
            // 1 takes anything it suspects, 0.5 is the default. An installed model runs through
            // this same function, so the knob means one thing and not two.
            const float t = 0.80f - 0.70f * sensitivity;
            const float bandHalf = 0.16f;
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const std::size_t i = (std::size_t)y * w + x;
                        score[i] = (Pixel)smoothstep(t - bandHalf, t + bandHalf, (float)smooth[i]);
                    }
            });
        }

        void resamplePlane(const std::vector<Pixel> &src, int sw, int sh,
                           std::vector<Pixel> &dst, int dw, int dh)
        {
            dst.assign((std::size_t)std::max(0, dw * dh), (Pixel)0);
            if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0) return;
            // Bilinear and not nearest because the plane is a LIKELIHOOD: a nearest-neighbour
            // edge would put visible steps into it at exactly the boundary everything downstream
            // is about to trace.
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

        void builtinScore(const Image &img, SemanticSubject subject, std::vector<Pixel> &out)
        {
            const int w = img.width(), h = img.height(), ch = img.channels();
            const std::size_t n = (std::size_t)std::max(0, w * h);
            out.assign(n, (Pixel)0);
            if (w <= 0 || h <= 0 || ch < 3 || !builtinHandles(subject)) return;

            const Pixel *src = img.data();

            // ── step 1: the gate ────────────────────────────────────────────────────────────
            // Every pixel's soft membership of deepgaze's skin range. Kept, rather than
            // recomputed, because step 3 multiplies by it: the back-projection is allowed to
            // refine WITHIN what could be skin, never to add something the gate refused. That is
            // the one place this departs from deepgaze, which back-projects over the raw frame,
            // and without it a histogram fitted to a face would go looking for that hue in the
            // grass. What it does NOT do is tell a face from a wall of the same colour: the
            // projection is relative, so the wall is scored DOWN against a larger face and
            // usually thresholded away, and a wall larger than the face wins. That limit is
            // written into R-AISEG-23 rather than hidden here, because it is the honest edge of
            // what a colour detector can be asked for.
            std::vector<Pixel> gate(n, (Pixel)0);
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const std::size_t i = (std::size_t)y * w + x;
                        gate[i] = (Pixel)skinGate(hsvOf(src + i * ch));
                    }
            });

            // ── step 2: the histogram, fitted to THIS photograph ────────────────────────────
            // Weighted by the gate's own confidence, so a pixel the gate barely admitted barely
            // votes. Single-threaded: it is one pass of two comparisons and an increment over an
            // image that is at most a megapixel, and a per-thread histogram plus a merge would
            // cost more to write than it saves.
            HsHistogram hist;
            std::size_t seed = 0;
            for (std::size_t i = 0; i < n; ++i)
            {
                const float g = (float)gate[i];
                if (g < 0.25f) continue;   // not a seed; the soft tail is for step 3, not for this
                ++seed;
                const Hsv c = hsvOf(src + i * ch);
                const int hi = std::min(kHueBins - 1, (int)(clampf(c.h, 0.f, 359.999f) / 360.f * kHueBins));
                const int si = std::min(kSatBins - 1, (int)(clampf(c.s, 0.f, 1.f) * kSatBins));
                hist.bin[hi * kSatBins + si] += g;
            }
            // Too few seeds is an ANSWER, not a failure: there is no skin in this photograph.
            // Proceeding would build a colour model out of stray pixels and then find it
            // everywhere, which is precisely the confident-and-wrong behaviour this replaced.
            if ((float)seed < kMinSeedFraction * (float)n) return;
            finishHistogram(hist);

            // ── step 3: back-project ────────────────────────────────────────────────────────
            par::parallelFor(h, [&](int y0, int y1) {
                for (int y = y0; y < y1; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const std::size_t i = (std::size_t)y * w + x;
                        const float g = (float)gate[i];
                        if (g <= 0.f) continue;
                        const Hsv c = hsvOf(src + i * ch);
                        out[i] = (Pixel)clampf(hist.at(c.h, c.s) * g, 0.f, 1.f);
                    }
            });
        }
    }
}
