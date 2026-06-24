#include "CosmoApp.h"
#include <algorithm>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kMargin = 16.0;
        constexpr double kTopBar = 56.0;
        double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

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

        mImageView = std::make_shared<ImageView>();
        mRoot->addChild(mImageView);

        mHistogram = std::make_shared<HistogramPanel>(mTheme, mAccent);
        mRoot->addChild(mHistogram);

        // Basic: every tone/colour/effect slider in one column (Light + Color + FX merged).
        using Spec = ParamPanel::Spec;
        std::vector<Spec> basic = {
            {"exposure", -5, 5, 0, [this](double v) { if (auto *p = curUi()) { p->exposure = v; mEngine.setExposure((float)v); markDirty(); } }},
            {"contrast", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->contrast = v; mEngine.setContrast((float)v); markDirty(); } }},
            {"highlights", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->highlights = v; mEngine.setHighlights((float)v); markDirty(); } }},
            {"shadows", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->shadows = v; mEngine.setShadows((float)v); markDirty(); } }},
            {"whites", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->whites = v; mEngine.setWhites((float)v); markDirty(); } }},
            {"blacks", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->blacks = v; mEngine.setBlacks((float)v); markDirty(); } }},
            {"temp", 2000, 50000, 6500, [this](double v) { if (auto *p = curUi()) { p->temp = v; mEngine.setTemperature((float)v); markDirty(); } }},
            {"tint", -150, 150, 0, [this](double v) { if (auto *p = curUi()) { p->tint = v; mEngine.setTint((float)v); markDirty(); } }},
            {"vibrance", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->vibrance = v; mEngine.setVibrance((float)v); markDirty(); } }},
            {"saturation", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->saturation = v; mEngine.setSaturation((float)v); markDirty(); } }},
            {"dehaze", -100, 100, 0, [this](double v) { if (auto *p = curUi()) { p->dehaze = v; mEngine.setDehaze((float)v); markDirty(); } }},
            {"grain", 0, 100, 0, [this](double v) { if (auto *p = curUi()) { p->grainAmount = v; mEngine.setGrainAmount((float)v); markDirty(); } }},
            {"grain size", 0, 100, 0, [this](double v) { if (auto *p = curUi()) { p->grainSize = v; mEngine.setGrainSize((float)v); markDirty(); } }},
        };
        mBasic = std::make_shared<ParamPanel>("BASIC", mTheme, mAccent, basic);

        mMixer = std::make_shared<MixerPanel>(mTheme, mAccent);
        mMixer->onChange = [this](int ch, const std::vector<std::pair<float, float>> &pts) {
            if (auto *p = curUi())
            {
                p->mixer[ch] = pts;
                mEngine.setMixerCurve((EditEngine::MixerChannel)ch, pts);
                markDirty();
            }
        };

        mCurve = std::make_shared<ToneCurvePanel>(mTheme, mAccent);
        mCurve->onCurveChange = [this](const std::vector<std::pair<float, float>> &pts) {
            if (auto *p = curUi()) { p->curve = pts; mEngine.setCurvePoints(pts); markDirty(); }
        };
        mCurve->onLogChange = [this](bool on) {
            if (auto *p = curUi()) { p->curveLog = on; mEngine.setCurveLogScale(on); markDirty(); }
        };

        mGrade = std::make_shared<ColorGradingPanel>(mTheme, mAccent);
        mGrade->onGrade = [this](int r, double h, double s, double l) {
            if (auto *p = curUi())
            {
                mEngine.setGradeHue((EditEngine::GradeRegion)r, (float)h);
                mEngine.setGradeSaturation((EditEngine::GradeRegion)r, (float)s);
                mEngine.setGradeLuminance((EditEngine::GradeRegion)r, (float)l);
                p->grade[r] = {h, s, l};
                markDirty();
            }
        };
        mGrade->onBalance = [this](double v) { if (auto *p = curUi()) { p->balance = v; mEngine.setGradeBalance((float)v); markDirty(); } };
        mGrade->onRemap = [this](bool on, double src, double range, double dst, double strength) {
            if (auto *p = curUi())
            {
                p->remapOn = on; p->remapSrc = src; p->remapRange = range; p->remapDst = dst;
                p->remapStrength = strength * 100.0;
                mEngine.setHueRemapEnabled(on);
                mEngine.setHueRemap((float)src, (float)range, (float)dst, (float)strength);
                markDirty();
            }
        };

        mXform = std::make_shared<TransformPanel>(mTheme, mAccent);
        mXform->onRotate = [this](double v) { if (auto *p = curUi()) { p->rotation = v; mEngine.setRotation((float)v); markDirty(); } };
        mXform->onQuarterTurns = [this](int i) { if (auto *p = curUi()) { p->quarter = i; mEngine.setQuarterTurns(i); markDirty(); } };
        mXform->onCrop = [this](double x, double y, double w, double h) {
            if (auto *p = curUi())
            {
                p->cropX = x; p->cropY = y; p->cropW = w; p->cropH = h;
                mEngine.setCrop((float)x, (float)y, (float)w, (float)h);
                markDirty();
            }
        };

        mTabs = std::make_shared<TabView>(mTheme.tab);
        mTabs->addPage("Basic", mBasic);
        mTabs->addPage("Mixer", mMixer);
        mTabs->addPage("Curve", mCurve);
        mTabs->addPage("Grade", mGrade);
        mTabs->addPage("Xform", mXform);
        mRoot->addChild(mTabs);

        mFilmstrip = std::make_shared<Filmstrip>(mAccent);
        mFilmstrip->onSelect = [this](int i) { selectImage(i); };
        mRoot->addChild(mFilmstrip);

        mRecognizer.setSink([this](const Gesture &g) { mRoot->onGesture(g); });
        layout();
    }

    void CosmoApp::layout()
    {
        const double rightW = clampd(mW * 0.30, 300.0, 460.0);
        const double filmH = clampd(mH * 0.10, 72.0, 110.0);
        const double histH = clampd(mH * 0.22, 150.0, 240.0);
        const double rightX = mW - rightW - kMargin;
        const double filmY = mH - filmH - kMargin;
        const double photoY = kTopBar + 8.0;
        mPhotoRect = Rect{kMargin, photoY, rightX - kMargin - kMargin, filmY - photoY - 8.0};

        mImageView->x.set(mPhotoRect.x);
        mImageView->y.set(mPhotoRect.y);
        mImageView->width.set(mPhotoRect.w);
        mImageView->height.set(mPhotoRect.h);

        mHistogram->x.set(rightX);
        mHistogram->y.set(photoY);
        mHistogram->layout(rightW, histH);

        const double tabsY = photoY + histH + 10.0;
        const double tabsH = (filmY - 8.0) - tabsY;
        mTabs->x.set(rightX);
        mTabs->y.set(tabsY);
        mTabs->width.set(rightW);
        mTabs->height.set(tabsH);
        const double contentH = tabsH - mTabs->tabHeight - 6.0;
        mBasic->layout(rightW, contentH);
        mMixer->layout(rightW, contentH);
        mCurve->layout(rightW, contentH);
        mGrade->layout(rightW, contentH);
        mXform->layout(rightW, contentH);

        mFilmstrip->x.set(kMargin);
        mFilmstrip->y.set(filmY);
        mFilmstrip->width.set(rightX - kMargin - kMargin);
        mFilmstrip->height.set(filmH);

        mEngine.setPreviewSize((int)(mPhotoRect.w > mPhotoRect.h ? mPhotoRect.w : mPhotoRect.h));
        markDirty();
    }

    void CosmoApp::setSize(double width, double height)
    {
        if (width < 320) width = 320;
        if (height < 240) height = 240;
        mW = width; mH = height;
        mRoot->width.set(width);
        mRoot->height.set(height);
        layout();
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
        mBasic->setValues({p.exposure, p.contrast, p.highlights, p.shadows, p.whites, p.blacks,
                           p.temp, p.tint, p.vibrance, p.saturation, p.dehaze, p.grainAmount, p.grainSize});
        mMixer->setCurves(p.mixer);
        mCurve->setCurve(p.curve);
        mCurve->setLog(p.curveLog);

        ColorGradingPanel::State gs;
        gs.grade = p.grade; gs.balance = p.balance; gs.remapOn = p.remapOn;
        gs.remapSrc = p.remapSrc; gs.remapRange = p.remapRange;
        gs.remapDst = p.remapDst; gs.remapStrength = p.remapStrength;
        mGrade->setState(gs);

        TransformPanel::State ts;
        ts.rotation = p.rotation; ts.quarter = p.quarter;
        ts.cropX = p.cropX; ts.cropY = p.cropY; ts.cropW = p.cropW; ts.cropH = p.cropH;
        mXform->setState(ts);
    }

    const uint8_t *CosmoApp::exportFullRes(int &w, int &h)
    {
        if (!mEngine.hasImage()) { w = h = 0; return nullptr; }
        PreviewBuffer pb = mEngine.renderFull();
        w = pb.width; h = pb.height;
        return pb.rgba;
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
