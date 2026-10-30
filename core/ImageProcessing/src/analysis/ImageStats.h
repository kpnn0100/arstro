/*
 *  Arstro ImageProcessing Library
 *
 *  ImageStats: what a photo IS, measured — its tone, its colour and its light — so that a
 *  grader (a person reading numbers, or an agent deciding an edit) starts from evidence
 *  rather than from a guess about the picture. An analysis SINK like Histogram: it reads
 *  pixels and never changes them, and it is not in the processing chain.
 *
 *  It reads DISPLAY-encoded RGBA8 (what a render hands out), because that is the picture a
 *  person judges; quantities that are only meaningful in light (key, dynamic range, the
 *  colour cast) are computed after decoding to linear, and say so in their names.
 *
 *  Measurement only, never interpretation: this file does not decide that a photo is "a
 *  night street" or "too dark". Those are judgements, and they belong to whoever grades —
 *  the numbers here are what makes a judgement checkable.
 *
 *  Platform-free and codec-free, like the rest of arstro_image.
 */
#pragma once
#include <array>
#include <cstdint>

namespace arstro
{
    struct ImageStats
    {
        int width = 0, height = 0;

        // ── tone (display-referred luminance, 0..1) ──
        /** Percentiles of display luminance: p1 p5 p25 p50 p75 p95 p99. */
        std::array<float, 7> lumPct{};
        float lumMean = 0;          ///< mean display luminance
        float lumStdDev = 0;        ///< global contrast: spread of display luminance
        /** Log-average LINEAR luminance as stops from 18 % grey (Reinhard's key): 0 is a
         *  normally exposed average scene, -2 a night scene, +1 a bright snowfield. */
        float keyEv = 0;
        /** Stops between the 1st and 99th percentile of LINEAR luminance (floored at 1/1024),
         *  i.e. how much scene range survived into the frame. */
        float rangeStops = 0;
        float clipShadows = 0;      ///< fraction with every channel <= 2/255
        float clipHighlights = 0;   ///< fraction with any channel >= 254/255

        // ── colour ──
        float meanSaturation = 0;   ///< HSV saturation, display-referred, 0..1
        /** Hasler & Süsstrunk colourfulness on 0..255 values: ~0 grey, ~33 moderately
         *  colourful, ~59 highly colourful, >80 extremely. */
        float colourfulness = 0;

        /** The colour cast, in LINEAR light, as log2 ratios: `warmth` = log2(R/B) (+ amber,
         *  - blue), `tint` = log2(G / sqrt(R*B)) (+ green, - magenta). Measured on the
         *  near-neutral pixels when enough of them exist (`castFromNeutrals`), else on the
         *  grey-world mean of every unclipped pixel. */
        float castWarmth = 0, castTint = 0;
        bool castFromNeutrals = false;
        float neutralFraction = 0;  ///< share of pixels near neutral (the cast's evidence)

        /** The same cast and the mean saturation inside three luminance zones — shadows
         *  (display L < 0.25), midtones, highlights (> 0.70) — so an existing split tone, or a
         *  mixed-light scene (warm lamps, blue shadows), is visible rather than averaged away. */
        struct Zone { float fraction = 0, warmth = 0, tint = 0, saturation = 0; };
        std::array<Zone, 3> zones{};

        /** Chroma-weighted hue distribution in 12 bins of 30 degrees starting at 0 (red),
         *  summing to 1 over the coloured pixels; `colouredFraction` is how many there were. */
        std::array<float, 12> hues{};
        float colouredFraction = 0;

        // ── content hints (fractions of the frame; evidence, not verdicts) ──
        float skinFraction = 0;     ///< hue 0-50, moderate saturation, R > G > B
        float skyFraction = 0;      ///< blue-cyan, bright, in the upper 40 % of the frame
        float foliageFraction = 0;  ///< yellow-green to green, saturated
        float neonFraction = 0;     ///< very saturated AND bright: signs, LEDs, neon tubes
    };

    /** Measure straight RGBA8 (display-encoded sRGB). An empty image measures as all zeros. */
    ImageStats measureImage(const uint8_t *rgba, int width, int height);
}
