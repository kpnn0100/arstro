/*
 *  interstellar_render — ColourTransform: colour management AROUND Cosmo's grade (R-COLOR-2..4).
 *
 *      decode → INPUT transform (the source's space → the working space) → Cosmo grades
 *             → effects → composite → OUTPUT transform (the working space → the delivery)
 *
 *  Cosmo grades what it is given as display-referred, sRGB-encoded pixels; it knows nothing of
 *  camera spaces and needs to. The transforms here are the media's INTERPRETATION, never a grade:
 *  a source says what it IS (a #rackobj's `input`), the project says where it is graded (the
 *  header's `colorspace`), a render says what it delivers (`--output`). Colour stays Cosmo's.
 *
 *  Working spaces:
 *    rec709   display-referred — Cosmo's own. A camera-log source is decoded to scene light,
 *             converted to Rec.709 primaries, tone-mapped (below) and sRGB-encoded, so Cosmo grades
 *             a viewable picture. Delivered as graded: the rec709 output changes nothing.
 *    acescct  scene-referred. Every source is converted to ACES AP1 linear and ACEScct-encoded,
 *             and Cosmo grades the ACEScct values as its encoded input — as a colourist's tools
 *             grade ACEScct values. The output transform then does the tone mapping, SDR or HDR.
 *
 *  Inputs, from the vendors' published formulas (a log curve on the file's 10-bit code values;
 *  the camera files carry video-range YCbCr, so FFmpeg's 0..1 RGB is mapped back to code values
 *  first — Canon Log 3 alone is defined on that IRE scale directly): ARRI LogC3 (EI 800, AWG3) and
 *  LogC4 (AWG4), Sony S-Log3 (S-Gamut3.Cine), Panasonic V-Log (V-Gamut), Canon Log 3 (Cinema Gamut),
 *  RED Log3G10 (REDWideGamutRGB), Blackmagic Film Gen 5 (BMD Wide Gamut Gen 5); plus Rec.709 / sRGB
 *  (display-referred: no transform in rec709) and linear (scene light in Rec.709 primaries).
 *
 *  Outputs: rec709 (the graded code values, BT.709-tagged — what the monitor showed), rec709-2.4
 *  (display light re-encoded with a pure 2.4 power, a BT.1886 master), srgb, p3d65 (P3 primaries,
 *  gamma 2.6), pq (Rec.2100 PQ, Rec.2020 primaries, SDR white at 203 cd/m² — BT.2408 — and a
 *  highlight shoulder to the mastering peak) and hlg (Rec.2100 HLG, reference white at 75 %).
 *
 *  The tone map is ONE function, shared by the rec709 working space's input transform and every
 *  SDR output of acescct, so a log source looks the same either way: on max(R,G,B), ratio-
 *  preserving (no hue shift), the identity up to 0.6 and a rational shoulder above it (slope 1 at
 *  the knee) that approaches 1 slowly enough to keep a log camera's top stops apart — 18 % grey
 *  stays 18 %. It is this program's tone map, not ACES's RRT, and is
 *  said so in DR-COLOR-2. Gamut changes are 3×3 matrices built from the primaries (Bradford
 *  adaptation between D65 and the ACES white). Negative (out-of-gamut) light is clipped at the
 *  encode.
 *
 *  Float maths per pixel, rows in parallel; 8-bit and deep (16-bit) rasters alike, alpha untouched.
 */
#pragma once
#include "Raster.h"
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
namespace render
{
namespace colour
{
    struct Def
    {
        const char *id, *label;
    };
    /** Every source space, in menu order (the first is the default). */
    const std::vector<Def> &inputs();
    /** rec709 (the default), acescct. */
    const std::vector<Def> &workings();
    /** rec709 (the default), rec709-2.4, srgb, p3d65, pq, hlg. */
    const std::vector<Def> &outputs();
    bool known(const std::vector<Def> &list, const std::string &id);
    const char *label(const std::vector<Def> &list, const std::string &id);
    /** pq and hlg: 10 bits at least, Rec.2020 primaries, HDR signalling. */
    bool isHdr(const std::string &output);

    class Transform
    {
    public:
        /** The source's space → the working space's encoded values. Unknown ids: identity. */
        static Transform input(const std::string &input, const std::string &working);
        /** The working space → the delivery. `peakNits` is PQ's mastering peak. */
        static Transform output(const std::string &working, const std::string &output, double peakNits = 1000.0);

        bool identity() const { return mOps.empty(); }
        /** In place; 8-bit or deep. */
        void apply(Raster &img) const;
        /** One pixel, encoded 0..1 in, encoded out (unclamped). */
        void map(const float in[3], float out[3]) const;
        /** What it does, for cache keys and the docs ("" = identity). */
        const std::string &key() const { return mKey; }

        enum class Op
        {
            DecodeSrgb, EncodeSrgb, EncodeGamma, DecodeAcescct, EncodeAcescct, DecodeLog, Matrix,
            ToneSdr, ToneInverse, ToneHdr, EncodePq, Scale, ShoulderUnit, EncodeHlg
        };

    private:
        struct Step
        {
            Op op;
            float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};   // Matrix
            float a = 0, b = 0;                          // the op's numbers (gamma, scale, peak …)
            int curve = 0;                               // DecodeLog: which camera curve
        };
        void add(Op op, float a = 0, float b = 0);
        void addMatrix(const double m[9]);
        std::vector<Step> mOps;
        std::string mKey;
    };
}
}
}
}
