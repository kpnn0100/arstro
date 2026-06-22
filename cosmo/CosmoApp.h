/*
 *  Cosmo by arstro — a Lightroom-style photo editor built on Artboard (UI) and the
 *  ImageProcessing library (headless edit engine). Mirrors pulsar/PulsarApp: a root
 *  Segment tree, render()/pointer(), Pulsar-style dark theme.
 *
 *  Strict layering: the UI only pushes parameters into the EditEngine and displays
 *  its output (preview image + histogram). It never touches pixels directly.
 *
 *  Right column: an always-visible histogram (log/linear) above a TabView of edit
 *  sections — Light (basic tone), Color (WB + presence), FX (dehaze/grain), Mixer
 *  (HSL). Curve/Grade/Transform sections arrive with their custom widgets.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "../ImageProcessing/src/image_processing.h"
#include "CosmoTheme.h"
#include "panels/ParamPanel.h"
#include "panels/HistogramPanel.h"
#include "panels/ColorMixerPanel.h"
#include "panels/ToneCurvePanel.h"
#include "panels/ColorGradingPanel.h"
#include "panels/TransformPanel.h"
#include "widgets/Filmstrip.h"
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class CosmoApp
    {
    public:
        CosmoApp(double width, double height);

        void render(artboard::IRenderTarget &target, double nowMs);
        void pointer(int kind, double x, double y, int button, double timeMs);

        /** Open an image from straight RGBA8 bytes; returns its slot (or -1). */
        int openImage(const uint8_t *rgba, int w, int h, const std::string &name);
        void selectImage(int slot);
        int imageCount() const { return mEngine.imageCount(); }

        /** Render the current edit at full resolution. Returns engine-owned straight
         *  RGBA8 (valid until the next full render), or nullptr if no image. */
        const uint8_t *exportFullRes(int &w, int &h);

    private:
        // The UI's mirror of each image's parameters (knob positions), restored on
        // slot switch — the engine keeps its own per-slot state for the pipeline.
        struct UiParams
        {
            double exposure = 0, contrast = 0, highlights = 0, shadows = 0, whites = 0, blacks = 0;
            double temp = 6500, tint = 0, vibrance = 0, saturation = 0;
            double dehaze = 0, grainAmount = 0, grainSize = 0;
            std::array<std::array<double, 3>, 8> mixer{};
            std::vector<std::pair<float, float>> curve{{0.f, 0.f}, {1.f, 1.f}};
            bool curveLog = true;
            std::array<std::array<double, 3>, 3> grade{};
            double balance = 0;
            bool remapOn = false;
            double remapSrc = 0, remapRange = 30, remapDst = 0, remapStrength = 0;
            double rotation = 0; int quarter = 0;
            double cropX = 0, cropY = 0, cropW = 1, cropH = 1;
        };

        void rebuildPreview();
        void markDirty() { mDirty = true; }
        void syncControlsToSlot();
        UiParams *curUi();

        double mW, mH;
        artboard::Rect mPhotoRect;
        artboard::Color mAccent;

        arstro::EditEngine mEngine;
        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<artboard::ImageView> mImageView;
        std::shared_ptr<HistogramPanel> mHistogram;
        std::shared_ptr<artboard::TabView> mTabs;
        std::shared_ptr<ParamPanel> mBasic;
        std::shared_ptr<ParamPanel> mColor;
        std::shared_ptr<ParamPanel> mEffects;
        std::shared_ptr<ColorMixerPanel> mMixer;
        std::shared_ptr<ToneCurvePanel> mCurve;
        std::shared_ptr<ColorGradingPanel> mGrade;
        std::shared_ptr<TransformPanel> mXform;
        std::shared_ptr<Filmstrip> mFilmstrip;

        artboard::Theme mTheme;
        artboard::GestureRecognizer mRecognizer;

        std::vector<UiParams> mSlotParams;
        std::vector<std::string> mSlotNames;
        bool mDirty = false;
    };
}
}
