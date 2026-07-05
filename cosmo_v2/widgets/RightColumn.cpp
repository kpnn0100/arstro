#include "RightColumn.h"
#include "../Theme.h"
#include "UnitConversions.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    RightColumn::RightColumn(cosmo::EditSession &session) : mSession(session)
    {
        clipToBounds = true;
        width.set(kWidth);

        mHistogram = std::make_shared<HistogramWidget>();
        addChild(mHistogram);

        mTabs = std::make_shared<TabView>(sharedTheme().tab);
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
                {"Temperature", -100, 100, set([](EditParams &p, double v) { p.temp = (float)toKelvin(v); })},
                {"Tint", -100, 100, set([](EditParams &p, double v) { p.tint = (float)toTint(v); })},
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
        mBasic = std::make_shared<ParamPanel>(std::move(basicSections));
        mTabs->addPage("Basic", mBasic);

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
        mDetail = std::make_shared<ParamPanel>(std::move(detailSections));
        mTabs->addPage("Detail", mDetail);

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
        mTabs->addPage("Mask", mMask);

        mMixer = std::make_shared<MixerPanel>();
        mMixer->onBandChange = [this](int channel, float hueDeg, float y) {
            if (auto *p = mSession.curParams())
            {
                auto &curve = p->mixer[channel];
                auto it = std::find_if(curve.begin(), curve.end(),
                                        [hueDeg](const auto &pt) { return std::abs(pt.first - hueDeg) < 0.5f; });
                if (it != curve.end()) it->second = y;
                else { curve.push_back({hueDeg, y}); std::sort(curve.begin(), curve.end()); }
                mSession.submit();
            }
        };
        mTabs->addPage("Mixer", mMixer);

        mCurve = std::make_shared<CurvePanel>();
        mCurve->onCurveChange = [this](std::vector<std::pair<float, float>> pts) {
            if (auto *p = mSession.curParams()) { p->curve = std::move(pts); mSession.submit(); }
        };
        mTabs->addPage("Curve", mCurve);

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
        mBasic->setValues({
            fromEv(p->exposure), p->contrast, p->highlights, p->shadows, p->whites, p->blacks,
            fromKelvin(p->temp), fromTint(p->tint), p->vibrance, p->saturation,
            p->texture, p->clarity, p->dehaze, p->grainAmount, p->grainSize,
        });
        mDetail->setValues({
            p->sharpenAmount, fromRadiusPx(p->sharpenRadius), p->sharpenMasking,
            p->nrLuminance, p->nrColor,
            p->lensDistortion, p->lensCA, p->lensVignette,
        });

        mSelectedMask = p->masks.empty() ? -1 : std::min(mSelectedMask < 0 ? 0 : mSelectedMask, (int)p->masks.size() - 1);
        mMask->setMasks(p->masks, mSelectedMask);
        mMixer->setMixer(p->mixer);
        mCurve->setCurve(p->curve);
        GradePanel::State gs;
        gs.grade = p->grade; gs.balance = p->balance; gs.remapEnable = p->remapEnable;
        gs.remapSrc = p->remapSrc; gs.remapRange = p->remapRange; gs.remapDst = p->remapDst; gs.remapStrength = p->remapStrength;
        mGrade->setState(gs);
        mXform->setState({p->rotation, p->quarterTurns, p->cropX, p->cropY, p->cropW, p->cropH});
    }

    void RightColumn::scrollActivePanel(double delta)
    {
        switch (mTabs->selectedIndex())
        {
            case 0: mBasic->scrollBy(delta); break;
            case 1: mDetail->scrollBy(delta); break;
            case 2: mMask->scrollBy(delta); break;
            case 3: mMixer->scrollBy(delta); break;
            case 5: mGrade->scrollBy(delta); break;
            default: break;
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

        const double pageH = std::max(0.0, tabsH - mTabs->tabHeight);
        mBasic->width.set(w); mBasic->height.set(pageH); mBasic->layout();
        mDetail->width.set(w); mDetail->height.set(pageH); mDetail->layout();
        mMask->width.set(w); mMask->height.set(pageH); mMask->layout();
        mMixer->width.set(w); mMixer->height.set(pageH); mMixer->layout();
        mCurve->width.set(w); mCurve->height.set(pageH); mCurve->layout();
        mGrade->width.set(w); mGrade->height.set(pageH); mGrade->layout();
        mXform->width.set(w); mXform->height.set(pageH); mXform->layout();

        mActionBar->x.set(0.0); mActionBar->y.set(h - actionBarH);
        mActionBar->width.set(w);
        mActionBar->layout();
    }
}
}
