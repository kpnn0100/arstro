/*
 *  interstellar_render — ColourTransform implementation. See ColourTransform.h for the contract and
 *  where each curve comes from. Constants are the vendors' published ones, unrounded.
 */
#include "ColourTransform.h"
#include "base/Parallel.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace interstellar
{
namespace render
{
namespace colour
{
    namespace
    {
        // ── the spaces ──────────────────────────────────────────────────────────────────────────
        struct Gamut
        {
            double rx, ry, gx, gy, bx, by, wx, wy;
        };
        constexpr double kD65x = 0.3127, kD65y = 0.3290;
        const Gamut kRec709{0.64, 0.33, 0.30, 0.60, 0.15, 0.06, kD65x, kD65y};
        const Gamut kP3D65{0.680, 0.320, 0.265, 0.690, 0.150, 0.060, kD65x, kD65y};
        const Gamut kRec2020{0.708, 0.292, 0.170, 0.797, 0.131, 0.046, kD65x, kD65y};
        const Gamut kAP1{0.713, 0.293, 0.165, 0.830, 0.128, 0.044, 0.32168, 0.33767};
        const Gamut kAWG3{0.6840, 0.3130, 0.2210, 0.8480, 0.0861, -0.1020, kD65x, kD65y};
        const Gamut kAWG4{0.7347, 0.2653, 0.1424, 0.8576, 0.0991, -0.0308, kD65x, kD65y};
        const Gamut kSGamut3Cine{0.766, 0.275, 0.225, 0.800, 0.089, -0.087, kD65x, kD65y};
        const Gamut kVGamut{0.730, 0.280, 0.165, 0.840, 0.100, -0.030, kD65x, kD65y};
        const Gamut kCinemaGamut{0.740, 0.270, 0.170, 1.140, 0.080, -0.100, kD65x, kD65y};
        const Gamut kRWG{0.780308, 0.304253, 0.121595, 1.493994, 0.095612, -0.084589, kD65x, kD65y};
        const Gamut kBmdWG5{0.7177215, 0.3171181, 0.2280410, 0.8615690, 0.1005841, -0.0820452, 0.3127170, 0.3290312};

        enum Curve { kNone = 0, kLogC3, kLogC4, kSLog3, kVLog, kCLog3, kLog3G10, kBmdFilm5 };

        struct InputSpace
        {
            const char *id;
            Curve curve;          // kNone + display = rec709/srgb; kNone + !display = linear
            bool display;
            const Gamut *gamut;
        };
        const InputSpace kInputs[] = {
            {"rec709", kNone, true, &kRec709},     {"srgb", kNone, true, &kRec709},
            {"linear", kNone, false, &kRec709},    {"logc3", kLogC3, false, &kAWG3},
            {"logc4", kLogC4, false, &kAWG4},      {"slog3", kSLog3, false, &kSGamut3Cine},
            {"vlog", kVLog, false, &kVGamut},      {"clog3", kCLog3, false, &kCinemaGamut},
            {"log3g10", kLog3G10, false, &kRWG},   {"bmdfilm5", kBmdFilm5, false, &kBmdWG5},
        };

        // ── 3×3 maths ─────────────────────────────────────────────────────────────────────────
        void mul(const double a[9], const double b[9], double o[9])
        {
            double t[9];
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) t[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
            std::copy(t, t + 9, o);
        }
        void inverse(const double m[9], double o[9])
        {
            const double a = m[0], b = m[1], c = m[2], d = m[3], e = m[4], f = m[5], g = m[6], h = m[7], i = m[8];
            const double A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
            const double det = a * A + b * B + c * C;
            const double t[9] = {A, -(b * i - c * h), b * f - c * e, B, a * i - c * g, -(a * f - c * d), C, -(a * h - b * g), a * e - b * d};
            for (int k = 0; k < 9; ++k) o[k] = t[k] / det;
        }
        void xyz(double x, double y, double o[3]) { o[0] = x / y; o[1] = 1.0; o[2] = (1.0 - x - y) / y; }

        /** RGB → XYZ from the primaries and the white (the standard derivation). */
        void rgbToXyz(const Gamut &g, double o[9])
        {
            double r[3], gg[3], b[3], w[3];
            xyz(g.rx, g.ry, r);
            xyz(g.gx, g.gy, gg);
            xyz(g.bx, g.by, b);
            xyz(g.wx, g.wy, w);
            const double P[9] = {r[0], gg[0], b[0], r[1], gg[1], b[1], r[2], gg[2], b[2]};
            double Pi[9];
            inverse(P, Pi);
            const double S[3] = {Pi[0] * w[0] + Pi[1] * w[1] + Pi[2] * w[2], Pi[3] * w[0] + Pi[4] * w[1] + Pi[5] * w[2],
                                 Pi[6] * w[0] + Pi[7] * w[1] + Pi[8] * w[2]};
            for (int row = 0; row < 3; ++row)
                for (int c = 0; c < 3; ++c) o[row * 3 + c] = P[row * 3 + c] * S[c];
        }

        /** Bradford adaptation from white (sx, sy) to (dx, dy), in XYZ. */
        void bradford(double sx, double sy, double dx, double dy, double o[9])
        {
            static const double Ma[9] = {0.8951, 0.2664, -0.1614, -0.7502, 1.7135, 0.0367, 0.0389, -0.0685, 1.0296};
            double Mi[9], s[3], d[3];
            inverse(Ma, Mi);
            xyz(sx, sy, s);
            xyz(dx, dy, d);
            double cs[3], cd[3];
            for (int r = 0; r < 3; ++r)
            {
                cs[r] = Ma[r * 3] * s[0] + Ma[r * 3 + 1] * s[1] + Ma[r * 3 + 2] * s[2];
                cd[r] = Ma[r * 3] * d[0] + Ma[r * 3 + 1] * d[1] + Ma[r * 3 + 2] * d[2];
            }
            const double D[9] = {cd[0] / cs[0], 0, 0, 0, cd[1] / cs[1], 0, 0, 0, cd[2] / cs[2]};
            double t[9];
            mul(D, Ma, t);
            mul(Mi, t, o);
        }

        /** Linear RGB in `from` → linear RGB in `to`. */
        void convert(const Gamut &from, const Gamut &to, double o[9])
        {
            double a[9], b[9], bi[9], cat[9], t[9];
            rgbToXyz(from, a);
            rgbToXyz(to, b);
            inverse(b, bi);
            if (std::fabs(from.wx - to.wx) > 1e-6 || std::fabs(from.wy - to.wy) > 1e-6)
            {
                bradford(from.wx, from.wy, to.wx, to.wy, cat);
                mul(cat, a, t);
            }
            else std::copy(a, a + 9, t);
            mul(bi, t, o);
        }

        // ── the curves ─────────────────────────────────────────────────────────────────────────
        inline float decodeLog(int curve, float x)
        {
            // a log curve on 10-bit code values; FFmpeg's 0..1 RGB of video-range YCbCr is (CV - 64) / 876
            const double cv = (x * 876.0 + 64.0) / 1023.0;
            switch (curve)
            {
                case kLogC3:
                {
                    const double cut = 0.010591, a = 5.555556, b = 0.052272, c = 0.247190, d = 0.385537, e = 5.367655, f = 0.092809;
                    return (float)(cv > e * cut + f ? (std::pow(10.0, (cv - d) / c) - b) / a : (cv - f) / e);
                }
                case kLogC4:
                {
                    const double a = (262144.0 - 16.0) / 117.45, b = (1023.0 - 95.0) / 1023.0, c = 95.0 / 1023.0;
                    const double s = (7.0 * std::log(2.0) * std::pow(2.0, 7.0 - 14.0 * c / b)) / (a * b);
                    const double t = (std::pow(2.0, 14.0 * (-c / b) + 6.0) - 64.0) / a;
                    return (float)(cv >= 0.0 ? (std::pow(2.0, 14.0 * (cv - c) / b + 6.0) - 64.0) / a : cv * s + t);
                }
                case kSLog3:
                {
                    const double y = cv * 1023.0;
                    return (float)(y >= 171.2102946929 ? std::pow(10.0, (y - 420.0) / 261.5) * 0.19 - 0.01
                                                       : (y - 95.0) * 0.01125 / (171.2102946929 - 95.0));
                }
                case kVLog:
                    return (float)(cv < 0.181 ? (cv - 0.125) / 5.6 : std::pow(10.0, (cv - 0.598206) / 0.241514) - 0.00873);
                case kCLog3:
                {
                    // defined on the IRE scale itself, and on reflectance / 0.9
                    const double v = x;
                    double r;
                    if (v < 0.097465473) r = -(std::pow(10.0, (0.12783901 - v) / 0.36726845) - 1.0) / 14.98325;
                    else if (v <= 0.15277891) r = (v - 0.12512219) / 1.9754798;
                    else r = (std::pow(10.0, (v - 0.12240537) / 0.36726845) - 1.0) / 14.98325;
                    return (float)(r * 0.9);
                }
                case kLog3G10:
                {
                    const double a = 0.224282, b = 155.975327, c = 0.01, g = 15.1927;
                    return (float)(cv < 0.0 ? cv / g - c : (std::pow(10.0, cv / a) - 1.0) / b - c);
                }
                case kBmdFilm5:
                {
                    const double A = 0.08692876065491224, B = 0.005494072432257808, C = 0.5300133392291939;
                    const double D = 8.283605932402494, E = 0.09246575342465753, linCut = 0.005;
                    return (float)(cv < D * linCut + E ? (cv - E) / D : std::exp((cv - C) / A) - B);
                }
            }
            return x;
        }

        inline float srgbDecode(float v)
        {
            v = std::min(1.0f, std::max(0.0f, v));
            return v <= 0.04045f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
        }
        inline float srgbEncode(float v)
        {
            v = std::min(1.0f, std::max(0.0f, v));
            return v <= 0.0031308f ? v * 12.92f : 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
        }
        inline float acescctEncode(float x)
        {
            if (x <= 0.0078125f) return 10.5402377416545f * x + 0.0729055341958355f;
            return (std::log2(x) + 9.72f) / 17.52f;
        }
        inline float acescctDecode(float y)
        {
            if (y <= 0.155251141552511f) return (y - 0.0729055341958355f) / 10.5402377416545f;
            return std::min(65504.0f, std::exp2(y * 17.52f - 9.72f));
        }
        inline float pqEncode(float nits)
        {
            const float m1 = 0.1593017578125f, m2 = 78.84375f, c1 = 0.8359375f, c2 = 18.8515625f, c3 = 18.6875f;
            const float y = std::min(1.0f, std::max(0.0f, nits / 10000.0f));
            const float p = std::pow(y, m1);
            return std::pow((c1 + c2 * p) / (1.0f + c3 * p), m2);
        }
        inline float hlgEncode(float e)
        {
            const float a = 0.17883277f, b = 0.28466892f, c = 0.55991073f;
            e = std::min(1.0f, std::max(0.0f, e));
            return e <= 1.0f / 12.0f ? std::sqrt(3.0f * e) : a * std::log(12.0f * e - b) + c;
        }

        constexpr float kKnee = 0.6f;   // the tone map's identity range ends here (see the header)

        /** Identity to the knee, then t / (1 + t) of the rest: slope 1 at the knee (no kink) and a
         *  slow approach to `top`, so S-Log3's last stops (scene 18 ≈ +6.7 stops) still separate —
         *  an exponential shoulder clipped everything past +4. */
        inline float shoulder(float m, float k, float top)
        {
            if (m <= k) return m;
            const float t = (m - k) / (top - k);
            return k + (top - k) * t / (1.0f + t);
        }
        inline float shoulderInverse(float y, float k, float top)
        {
            if (y <= k) return y;
            const float u = std::min((y - k) / (top - k), 0.9999f);
            return k + (top - k) * u / (1.0f - u);
        }
        /** Apply f to max(r,g,b) and scale the pixel by the same ratio: no hue shift. */
        template <class F>
        inline void onMax(float *p, F f)
        {
            const float m = std::max(p[0], std::max(p[1], p[2]));
            if (!(m > 0.0f)) return;
            const float s = f(m) / m;
            p[0] *= s;
            p[1] *= s;
            p[2] *= s;
        }

        const InputSpace *inputSpace(const std::string &id)
        {
            for (const auto &s : kInputs)
                if (id == s.id) return &s;
            return nullptr;
        }
        const Gamut *outputGamut(const std::string &out)
        {
            if (out == "p3d65") return &kP3D65;
            if (out == "pq" || out == "hlg") return &kRec2020;
            return &kRec709;
        }
    }

    const std::vector<Def> &inputs()
    {
        static const std::vector<Def> k = {
            {"rec709", "Rec.709"},       {"srgb", "sRGB"},                  {"linear", "Linear (Rec.709)"},
            {"logc3", "ARRI LogC3"},     {"logc4", "ARRI LogC4"},           {"slog3", "Sony S-Log3"},
            {"vlog", "Panasonic V-Log"}, {"clog3", "Canon Log 3"},          {"log3g10", "RED Log3G10"},
            {"bmdfilm5", "Blackmagic Film Gen 5"},
        };
        return k;
    }
    const std::vector<Def> &workings()
    {
        static const std::vector<Def> k = {{"rec709", "Rec.709"}, {"acescct", "ACEScct"}};
        return k;
    }
    const std::vector<Def> &outputs()
    {
        static const std::vector<Def> k = {{"rec709", "Rec.709"}, {"rec709-2.4", "Rec.709 2.4"}, {"srgb", "sRGB"},
                                           {"p3d65", "P3-D65"},   {"pq", "HDR PQ"},              {"hlg", "HDR HLG"},
                                           {"dcdm", "DCI XYZ"}};   // R-DLV-4: a DCP's picture, and only a DCP's
        return k;
    }
    bool known(const std::vector<Def> &list, const std::string &id)
    {
        for (const auto &d : list)
            if (id == d.id) return true;
        return false;
    }
    const char *label(const std::vector<Def> &list, const std::string &id)
    {
        for (const auto &d : list)
            if (id == d.id) return d.label;
        return "";
    }
    bool isHdr(const std::string &output) { return output == "pq" || output == "hlg"; }

    void Transform::add(Op op, float a, float b)
    {
        Step s;
        s.op = op;
        s.a = a;
        s.b = b;
        mOps.push_back(s);
    }

    void Transform::addMatrix(const double m[9])
    {
        bool ident = true;
        for (int k = 0; k < 9; ++k) ident = ident && std::fabs(m[k] - (k % 4 == 0 ? 1.0 : 0.0)) < 1e-9;
        if (ident) return;
        Step s;
        s.op = Op::Matrix;
        for (int k = 0; k < 9; ++k) s.m[k] = (float)m[k];
        mOps.push_back(s);
    }

    Transform Transform::input(const std::string &input, const std::string &working)
    {
        Transform t;
        const InputSpace *in = inputSpace(input);
        const bool aces = working == "acescct";
        if (!in || (in->display && !aces)) return t;   // rec709 / srgb into rec709: as decoded
        const Gamut &target = aces ? kAP1 : kRec709;
        double m[9];
        convert(*in->gamut, target, m);
        if (in->display)
        {
            // a display-referred picture into a scene-referred space: undo the tone map, so the
            // rec709 output gives the picture back
            t.add(Op::DecodeSrgb);
            t.add(Op::ToneInverse);
        }
        else if (in->curve != kNone)
        {
            Step s;
            s.op = Op::DecodeLog;
            s.curve = in->curve;
            t.mOps.push_back(s);
        }
        t.addMatrix(m);
        if (aces) t.add(Op::EncodeAcescct);
        else
        {
            t.add(Op::ToneSdr);
            t.add(Op::EncodeSrgb);
        }
        t.mKey = "in:" + input + ">" + working;
        return t;
    }

    Transform Transform::output(const std::string &working, const std::string &output, double peakNits)
    {
        Transform t;
        const bool aces = working == "acescct";
        const std::string out = known(outputs(), output) ? output : "rec709";
        if (!aces && (out == "rec709" || out == "srgb")) return t;   // the graded code values, as the monitor showed them
        if (out == "dcdm")
        {
            // R-DLV-4: DCI X'Y'Z' (SMPTE ST 428-1) — the display's light as CIE XYZ, not adapted (a D65
            // white stays D65), white at 48 cd/m² of the 52.37 the code range spans, then gamma 2.6
            double toRec[9], xyz[9];
            t.add(aces ? Op::DecodeAcescct : Op::DecodeSrgb);
            if (aces)
            {
                convert(kAP1, kRec709, toRec);
                t.addMatrix(toRec);
                t.add(Op::ToneSdr);
            }
            rgbToXyz(kRec709, xyz);
            for (double &v : xyz) v *= 48.0 / 52.37;
            t.addMatrix(xyz);
            t.add(Op::EncodeGamma, 2.6f);
            t.mKey = "out:" + working + ">dcdm";
            return t;
        }
        double m[9];
        convert(aces ? kAP1 : kRec709, *outputGamut(out), m);
        t.add(aces ? Op::DecodeAcescct : Op::DecodeSrgb);
        t.addMatrix(m);
        const float peak = (float)std::clamp(peakNits, 400.0, 10000.0);
        if (out == "pq")
        {
            if (aces) t.add(Op::ToneHdr, 203.0f, peak);   // scene 1.0 at 203 cd/m², a shoulder to the peak
            else t.add(Op::Scale, 203.0f);                // SDR white at 203 cd/m² (BT.2408)
            t.add(Op::EncodePq);
        }
        else if (out == "hlg")
        {
            t.add(Op::Scale, 0.265f);                     // reference white at 75 % (BT.2408)
            if (aces) t.add(Op::ShoulderUnit, 0.6f);
            t.add(Op::EncodeHlg);
        }
        else
        {
            if (aces) t.add(Op::ToneSdr);
            if (out == "rec709" || out == "srgb") t.add(Op::EncodeSrgb);
            else t.add(Op::EncodeGamma, out == "p3d65" ? 2.6f : 2.4f);
        }
        char pk[32];
        std::snprintf(pk, sizeof pk, "@%g", (double)peak);
        t.mKey = "out:" + working + ">" + out + (out == "pq" ? std::string(pk) : std::string());
        return t;
    }

    void Transform::map(const float in[3], float out[3]) const
    {
        float p[3] = {in[0], in[1], in[2]};
        for (const Step &s : mOps)
        {
            switch (s.op)
            {
                case Op::DecodeSrgb: for (float &v : p) v = srgbDecode(v); break;
                case Op::EncodeSrgb: for (float &v : p) v = srgbEncode(v); break;
                case Op::EncodeGamma: for (float &v : p) v = std::pow(std::min(1.0f, std::max(0.0f, v)), 1.0f / s.a); break;
                case Op::DecodeAcescct: for (float &v : p) v = acescctDecode(v); break;
                case Op::EncodeAcescct: for (float &v : p) v = acescctEncode(v); break;
                case Op::DecodeLog: for (float &v : p) v = decodeLog(s.curve, v); break;
                case Op::Matrix:
                {
                    const float r = p[0], g = p[1], b = p[2];
                    p[0] = s.m[0] * r + s.m[1] * g + s.m[2] * b;
                    p[1] = s.m[3] * r + s.m[4] * g + s.m[5] * b;
                    p[2] = s.m[6] * r + s.m[7] * g + s.m[8] * b;
                    break;
                }
                case Op::ToneSdr: onMax(p, [](float m) { return shoulder(m, kKnee, 1.0f); }); break;
                case Op::ToneInverse: onMax(p, [](float m) { return shoulderInverse(m, kKnee, 1.0f); }); break;
                case Op::ToneHdr:
                {
                    for (float &v : p) v *= s.a;
                    const float peak = s.b;
                    onMax(p, [peak](float m) { return shoulder(m, 0.5f * peak, peak); });
                    break;
                }
                case Op::EncodePq: for (float &v : p) v = pqEncode(v); break;
                case Op::Scale: for (float &v : p) v *= s.a; break;
                case Op::ShoulderUnit: { const float k = s.a; onMax(p, [k](float m) { return shoulder(m, k, 1.0f); }); break; }
                case Op::EncodeHlg: for (float &v : p) v = hlgEncode(v); break;
            }
        }
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    }

    void Transform::apply(Raster &img) const
    {
        if (identity() || img.empty()) return;
        const int w = img.width;
        const bool deep = img.deep();
        if ((deep ? img.rgba16.size() : img.rgba.size()) < (size_t)w * img.height * 4) return;
        par::parallelFor(img.height, [&](int y0, int y1) {
            float in[3], out[3];
            for (int y = y0; y < y1; ++y)
            {
                if (deep)
                {
                    uint16_t *p = img.rgba16.data() + (size_t)y * w * 4;
                    for (int x = 0; x < w; ++x, p += 4)
                    {
                        for (int c = 0; c < 3; ++c) in[c] = p[c] / 65535.0f;
                        map(in, out);
                        for (int c = 0; c < 3; ++c) p[c] = (uint16_t)(std::min(1.0f, std::max(0.0f, out[c])) * 65535.0f + 0.5f);
                    }
                }
                else
                {
                    uint8_t *p = img.rgba.data() + (size_t)y * w * 4;
                    for (int x = 0; x < w; ++x, p += 4)
                    {
                        for (int c = 0; c < 3; ++c) in[c] = p[c] / 255.0f;
                        map(in, out);
                        for (int c = 0; c < 3; ++c) p[c] = (uint8_t)(std::min(1.0f, std::max(0.0f, out[c])) * 255.0f + 0.5f);
                    }
                }
            }
        });
    }
}
}
}
}
