#include "RightColumn.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kTabFontPx = 10.0;
        const std::vector<std::string> kTabTitles = {"Basic", "Detail", "Mask", "Mixer", "Curve", "Grade", "Xform"};

        // Some Figma slider ranges are the engine's own units 1:1 (contrast,
        // highlights, ... -100..100 both sides); a few use a UI-friendlier scale
        // the mock invented (exposure -400..400 vs the engine's -5..+5 EV;
        // temperature -100..100 relative vs the engine's 2000..50000 Kelvin;
        // tint -100..100 vs the engine's -150..150; sharpen radius 5..30 vs the
        // engine's 0.5..3px). Each conversion below maps the Figma slider's own
        // endpoints onto the engine's real endpoints exactly, and the Kelvin
        // conversion reuses the same relative-shift formula already used for
        // group LocalAdjust.temp elsewhere in the codebase (temp/100*3500).
        double toEv(double v) { return v / 80.0; }
        double toKelvin(double v) { return 6500.0 + v / 100.0 * 3500.0; }
        double toTint(double v) { return v * 1.5; }
        double toRadiusPx(double v) { return v / 10.0; }
    }

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

        // Mask/Mixer/Curve/Grade/Xform land in the next pass; empty pages keep
        // the 7-tab strip pixel-correct in the meantime.
        for (size_t i = 2; i < kTabTitles.size(); ++i)
            mTabs->addPage(kTabTitles[i], std::make_shared<Segment>());

        addChild(mTabs);

        mActionBar = std::make_shared<ActionBar>();
        addChild(mActionBar);
    }

    void RightColumn::syncToSlot()
    {
        const EditParams *p = mSession.curParams();
        if (!p) return;
        mBasic->setValues({
            p->exposure * 80.0, p->contrast, p->highlights, p->shadows, p->whites, p->blacks,
            (p->temp - 6500.0) / 3500.0 * 100.0, p->tint / 1.5, p->vibrance, p->saturation,
            p->texture, p->clarity, p->dehaze, p->grainAmount, p->grainSize,
        });
        mDetail->setValues({
            p->sharpenAmount, p->sharpenRadius * 10.0, p->sharpenMasking,
            p->nrLuminance, p->nrColor,
            p->lensDistortion, p->lensCA, p->lensVignette,
        });
    }

    void RightColumn::scrollActivePanel(double delta)
    {
        switch (mTabs->selectedIndex())
        {
            case 0: mBasic->scrollBy(delta); break;
            case 1: mDetail->scrollBy(delta); break;
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

        mActionBar->x.set(0.0); mActionBar->y.set(h - actionBarH);
        mActionBar->width.set(w);
        mActionBar->layout();
    }
}
}
