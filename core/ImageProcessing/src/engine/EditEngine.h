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
        /** Whether the LAST render actually ran on the accelerator. An available backend
         *  may still DECLINE a job it does not fully support (IComputeBackend::process
         *  returning false), in which case the CPU reference path produced the pixels —
         *  so activeBackendName() states the intent and this states the outcome. Read-only
         *  observability: a caller that reports which backend produced a result (a
         *  benchmark, a diagnostics panel) cannot otherwise tell the two apart. */
        bool lastRenderAccelerated() const { return mLastAccelerated; }
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
        void setCurvePoints(const std::vector<CurvePoint> &pts);  // RGB master (bezier control points)
        void setCurveChannelPoints(int ch, const std::vector<CurvePoint> &pts);  // ch 0=R,1=G,2=B
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

        // ── resident pixel memory (R-MEM) ────────────────────────────────────────────
        /** Cap the two pixel pools, in BYTES. A slot's *source* is the decoded image in
         *  linear float RGBA — 24 MP is 387 MB — and holding one per slot made resident
         *  memory a function of how many photos the project had rather than of how many
         *  the user is looking at: 465 MB per photo measured, ~54 GB for a 120-RAW
         *  catalog. Bytes rather than a count of images on purpose: an image is not a
         *  unit of memory, so `kMaxProxies = 6` silently meant six times whatever the
         *  camera produced (R-MEM-1).
         *
         *  Proxies get the larger share because browsing is what must stay instant and a
         *  proxy is ~14x smaller than its source; sources are kept only for the few slots
         *  actually being rendered or exported (R-MEM-5). */
        void setMemoryCaps(size_t sourceBytes, size_t proxyBytes);
        size_t sourceCapBytes() const { return mSourceCap; }
        size_t proxyCapBytes() const { return mProxyCap; }
        /** Measured, not assumed (R-MEM-4): what the two pools actually hold right now. */
        size_t residentSourceBytes() const;
        size_t residentProxyBytes() const;
        size_t residentBytes() const { return residentSourceBytes() + residentProxyBytes(); }

        /** True when `slot` exists but both its source and a usable proxy have been
         *  evicted — it is **cold**, not broken (R-MEM-2). The caller re-decodes the
         *  original file and calls `supplySource`; `RenderService` does this for its own
         *  worker through the `SourceLoader` seam. */
        bool slotNeedsSource(int slot) const;
        /** Whether `slot` still holds full-resolution pixels. A preview can run off a
         *  proxy, but `renderFull` (export) cannot, so the two questions are different. */
        bool slotHasSource(int slot) const;
        /** Hand an evicted slot its pixels back. Same bytes `addImage` takes. */
        bool supplySource(int slot, const uint8_t *rgba, int width, int height, int channels = 4);
        /** How many times eviction has forced a re-decode. Read back by the model so
         *  R-MEM-5's "the caps are generous enough" is a number and not a hope. */
        int rehydrations() const { return mRehydrations; }

    private:
        // Each slot caches its own downscaled preview proxy so SWITCHING between
        // already-viewed images is instant (no re-downscale); a byte-capped LRU bounds
        // how much the proxies and the full-resolution sources may hold at once (R-MEM-1).
        // `released` is what tells a slot that was REMOVED from the session apart from one
        // that was merely EVICTED (R-MEM-1). Both have no source, and before the byte caps
        // existed the engine could read "no source" as "dead" because the two never differed
        // — `releaseImage` was the only way a source went away. Now eviction is routine, so
        // conflating them would make every cached-out photo unselectable instead of slow.
        struct Slot { Image source; EditParams params; Image proxy; int proxyEdge = -1; bool released = false; };

        void buildPipeline();
        PreviewBuffer renderInto(const Image &linearSource, const EditParams &params, std::vector<uint8_t> &outBytes);
        /** False when the slot is cold — no proxy at this size and no source to build one
         *  from (R-MEM-2). The caller must re-decode before it can render. */
        bool ensurePreviewProxy();
        void touchProxyLRU(int slot);   // mark `slot`'s proxy most-recently-used; evict back to the byte cap
        void touchSourceLRU(int slot);  // same, for full-resolution sources (R-MEM-1)
        void dropProxy(int slot);       // free a slot's cached proxy + drop it from the LRU
        void dropSource(int slot);      // free a slot's source + drop it from the source LRU
        static size_t imageBytes(const Image &i) { return i.pixelCount() * (size_t)i.channels() * sizeof(Pixel); }
        EditParams *cur() { return mCurrent >= 0 ? &mSlots[mCurrent].params : nullptr; }

        // Defaults, in bytes. ~1 GB of proxies is ~37 previews at 1600 px — enough that
        // stepping along a filmstrip hits resident pixels (R-MEM-5) — and ~0.8 GB of
        // sources is two 24 MP frames, which is what rendering and exporting need at once.
        // A host may raise or lower both; they are not a function of the project's size.
        static constexpr size_t kDefaultSourceCap = (size_t)800 * 1024 * 1024;
        static constexpr size_t kDefaultProxyCap = (size_t)1024 * 1024 * 1024;

        std::vector<Slot> mSlots;
        int mCurrent = -1;
        int mPreviewMaxEdge = 2048;
        std::vector<int> mProxyLRU;    // slots holding a live proxy, most-recent first
        std::vector<int> mSourceLRU;   // slots holding full-resolution pixels, most-recent first
        size_t mSourceCap = kDefaultSourceCap;
        size_t mProxyCap = kDefaultProxyCap;
        int mRehydrations = 0;

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
        bool mLastAccelerated = false;  ///< did the accelerator take the last render?
        HistogramData mPreCurveHist;   // luma entering ToneCurve
        HueHistogram mPreMixerHue;     // hue entering ColorMixer
    };
}
