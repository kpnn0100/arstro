#include "RightColumn.h"
#include "../Theme.h"
#include "UnitConversions.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        // Fixed content heights for the merged Mixer/Curve stack tab: Mixer's editor
        // caps at ~200px + its picker; Curve is header + picker + a 164px plot.
        constexpr double kMixerStackH = 250.0;
        constexpr double kCurveStackH = 235.0;
    }

    RightColumn::RightColumn(cosmo::EditSession &session) : mSession(session)
    {
        clipToBounds = true;
        width.set(kWidth);

        mHistogram = std::make_shared<HistogramWidget>();
        addChild(mHistogram);

        mTabs = std::make_shared<EditStackTabs>();
        mTabs->tabHeight = 27.0;

        auto set = [this](std::function<void(EditParams &, double)> setter) {
            return [this, setter](double v) {
                if (auto *p = mSession.curParams()) { setter(*p, v); mSession.submit(); }
            };
        };

        std::vector<ParamPanel::Section> basicSections = {
            {"TONE", {
                {"Exposure", -400, 400, set([](EditParams &p, double v) { p.exposure = (float)toEv(v); })},
                {"Contrast", -100, 100, set([](EditParams &p, double v) { p.contrast = (float)v; })},
                {"Highlights", -100, 100, set([](EditParams &p, double v) { p.highlights = (float)v; })},
                {"Shadows", -100, 100, set([](EditParams &p, double v) { p.shadows = (float)v; })},
                {"Whites", -100, 100, set([](EditParams &p, double v) { p.whites = (float)v; })},
                {"Blacks", -100, 100, set([](EditParams &p, double v) { p.blacks = (float)v; })},
            }},
            {"COLOUR", {
                // Temperature / Tint tracks carry a colour ramp so the drag
                // direction reads as the colour it pushes toward (task point 6):
                // temperature cool-blue -> warm-amber, tint green -> magenta.
                {"Temperature", -100, 100, set([](EditParams &p, double v) { p.temp = (float)toKelvin(v); }),
                 true, Color::rgba(74, 132, 232), Color::rgba(240, 178, 84)},
                {"Tint", -100, 100, set([](EditParams &p, double v) { p.tint = (float)toTint(v); }),
                 true, Color::rgba(88, 196, 118), Color::rgba(206, 104, 196)},
                {"Vibrance", -100, 100, set([](EditParams &p, double v) { p.vibrance = (float)v; })},
                {"Saturation", -100, 100, set([](EditParams &p, double v) { p.saturation = (float)v; })},
            }},
            {"PRESENCE", {
                {"Texture", -100, 100, set([](EditParams &p, double v) { p.texture = (float)v; })},
                {"Clarity", -100, 100, set([](EditParams &p, double v) { p.clarity = (float)v; })},
                {"Dehaze", 0, 100, set([](EditParams &p, double v) { p.dehaze = (float)v; })},
            }},
            {"EFFECTS", {
                {"Grain Amount", 0, 100, set([](EditParams &p, double v) { p.grainAmount = (float)v; })},
                {"Grain Size", 0, 100, set([](EditParams &p, double v) { p.grainSize = (float)v; })},
            }},
        };
        std::vector<ParamPanel::Section> detailSections = {
            {"SHARPENING", {
                {"Amount", 0, 150, set([](EditParams &p, double v) { p.sharpenAmount = (float)v; })},
                {"Radius", 5, 30, set([](EditParams &p, double v) { p.sharpenRadius = (float)toRadiusPx(v); })},
                {"Masking", 0, 100, set([](EditParams &p, double v) { p.sharpenMasking = (float)v; })},
            }},
            {"NOISE REDUCTION", {
                {"Luminance", 0, 100, set([](EditParams &p, double v) { p.nrLuminance = (float)v; })},
                {"Colour", 0, 100, set([](EditParams &p, double v) { p.nrColor = (float)v; })},
            }},
            {"LENS", {
                {"Distortion", -100, 100, set([](EditParams &p, double v) { p.lensDistortion = (float)v; })},
                {"Defringe", 0, 100, set([](EditParams &p, double v) { p.lensCA = (float)v; })},
                {"Vignette", -100, 100, set([](EditParams &p, double v) { p.lensVignette = (float)v; })},
            }},
        };
        // Merge Basic + Detail into one scrollable tab: all sections stacked in order.
        for (auto &sec : detailSections) basicSections.push_back(std::move(sec));
        mBasicDetail = std::make_shared<ParamPanel>(std::move(basicSections));
        mTabs->addPage("Basic/Detail", mBasicDetail);

        mMask = std::make_shared<MaskPanel>();
        mMask->onAddMask = [this](int type) {
            if (auto *p = mSession.curParams())
            {
                MaskParams m;
                m.type = type;
                p->masks.push_back(m);
                mSelectedMask = (int)p->masks.size() - 1;
                mSession.submit();
                mMask->setMasks(p->masks, mSelectedMask);
            }
        };
        mMask->onToggleInvert = [this] {
            if (auto *p = mSession.curParams(); p && mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
            {
                p->masks[mSelectedMask].inverted = !p->masks[mSelectedMask].inverted;
                mSession.submit();
                mMask->setMasks(p->masks, mSelectedMask);
            }
        };
        mMask->onDeleteMask = [this] {
            if (auto *p = mSession.curParams(); p && mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
            {
                p->masks.erase(p->masks.begin() + mSelectedMask);
                mSelectedMask = p->masks.empty() ? -1 : std::min(mSelectedMask, (int)p->masks.size() - 1);
                mSession.submit();
                mMask->setMasks(p->masks, mSelectedMask);
            }
        };
        mMask->onSelectMask = [this](int i) {
            if (auto *p = mSession.curParams(); p && i >= 0 && i < (int)p->masks.size())
            {
                mSelectedMask = i;
                mMask->setMasks(p->masks, mSelectedMask);
            }
        };
        // Writing feather / adjust gives the mask a non-identity LocalAdjust so the
        // engine's applyMaskStack stops skipping it -> the mask visibly renders. No
        // setMasks() here (would fight a live drag); it re-syncs on select/add/slot.
        mMask->onFeatherChange = [this](double f) {
            if (auto *p = mSession.curParams(); p && mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
            {
                p->masks[mSelectedMask].feather = (float)f;
                mSession.submit();
            }
        };
        mMask->onAdjustChange = [this](const LocalAdjust &a) {
            if (auto *p = mSession.curParams(); p && mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
            {
                p->masks[mSelectedMask].adjust = a;
                mSession.submit();
            }
        };
        mTabs->addPage("Mask", mMask);

        mMixer = std::make_shared<MixerPanel>();
        mMixer->onCurveChange = [this](int channel, std::vector<CurvePoint> pts) {
            if (auto *p = mSession.curParams()) { p->mixer[channel] = std::move(pts); mSession.submit(); }
        };

        mCurve = std::make_shared<CurvePanel>();
        mCurve->onCurveChange = [this](int channel, CurvePanel::Points pts) {
            if (auto *p = mSession.curParams())
            {
                if (channel == 0) p->curve = std::move(pts);        // RGB master
                else p->curveChannel[channel - 1] = std::move(pts); // R / G / B
                mSession.submit();
            }
        };

        // Merge Mixer + Curve into one scrollable tab (Mixer above, Curve below).
        mColorTab = std::make_shared<StackPanel>();
        mColorTab->addItem(mMixer, kMixerStackH, [this] { mMixer->layout(); });
        mColorTab->addItem(mCurve, kCurveStackH, [this] { mCurve->layout(); });
        mTabs->addPage("Mixer/Curve", mColorTab);

        mGrade = std::make_shared<GradePanel>();
        mGrade->onRegionChange = [this](int region, double hue, double sat, double lum) {
            if (auto *p = mSession.curParams())
            {
                p->grade[region] = {(float)hue, (float)sat, (float)lum};
                mSession.submit();
            }
        };
        mGrade->onBalanceChange = [this](double v) {
            if (auto *p = mSession.curParams()) { p->balance = (float)v; mSession.submit(); }
        };
        mGrade->onRemapEnableChange = [this](bool on) {
            if (auto *p = mSession.curParams()) { p->remapEnable = on; mSession.submit(); }
        };
        mGrade->onRemapChange = [this](double src, double range, double dst, double strength) {
            if (auto *p = mSession.curParams())
            {
                p->remapSrc = (float)src; p->remapRange = (float)range; p->remapDst = (float)dst;
                p->remapStrength = (float)(strength / 100.0);
                mSession.submit();
            }
        };
        mTabs->addPage("Grade", mGrade);

        mXform = std::make_shared<XformPanel>();
        mXform->onRotationChange = [this](double deg) {
            if (auto *p = mSession.curParams()) { p->rotation = (float)deg; mSession.submit(); }
        };
        mXform->onQuarterTurn = [this](int dir) {
            if (auto *p = mSession.curParams())
            {
                p->quarterTurns = ((p->quarterTurns + dir) % 4 + 4) % 4;
                mSession.submit();
                mXform->setState({p->rotation, p->quarterTurns, p->cropX, p->cropY, p->cropW, p->cropH});
            }
        };
        mXform->onResetRotation = [this] {
            if (auto *p = mSession.curParams()) { p->rotation = 0; mSession.submit(); mXform->setState({p->rotation, p->quarterTurns, p->cropX, p->cropY, p->cropW, p->cropH}); }
        };
        mXform->onCropChange = [this](double x, double y, double w, double h) {
            if (auto *p = mSession.curParams())
            {
                p->cropX = (float)x; p->cropY = (float)y; p->cropW = (float)w; p->cropH = (float)h;
                mSession.submit();
            }
        };
        mTabs->addPage("Xform", mXform);

        addChild(mTabs);

        mActionBar = std::make_shared<ActionBar>();
        addChild(mActionBar);
    }

    void RightColumn::syncToSlot()
    {
        const EditParams *p = mSession.curParams();
        if (!p) return;
        // Basic sections then Detail sections, in the merged panel's flattened order.
        auto flat = [](const EditParams &q) {
            return std::vector<double>{
                fromEv(q.exposure), q.contrast, q.highlights, q.shadows, q.whites, q.blacks,
                fromKelvin(q.temp), fromTint(q.tint), q.vibrance, q.saturation,
                q.texture, q.clarity, q.dehaze, q.grainAmount, q.grainSize,
                q.sharpenAmount, fromRadiusPx(q.sharpenRadius), q.sharpenMasking,
                q.nrLuminance, q.nrColor,
                q.lensDistortion, q.lensCA, q.lensVignette,
            };
        };
        const std::vector<double> own = flat(*p);
        mBasicDetail->setValues(own);
        // Green stacked reach: how much ancestor groups add on top of each own value
        // (effective - own, in slider units). Zero (no groups) hides it (DR-EDIT-4).
        const std::vector<double> effv = flat(mSession.effectiveEditParams());
        std::vector<double> offsets(own.size(), 0.0);
        for (size_t i = 0; i < own.size(); ++i) offsets[i] = effv[i] - own[i];
        mBasicDetail->setSubValues(offsets);

        mSelectedMask = p->masks.empty() ? -1 : std::min(mSelectedMask < 0 ? 0 : mSelectedMask, (int)p->masks.size() - 1);
        mMask->setMasks(p->masks, mSelectedMask);
        mMixer->setMixer(p->mixer);
        mCurve->setCurves(p->curve, p->curveChannel);
        GradePanel::State gs;
        gs.grade = p->grade; gs.balance = p->balance; gs.remapEnable = p->remapEnable;
        gs.remapSrc = p->remapSrc; gs.remapRange = p->remapRange; gs.remapDst = p->remapDst; gs.remapStrength = p->remapStrength;
        mGrade->setState(gs);
        mXform->setState({p->rotation, p->quarterTurns, p->cropX, p->cropY, p->cropW, p->cropH});
    }

    int RightColumn::activeTab() const { return mTabs->selectedIndex(); }
    bool RightColumn::maskTabActive() const { return mTabs->selectedIndex() == kTabMask; }

    const MaskParams *RightColumn::selectedMaskParams() const
    {
        const EditParams *p = mSession.curParams();
        if (!p || mSelectedMask < 0 || mSelectedMask >= (int)p->masks.size()) return nullptr;
        return &p->masks[mSelectedMask];
    }

    void RightColumn::writeSelectedMask(const MaskParams &m)
    {
        // The overlay is fed the full current mask each frame, so `m` already carries
        // the panel-owned fields (adjust/feather/inverted) alongside the dragged
        // geometry — write it back wholesale, then re-render. No setMasks() (would
        // fight a live drag); the mask count is unchanged so the panel stays in sync.
        if (auto *p = mSession.curParams(); p && mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
        {
            p->masks[mSelectedMask] = m;
            mSession.submit();
        }
    }

    void RightColumn::scrollActivePanel(double delta)
    {
        switch (mTabs->selectedIndex())
        {
            case kTabBasicDetail: mBasicDetail->scrollBy(delta); break;
            case kTabMask:        mMask->scrollBy(delta); break;
            case kTabColor:       mColorTab->scrollBy(delta); break;
            case kTabGrade:       mGrade->scrollBy(delta); break;
            default: break;  // Xform is fixed-height (no scroll)
        }
    }

    void RightColumn::layout()
    {
        const double w = width.value(), h = height.value();
        mHistogram->x.set(0.0); mHistogram->y.set(0.0);
        mHistogram->width.set(w);

        const double actionBarH = ActionBar::kHeight;
        const double tabsY = HistogramWidget::kHeight;
        const double tabsH = h - tabsY - actionBarH;
        mTabs->x.set(0.0); mTabs->y.set(tabsY);
        mTabs->width.set(w); mTabs->height.set(std::max(0.0, tabsH));
        mTabs->layoutPages();  // sizes/positions/visibility of all pages

        // Each concrete panel lays out its own internals at the size the tab stack gave
        // it; the merged Mixer/Curve stack lays out its two children via callbacks.
        mBasicDetail->layout(); mMask->layout(); mColorTab->layout(); mGrade->layout(); mXform->layout();

        mActionBar->x.set(0.0); mActionBar->y.set(h - actionBarH);
        mActionBar->width.set(w);
        mActionBar->layout();
    }

    void RightColumn::onPaint(IRenderTarget &t) const
    {
        // The card surface behind the whole column (Figma's `bg-card`): the
        // panel body shows it directly and the active tab is filled with the same
        // card colour, so the highlighted tab blends into the editing section.
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(palette::card()));
    }
}
}
