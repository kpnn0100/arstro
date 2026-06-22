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
                    long acc[4] = {0, 0, 0, 0}; int n = 0;
                    for (int sy = y0; sy < y1; ++sy)
                        for (int sx = x0; sx < x1; ++sx)
                        {
                            const uint8_t *p = rgba + ((size_t)sy * w + sx) * 4;
                            acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3]; ++n;
                        }
                    uint8_t *d = out.data() + ((size_t)y * tw + x) * 4;
                    for (int c = 0; c < 4; ++c) d[c] = (uint8_t)(acc[c] / (n > 0 ? n : 1));
                }
            }
            return out;
        }
    }

    CosmoApp::UiParams *CosmoApp::curUi()
    {
        const int s = mEngine.currentSlot();
        return s >= 0 ? &mSlotParams[s] : nullptr;
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

        mImageView = std::make_shared<ImageView>();
        mImageView->x.set(mPhotoRect.x);
        mImageView->y.set(mPhotoRect.y);
        mImageView->width.set(mPhotoRect.w);
        mImageView->height.set(mPhotoRect.h);
        mRoot->addChild(mImageView);

        mHistogram = std::make_shared<HistogramPanel>(mTheme, mAccent);
        mHistogram->x.set(rightX);
        mHistogram->y.set(photoY);
        mRoot->addChild(mHistogram);

        // ── edit sections (each pushes params straight into the engine) ──
        using Spec = ParamPanel::Spec;
        std::vector<Spec> basic = {
            {"exposure", -5, 5, 0, [this](double v) { if (auto *p = curUi()) { p->exposure = v; mEngine.setExposure((float)v); markDirty(); } }},
            {"contrast", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->contrast = v; mEngine.setContrast((float)v); markDirty(); } }},
            {"highlight", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->highlights = v; mEngine.setHighlights((float)v); markDirty(); } }},
            {"shadow", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->shadows = v; mEngine.setShadows((float)v); markDirty(); } }},
            {"white", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->whites = v; mEngine.setWhites((float)v); markDirty(); } }},
            {"black", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->blacks = v; mEngine.setBlacks((float)v); markDirty(); } }},
        };
        std::vector<Spec> color = {
            {"temp", 2000, 50000, 6500, [this](double v) { if (auto *p = curUi()) { p->temp = v; mEngine.setTemperature((float)v); markDirty(); } }},
            {"tint", -150, 150, 0, [this](double v) { if (auto *p = curUi()) { p->tint = v; mEngine.setTint((float)v); markDirty(); } }},
            {"vibrance", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->vibrance = v; mEngine.setVibrance((float)v); markDirty(); } }},
            {"sat", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->saturation = v; mEngine.setSaturation((float)v); markDirty(); } }},
        };
        std::vector<Spec> fx = {
            {"dehaze", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->dehaze = v; mEngine.setDehaze((float)v); markDirty(); } }},
            {"grain", 0, 100, 0, [this](double v) { if (auto *p = curUi()) { p->grainAmount = v; mEngine.setGrainAmount((float)v); markDirty(); } }},
            {"grain sz", 0, 100, 0, [this](double v) { if (auto *p = curUi()) { p->grainSize = v; mEngine.setGrainSize((float)v); markDirty(); } }},
        };
        mBasic = std::make_shared<ParamPanel>("BASIC", mTheme, mAccent, basic, 3);
        mColor = std::make_shared<ParamPanel>("COLOR", mTheme, mAccent, color, 2);
        mEffects = std::make_shared<ParamPanel>("EFFECTS", mTheme, mAccent, fx, 3);
        mMixer = std::make_shared<ColorMixerPanel>(mTheme, mAccent);
        mMixer->onChange = [this](int b, double h, double s, double l) {
            if (auto *p = curUi())
            {
                mEngine.setBandHue((EditEngine::HslBand)b, (float)h);
                mEngine.setBandSaturation((EditEngine::HslBand)b, (float)s);
                mEngine.setBandLuminance((EditEngine::HslBand)b, (float)l);
                p->mixer[b] = {h, s, l};
                markDirty();
            }
        };

        mTabs = std::make_shared<TabView>(mTheme.tab);
        mTabs->x.set(rightX);
        mTabs->y.set(photoY + kHistH + 10.0);
        mTabs->width.set(kRightW);
        mTabs->height.set((filmY - 8.0) - (photoY + kHistH + 10.0));
        mTabs->addPage("Light", mBasic);
        mTabs->addPage("Color", mColor);
        mTabs->addPage("FX", mEffects);
        mTabs->addPage("Mixer", mMixer);
        mRoot->addChild(mTabs);

        mFilmstrip = std::make_shared<Filmstrip>(mAccent);
        mFilmstrip->x.set(kMargin);
        mFilmstrip->y.set(filmY);
        mFilmstrip->width.set(rightX - kMargin - kMargin);
        mFilmstrip->onSelect = [this](int i) { selectImage(i); };
        mRoot->addChild(mFilmstrip);

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
        selectImage(slot);
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
        const UiParams &p = mSlotParams[s];
        mBasic->setValues({p.exposure, p.contrast, p.highlights, p.shadows, p.whites, p.blacks});
        mColor->setValues({p.temp, p.tint, p.vibrance, p.saturation});
        mEffects->setValues({p.dehaze, p.grainAmount, p.grainSize});
        mMixer->setValues(p.mixer);
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
        target.setFill(mAccent);
        target.drawText("COSMO", 18.0, 34.0, 22.0);
        target.setFill(Color{1, 1, 1, 0.35});
        target.drawText("by arstro", 104.0, 34.0, 12.0);

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
            drawRoundedRect(target, mPhotoRect, 10.0,
                            Paint::filledStroked(Color::hex(0x101218), Color::hex(0x2a3040), 1.0));
        }
        target.restore();

        mRoot->render(target);
    }
}
}
