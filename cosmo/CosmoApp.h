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
#include "widgets/MenuBar.h"
#include <cstdint>
#include <functional>
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

        // `path` is the source file path (used by Save to record the working image).
        int openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path = "");
        void selectImage(int slot);
        int imageCount() const { return (int)mSlotParams.size(); }
        const uint8_t *exportFullRes(int &w, int &h);

        // ── File-menu actions wired by the host (it owns the file dialogs) ──
        std::function<void()> onOpenRequested;     // host shows an open dialog
        std::function<void()> onSaveRequested;     // Save (falls back to Save As if no path)
        std::function<void()> onSaveAsRequested;   // host shows a save dialog

        /** Source image path of the current slot (for the host's Save dialog default). */
        std::string currentSourcePath() const;
        /** Apply a full param set to the current slot (e.g. after loading a session). */
        void applyParams(const EditParams &p);
        /** Save the current slot's session (image path + params) to its remembered path,
         *  or trigger Save As if it has none yet. */
        void saveSession();
        /** Write the current slot's session to `path` and remember it. */
        bool saveSessionAs(const std::string &path);
        /** Parse a .cosmo session file into the referenced image path + params. */
        static bool readSessionFile(const std::string &path, std::string &imagePath, EditParams &params);

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
        std::shared_ptr<ParamPanel> mDetail;   // sharpening + noise reduction + lens
        std::shared_ptr<MixerPanel> mMixer;
        std::shared_ptr<ToneCurvePanel> mCurve;
        std::shared_ptr<ColorGradingPanel> mGrade;
        std::shared_ptr<TransformPanel> mXform;
        std::shared_ptr<SettingsPanel> mSettings;   // floating overlay (not a tab)
        std::shared_ptr<Filmstrip> mFilmstrip;
        std::shared_ptr<MenuBar> mMenuBar;
        double mMenuBarX = 0, mMenuBarY = 0;        // position in root space (for outside-click)

        artboard::Theme mTheme;
        artboard::GestureRecognizer mRecognizer;

        std::vector<EditParams> mSlotParams;  // UI-authoritative per-image params
        std::vector<std::string> mSlotNames;
        std::vector<std::string> mSlotPaths;      // source image file path per slot
        std::vector<std::string> mSlotSessions;   // last .cosmo save path per slot
        int mCurrentSlot = -1;
        int mPreviewEdge = 1600;
        RenderService::Frame mExportFrame;
    };
}
}
