/*
 *  Arstro ImageProcessing Library
 *
 *  EditEngine: the headless facade the UI codes against. It owns the source
 *  image(s) and a fixed-order pipeline of processors, exposes a FLAT parameter
 *  API (one setter per edit control), and returns processed RGBA8 pixels plus a
 *  histogram. The UI only pushes parameters in and displays what comes out — it
 *  never touches pixels or the processors directly (the DSP -> Pulsar layering,
 *  applied to images).
 *
 *  Interactive edits run on a downscaled PREVIEW proxy (renderPreview); export
 *  runs the full-resolution path (renderFull). Multiple images are held in slots,
 *  each with its own parameter set.
 *
 *  Pipeline order (the hard contract — geometry first so spatial effects and the
 *  histogram describe the framed image, and grain isn't rotated):
 *      Crop -> Rotate -> Exposure -> Contrast -> ToneRegions -> WhiteBalance
 *      -> ToneCurve -> Vibrance -> ColorMixer -> ColorGrading -> Dehaze -> Grain
 */
#pragma once
#include "../base/Image.h"
#include "../base/ImageBlock.h"
#include "../analysis/Histogram.h"
#include "../tone/Exposure.h"
#include "../tone/Contrast.h"
#include "../tone/ToneRegions.h"
#include "../tone/ToneCurve.h"
#include "../color/WhiteBalance.h"
#include "../color/Vibrance.h"
#include "../color/ColorMixer.h"
#include "../color/ColorGrading.h"
#include "../effect/Dehaze.h"
#include "../effect/Grain.h"
#include "../transform/Crop.h"
#include "../transform/Rotate.h"
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

namespace arstro
{
    /** A view onto the engine-owned, gamma-encoded straight RGBA8 output. */
    struct PreviewBuffer
    {
        const uint8_t *rgba = nullptr;  ///< w*h*4 bytes, valid until the next render
        int width = 0;
        int height = 0;
    };

    class EditEngine
    {
    public:
        enum MixerChannel { MixerHue, MixerSat, MixerLum };
        enum GradeRegion { Shadows, Midtones, Highlights };

        EditEngine();

        // ── images / slots ──
        int addImage(const uint8_t *rgba, int width, int height, int channels = 4);
        void selectImage(int slot);
        int imageCount() const { return (int)mSlots.size(); }
        int currentSlot() const { return mCurrent; }
        bool hasImage() const { return mCurrent >= 0; }

        // ── preview / render ──
        void setPreviewSize(int maxEdge);
        PreviewBuffer renderPreview();
        PreviewBuffer renderFull();
        const HistogramData &histogram() const { return mLastHistogram; }

        // ── basic tone ──
        void setExposure(float ev);       // -5..+5
        void setContrast(float v);        // -100..+100
        void setHighlights(float v);      // -100..+100
        void setShadows(float v);
        void setWhites(float v);
        void setBlacks(float v);

        // ── white balance / presence ──
        void setTemperature(float kelvin);  // 2000..50000
        void setTint(float v);              // -150..+150
        void setVibrance(float v);          // -100..+100
        void setSaturation(float v);
        void setDehaze(float v);            // -100..+100
        void setGrainAmount(float v);       // 0..100
        void setGrainSize(float v);         // 0..100

        // ── tone curve ──
        void setCurvePoints(const std::vector<std::pair<float, float>> &pts);
        void setCurveLogScale(bool log);

        // ── colour mixer (cyclic per-hue curves) ──
        // points: (hue 0..360, y in [-1,1]); the curve wraps at the 360/0 seam.
        void setMixerCurve(MixerChannel c, const std::vector<std::pair<float, float>> &points);

        // ── colour grading (3-way wheels + hue remap) ──
        void setGradeHue(GradeRegion r, float deg);   // 0..360
        void setGradeSaturation(GradeRegion r, float v);  // 0..100
        void setGradeLuminance(GradeRegion r, float v);   // -100..+100
        void setGradeBalance(float v);                 // -100..+100
        void setHueRemapEnabled(bool on);
        void setHueRemap(float srcHueDeg, float rangeDeg, float dstHueDeg, float strength01);

        // ── transform ──
        void setCrop(float x, float y, float w, float h);  // normalized 0..1
        void resetCrop();
        void setRotation(float degrees);                    // straighten
        void setQuarterTurns(int turns);                    // 0..3

        // ── global ──
        void setBypass(bool b);
        void resetAll();

    private:
        struct Grade { float hue = 0, sat = 0, lum = 0; };
        struct Params
        {
            float exposure = 0, contrast = 0;
            float highlights = 0, shadows = 0, whites = 0, blacks = 0;
            float temp = 6500, tint = 0;
            float vibrance = 0, saturation = 0;
            float dehaze = 0, grainAmount = 0, grainSize = 0;
            std::vector<std::pair<float, float>> curve{{0.f, 0.f}, {1.f, 1.f}};
            bool curveLog = true;
            std::array<std::vector<std::pair<float, float>>, 3> mixer{};  // hue/sat/lum curves
            std::array<Grade, 3> grade{};
            float balance = 0;
            bool remapEnable = false;
            float remapSrc = 0, remapRange = 30, remapDst = 0, remapStrength = 0;
            float cropX = 0, cropY = 0, cropW = 1, cropH = 1;
            float rotation = 0;
            int quarterTurns = 0;
        };
        struct Slot { Image source; Params params; };

        void buildPipeline();
        void applyParamsToProcessors();
        PreviewBuffer renderInto(const Image &linearSource, std::vector<uint8_t> &outBytes);
        void ensurePreviewProxy();
        Params *cur() { return mCurrent >= 0 ? &mSlots[mCurrent].params : nullptr; }

        std::vector<Slot> mSlots;
        int mCurrent = -1;
        int mPreviewMaxEdge = 2048;

        Image mPreviewProxy;
        int mProxySlot = -1;
        int mProxyEdge = -1;

        ImageBlock mPipeline;
        Crop mCrop;
        Rotate mRotate;
        Exposure mExposure;
        Contrast mContrast;
        ToneRegions mToneRegions;
        WhiteBalance mWhiteBalance;
        ToneCurve mToneCurve;
        Vibrance mVibrance;
        ColorMixer mColorMixer;
        ColorGrading mColorGrading;
        Dehaze mDehaze;
        Grain mGrain;

        std::vector<uint8_t> mPreviewOut;
        std::vector<uint8_t> mFullOut;
        HistogramData mLastHistogram;
    };
}
