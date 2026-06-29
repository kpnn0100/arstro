#include "MaskPanel.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace { constexpr double kPad = 10, kBtnH = 24, kComboH = 24, kGap = 6, kLabelH = 14, kSliderH = 18; }

    MaskPanel::MaskPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0); height.set(400.0);

        auto mkBtn = [&](const char *label, int type) {
            auto b = std::make_shared<Button>(label, theme.button);
            b->onClick = [this, type] { if (onAdd) onAdd(type); };
            addChild(b);
            return b;
        };
        mAddRadial = mkBtn("Radial", 0);
        mAddLinear = mkBtn("Linear", 1);
        mAddBrush = mkBtn("Brush", 2);

        mSelect = std::make_shared<ComboBox>(theme.combo);
        mSelect->onChange = [this](int i) { if (onSelect) onSelect(i); };
        addChild(mSelect);

        mDelete = std::make_shared<Button>("Delete", theme.button);
        mDelete->onClick = [this] { if (onDelete) onDelete(); };
        addChild(mDelete);

        mInvert = std::make_shared<TextToggle>("invert", accent);
        mInvert->onChange = [this](bool on) { if (mHasSelection && onInvert) onInvert(on); };
        addChild(mInvert);

        mFeather = std::make_shared<Slider>(theme.slider);
        mFeather->setRange(0, 100); mFeather->setValue(50); mFeather->setClickJumps(false);
        mFeather->onChange = [this](double v) { if (mHasSelection && onFeather) onFeather(v / 100.0); };
        addChild(mFeather);

        auto spec = [this](const char *label, double mn, double mx, float LocalAdjust::*field) {
            return ParamPanel::Spec{label, mn, mx, 0, [this, field](double v) {
                                        mEditing.*field = (float)v;
                                        if (mHasSelection && onLocal) onLocal(mEditing);
                                    }};
        };
        std::vector<ParamPanel::Section> sections = {{"LOCAL ADJUST", {
            spec("exposure", -5, 5, &LocalAdjust::exposure),
            spec("contrast", -100, 100, &LocalAdjust::contrast),
            spec("highlights", -100, 100, &LocalAdjust::highlights),
            spec("shadows", -100, 100, &LocalAdjust::shadows),
            spec("whites", -100, 100, &LocalAdjust::whites),
            spec("blacks", -100, 100, &LocalAdjust::blacks),
            spec("temp", -100, 100, &LocalAdjust::temp),
            spec("tint", -100, 100, &LocalAdjust::tint),
            spec("saturation", -100, 100, &LocalAdjust::saturation),
            spec("texture", -100, 100, &LocalAdjust::texture),
            spec("clarity", -100, 100, &LocalAdjust::clarity),
            spec("dehaze", -100, 100, &LocalAdjust::dehaze),
        }}};
        mLocal = std::make_shared<ParamPanel>("", theme, accent, sections);
        addChild(mLocal);
    }

    void MaskPanel::setMasks(const std::vector<arstro::MaskParams> &masks, int selected)
    {
        std::vector<std::string> opts;
        for (size_t i = 0; i < masks.size(); ++i)
        {
            const char *base = masks[i].type == arstro::MaskParams::Radial ? "Radial "
                             : masks[i].type == arstro::MaskParams::Linear ? "Linear " : "Brush ";
            opts.push_back(base + std::to_string(i + 1));
        }
        mSelect->setOptions(opts);
        mHasSelection = selected >= 0 && selected < (int)masks.size();
        if (mHasSelection)
        {
            mSelect->setSelectedIndex(selected);
            const arstro::MaskParams &m = masks[selected];
            mInvert->setOn(m.inverted);
            mFeather->setValue(m.feather * 100.0);
            mEditing = m.adjust;
            const LocalAdjust &a = m.adjust;
            mLocal->setValues({a.exposure, a.contrast, a.highlights, a.shadows, a.whites, a.blacks,
                               a.temp, a.tint, a.saturation, a.texture, a.clarity, a.dehaze});
        }
    }

    void MaskPanel::layout(double w, double h)
    {
        width.set(w); height.set(h);
        const double cw = w - 2 * kPad;
        double y = 8;
        const double bw = (cw - 2 * kGap) / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto &b = i == 0 ? mAddRadial : (i == 1 ? mAddLinear : mAddBrush);
            b->x.set(kPad + i * (bw + kGap)); b->y.set(y);
            b->width.set(bw); b->height.set(kBtnH);
        }
        y += kBtnH + kGap;
        mSelect->x.set(kPad); mSelect->y.set(y); mSelect->width.set(cw); mSelect->height.set(kComboH);
        y += kComboH + kGap;
        const double half = (cw - kGap) / 2;
        mDelete->x.set(kPad); mDelete->y.set(y); mDelete->width.set(half); mDelete->height.set(kBtnH);
        mInvert->x.set(kPad + half + kGap); mInvert->y.set(y + (kBtnH - 18) * 0.5);
        mInvert->width.set(half); mInvert->height.set(18);
        y += kBtnH + kGap;
        mFeatherLabelY = y + kLabelH - 3;
        y += kLabelH;
        mFeather->x.set(kPad); mFeather->y.set(y); mFeather->width.set(cw); mFeather->height.set(kSliderH);
        y += kSliderH + kGap;
        mLocal->x.set(0); mLocal->y.set(y);
        mLocal->width.set(w); mLocal->height.set(h - y);
        mLocal->layout(w, h - y);
    }

    void MaskPanel::onPaint(IRenderTarget &t) const
    {
        t.setFill(palette::muted());
        t.drawText("FEATHER", kPad, mFeatherLabelY, 10.0);
        if (!mHasSelection)
        {
            t.setFill(palette::faint());
            t.drawText("Add a mask, then paint or drag on the photo.", kPad, 8 + kBtnH + kComboH + 64, 11.0);
        }
    }
}
}
