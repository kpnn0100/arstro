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

    EditParams *CosmoApp::curParams() { return mCurrentSlot >= 0 ? &mSlotParams[mCurrentSlot] : nullptr; }

    void CosmoApp::submit()
    {
        if (mCurrentSlot >= 0)
            mService.render(mCurrentSlot, mSlotParams[mCurrentSlot]);
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

        using Section = ParamPanel::Section;
        std::vector<Section> basic = {
            {"TONE", {
                {"exposure", -5, 5, 0, [this](double v) { if (auto *p = curParams()) { p->exposure = (float)v; submit(); } }},
                {"contrast", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->contrast = (float)v; submit(); } }},
                {"highlights", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->highlights = (float)v; submit(); } }},
                {"shadows", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->shadows = (float)v; submit(); } }},
                {"whites", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->whites = (float)v; submit(); } }},
                {"blacks", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->blacks = (float)v; submit(); } }},
            }},
            {"COLOR", {
                {"temp", 2000, 50000, 6500, [this](double v) { if (auto *p = curParams()) { p->temp = (float)v; submit(); } }},
                {"tint", -150, 150, 0, [this](double v) { if (auto *p = curParams()) { p->tint = (float)v; submit(); } }},
                {"vibrance", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->vibrance = (float)v; submit(); } }},
                {"saturation", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->saturation = (float)v; submit(); } }},
            }},
            {"EFFECTS", {
                {"dehaze", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->dehaze = (float)v; submit(); } }},
                {"grain", 0, 100, 0, [this](double v) { if (auto *p = curParams()) { p->grainAmount = (float)v; submit(); } }},
                {"grain size", 0, 100, 0, [this](double v) { if (auto *p = curParams()) { p->grainSize = (float)v; submit(); } }},
            }},
        };
        mBasic = std::make_shared<ParamPanel>("BASIC", mTheme, mAccent, basic);

        mMixer = std::make_shared<MixerPanel>(mTheme, mAccent);
        mMixer->onChange = [this](int ch, const std::vector<std::pair<float, float>> &pts) {
            if (auto *p = curParams()) { p->mixer[ch] = pts; submit(); }
        };

        mCurve = std::make_shared<ToneCurvePanel>(mTheme, mAccent);
        mCurve->onCurveChange = [this](const std::vector<std::pair<float, float>> &pts) {
            if (auto *p = curParams()) { p->curve = pts; submit(); }
        };
        mCurve->onLogChange = [this](bool on) { if (auto *p = curParams()) { p->curveLog = on; submit(); } };

        mGrade = std::make_shared<ColorGradingPanel>(mTheme, mAccent);
        mGrade->onGrade = [this](int r, double h, double s, double l) {
            if (auto *p = curParams()) { p->grade[r].hue = (float)h; p->grade[r].sat = (float)s; p->grade[r].lum = (float)l; submit(); }
        };
        mGrade->onBalance = [this](double v) { if (auto *p = curParams()) { p->balance = (float)v; submit(); } };
        mGrade->onRemap = [this](bool on, double src, double range, double dst, double strength) {
            if (auto *p = curParams())
            {
                p->remapEnable = on; p->remapSrc = (float)src; p->remapRange = (float)range;
                p->remapDst = (float)dst; p->remapStrength = (float)strength;  // 0..1
                submit();
            }
        };

        mXform = std::make_shared<TransformPanel>(mTheme, mAccent);
        mXform->onRotate = [this](double v) { if (auto *p = curParams()) { p->rotation = (float)v; submit(); } };
        mXform->onQuarterTurns = [this](int i) { if (auto *p = curParams()) { p->quarterTurns = i; submit(); } };
        mXform->onCrop = [this](double x, double y, double w, double h) {
            if (auto *p = curParams()) { p->cropX = (float)x; p->cropY = (float)y; p->cropW = (float)w; p->cropH = (float)h; submit(); }
        };

        mSettings = std::make_shared<SettingsPanel>(mTheme, mAccent);
        mSettings->onPreviewEdge = [this](int edge) { mPreviewEdge = edge; mService.setPreviewSize(edge); submit(); };
        mSettings->onThreads = [this](int n) { par::setThreads(n); submit(); };

        mTabs = std::make_shared<TabView>(mTheme.tab);
        mTabs->addPage("Basic", mBasic);
        mTabs->addPage("Mixer", mMixer);
        mTabs->addPage("Curve", mCurve);
        mTabs->addPage("Grade", mGrade);
        mTabs->addPage("Xform", mXform);
        mTabs->addPage("Settings", mSettings);
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

        mImageView->x.set(mPhotoRect.x); mImageView->y.set(mPhotoRect.y);
        mImageView->width.set(mPhotoRect.w); mImageView->height.set(mPhotoRect.h);

        mHistogram->x.set(rightX); mHistogram->y.set(photoY);
        mHistogram->layout(rightW, histH);

        const double tabsY = photoY + histH + 10.0;
        const double tabsH = (filmY - 8.0) - tabsY;
        mTabs->x.set(rightX); mTabs->y.set(tabsY);
        mTabs->width.set(rightW); mTabs->height.set(tabsH);
        const double contentH = tabsH - mTabs->tabHeight - 6.0;
        mBasic->layout(rightW, contentH);
        mMixer->layout(rightW, contentH);
        mCurve->layout(rightW, contentH);
        mGrade->layout(rightW, contentH);
        mXform->layout(rightW, contentH);
        mSettings->layout(rightW, contentH);

        mFilmstrip->x.set(kMargin); mFilmstrip->y.set(filmY);
        mFilmstrip->width.set(rightX - kMargin - kMargin); mFilmstrip->height.set(filmH);

        mService.setPreviewSize(mPreviewEdge);
        submit();
    }

    void CosmoApp::setSize(double width, double height)
    {
        if (width < 320) width = 320;
        if (height < 240) height = 240;
        mW = width; mH = height;
        mRoot->width.set(width); mRoot->height.set(height);
        layout();
    }

    int CosmoApp::openImage(const uint8_t *rgba, int w, int h, const std::string &name)
    {
        const int slot = mService.addImage(rgba, w, h, 4);
        if (slot < 0) return -1;
        mSlotParams.push_back(EditParams{});
        mSlotNames.push_back(name);
        int tw = 0, th = 0;
        std::vector<uint8_t> thumb = makeThumb(rgba, w, h, 110, tw, th);
        mFilmstrip->addThumb(thumb.data(), tw, th);
        selectImage(slot);
        return slot;
    }

    void CosmoApp::selectImage(int slot)
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return;
        mCurrentSlot = slot;
        mFilmstrip->setSelected(slot);
        syncControlsToSlot();
        submit();
    }

    void CosmoApp::syncControlsToSlot()
    {
        if (mCurrentSlot < 0) return;
        const EditParams &p = mSlotParams[mCurrentSlot];
        mBasic->setValues({p.exposure, p.contrast, p.highlights, p.shadows, p.whites, p.blacks,
                           p.temp, p.tint, p.vibrance, p.saturation, p.dehaze, p.grainAmount, p.grainSize});
        mMixer->setCurves(p.mixer);
        mCurve->setCurve(p.curve);
        mCurve->setLog(p.curveLog);

        ColorGradingPanel::State gs;
        for (int r = 0; r < 3; ++r) gs.grade[r] = {p.grade[r].hue, p.grade[r].sat, p.grade[r].lum};
        gs.balance = p.balance; gs.remapOn = p.remapEnable;
        gs.remapSrc = p.remapSrc; gs.remapRange = p.remapRange; gs.remapDst = p.remapDst;
        gs.remapStrength = p.remapStrength * 100.0;  // panel uses 0..100
        mGrade->setState(gs);

        TransformPanel::State ts;
        ts.rotation = p.rotation; ts.quarter = p.quarterTurns;
        ts.cropX = p.cropX; ts.cropY = p.cropY; ts.cropW = p.cropW; ts.cropH = p.cropH;
        mXform->setState(ts);
    }

    const uint8_t *CosmoApp::exportFullRes(int &w, int &h)
    {
        if (mCurrentSlot < 0) { w = h = 0; return nullptr; }
        if (!mService.renderFull(mCurrentSlot, mSlotParams[mCurrentSlot], mExportFrame)) { w = h = 0; return nullptr; }
        w = mExportFrame.width; h = mExportFrame.height;
        return mExportFrame.rgba.data();
    }

    void CosmoApp::pointer(int kind, double x, double y, int button, double timeMs, bool alt)
    {
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        RawPointer rp{k, Point{x, y}, b, timeMs};
        rp.alt = alt;
        mRecognizer.feed(rp);
    }

    void CosmoApp::render(IRenderTarget &target, double nowMs)
    {
        mRoot->advance(nowMs);

        // Pick up any completed frame from the worker (non-blocking).
        RenderService::Frame f;
        if (mService.tryAcquire(f) && f.width > 0)
        {
            mImageView->setImage(f.rgba.data(), f.width, f.height);
            mHistogram->setHistogram(f.hist);
        }

        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::bg()));
        target.setFill(mAccent);
        for (double ox : {0.0, 0.5})
            target.drawText("COSMO", 18.0 + ox, 33.0, 19.0);
        target.setFill(palette::faint());
        target.drawText("by arstro", 96.0, 33.0, 11.0);
        target.beginPath();
        target.moveTo(0.0, kTopBar); target.lineTo(mW, kTopBar);
        target.setStroke(palette::line(), 1.0); target.strokePath();

        target.setFill(palette::muted());
        if (mCurrentSlot >= 0)
        {
            const std::string status = mSlotNames[mCurrentSlot] + "   (" + std::to_string(mCurrentSlot + 1) +
                                       "/" + std::to_string(imageCount()) + ")";
            target.drawText(status, 190.0, 33.0, 12.0);
        }
        else
        {
            target.drawText("Open an image  -  press O (native) or use the file picker (web)", 190.0, 33.0, 12.0);
            drawRoundedRect(target, mPhotoRect, radius::panel(),
                            Paint::filledStroked(palette::panel(), palette::line(), 1.0));
        }
        target.restore();

        mRoot->render(target);
    }
}
}
