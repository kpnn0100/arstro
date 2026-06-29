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
#include <array>
#include <utility>
#include <vector>

namespace arstro
{
    struct GradeWheel { float hue = 0, sat = 0, lum = 0; };

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
        // colour mixer: 3 cyclic per-hue curves (hue / sat / lum)
        std::array<std::vector<std::pair<float, float>>, 3> mixer{};
        // colour grading: 3-way wheels + balance + hue-range remap
        std::array<GradeWheel, 3> grade{};
        float balance = 0;
        bool remapEnable = false;
        float remapSrc = 0, remapRange = 30, remapDst = 0, remapStrength = 0;
        // transform
        float cropX = 0, cropY = 0, cropW = 1, cropH = 1;
        float rotation = 0;
        int quarterTurns = 0;
    };
}
