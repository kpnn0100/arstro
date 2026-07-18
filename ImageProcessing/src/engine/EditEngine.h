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
 *  histogram describe the framed image, noise reduction before tone amplifies it,
 *  and sharpening/grain last so they aren't blurred or rotated):
 *      Crop -> Rotate -> LensCorrection -> NoiseReduction -> Exposure -> Contrast
 *      -> ToneRegions -> WhiteBalance -> ToneCurve -> Texture -> Clarity
 *      -> Vibrance -> ColorMixer -> ColorGrading -> Dehaze -> Sharpen -> Grain
 */
#pragma once
#include "../base/Image.h"
#include "../base/ImageBlock.h"
#include "../analysis/Histogram.h"
#include "../compute/ComputeBackend.h"
#include "EditParams.h"
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
#include "../effect/Texture.h"
#include "../effect/Clarity.h"
#include "../detail/Sharpen.h"
#include "../detail/NoiseReduction.h"
#include "../transform/Crop.h"
#include "../transform/Rotate.h"
#include "../transform/LensCorrection.h"
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
        /** Free a slot's source pixels in place (its index stays valid but unusable) --
         *  for "remove from session" without shifting every other slot's index. */
        void releaseImage(int slot);
        /** Drop every slot and reset selection — for a full workspace reset, after
         *  which the next addImage() starts from slot 0 again (restores the
         *  "slot id == index" invariant the front end relies on). */
        void clearImages();
        int imageCount() const { return (int)mSlots.size(); }
        int currentSlot() const { return mCurrent; }
        bool hasImage() const { return mCurrent >= 0; }

        // ── preview / render ──
        void setPreviewSize(int maxEdge);
        PreviewBuffer renderPreview();
        PreviewBuffer renderFull();
        const HistogramData &histogram() const { return mLastHistogram; }
        /** Luminance histogram of the image entering the tone curve (pre-curve). */
        const HistogramData &preCurveHistogram() const { return mPreCurveHist; }
        /** Hue distribution of the image entering the colour mixer (pre-mixer). */
        const HueHistogram &preMixerHue() const { return mPreMixerHue; }

        // ── whole-EditParams API (UI-independent; the reusable seam) ──
        void applyParams(const EditParams &p);                  // push a full set to the pipeline
        const EditParams &currentParams() const;                // current slot's params
        void setCurrentParams(const EditParams &p);             // replace current slot's params
        /** Decode straight RGBA8/RGB8 (gamma sRGB) bytes into a linear-light Image. */
        static Image fromEncodedBytes(const uint8_t *rgba, int width, int height, int channels);
        /** Render ANY linear image with a param set (downscaled to maxEdge if larger).
         *  This is the seam a video editor reuses: decode a frame -> Image -> renderImage.
         *  Returns engine-owned RGBA8, valid until the next render on this engine. */
        PreviewBuffer renderImage(const Image &linearSrc, const EditParams &p, int maxEdge);

        // ── compute backend (GPU acceleration seam; the CPU pipeline is the reference + fallback, R-GPU) ──
        /** Opt into the GPU accelerator when one is available; else render on the CPU. */
        void setPreferGpu(bool prefer) { mPreferGpu = prefer; }
        bool preferGpu() const { return mPreferGpu; }
        /** True when a platform GPU accelerator exists and is usable on this device. */
        bool gpuAvailable() const { return mAccel && mAccel->available(); }
        /** "CPU" or the accelerator's name — the backend the next render will use. */
        const char *activeBackendName() const { return (mPreferGpu && gpuAvailable()) ? mAccel->name() : "CPU"; }
        /** Injection seam (tests / a per-platform host): replace the accelerator the
         *  ctor installed from createComputeAccelerator(). nullptr forces CPU-only. */
        void setComputeAccelerator(std::unique_ptr<IComputeBackend> backend) { mAccel = std::move(backend); }

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
        void setTexture(float v);           // -100..+100 (fine local contrast)
        void setClarity(float v);           // -100..+100 (midtone local contrast)
        void setDehaze(float v);            // -100..+100
        void setGrainAmount(float v);       // 0..100
        void setGrainSize(float v);         // 0..100

        // ── detail (sharpening + noise reduction) ──
        void setSharpenAmount(float v);     // 0..150
        void setSharpenRadius(float px);    // 0.5..3
        void setSharpenMasking(float v);    // 0..100
        void setNoiseLuminance(float v);    // 0..100
        void setNoiseColor(float v);        // 0..100

        // ── lens corrections ──
        void setLensDistortion(float v);    // -100..+100
        void setLensCA(float v);            // -100..+100
        void setLensVignette(float v);      // -100..+100

        // ── tone curve ──
        void setCurvePoints(const std::vector<std::pair<float, float>> &pts);
        void setCurveLogScale(bool log);

        // ── colour mixer (cyclic per-hue curves) ──
        // points: bezier control points (hue 0..360, y in [-1,1]); the curve wraps at the 360/0 seam.
        void setMixerCurve(MixerChannel c, const std::vector<CurvePoint> &points);

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

        // ── local adjustments (masks) ──
        void setMasks(const std::vector<MaskParams> &masks);

        // ── global ──
        void setBypass(bool b);
        void resetAll();

    private:
        // Each slot caches its own downscaled preview proxy so SWITCHING between
        // already-viewed images is instant (no re-downscale); an LRU caps how many
        // proxies are kept in memory at once.
        struct Slot { Image source; EditParams params; Image proxy; int proxyEdge = -1; };

        void buildPipeline();
        PreviewBuffer renderInto(const Image &linearSource, const EditParams &params, std::vector<uint8_t> &outBytes);
        void ensurePreviewProxy();
        void touchProxyLRU(int slot);   // mark `slot`'s proxy most-recently-used; evict the oldest beyond the cap
        void dropProxy(int slot);       // free a slot's cached proxy + drop it from the LRU
        EditParams *cur() { return mCurrent >= 0 ? &mSlots[mCurrent].params : nullptr; }

        static constexpr int kMaxProxies = 6;  // ~cached preview images (bounds memory)

        std::vector<Slot> mSlots;
        int mCurrent = -1;
        int mPreviewMaxEdge = 2048;
        std::vector<int> mProxyLRU;   // slots holding a live proxy, most-recent first

        // pipeline split into three segments so histograms can be tapped at the
        // boundaries: pre (before ToneCurve), mid (before ColorMixer), post.
        ImageBlock mChainPre, mChainMid, mChainPost;
        Crop mCrop;
        Rotate mRotate;
        LensCorrection mLens;
        NoiseReduction mNoiseReduction;
        Exposure mExposure;
        Contrast mContrast;
        ToneRegions mToneRegions;
        WhiteBalance mWhiteBalance;
        ToneCurve mToneCurve;
        Texture mTexture;
        Clarity mClarity;
        Vibrance mVibrance;
        ColorMixer mColorMixer;
        ColorGrading mColorGrading;
        Dehaze mDehaze;
        Sharpen mSharpen;
        Grain mGrain;

        std::vector<MaskParams> mMasks;
        std::unique_ptr<IComputeBackend> mAccel;  // optional accelerator (nullptr = CPU only); the CPU path is the reference
        bool mPreferGpu = false;                  // user opt-in; only takes effect when mAccel->available()
        std::vector<uint8_t> mPreviewOut;
        std::vector<uint8_t> mFullOut;
        HistogramData mLastHistogram;
        HistogramData mPreCurveHist;   // luma entering ToneCurve
        HueHistogram mPreMixerHue;     // hue entering ColorMixer
    };
}
