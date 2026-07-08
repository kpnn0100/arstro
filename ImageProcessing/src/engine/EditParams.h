/*
 *  Arstro ImageProcessing Library
 *
 *  EditParams: the complete, UI-independent description of an edit. It is plain data
 *  (no Artboard, no threads, no rendering) so it can be built by ANY front end — the
 *  cosmo photo editor, a future video editor, a batch CLI, a preset file — and handed
 *  to EditEngine to render. Keeping the parameter set separate from both the engine's
 *  processors and the UI is what makes the core reusable.
 */
#pragma once
#include "../base/CurvePoint.h"
#include <array>
#include <utility>
#include <vector>

namespace arstro
{
    struct GradeWheel { float hue = 0, sat = 0, lum = 0; };

    /** The subset of adjustments a local mask can carry (relative shifts, all
     *  identity at 0). temp/tint are -100..100 relative, not Kelvin. */
    struct LocalAdjust
    {
        float exposure = 0, contrast = 0;
        float highlights = 0, shadows = 0, whites = 0, blacks = 0;
        float temp = 0, tint = 0, saturation = 0;
        float texture = 0, clarity = 0, dehaze = 0;
    };

    /** One brush stamp in normalised framed-image coords (0..1). */
    struct BrushDab { float x = 0, y = 0, radius = 0.05f, flow = 1.f; };

    /** A local-adjustment mask: a coverage region (0..1) + the adjustments applied
     *  through it. All geometry is normalised to the framed image so it is
     *  resolution-independent (and matches the UI overlay 1:1). */
    struct MaskParams
    {
        enum Type { Radial = 0, Linear = 1, Brush = 2 };
        int type = Radial;
        bool inverted = false;
        float feather = 0.5f;                 // 0..1 edge softness
        // radial: centre + half-extents
        float cx = 0.5f, cy = 0.5f, rx = 0.3f, ry = 0.3f;
        // linear: gradient from p0 (0%) to p1 (100%)
        float x0 = 0.5f, y0 = 0.35f, x1 = 0.5f, y1 = 0.65f;
        // brush: union of dabs
        std::vector<BrushDab> dabs;
        LocalAdjust adjust;
    };

    struct EditParams
    {
        // basic tone
        float exposure = 0, contrast = 0;
        float highlights = 0, shadows = 0, whites = 0, blacks = 0;
        // colour / presence
        float temp = 6500, tint = 0, vibrance = 0, saturation = 0;
        // presence: local contrast
        float texture = 0, clarity = 0;
        // effects
        float dehaze = 0, grainAmount = 0, grainSize = 0;
        // detail: sharpening + noise reduction
        float sharpenAmount = 0, sharpenRadius = 1, sharpenMasking = 0;
        float nrLuminance = 0, nrColor = 0;
        // lens corrections
        float lensDistortion = 0, lensCA = 0, lensVignette = 0;
        // tone curve (display-domain control points) + domain
        std::vector<std::pair<float, float>> curve{{0.f, 0.f}, {1.f, 1.f}};
        bool curveLog = true;
        // colour mixer: 3 cyclic per-hue curves (hue / sat / lum), stored as bezier
        // CONTROL points so a reopened project restores the exact editable curve.
        std::array<std::vector<CurvePoint>, 3> mixer{};
        // colour grading: 3-way wheels + balance + hue-range remap
        std::array<GradeWheel, 3> grade{};
        float balance = 0;
        bool remapEnable = false;
        float remapSrc = 0, remapRange = 30, remapDst = 0, remapStrength = 0;
        // transform
        float cropX = 0, cropY = 0, cropW = 1, cropH = 1;
        float rotation = 0;
        int quarterTurns = 0;
        // local adjustments (masks), applied after the global pipeline
        std::vector<MaskParams> masks;
    };
}
