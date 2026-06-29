/*
 *  Cosmo by arstro — a Lightroom-style photo editor built on Artboard (UI) and the
 *  ImageProcessing library (headless edit engine). Mirrors pulsar/PulsarApp.
 *
 *  Strict layering: the UI owns only an EditParams per image and submits it to a
 *  RenderService, which runs the EditEngine on its OWN worker thread (UI core +
 *  engine core). The UI never renders pixels or blocks; it polls finished frames.
 *  The engine + EditParams + RenderService are UI-free and reusable (video editor).
 *
 *  Right column: histogram (log/linear) above a TabView of edit sections — Basic,
 *  Mixer, Curve, Grade, Transform, Settings. Layout is responsive (setSize).
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "../ImageProcessing/src/image_processing.h"
#include "CosmoTheme.h"
#include "panels/ParamPanel.h"
#include "panels/HistogramPanel.h"
#include "panels/MixerPanel.h"
#include "panels/ToneCurvePanel.h"
#include "panels/ColorGradingPanel.h"
#include "panels/TransformPanel.h"
#include "panels/SettingsPanel.h"
#include "widgets/Filmstrip.h"
#include <cstdint>
#include <memory>
#include <string>
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
        void pointer(int kind, double x, double y, int button, double timeMs, bool alt = false);
        void setSize(double width, double height);

        int openImage(const uint8_t *rgba, int w, int h, const std::string &name);
        void selectImage(int slot);
        int imageCount() const { return (int)mSlotParams.size(); }

        const uint8_t *exportFullRes(int &w, int &h);

    private:
        void layout();
        void submit();                 // push the current slot's params to the render service
        void syncControlsToSlot();
        EditParams *curParams();

        double mW, mH;
        artboard::Rect mPhotoRect;
        artboard::Color mAccent;

        arstro::RenderService mService;  // engine on its own thread
        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<artboard::ImageView> mImageView;
        std::shared_ptr<HistogramPanel> mHistogram;
        std::shared_ptr<artboard::TabView> mTabs;
        std::shared_ptr<ParamPanel> mBasic;
        std::shared_ptr<MixerPanel> mMixer;
        std::shared_ptr<ToneCurvePanel> mCurve;
        std::shared_ptr<ColorGradingPanel> mGrade;
        std::shared_ptr<TransformPanel> mXform;
        std::shared_ptr<SettingsPanel> mSettings;
        std::shared_ptr<Filmstrip> mFilmstrip;

        artboard::Theme mTheme;
        artboard::GestureRecognizer mRecognizer;

        std::vector<EditParams> mSlotParams;  // UI-authoritative per-image params
        std::vector<std::string> mSlotNames;
        int mCurrentSlot = -1;
        int mPreviewEdge = 1600;
        RenderService::Frame mExportFrame;
    };
}
}
