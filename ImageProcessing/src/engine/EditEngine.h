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
 *  histogram describe the framed image):
 *      Crop -> Rotate -> Exposure -> Contrast -> ToneRegions -> WhiteBalance
 *      -> ToneCurve -> Vibrance -> ColorMixer -> ColorGrading -> Dehaze -> Grain
 *  This milestone implements the Exposure + Contrast stages; later stages slot in
 *  at their reserved position without changing the contract.
 */
#pragma once
#include "../base/Image.h"
#include "../base/ImageBlock.h"
#include "../analysis/Histogram.h"
#include "../tone/Exposure.h"
#include "../tone/Contrast.h"
#include <cstdint>
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
        EditEngine();

        // ── images / slots ──
        /** Add a gamma-encoded straight RGBA8 (or RGB8) image; returns its slot. */
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

        // ── flat parameter API (grows as processors are added) ──
        void setExposure(float ev);    // -5..+5
        void setContrast(float v);     // -100..+100

        void setBypass(bool b);        // show the unedited image
        void resetAll();               // current slot's params -> defaults

    private:
        struct Params
        {
            float exposure = 0.0f;
            float contrast = 0.0f;
        };
        struct Slot
        {
            Image source;  // full-res, linear light
            Params params;
        };

        void buildPipeline();
        void applyParamsToProcessors();
        PreviewBuffer renderInto(const Image &linearSource, std::vector<uint8_t> &outBytes);
        void ensurePreviewProxy();

        std::vector<Slot> mSlots;
        int mCurrent = -1;
        int mPreviewMaxEdge = 2048;

        // proxy cache
        Image mPreviewProxy;
        int mProxySlot = -1;
        int mProxyEdge = -1;

        ImageBlock mPipeline;
        Exposure mExposure;
        Contrast mContrast;

        std::vector<uint8_t> mPreviewOut;
        std::vector<uint8_t> mFullOut;
        HistogramData mLastHistogram;
    };
}
