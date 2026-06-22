#include "CosmoApp.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kMargin = 16.0;
        constexpr double kTopBar = 56.0;
        constexpr double kRightW = 352.0;
        constexpr double kFilmH = 80.0;
        constexpr double kHistH = 190.0;

        // Box-average downscale of straight RGBA8 to fit `maxEdge` (preserve aspect).
        std::vector<uint8_t> makeThumb(const uint8_t *rgba, int w, int h, int maxEdge, int &tw, int &th)
        {
            const int longEdge = w > h ? w : h;
            double scale = longEdge > maxEdge ? (double)maxEdge / longEdge : 1.0;
            tw = (int)(w * scale + 0.5); if (tw < 1) tw = 1;
            th = (int)(h * scale + 0.5); if (th < 1) th = 1;
            std::vector<uint8_t> out((size_t)tw * th * 4);
            for (int y = 0; y < th; ++y)
            {
                const int y0 = (int)((double)y * h / th);
                int y1 = (int)((double)(y + 1) * h / th); if (y1 <= y0) y1 = y0 + 1;
                for (int x = 0; x < tw; ++x)
                {
                    const int x0 = (int)((double)x * w / tw);
                    int x1 = (int)((double)(x + 1) * w / tw); if (x1 <= x0) x1 = x0 + 1;
                    long acc[4] = {0, 0, 0, 0};
                    int n = 0;
                    for (int sy = y0; sy < y1; ++sy)
                        for (int sx = x0; sx < x1; ++sx)
                        {
                            const uint8_t *p = rgba + ((size_t)sy * w + sx) * 4;
                            acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3];
                            ++n;
                        }
                    uint8_t *d = out.data() + ((size_t)y * tw + x) * 4;
                    for (int c = 0; c < 4; ++c) d[c] = (uint8_t)(acc[c] / (n > 0 ? n : 1));
                }
            }
            return out;
        }
    }

    CosmoApp::CosmoApp(double width, double height)
        : mW(width), mH(height), mAccent(palette::accent()), mTheme(makeCosmoTheme(palette::accent()))
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);

        const double rightX = mW - kRightW - kMargin;
        const double filmY = mH - kFilmH - kMargin;
        const double photoY = kTopBar + 8.0;
        mPhotoRect = Rect{kMargin, photoY, rightX - kMargin - kMargin, filmY - photoY - 8.0};

        // center photo
        mImageView = std::make_shared<ImageView>();
        mImageView->x.set(mPhotoRect.x);
        mImageView->y.set(mPhotoRect.y);
        mImageView->width.set(mPhotoRect.w);
        mImageView->height.set(mPhotoRect.h);
        mRoot->addChild(mImageView);

        // right column: histogram + basic panel
        mHistogram = std::make_shared<HistogramPanel>(mTheme, mAccent);
        mHistogram->x.set(rightX);
        mHistogram->y.set(photoY);
        mRoot->addChild(mHistogram);

        std::vector<ParamPanel::Spec> basic = {
            {"exposure", -5.0, 5.0, 0.0, [this](double v) {
                 const int s = mEngine.currentSlot(); if (s < 0) return;
                 mEngine.setExposure((float)v); mSlotParams[s].exposure = v; markDirty();
             }},
            {"contrast", -100.0, 100.0, 0.0, [this](double v) {
                 const int s = mEngine.currentSlot(); if (s < 0) return;
                 mEngine.setContrast((float)v); mSlotParams[s].contrast = v; markDirty();
             }},
        };
        mBasic = std::make_shared<ParamPanel>("BASIC", mTheme, mAccent, basic, 3);
        mBasic->x.set(rightX);
        mBasic->y.set(photoY + kHistH + 10.0);
        mRoot->addChild(mBasic);

        // bottom filmstrip
        mFilmstrip = std::make_shared<Filmstrip>(mAccent);
        mFilmstrip->x.set(kMargin);
        mFilmstrip->y.set(filmY);
        mFilmstrip->width.set(rightX - kMargin - kMargin);
        mFilmstrip->onSelect = [this](int i) { selectImage(i); };
        mRoot->addChild(mFilmstrip);

        // interactive preview at roughly screen resolution
        mEngine.setPreviewSize((int)(mPhotoRect.w > mPhotoRect.h ? mPhotoRect.w : mPhotoRect.h));

        mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });
    }

    int CosmoApp::openImage(const uint8_t *rgba, int w, int h, const std::string &name)
    {
        const int slot = mEngine.addImage(rgba, w, h, 4);
        if (slot < 0) return -1;
        mSlotParams.push_back(UiParams{});
        mSlotNames.push_back(name);

        int tw = 0, th = 0;
        std::vector<uint8_t> thumb = makeThumb(rgba, w, h, 110, tw, th);
        mFilmstrip->addThumb(thumb.data(), tw, th);

        selectImage(slot);  // show the newly opened image
        return slot;
    }

    void CosmoApp::selectImage(int slot)
    {
        if (slot < 0 || slot >= mEngine.imageCount()) return;
        mEngine.selectImage(slot);
        mFilmstrip->setSelected(slot);
        syncControlsToSlot();
        markDirty();
    }

    void CosmoApp::syncControlsToSlot()
    {
        const int s = mEngine.currentSlot();
        if (s < 0) return;
        mBasic->setValues({mSlotParams[s].exposure, mSlotParams[s].contrast});
    }

    void CosmoApp::rebuildPreview()
    {
        PreviewBuffer pv = mEngine.renderPreview();
        if (!pv.rgba) return;
        mImageView->setImage(pv.rgba, pv.width, pv.height);
        mHistogram->setHistogram(mEngine.histogram());
    }

    void CosmoApp::pointer(int kind, double x, double y, int button, double timeMs)
    {
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        mRecognizer.feed(RawPointer{k, Point{x, y}, b, timeMs});
    }

    void CosmoApp::render(IRenderTarget &target, double nowMs)
    {
        mRoot->advance(nowMs);

        if (mDirty && mEngine.hasImage())
        {
            rebuildPreview();
            mDirty = false;
        }

        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::bg()));

        // title
        target.setFill(mAccent);
        target.drawText("COSMO", 18.0, 34.0, 22.0);
        target.setFill(Color{1, 1, 1, 0.35});
        target.drawText("by arstro", 104.0, 34.0, 12.0);

        // status / hint
        target.setFill(palette::muted());
        if (mEngine.hasImage())
        {
            const int s = mEngine.currentSlot();
            const std::string status = mSlotNames[s] + "   (" + std::to_string(s + 1) + "/" +
                                       std::to_string(imageCount()) + ")";
            target.drawText(status, 190.0, 34.0, 12.0);
        }
        else
        {
            target.drawText("Open an image — press O (native) or use the file picker (web)",
                            190.0, 34.0, 12.0);
            // placeholder frame in the photo area
            drawRoundedRect(target, mPhotoRect, 10.0,
                            Paint::filledStroked(Color::hex(0x101218), Color::hex(0x2a3040), 1.0));
        }
        target.restore();

        mRoot->render(target);
    }
}
}
