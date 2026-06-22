/*
 *  Cosmo by arstro — a Lightroom-style photo editor built on Artboard (UI) and the
 *  ImageProcessing library (headless edit engine). Mirrors pulsar/PulsarApp: a root
 *  Segment tree, render()/pointer(), Pulsar-style dark theme.
 *
 *  Strict layering: the UI only pushes parameters into the EditEngine and displays
 *  its output (preview image + histogram). It never touches pixels directly.
 *
 *  M3 scope: open multiple images, display the photo, live histogram (log/linear),
 *  and the Basic panel (exposure/contrast). More panels/processors arrive in M4.
 */
#pragma once
#include "../Artboard/include/artboard/artboard.h"
#include "../ImageProcessing/src/image_processing.h"
#include "CosmoTheme.h"
#include "panels/ParamPanel.h"
#include "panels/HistogramPanel.h"
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
        void pointer(int kind, double x, double y, int button, double timeMs);

        /** Open an image from straight RGBA8 bytes; returns its slot (or -1). */
        int openImage(const uint8_t *rgba, int w, int h, const std::string &name);
        void selectImage(int slot);
        int imageCount() const { return mEngine.imageCount(); }

    private:
        struct UiParams { double exposure = 0.0; double contrast = 0.0; };

        void rebuildPreview();
        void markDirty() { mDirty = true; }
        void syncControlsToSlot();

        double mW, mH;
        artboard::Rect mPhotoRect;
        artboard::Color mAccent;

        arstro::EditEngine mEngine;
        std::shared_ptr<artboard::Segment> mRoot;
        std::shared_ptr<artboard::ImageView> mImageView;
        std::shared_ptr<HistogramPanel> mHistogram;
        std::shared_ptr<ParamPanel> mBasic;
        std::shared_ptr<Filmstrip> mFilmstrip;

        artboard::Theme mTheme;
        artboard::GestureRecognizer mRecognizer;

        std::vector<UiParams> mSlotParams;
        std::vector<std::string> mSlotNames;
        bool mDirty = false;
    };
}
}
