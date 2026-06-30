#include "CosmoApp.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kMargin = 16.0;
        constexpr double kTopBar = 64.0;   // wordmark row + menu bar row
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

    namespace
    {
        // Compose a group base with a per-image scalar offset (the double adjustment).
        EditParams composeParams(const EditParams &base, const arstro::LocalAdjust &d)
        {
            EditParams e = base;
            e.exposure += d.exposure; e.contrast += d.contrast;
            e.highlights += d.highlights; e.shadows += d.shadows; e.whites += d.whites; e.blacks += d.blacks;
            e.temp += d.temp / 100.f * 3500.f;  // relative warm/cool shift in Kelvin
            e.tint += d.tint; e.saturation += d.saturation;
            e.texture += d.texture; e.clarity += d.clarity; e.dehaze += d.dehaze;
            return e;
        }
    }

    EditParams CosmoApp::effectiveParams(int slot) const
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return EditParams{};
        if (mSlotGrouped[slot]) return composeParams(mGroupBase, mSlotDelta[slot]);
        return mSlotParams[slot];
    }

    // The panels edit the group base when the current image is grouped (so changes
    // apply to the whole group); otherwise they edit the image's own params.
    EditParams *CosmoApp::curParams()
    {
        if (mCurrentSlot < 0) return nullptr;
        return mSlotGrouped[mCurrentSlot] ? &mGroupBase : &mSlotParams[mCurrentSlot];
    }

    void CosmoApp::submit()
    {
        if (mCurrentSlot < 0) return;
        EditParams p = effectiveParams(mCurrentSlot);
        // In crop mode (Transform tab) render the FULL frame so the crop box can be
        // dragged over the whole image; the slot keeps the real crop.
        if (mTabs && mTabs->selectedIndex() == mXformTabIndex)
        { p.cropX = 0; p.cropY = 0; p.cropW = 1; p.cropH = 1; }
        mService.render(mCurrentSlot, p);
    }

    CosmoApp::CosmoApp(double width, double height)
        : mW(width), mH(height), mAccent(palette::accent()), mTheme(makeCosmoTheme(palette::accent()))
    {
        mRoot = std::make_shared<Segment>();
        mRoot->width.set(width);
        mRoot->height.set(height);

        mImageView = std::make_shared<ImageView>();
        mRoot->addChild(mImageView);

        mMaskOverlay = std::make_shared<MaskOverlay>(mAccent);  // sits over the photo
        mMaskOverlay->onChange = [this](const arstro::MaskParams &m) {
            if (auto *p = curParams())
                if (mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
                { p->masks[mSelectedMask] = m; submit(); }
        };
        mRoot->addChild(mMaskOverlay);

        mCropOverlay = std::make_shared<CropOverlay>(mAccent);  // active on the Transform tab
        mCropOverlay->onChange = [this](double x, double y, double w, double h) {
            if (auto *p = curParams())
            { p->cropX = (float)x; p->cropY = (float)y; p->cropW = (float)w; p->cropH = (float)h; submit(); }
        };
        mRoot->addChild(mCropOverlay);

        mCompareView = std::make_shared<CompareView>(mAccent);  // before/after split over the photo
        mRoot->addChild(mCompareView);

        mCompareToggle = std::make_shared<TextToggle>("before / after", mAccent);
        mCompareToggle->onChange = [this](bool on) {
            if (on) renderBefore();
            mCompareView->setActive(on && mCurrentSlot >= 0);
        };
        mRoot->addChild(mCompareToggle);

        mGroupBar = std::make_shared<GroupDeltaBar>(mTheme, mAccent);  // per-image offset (double adjustment)
        mGroupBar->visible = false;
        mGroupBar->onChange = [this](double ev, double temp) {
            if (mCurrentSlot >= 0 && mSlotGrouped[mCurrentSlot])
            { mSlotDelta[mCurrentSlot].exposure = (float)ev; mSlotDelta[mCurrentSlot].temp = (float)temp; submit(); }
        };
        mRoot->addChild(mGroupBar);

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
                {"temp", 2000, 50000, 6500, [this](double v) { if (auto *p = curParams()) { p->temp = (float)v; submit(); } },
                 true, Color{0.30f, 0.55f, 1.0f, 1.0f}, Color{1.0f, 0.82f, 0.30f, 1.0f}},   // blue -> warm
                {"tint", -150, 150, 0, [this](double v) { if (auto *p = curParams()) { p->tint = (float)v; submit(); } },
                 true, Color{0.40f, 0.80f, 0.35f, 1.0f}, Color{0.85f, 0.30f, 0.78f, 1.0f}},   // green -> magenta
                {"vibrance", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->vibrance = (float)v; submit(); } }},
                {"saturation", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->saturation = (float)v; submit(); } }},
            }},
            {"PRESENCE", {
                {"texture", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->texture = (float)v; submit(); } }},
                {"clarity", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->clarity = (float)v; submit(); } }},
                {"dehaze", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->dehaze = (float)v; submit(); } }},
            }},
            {"EFFECTS", {
                {"grain", 0, 100, 0, [this](double v) { if (auto *p = curParams()) { p->grainAmount = (float)v; submit(); } }},
                {"grain size", 0, 100, 0, [this](double v) { if (auto *p = curParams()) { p->grainSize = (float)v; submit(); } }},
            }},
        };
        mBasic = std::make_shared<ParamPanel>("BASIC", mTheme, mAccent, basic);

        std::vector<Section> detail = {
            {"SHARPENING", {
                {"amount", 0, 150, 0, [this](double v) { if (auto *p = curParams()) { p->sharpenAmount = (float)v; submit(); } }},
                {"radius", 0.5, 3, 1, [this](double v) { if (auto *p = curParams()) { p->sharpenRadius = (float)v; submit(); } }},
                {"masking", 0, 100, 0, [this](double v) { if (auto *p = curParams()) { p->sharpenMasking = (float)v; submit(); } }},
            }},
            {"NOISE REDUCTION", {
                {"luminance", 0, 100, 0, [this](double v) { if (auto *p = curParams()) { p->nrLuminance = (float)v; submit(); } }},
                {"color", 0, 100, 0, [this](double v) { if (auto *p = curParams()) { p->nrColor = (float)v; submit(); } }},
            }},
            {"LENS", {
                {"distortion", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->lensDistortion = (float)v; submit(); } }},
                {"defringe", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->lensCA = (float)v; submit(); } }},
                {"vignette", -100, 100, 0, [this](double v) { if (auto *p = curParams()) { p->lensVignette = (float)v; submit(); } }},
            }},
        };
        mDetail = std::make_shared<ParamPanel>("DETAIL", mTheme, mAccent, detail);

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
        mXform->onAspect = [this](double ratio) { mCropOverlay->setAspect(ratio); };
        mXform->currentRatio = [this] { return mCropOverlay->currentPixelRatio(); };

        mMaskPanel = std::make_shared<MaskPanel>(mTheme, mAccent);
        mMaskPanel->onAdd = [this](int type) {
            if (auto *p = curParams())
            {
                arstro::MaskParams m; m.type = type;
                p->masks.push_back(m);
                mSelectedMask = (int)p->masks.size() - 1;
                syncMaskUI(); submit();
            }
        };
        mMaskPanel->onSelect = [this](int i) { mSelectedMask = i; syncMaskUI(); };
        mMaskPanel->onDelete = [this] {
            if (auto *p = curParams())
                if (mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
                {
                    p->masks.erase(p->masks.begin() + mSelectedMask);
                    if (mSelectedMask >= (int)p->masks.size()) mSelectedMask = (int)p->masks.size() - 1;
                    syncMaskUI(); submit();
                }
        };
        auto editMask = [this](auto fn) {
            if (auto *p = curParams())
                if (mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
                {
                    fn(p->masks[mSelectedMask]);
                    mMaskOverlay->updateMask(p->masks[mSelectedMask]);  // keep overlay copy current
                    submit();
                }
        };
        mMaskPanel->onInvert = [editMask](bool on) { editMask([on](arstro::MaskParams &m) { m.inverted = on; }); };
        mMaskPanel->onFeather = [editMask](double v) { editMask([v](arstro::MaskParams &m) { m.feather = (float)v; }); };
        mMaskPanel->onLocal = [editMask](const arstro::LocalAdjust &a) { editMask([&a](arstro::MaskParams &m) { m.adjust = a; }); };

        mTabs = std::make_shared<TabView>(mTheme.tab);
        mTabs->addPage("Basic", mBasic);
        mTabs->addPage("Detail", mDetail);
        mTabs->addPage("Mask", mMaskPanel);
        mTabs->addPage("Mixer", mMixer);
        mTabs->addPage("Curve", mCurve);
        mTabs->addPage("Grade", mGrade);
        mTabs->addPage("Xform", mXform);
        mMaskTabIndex = 2; mXformTabIndex = 6;
        mTabs->onChange = [this](int idx) {
            syncMaskUI();                                  // mask overlay only on the Mask tab
            mCropOverlay->setActive(idx == mXformTabIndex);  // crop box only on the Transform tab
            if (auto *p = curParams()) mCropOverlay->setCrop(p->cropX, p->cropY, p->cropW, p->cropH);
            submit();                                      // crop mode toggled -> full/cropped re-render
        };
        mRoot->addChild(mTabs);

        mFilmstrip = std::make_shared<Filmstrip>(mAccent);
        mFilmstrip->onSelect = [this](int i) { selectImage(i); };
        mRoot->addChild(mFilmstrip);

        // Settings now lives in the menu bar (a floating overlay, not a tab).
        mSettings = std::make_shared<SettingsPanel>(mTheme, mAccent);
        mSettings->onPreviewEdge = [this](int edge) { mPreviewEdge = edge; mService.setPreviewSize(edge); submit(); };
        mSettings->onThreads = [this](int n) { par::setThreads(n); submit(); };
        mSettings->visible = false;
        mRoot->addChild(mSettings);

        // Top-left menu bar (drawn last -> dropdowns overlay the canvas).
        mMenuBar = std::make_shared<MenuBar>(mAccent);
        MenuBar::Menu file;
        file.title = "File";
        file.items.push_back({"Open...", [this] { if (onOpenRequested) onOpenRequested(); }});
        file.items.push_back({"Save", [this] { saveSession(); }});
        file.items.push_back({"Save As...", [this] { if (onSaveAsRequested) onSaveAsRequested(); }});
        mMenuBar->addMenu(file);
        MenuBar::Menu settings;
        settings.title = "Settings";
        settings.overlay = true;  // its open state is the floating panel below
        mMenuBar->addMenu(settings);

        MenuBar::Menu develop;
        develop.title = "Develop";
        develop.items.push_back({"Copy Settings", [this] {
            if (auto *p = curParams()) { mClipboard = *p; mHasClip = true; }
        }});
        develop.items.push_back({"Paste to Selected", [this] {
            pasteTo(mFilmstrip->selection());
        }});
        develop.items.push_back({"Paste to All Images", [this] {
            std::vector<int> all(imageCount());
            for (int i = 0; i < imageCount(); ++i) all[i] = i;
            pasteTo(all);
        }});
        develop.items.push_back({"Group Selected", [this] { groupSelected(); }});
        develop.items.push_back({"Ungroup Selected", [this] { ungroupSelected(); }});
        mMenuBar->addMenu(develop);

        MenuBar::Menu preset;
        preset.title = "Preset";
        preset.items.push_back({"Save Preset...", [this] { if (onSavePresetRequested) onSavePresetRequested(); }});
        mMenuBar->addMenu(preset);
        mPresetMenuIndex = mMenuBar->menuCount() - 1;
        // One active menu at a time (tab-like): the Settings panel shows iff Settings
        // is the active menu, so opening File closes Settings and vice versa.
        mMenuBar->onOpenChanged = [this](int open) { mSettings->visible = (open == 1); };
        mRoot->addChild(mMenuBar);

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

        mMaskOverlay->x.set(mPhotoRect.x); mMaskOverlay->y.set(mPhotoRect.y);
        mMaskOverlay->width.set(mPhotoRect.w); mMaskOverlay->height.set(mPhotoRect.h);
        mCropOverlay->x.set(mPhotoRect.x); mCropOverlay->y.set(mPhotoRect.y);
        mCropOverlay->width.set(mPhotoRect.w); mCropOverlay->height.set(mPhotoRect.h);
        mCompareView->x.set(mPhotoRect.x); mCompareView->y.set(mPhotoRect.y);
        mCompareView->width.set(mPhotoRect.w); mCompareView->height.set(mPhotoRect.h);
        mCompareToggle->x.set(mPhotoRect.x + mPhotoRect.w - 112.0); mCompareToggle->y.set(mPhotoRect.y - 22.0);
        mCompareToggle->width.set(112.0); mCompareToggle->height.set(18.0);
        mGroupBar->x.set(mPhotoRect.x); mGroupBar->y.set(mPhotoRect.y - 24.0);
        mGroupBar->layout(mPhotoRect.w - 124.0, 20.0);

        mHistogram->x.set(rightX); mHistogram->y.set(photoY);
        mHistogram->layout(rightW, histH);

        const double tabsY = photoY + histH + 10.0;
        const double tabsH = (filmY - 8.0) - tabsY;
        mTabs->x.set(rightX); mTabs->y.set(tabsY);
        mTabs->width.set(rightW); mTabs->height.set(tabsH);
        const double contentH = tabsH - mTabs->tabHeight - 6.0;
        mBasic->layout(rightW, contentH);
        mDetail->layout(rightW, contentH);
        mMaskPanel->layout(rightW, contentH);
        mMixer->layout(rightW, contentH);
        mCurve->layout(rightW, contentH);
        mGrade->layout(rightW, contentH);
        mXform->layout(rightW, contentH);

        mFilmstrip->x.set(kMargin); mFilmstrip->y.set(filmY);
        mFilmstrip->width.set(rightX - kMargin - kMargin); mFilmstrip->height.set(filmH);

        // menu bar (top-left, under the wordmark) + floating settings overlay
        mMenuBarX = kMargin; mMenuBarY = 34.0;
        mMenuBar->x.set(mMenuBarX); mMenuBar->y.set(mMenuBarY);
        mMenuBar->width.set(160.0); mMenuBar->height.set(22.0);
        mSettings->x.set(kMargin); mSettings->y.set(kTopBar + 6.0);
        mSettings->layout(280.0, 150.0);

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

    int CosmoApp::openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        const int slot = mService.addImage(rgba, w, h, 4);
        if (slot < 0) return -1;
        mSlotParams.push_back(EditParams{});
        mSlotNames.push_back(name);
        mSlotPaths.push_back(path);
        mSlotSessions.push_back("");
        mSlotGrouped.push_back(0);
        mSlotDelta.push_back(arstro::LocalAdjust{});
        int tw = 0, th = 0;
        std::vector<uint8_t> thumb = makeThumb(rgba, w, h, 110, tw, th);
        mFilmstrip->addThumb(thumb.data(), tw, th);
        selectImage(slot);
        return slot;
    }

    std::string CosmoApp::currentSourcePath() const
    {
        return mCurrentSlot >= 0 ? mSlotPaths[mCurrentSlot] : std::string();
    }

    void CosmoApp::applyParams(const EditParams &p)
    {
        if (mCurrentSlot < 0) return;
        mSlotParams[mCurrentSlot] = p;
        syncControlsToSlot();
        submit();
    }

    bool CosmoApp::saveSessionAs(const std::string &path)
    {
        if (mCurrentSlot < 0) return false;
        std::ofstream f(path);
        if (!f) return false;
        f << "image=" << mSlotPaths[mCurrentSlot] << "\n" << serializeParams(effectiveParams(mCurrentSlot));
        mSlotSessions[mCurrentSlot] = path;
        return true;
    }

    void CosmoApp::saveSession()
    {
        if (mCurrentSlot < 0) return;
        if (mSlotSessions[mCurrentSlot].empty())
        {
            if (onSaveAsRequested) onSaveAsRequested();  // no path yet -> prompt
        }
        else
            saveSessionAs(mSlotSessions[mCurrentSlot]);
    }

    bool CosmoApp::readSessionFile(const std::string &path, std::string &imagePath, EditParams &params)
    {
        std::ifstream f(path);
        if (!f) return false;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        imagePath.clear();
        std::stringstream ss(text);
        std::string line;
        while (std::getline(ss, line))
            if (line.rfind("image=", 0) == 0) { imagePath = line.substr(6); break; }
        deserializeParams(text, params);  // ignores the image= line
        return true;
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
        const EditParams &p = *curParams();  // group base when grouped, else the image's own params

        const bool grouped = mSlotGrouped[mCurrentSlot] != 0;
        mGroupBar->visible = grouped;
        if (grouped) mGroupBar->setValues(mSlotDelta[mCurrentSlot].exposure, mSlotDelta[mCurrentSlot].temp);
        mBasic->setValues({p.exposure, p.contrast, p.highlights, p.shadows, p.whites, p.blacks,
                           p.temp, p.tint, p.vibrance, p.saturation,
                           p.texture, p.clarity, p.dehaze, p.grainAmount, p.grainSize});
        mDetail->setValues({p.sharpenAmount, p.sharpenRadius, p.sharpenMasking,
                            p.nrLuminance, p.nrColor,
                            p.lensDistortion, p.lensCA, p.lensVignette});
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

        mCropOverlay->setCrop(p.cropX, p.cropY, p.cropW, p.cropH);
        mCropOverlay->setActive(mTabs && mTabs->selectedIndex() == mXformTabIndex);

        mSelectedMask = p.masks.empty() ? -1 : 0;
        syncMaskUI();

        if (mCompareToggle && mCompareToggle->on()) renderBefore();  // refresh baseline for new slot
    }

    void CosmoApp::syncMaskUI()
    {
        EditParams *p = curParams();
        const int n = p ? (int)p->masks.size() : 0;
        if (mSelectedMask >= n) mSelectedMask = n - 1;
        if (mSelectedMask < 0 && n > 0) mSelectedMask = 0;

        static const std::vector<arstro::MaskParams> kEmpty;
        mMaskPanel->setMasks(p ? p->masks : kEmpty, mSelectedMask);

        // The overlay is interactive only on the Mask tab with a mask selected.
        const bool onMaskTab = mTabs && mTabs->selectedIndex() == mMaskTabIndex;
        const bool active = onMaskTab && p && mSelectedMask >= 0 && mSelectedMask < n;
        mMaskOverlay->setMask(active ? p->masks[mSelectedMask] : arstro::MaskParams{}, active);
    }

    void CosmoApp::pasteTo(const std::vector<int> &slots)
    {
        if (!mHasClip) return;
        bool affectedCurrent = false;
        for (int i : slots)
            if (i >= 0 && i < (int)mSlotParams.size())
            {
                mSlotParams[i] = mClipboard;       // replace this image's develop settings
                if (i == mCurrentSlot) affectedCurrent = true;
            }
        if (affectedCurrent) { syncControlsToSlot(); submit(); }  // others re-render when selected
    }

    void CosmoApp::setPresetDir(const std::string &dir)
    {
        mPresetDir = dir;
        refreshPresetMenu();
    }

    bool CosmoApp::savePreset(const std::string &name)
    {
        if (mPresetDir.empty() || name.empty() || mCurrentSlot < 0) return false;
        std::error_code ec;
        std::filesystem::create_directories(mPresetDir, ec);
        std::ofstream f(mPresetDir + "/" + name + ".cosmopreset");
        if (!f) return false;
        f << serializeParams(mSlotParams[mCurrentSlot]);  // image-independent: develop settings only
        refreshPresetMenu();
        return true;
    }

    bool CosmoApp::applyPreset(const std::string &name)
    {
        if (mPresetDir.empty() || mCurrentSlot < 0) return false;
        std::ifstream f(mPresetDir + "/" + name + ".cosmopreset");
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        EditParams p;
        if (!deserializeParams(ss.str(), p)) return false;
        mSlotParams[mCurrentSlot] = p;
        syncControlsToSlot();
        submit();
        return true;
    }

    void CosmoApp::refreshPresetMenu()
    {
        if (!mMenuBar || mPresetMenuIndex < 0) return;
        std::vector<MenuBar::Item> items;
        items.push_back({"Save Preset...", [this] { if (onSavePresetRequested) onSavePresetRequested(); }});
        std::error_code ec;
        if (!mPresetDir.empty() && std::filesystem::is_directory(mPresetDir, ec))
        {
            std::vector<std::string> names;
            for (const auto &e : std::filesystem::directory_iterator(mPresetDir, ec))
                if (e.path().extension() == ".cosmopreset")
                    names.push_back(e.path().stem().string());
            std::sort(names.begin(), names.end());
            for (const auto &n : names)
                items.push_back({n, [this, n] { applyPreset(n); }});
        }
        mMenuBar->setMenuItems(mPresetMenuIndex, std::move(items));
    }

    void CosmoApp::groupSelected()
    {
        if (mCurrentSlot < 0) return;
        const std::vector<int> &sel = mFilmstrip->selection();
        mGroupBase = effectiveParams(mCurrentSlot);  // current look becomes the shared base
        for (int i : sel)
            if (i >= 0 && i < (int)mSlotGrouped.size()) { mSlotGrouped[i] = 1; mSlotDelta[i] = arstro::LocalAdjust{}; }
        syncControlsToSlot();
        submit();
    }

    void CosmoApp::ungroupSelected()
    {
        const std::vector<int> &sel = mFilmstrip->selection();
        for (int i : sel)
            if (i >= 0 && i < (int)mSlotGrouped.size() && mSlotGrouped[i])
            {
                mSlotParams[i] = effectiveParams(i);  // bake the current look so it is preserved
                mSlotGrouped[i] = 0;
            }
        syncControlsToSlot();
        submit();
    }

    void CosmoApp::renderBefore()
    {
        if (mCurrentSlot < 0) return;
        // The baseline keeps geometry (crop/rotate/lens) but drops all tonal/colour
        // edits, so the split aligns and shows exactly what the adjustments did.
        const EditParams cur = effectiveParams(mCurrentSlot);
        EditParams b;
        b.cropX = cur.cropX; b.cropY = cur.cropY; b.cropW = cur.cropW; b.cropH = cur.cropH;
        b.rotation = cur.rotation; b.quarterTurns = cur.quarterTurns;
        b.lensDistortion = cur.lensDistortion; b.lensCA = cur.lensCA; b.lensVignette = cur.lensVignette;
        if (mService.renderFull(mCurrentSlot, b, mBeforeFrame) && mBeforeFrame.width > 0)
            mCompareView->setBefore(mBeforeFrame.rgba.data(), mBeforeFrame.width, mBeforeFrame.height);
    }

    const uint8_t *CosmoApp::exportFullRes(int &w, int &h)
    {
        if (mCurrentSlot < 0) { w = h = 0; return nullptr; }
        if (!mService.renderFull(mCurrentSlot, effectiveParams(mCurrentSlot), mExportFrame)) { w = h = 0; return nullptr; }
        w = mExportFrame.width; h = mExportFrame.height;
        return mExportFrame.rgba.data();
    }

    void CosmoApp::pointer(int kind, double x, double y, int button, double timeMs, bool alt, bool shift, bool ctrl)
    {
        RawPointer::Kind k = kind == 0 ? RawPointer::Kind::Down
                             : kind == 2 ? RawPointer::Kind::Up
                                         : RawPointer::Kind::Move;
        PointerButton b = button == 2 ? PointerButton::Right : PointerButton::Left;
        RawPointer rp{k, Point{x, y}, b, timeMs};
        rp.alt = alt; rp.shift = shift; rp.ctrl = ctrl;
        if (kind == 0 && mMenuBar)  // a press outside the bar/dropdown AND the settings overlay closes the menu
        {
            const bool inMenu = mMenuBar->pointInActiveArea(Point{x - mMenuBarX, y - mMenuBarY});
            const bool inSettings = mSettings->visible &&
                Rect{mSettings->x.value(), mSettings->y.value(), mSettings->width.value(), mSettings->height.value()}
                    .contains(Point{x, y});
            if (!inMenu && !inSettings)
                mMenuBar->close();
        }
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
        mMaskOverlay->setFittedRect(mImageView->fittedRect());  // photo display area (local)
        mCropOverlay->setFittedRect(mImageView->fittedRect());
        mCompareView->setFittedRect(mImageView->fittedRect());

        target.save();
        target.setTransform(Transform::identity());
        drawRoundedRect(target, Rect{0, 0, mW, mH}, 0.0, Paint::filled(palette::bg()));
        target.setFill(mAccent);  // wordmark on the top row; the menu bar sits below it
        for (double ox : {0.0, 0.5})
            target.drawText("COSMO", 18.0 + ox, 22.0, 18.0);
        target.setFill(palette::faint());
        target.drawText("by arstro", 92.0, 22.0, 11.0);
        target.beginPath();
        target.moveTo(0.0, kTopBar); target.lineTo(mW, kTopBar);
        target.setStroke(palette::line(), 1.0); target.strokePath();

        target.setFill(palette::muted());
        if (mCurrentSlot >= 0)
        {
            const std::string status = mSlotNames[mCurrentSlot] + "   (" + std::to_string(mCurrentSlot + 1) +
                                       "/" + std::to_string(imageCount()) + ")";
            target.drawText(status, mW - 280.0, 22.0, 12.0);
        }
        else
        {
            target.drawText("Open an image  -  press O or File > Open", mW - 320.0, 22.0, 12.0);
            drawRoundedRect(target, mPhotoRect, radius::panel(),
                            Paint::filledStroked(palette::panel(), palette::line(), 1.0));
        }
        target.restore();

        mRoot->render(target);
        mRoot->renderOverlay(target);  // open dropdowns/popups, on top of everything
    }
}
}
