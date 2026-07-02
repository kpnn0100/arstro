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
        // Add a group's scalar offset onto an EditParams (one level of the chain).
        void addOffset(EditParams &e, const arstro::LocalAdjust &d)
        {
            e.exposure += d.exposure; e.contrast += d.contrast;
            e.highlights += d.highlights; e.shadows += d.shadows; e.whites += d.whites; e.blacks += d.blacks;
            e.temp += d.temp / 100.f * 3500.f;  // relative warm/cool shift in Kelvin
            e.tint += d.tint; e.saturation += d.saturation;
            e.texture += d.texture; e.clarity += d.clarity; e.dehaze += d.dehaze;
        }

        // Friendly label for a generic .apf category key (see EditParamsApf).
        std::string catLabel(const std::string &key)
        {
            if (key == "basic") return "Basic (exposure, contrast, tone)";
            if (key == "color") return "Color (white balance, vibrance)";
            if (key == "presence") return "Presence (texture, clarity, dehaze)";
            if (key == "effects") return "Effects (grain)";
            if (key == "detail") return "Detail (sharpen, noise reduction)";
            if (key == "lens") return "Lens corrections";
            if (key == "curve") return "Tone curve";
            if (key == "mixer") return "Color mixer";
            if (key == "grade") return "Color grading";
            if (key == "transform") return "Crop and rotate";
            if (key == "masks") return "Masks (local adjustments)";
            return key;
        }
    }

    std::vector<PresetDialog::Row> CosmoApp::buildPresetRows(const std::vector<std::string> &keys)
    {
        std::vector<PresetDialog::Row> rows;
        for (const auto &k : keys) rows.push_back({k, catLabel(k), true});
        return rows;
    }

    bool CosmoApp::presetPickerOpen() const { return mPresetDialog && mPresetDialog->isOpen(); }
    std::vector<double> CosmoApp::testBarRect() const
    { return {mPresetBar->x.value(), mPresetBar->y.value(), mPresetBar->width.value(), mPresetBar->height.value()}; }
    std::vector<double> CosmoApp::testHistoryNodeXY(int i) const
    { auto p = mHistoryView->testNodeCenter(i); return {p.x, p.y}; }
    bool CosmoApp::historyPopupOpen() const { return mHistoryView && mHistoryView->isOpen(); }
    int CosmoApp::filmstripCells() const { return mFilmstrip ? mFilmstrip->cellCount() : 0; }
    double CosmoApp::imageZoom() const { return mImageView ? mImageView->zoom() : 1.0; }
    int CosmoApp::previewPixelWidth() const { return mImageView ? mImageView->imageWidth() : 0; }

    int CosmoApp::nodeForSlot(int slot) const
    {
        for (int i = 0; i < (int)mNodes.size(); ++i)
            if (!mNodes[i].group && mNodes[i].slot == slot) return i;
        return -1;
    }

    // An image renders as its own params with every ancestor group's offset summed in.
    EditParams CosmoApp::effectiveParams(int slot) const
    {
        if (slot < 0 || slot >= (int)mSlotParams.size()) return EditParams{};
        EditParams e = mSlotParams[slot];
        int n = nodeForSlot(slot);
        if (n >= 0)
            for (int g = mNodes[n].parent; ; g = mNodes[g].parent)  // walk up to the root
            {
                addOffset(e, mNodes[g].offset);
                if (g == 0) break;
            }
        return e;
    }

    EditParams *CosmoApp::curParams() { return mCurrentSlot >= 0 ? &mSlotParams[mCurrentSlot] : nullptr; }

    void CosmoApp::submit()
    {
        if (mCurrentSlot < 0) return;
        recordHistory();  // capture the edit that led here (no-op for navigation / undo re-apply)
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

        mNodes.push_back(GNode{true, "All Photos", 0, -1, {}, {}});  // root group (index 0)

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

        // Group offset panel: scalar offsets added to every descendant image (shown
        // in place of the tabs when a group is the edit target).
        auto goff = [this](const char *label, double mn, double mx, float arstro::LocalAdjust::*field) {
            return ParamPanel::Spec{label, mn, mx, 0, [this, field](double v) {
                                        if (mEditGroup >= 0 && mEditGroup < (int)mNodes.size())
                                        { mNodes[mEditGroup].offset.*field = (float)v; submit(); }
                                    }};
        };
        std::vector<ParamPanel::Section> groupSecs = {{"GROUP OFFSET", {
            goff("exposure", -5, 5, &arstro::LocalAdjust::exposure),
            goff("contrast", -100, 100, &arstro::LocalAdjust::contrast),
            goff("highlights", -100, 100, &arstro::LocalAdjust::highlights),
            goff("shadows", -100, 100, &arstro::LocalAdjust::shadows),
            goff("whites", -100, 100, &arstro::LocalAdjust::whites),
            goff("blacks", -100, 100, &arstro::LocalAdjust::blacks),
            goff("temp", -100, 100, &arstro::LocalAdjust::temp),
            goff("tint", -100, 100, &arstro::LocalAdjust::tint),
            goff("saturation", -100, 100, &arstro::LocalAdjust::saturation),
            goff("texture", -100, 100, &arstro::LocalAdjust::texture),
            goff("clarity", -100, 100, &arstro::LocalAdjust::clarity),
            goff("dehaze", -100, 100, &arstro::LocalAdjust::dehaze),
        }}};
        mGroupPanel = std::make_shared<ParamPanel>("GROUP", mTheme, mAccent, groupSecs);
        mGroupPanel->visible = false;
        mRoot->addChild(mGroupPanel);

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

        // Save / Import / Export bar snapped to the bottom of the edit column.
        mPresetBar = std::make_shared<PresetBar>(mAccent);
        mPresetBar->onSave = [this] { presetSaveClicked(); };
        mPresetBar->onImport = [this] { presetImportClicked(); };
        mPresetBar->onExport = [this] { presetExportClicked(); };
        mRoot->addChild(mPresetBar);

        mFilmstrip = std::make_shared<Filmstrip>(mAccent);
        mFilmstrip->onSelect = [this](int cell, bool shift, bool ctrl) { selectNode(cell, shift, ctrl); };
        mFilmstrip->onActivate = [this](int cell) {
            const auto &kids = mNodes[mCurGroup].kids;
            if (cell >= 0 && cell < (int)kids.size() && mNodes[kids[cell]].group) navigateToGroup(kids[cell]);
        };
        mFilmstrip->onContext = [this](int cell, double x, double y) { showCellContext(cell, x, y); };
        mRoot->addChild(mFilmstrip);

        mBreadcrumb = std::make_shared<Breadcrumb>(mAccent);
        mBreadcrumb->onNavigate = [this](int level) {
            // walk up from mCurGroup to the chosen depth
            std::vector<int> chain; for (int n = mCurGroup; ; n = mNodes[n].parent) { chain.push_back(n); if (n == 0) break; }
            std::reverse(chain.begin(), chain.end());
            if (level >= 0 && level < (int)chain.size()) navigateToGroup(chain[level]);
        };
        mRoot->addChild(mBreadcrumb);

        // Settings now lives in the menu bar (a floating overlay, not a tab).
        mSettings = std::make_shared<SettingsPanel>(mTheme, mAccent);
        mSettings->onPreviewEdge = [this](int edge) { mPreviewEdge = edge; mService.setPreviewSize(edge); submit(); };
        mSettings->onThreads = [this](int n) { par::setThreads(n); submit(); };
        mSettings->onHistorySteps = [this](int steps) { setHistoryLimits(steps, mHistoryCoalesceMs); };
        mSettings->onHistoryCoalesce = [this](int ms) { setHistoryLimits(mHistorySteps, (double)ms); };
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
            std::vector<int> slots;
            for (int n : mSel)
                if (n >= 0 && n < (int)mNodes.size() && !mNodes[n].group && mNodes[n].slot >= 0)
                    slots.push_back(mNodes[n].slot);
            if (slots.empty() && mCurrentSlot >= 0) slots.push_back(mCurrentSlot);
            pasteTo(slots);
        }});
        develop.items.push_back({"Paste to All Images", [this] {
            std::vector<int> all(imageCount());
            for (int i = 0; i < imageCount(); ++i) all[i] = i;
            pasteTo(all);
        }});
        develop.items.push_back({"Group Selection", [this] { createGroupFromSelection(); }});
        develop.items.push_back({"Ungroup Selection", [this] { ungroupSelected(); }});
        mMenuBar->addMenu(develop);

        MenuBar::Menu history;
        history.title = "History";
        history.items.push_back({"Undo   (Ctrl+Z)", [this] { undo(); }});
        history.items.push_back({"Redo   (Ctrl+Y)", [this] { redo(); }});
        history.items.push_back({"Show History Tree...", [this] { openHistoryView(); }});
        mMenuBar->addMenu(history);

        MenuBar::Menu preset;
        preset.title = "Preset";
        preset.items.push_back({"Save Preset...", [this] { presetSaveClicked(); }});
        preset.items.push_back({"Import Preset...", [this] { presetImportClicked(); }});
        mMenuBar->addMenu(preset);
        mPresetMenuIndex = mMenuBar->menuCount() - 1;
        // One active menu at a time (tab-like): the Settings panel shows iff Settings
        // is the active menu, so opening File closes Settings and vice versa.
        mMenuBar->onOpenChanged = [this](int open) { mSettings->visible = (open == 1); };
        mRoot->addChild(mMenuBar);

        mContextMenu = std::make_shared<ContextMenu>(mAccent);  // right-click popup (modal when open)
        mRoot->addChild(mContextMenu);

        mPresetDialog = std::make_shared<PresetDialog>(mAccent);  // modal category picker (overlay)
        mRoot->addChild(mPresetDialog);

        mHistoryView = std::make_shared<HistoryView>(mAccent);    // git-tree history popup (overlay)
        mHistoryView->onSelect = [this](int node) { jumpToHistory(node); };
        mRoot->addChild(mHistoryView);

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
        const double crumbY = filmY - 18.0;         // breadcrumb sits just above the filmstrip
        const double compareRowY = crumbY - 28.0;   // a control row under the photo (before/after)
        mPhotoRect = Rect{kMargin, photoY, rightX - kMargin - kMargin, compareRowY - photoY - 6.0};

        mImageView->x.set(mPhotoRect.x); mImageView->y.set(mPhotoRect.y);
        mImageView->width.set(mPhotoRect.w); mImageView->height.set(mPhotoRect.h);

        mMaskOverlay->x.set(mPhotoRect.x); mMaskOverlay->y.set(mPhotoRect.y);
        mMaskOverlay->width.set(mPhotoRect.w); mMaskOverlay->height.set(mPhotoRect.h);
        mCropOverlay->x.set(mPhotoRect.x); mCropOverlay->y.set(mPhotoRect.y);
        mCropOverlay->width.set(mPhotoRect.w); mCropOverlay->height.set(mPhotoRect.h);
        mCompareView->x.set(mPhotoRect.x); mCompareView->y.set(mPhotoRect.y);
        mCompareView->width.set(mPhotoRect.w); mCompareView->height.set(mPhotoRect.h);
        // before/after toggle: its own row under the photo, centred
        mCompareToggle->x.set(mPhotoRect.x + (mPhotoRect.w - 112.0) * 0.5); mCompareToggle->y.set(compareRowY + 3.0);
        mCompareToggle->width.set(112.0); mCompareToggle->height.set(20.0);
        mHistogram->x.set(rightX); mHistogram->y.set(photoY);
        mHistogram->layout(rightW, histH);

        const double colBottom = filmY - 8.0;          // right column's bottom edge
        const double barH = 30.0;                       // preset button bar
        const double barY = colBottom - barH;
        const double tabsY = photoY + histH + 10.0;
        const double tabsH = (barY - 8.0) - tabsY;      // leave an 8px gap above the bar
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
        // group offset panel shares the tabs' rect (shown when a group is selected)
        mGroupPanel->x.set(rightX); mGroupPanel->y.set(tabsY);
        mGroupPanel->layout(rightW, tabsH);

        // preset bar pinned to the bottom of the edit column
        mPresetBar->x.set(rightX); mPresetBar->y.set(barY);
        mPresetBar->width.set(rightW); mPresetBar->height.set(barH);
        // modal category picker covers the root so it can centre + be modal
        mPresetDialog->x.set(0); mPresetDialog->y.set(0);
        mPresetDialog->width.set(mW); mPresetDialog->height.set(mH);
        // history popup also covers the root (centred, modal)
        mHistoryView->x.set(0); mHistoryView->y.set(0);
        mHistoryView->width.set(mW); mHistoryView->height.set(mH);

        // breadcrumb row above the filmstrip
        mBreadcrumb->x.set(kMargin); mBreadcrumb->y.set(crumbY);
        mBreadcrumb->width.set(rightX - 2 * kMargin); mBreadcrumb->height.set(18.0);
        mFilmstrip->x.set(kMargin); mFilmstrip->y.set(filmY);
        mFilmstrip->width.set(rightX - kMargin - kMargin); mFilmstrip->height.set(filmH);

        // context menu covers the root so it can clamp + be modal
        mContextMenu->x.set(0); mContextMenu->y.set(0);
        mContextMenu->width.set(mW); mContextMenu->height.set(mH);

        // menu bar (top-left, under the wordmark) + floating settings overlay
        mMenuBarX = kMargin; mMenuBarY = 34.0;
        mMenuBar->x.set(mMenuBarX); mMenuBar->y.set(mMenuBarY);
        mMenuBar->width.set(380.0); mMenuBar->height.set(22.0);
        mSettings->x.set(kMargin); mSettings->y.set(kTopBar + 6.0);
        mSettings->layout(280.0, 214.0);

        mService.setPreviewSize(mPreviewEdge);
        submit();
    }

    void CosmoApp::setSize(double width, double height)
    {
        if (width < 320) width = 320;
        if (height < 240) height = 240;
        mW = width; mH = height;
        mRoot->width.set(width); mRoot->height.set(height);
        mImageView->resetView();  // zoom math depends on the photo bounds; reset on resize
        mService.setPreviewSize(mPreviewEdge);  // base preview resolution at 1x
        layout();
    }

    int CosmoApp::openImage(const uint8_t *rgba, int w, int h, const std::string &name, const std::string &path)
    {
        const int slot = mService.addImage(rgba, w, h, 4);
        if (slot < 0) return -1;
        mSlotParams.push_back(EditParams{});
        History hist; hist.maxSteps = mHistorySteps; hist.coalesceMs = mHistoryCoalesceMs;
        hist.init(EditParams{});   // root = the freshly-opened, unedited state
        mSlotHistory.push_back(std::move(hist));
        mSlotNames.push_back(name);
        mSlotPaths.push_back(path);
        mSlotSessions.push_back("");
        int tw = 0, th = 0;
        std::vector<uint8_t> thumb = makeThumb(rgba, w, h, 110, tw, th);
        mFilmstrip->addThumb(thumb.data(), tw, th);

        // add an image leaf node under the current group
        GNode leaf; leaf.group = false; leaf.name = name; leaf.parent = mCurGroup; leaf.slot = slot;
        const int node = (int)mNodes.size();
        mNodes.push_back(leaf);
        mNodes[mCurGroup].kids.push_back(node);

        rebuildFilmstrip();
        // select the new image (find its cell in the current group)
        const auto &kids = mNodes[mCurGroup].kids;
        for (int c = 0; c < (int)kids.size(); ++c)
            if (kids[c] == node) { selectNode(c, false, false); break; }
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
        const int n = nodeForSlot(slot);
        if (n >= 0)
        {
            mCurGroup = mNodes[n].parent;
            rebuildFilmstrip();
            const auto &kids = mNodes[mCurGroup].kids;
            for (int c = 0; c < (int)kids.size(); ++c)
                if (kids[c] == n) { selectNode(c, false, false); return; }
        }
        mCurrentSlot = slot;  // fallback (no node yet)
        syncControlsToSlot();
        submit();
    }

    void CosmoApp::syncControlsToSlot()
    {
        if (mCurrentSlot < 0) return;
        const EditParams &p = mSlotParams[mCurrentSlot];  // the tabs edit the image's own params
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
                recordSlotEdit(i);                 // a discrete "Paste" step in that image's history
                if (i == mCurrentSlot) affectedCurrent = true;
            }
        if (affectedCurrent) { syncControlsToSlot(); submit(); }  // others re-render when selected
    }

    // ── edit history (branching time machine) ──
    void CosmoApp::recordHistory()
    {
        if (mSuppressHistory) return;
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return;
        History &h = mSlotHistory[mCurrentSlot];
        if (h.empty()) h.init(mSlotParams[mCurrentSlot]);
        h.record(mSlotParams[mCurrentSlot], mNowMs);
    }

    void CosmoApp::recordSlotEdit(int slot)
    {
        if (slot < 0 || slot >= (int)mSlotHistory.size()) return;
        History &h = mSlotHistory[slot];
        if (h.empty()) h.init(mSlotParams[slot]);
        h.breakCoalesce();                              // paste/import/apply = a discrete step
        h.record(mSlotParams[slot], mNowMs);
    }

    void CosmoApp::applyHistoryParams(const EditParams *p)
    {
        if (!p || mCurrentSlot < 0) return;
        mSlotParams[mCurrentSlot] = *p;
        mSuppressHistory = true;                        // re-applying a node must not add one
        syncControlsToSlot();
        submit();
        mSuppressHistory = false;
        if (mHistoryView && mHistoryView->isOpen())
            mHistoryView->setCurrent(mSlotHistory[mCurrentSlot].current);
    }

    void CosmoApp::undo()
    {
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return;
        applyHistoryParams(mSlotHistory[mCurrentSlot].undo());
    }

    void CosmoApp::redo()
    {
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return;
        applyHistoryParams(mSlotHistory[mCurrentSlot].redo());
    }

    void CosmoApp::jumpToHistory(int node)
    {
        if (mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return;
        applyHistoryParams(mSlotHistory[mCurrentSlot].jumpTo(node));
    }

    void CosmoApp::openHistoryView()
    {
        if (!mHistoryView || mCurrentSlot < 0 || mCurrentSlot >= (int)mSlotHistory.size()) return;
        const History &h = mSlotHistory[mCurrentSlot];
        std::vector<HistoryView::Node> snap;
        snap.reserve(h.nodes.size());
        for (const auto &n : h.nodes) snap.push_back({n.parent, n.label});
        mHistoryView->show(std::move(snap), h.current);
    }

    bool CosmoApp::canUndo() const
    { return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() && mSlotHistory[mCurrentSlot].canUndo(); }
    bool CosmoApp::canRedo() const
    { return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() && mSlotHistory[mCurrentSlot].canRedo(); }
    int CosmoApp::historyNodeCount() const
    { return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() ? (int)mSlotHistory[mCurrentSlot].nodes.size() : 0; }
    int CosmoApp::historyCurrent() const
    { return mCurrentSlot >= 0 && mCurrentSlot < (int)mSlotHistory.size() ? mSlotHistory[mCurrentSlot].current : -1; }

    void CosmoApp::setHistoryLimits(int maxSteps, double coalesceMs)
    {
        mHistorySteps = maxSteps > 2 ? maxSteps : 2;
        mHistoryCoalesceMs = coalesceMs < 0 ? 0 : coalesceMs;
        for (auto &h : mSlotHistory) h.setLimits(mHistorySteps, mHistoryCoalesceMs);
    }

    void CosmoApp::setPresetDir(const std::string &dir)
    {
        mPresetDir = dir;
        refreshPresetMenu();
    }

    // Every image slot implied by the current selection (a selected group expands to
    // all its descendant image leaves); falls back to the current image.
    std::vector<int> CosmoApp::selectedImageSlots() const
    {
        std::vector<int> slots;
        std::function<void(int)> collect = [&](int node) {
            if (node < 0 || node >= (int)mNodes.size()) return;
            if (mNodes[node].group) { for (int k : mNodes[node].kids) collect(k); }
            else if (mNodes[node].slot >= 0) slots.push_back(mNodes[node].slot);
        };
        for (int n : mSel) collect(n);
        if (slots.empty() && mCurrentSlot >= 0) slots.push_back(mCurrentSlot);
        return slots;
    }

    void CosmoApp::presetSaveClicked()
    {
        if (mCurrentSlot < 0 || !mPresetDialog) return;
        mPresetDialog->show("Save preset", "Continue", buildPresetRows(apfImageCategories()),
            [this](std::vector<std::string> cats) {
                mPendingCategories = std::move(cats);
                if (onSavePresetRequested) onSavePresetRequested();  // host name dialog -> savePreset(name)
            });
    }

    void CosmoApp::presetExportClicked()
    {
        if (mCurrentSlot < 0 || !mPresetDialog) return;
        mPresetDialog->show("Export preset", "Continue", buildPresetRows(apfImageCategories()),
            [this](std::vector<std::string> cats) {
                mPendingCategories = std::move(cats);
                if (onExportPresetRequested) onExportPresetRequested();  // host path dialog -> exportPresetTo(path)
            });
    }

    void CosmoApp::presetImportClicked()
    {
        if (onImportPresetRequested) onImportPresetRequested();  // host open dialog -> importPresetFrom(path)
    }

    bool CosmoApp::savePreset(const std::string &name)
    {
        if (mPresetDir.empty() || name.empty() || mCurrentSlot < 0) return false;
        std::error_code ec;
        std::filesystem::create_directories(mPresetDir, ec);
        const std::vector<std::string> cats = mPendingCategories.empty() ? apfImageCategories() : mPendingCategories;
        const apf::Document doc = editParamsToApf(mSlotParams[mCurrentSlot], cats, name);
        std::ofstream f(mPresetDir + "/" + name + ".apf");
        if (!f) return false;
        f << apf::serialize(doc);
        refreshPresetMenu();
        return true;
    }

    bool CosmoApp::exportPresetTo(const std::string &path)
    {
        if (mCurrentSlot < 0 || path.empty()) return false;
        const std::vector<std::string> cats = mPendingCategories.empty() ? apfImageCategories() : mPendingCategories;
        const std::string name = std::filesystem::path(path).stem().string();
        const apf::Document doc = editParamsToApf(mSlotParams[mCurrentSlot], cats, name);
        std::ofstream f(path);
        if (!f) return false;
        f << apf::serialize(doc);
        return true;
    }

    bool CosmoApp::importPresetFrom(const std::string &path)
    {
        std::ifstream f(path);
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        apf::Document doc;
        if (!apf::parse(ss.str(), doc)) return false;
        if (!doc.engine.empty() && doc.engine != apfImageEngine()) return false;  // different engine domain
        const std::vector<std::string> present = apfPresentImageCategories(doc);
        if (present.empty()) return false;
        mPendingApf = doc;
        if (mPresetDialog)
            mPresetDialog->show("Import preset", "Apply", buildPresetRows(present),
                [this](std::vector<std::string> cats) { applyImport(cats); });
        else
            applyImport(present);
        return true;
    }

    void CosmoApp::applyImport(const std::vector<std::string> &categories)
    {
        bool affectedCurrent = false;
        for (int s : selectedImageSlots())  // current image, or all images in the selected group
            if (s >= 0 && s < (int)mSlotParams.size())
            {
                if (applyApfToEditParams(mPendingApf, categories, mSlotParams[s]))
                {
                    recordSlotEdit(s);          // a discrete "import preset" step per image
                    if (s == mCurrentSlot) affectedCurrent = true;
                }
            }
        if (affectedCurrent) { syncControlsToSlot(); submit(); }  // others re-render when selected
    }

    bool CosmoApp::applyPreset(const std::string &name)
    {
        if (mPresetDir.empty() || mCurrentSlot < 0) return false;
        std::ifstream f(mPresetDir + "/" + name + ".apf");
        if (!f) return false;
        std::stringstream ss; ss << f.rdbuf();
        apf::Document doc;
        if (!apf::parse(ss.str(), doc)) return false;
        if (!doc.engine.empty() && doc.engine != apfImageEngine()) return false;
        if (!applyApfToEditParams(doc, apfPresentImageCategories(doc), mSlotParams[mCurrentSlot])) return false;
        syncControlsToSlot();
        submit();
        return true;
    }

    void CosmoApp::refreshPresetMenu()
    {
        if (!mMenuBar || mPresetMenuIndex < 0) return;
        std::vector<MenuBar::Item> items;
        items.push_back({"Save Preset...", [this] { presetSaveClicked(); }});
        items.push_back({"Import Preset...", [this] { presetImportClicked(); }});
        std::error_code ec;
        if (!mPresetDir.empty() && std::filesystem::is_directory(mPresetDir, ec))
        {
            std::vector<std::string> names;
            for (const auto &e : std::filesystem::directory_iterator(mPresetDir, ec))
                if (e.path().extension() == ".apf")
                    names.push_back(e.path().stem().string());
            std::sort(names.begin(), names.end());
            for (const auto &n : names)
                items.push_back({n, [this, n] { applyPreset(n); }});
        }
        mMenuBar->setMenuItems(mPresetMenuIndex, std::move(items));
    }

    void CosmoApp::rebuildFilmstrip()
    {
        std::vector<Filmstrip::Cell> cells;
        for (int n : mNodes[mCurGroup].kids)
        {
            const GNode &g = mNodes[n];
            Filmstrip::Cell c;
            c.group = g.group; c.node = n; c.name = g.name;
            if (g.group) c.count = (int)g.kids.size();
            else c.thumbSlot = g.slot;
            cells.push_back(c);
        }
        mFilmstrip->setCells(std::move(cells));

        // breadcrumb: root -> ... -> current group
        std::vector<int> chain;
        for (int n = mCurGroup; ; n = mNodes[n].parent) { chain.push_back(n); if (n == 0) break; }
        std::reverse(chain.begin(), chain.end());
        std::vector<std::string> names;
        for (int n : chain) names.push_back(mNodes[n].name);
        mBreadcrumb->setPath(names);

        // refresh the highlight from the current selection
        std::vector<int> selCells; int primary = -1;
        const auto &kids = mNodes[mCurGroup].kids;
        for (int c = 0; c < (int)kids.size(); ++c)
            if (std::find(mSel.begin(), mSel.end(), kids[c]) != mSel.end()) selCells.push_back(c);
        for (int c = 0; c < (int)kids.size(); ++c)
            if ((mEditGroup >= 0 && kids[c] == mEditGroup) ||
                (mEditGroup < 0 && !mNodes[kids[c]].group && mNodes[kids[c]].slot == mCurrentSlot)) primary = c;
        mFilmstrip->setSelection(selCells, primary);
    }

    void CosmoApp::navigateToGroup(int node)
    {
        if (node < 0 || node >= (int)mNodes.size() || !mNodes[node].group) return;
        mCurGroup = node;
        mSel.clear(); mSelAnchor = -1;
        rebuildFilmstrip();
    }

    void CosmoApp::setEditTarget(int node)
    {
        if (node < 0 || node >= (int)mNodes.size()) return;
        if (mNodes[node].group)
        {
            mEditGroup = node;
            mTabs->visible = false;
            mGroupPanel->visible = true;
            const arstro::LocalAdjust &o = mNodes[node].offset;
            mGroupPanel->setValues({o.exposure, o.contrast, o.highlights, o.shadows, o.whites, o.blacks,
                                    o.temp, o.tint, o.saturation, o.texture, o.clarity, o.dehaze});
        }
        else
        {
            mEditGroup = -1;
            mTabs->visible = true;
            mGroupPanel->visible = false;
            mCurrentSlot = mNodes[node].slot;
            mImageView->resetView();  // start each image at 1x
            mService.setPreviewSize(mPreviewEdge);  // back to the base preview resolution
            syncControlsToSlot();
            submit();
        }
    }

    void CosmoApp::selectNode(int cell, bool shift, bool ctrl)
    {
        const auto &kids = mNodes[mCurGroup].kids;
        if (cell < 0 || cell >= (int)kids.size()) return;
        const int node = kids[cell];
        if (shift && mSelAnchor >= 0 && mSelAnchor < (int)kids.size())
        {
            mSel.clear();
            const int lo = mSelAnchor < cell ? mSelAnchor : cell, hi = mSelAnchor < cell ? cell : mSelAnchor;
            for (int k = lo; k <= hi; ++k) mSel.push_back(kids[k]);
        }
        else if (ctrl)
        {
            auto it = std::find(mSel.begin(), mSel.end(), node);
            if (it != mSel.end()) { if (mSel.size() > 1) mSel.erase(it); }
            else mSel.push_back(node);
            mSelAnchor = cell;
        }
        else
        {
            mSel = {node};
            mSelAnchor = cell;
        }
        setEditTarget(node);  // edit the (last-touched) node: image -> tabs, group -> offsets
        rebuildFilmstrip();
    }

    void CosmoApp::createGroupFromSelection()
    {
        if (mSel.empty()) return;
        // new group under the current group, named sequentially
        int gi = 1; for (const auto &n : mNodes) if (n.group) ++gi;
        GNode grp; grp.group = true; grp.name = "Group " + std::to_string(gi); grp.parent = mCurGroup;
        const int gnode = (int)mNodes.size();
        mNodes.push_back(grp);
        auto &siblings = mNodes[mCurGroup].kids;
        // move every selected node (that is a direct child of mCurGroup) into the new group
        std::vector<int> moved = mSel;
        siblings.erase(std::remove_if(siblings.begin(), siblings.end(),
                       [&](int k) { return std::find(moved.begin(), moved.end(), k) != moved.end(); }),
                       siblings.end());
        for (int k : moved) { mNodes[k].parent = gnode; mNodes[gnode].kids.push_back(k); }
        siblings.push_back(gnode);
        mSel = {gnode};
        mSelAnchor = -1;
        rebuildFilmstrip();
        // select the new group cell for editing
        const auto &kids = mNodes[mCurGroup].kids;
        for (int c = 0; c < (int)kids.size(); ++c) if (kids[c] == gnode) { selectNode(c, false, false); break; }
    }

    void CosmoApp::ungroupSelected()
    {
        // ungroup each selected group: move its children up to the current group, drop the group
        std::vector<int> sel = mSel;
        for (int n : sel)
        {
            if (n < 0 || n >= (int)mNodes.size() || !mNodes[n].group || n == 0) continue;
            auto &kids = mNodes[mCurGroup].kids;
            for (int child : mNodes[n].kids) { mNodes[child].parent = mCurGroup; kids.push_back(child); }
            mNodes[n].kids.clear();
            kids.erase(std::remove(kids.begin(), kids.end(), n), kids.end());  // detach the empty group
        }
        mSel.clear(); mSelAnchor = -1;
        rebuildFilmstrip();
        submit();
    }

    void CosmoApp::showCellContext(int cell, double x, double y)
    {
        const auto &kids = mNodes[mCurGroup].kids;
        if (cell < 0 || cell >= (int)kids.size()) return;
        const int node = kids[cell];
        if (std::find(mSel.begin(), mSel.end(), node) == mSel.end())
            selectNode(cell, false, false);  // right-click selects if not already

        std::vector<ContextMenu::Item> items;
        items.push_back({"Group Selection", [this] { createGroupFromSelection(); }, !mSel.empty()});
        const bool oneGroup = mSel.size() == 1 && mNodes[mSel[0]].group;
        items.push_back({"Rename Group...", [this] {
            if (!mSel.empty() && mNodes[mSel[0]].group) { mRenameTarget = mSel[0]; if (onRenameGroupRequested) onRenameGroupRequested(); }
        }, oneGroup});
        items.push_back({"Ungroup", [this] { ungroupSelected(); }, oneGroup});
        mContextMenu->show(x, y, std::move(items));
    }

    void CosmoApp::renameGroup(const std::string &name)
    {
        if (mRenameTarget > 0 && mRenameTarget < (int)mNodes.size() && mNodes[mRenameTarget].group && !name.empty())
        {
            mNodes[mRenameTarget].name = name;
            rebuildFilmstrip();
        }
        mRenameTarget = -1;
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
        // Preview-size (not full-res) so the compare overlay is cheap to draw each frame.
        if (mService.renderPreviewSync(mCurrentSlot, b, mBeforeFrame) && mBeforeFrame.width > 0)
            mCompareView->setBefore(mBeforeFrame.rgba.data(), mBeforeFrame.width, mBeforeFrame.height);
    }

    const uint8_t *CosmoApp::exportFullRes(int &w, int &h)
    {
        if (mCurrentSlot < 0) { w = h = 0; return nullptr; }
        if (!mService.renderFull(mCurrentSlot, effectiveParams(mCurrentSlot), mExportFrame)) { w = h = 0; return nullptr; }
        w = mExportFrame.width; h = mExportFrame.height;
        return mExportFrame.rgba.data();
    }

    void CosmoApp::wheel(double x, double y, double delta, bool ctrl)
    {
        if (!ctrl || delta == 0.0) return;  // ctrl+scroll = zoom; plain scroll ignored
        if (x < mPhotoRect.x || x > mPhotoRect.x + mPhotoRect.w ||
            y < mPhotoRect.y || y > mPhotoRect.y + mPhotoRect.h)
            return;  // only over the photo
        mImageView->zoomAbout(delta > 0 ? 1.15 : 1.0 / 1.15, Point{x - mPhotoRect.x, y - mPhotoRect.y});
        // Re-render the preview at a higher resolution proportional to the zoom so a
        // high-res original stays sharp when magnified (engine caps at the source size).
        const int eff = std::max(mPreviewEdge, (int)(mPreviewEdge * mImageView->zoom() + 0.5));
        mService.setPreviewSize(eff);
        submit();
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
        mNowMs = nowMs;  // history coalescing uses the latest frame time
        mRoot->advance(nowMs);

        // Pick up any completed frame from the worker (non-blocking).
        RenderService::Frame f;
        if (mService.tryAcquire(f) && f.width > 0)
        {
            mImageView->setImage(f.rgba.data(), f.width, f.height);
            mHistogram->setHistogram(f.hist);

            // curve background = luminance ENTERING the curve (pre-curve tap) (#15)
            std::vector<float> lum(HistogramData::kBins);
            const double lmax = f.preCurveHist.maxCount > 0 ? (double)f.preCurveHist.maxCount : 1.0;
            for (int i = 0; i < HistogramData::kBins; ++i) lum[i] = (float)(f.preCurveHist.lum[i] / lmax);
            mCurve->setHistogram(std::move(lum));

            // mixer background = hue distribution ENTERING the mixer (pre-mixer tap) (#2)
            mMixer->setHueHistogram(std::vector<float>(f.preMixerHue.bins.begin(), f.preMixerHue.bins.end()));
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
            drawRoundedRect(target, mPhotoRect, radius::panel(),
                            Paint::filledStroked(palette::panel(), palette::line(), 1.0));
        }
        target.restore();

        mRoot->render(target);
        mRoot->renderOverlay(target);  // open dropdowns/popups, on top of everything
    }
}
}
