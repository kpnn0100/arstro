#include "MaskPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace { constexpr double kHeaderH = 34, kPad = 12, kBtnH = 26, kComboH = 26, kGap = 6, kRowH = 22; }

    MaskPanel::MaskPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0); height.set(400.0);

        // flat "chip" style for the add buttons: filled, no border, less rounded
        ButtonStyle flat = theme.button;
        flat.idle = {Paint::filled(palette::surface()), radius::control()};
        flat.pressed = {Paint::filled(palette::line()), radius::control()};
        flat.label.color = palette::ink();

        auto mkBtn = [&](const char *label, int type) {
            auto b = std::make_shared<Button>(label, flat);
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

        mDelete = std::make_shared<IconButton>(IconButton::Icon::Trash, accent);
        mDelete->setColors(Color{0.80f, 0.52f, 0.55f, 1.0f}, Color{0.16f, 0.10f, 0.11f, 1.0f});  // pastel red
        mDelete->onClick = [this] { if (onDelete) onDelete(); };
        addChild(mDelete);

        mInvert = std::make_shared<TextToggle>("invert", accent);
        mInvert->onChange = [this](bool on) { if (mHasSelection && onInvert) onInvert(on); };
        addChild(mInvert);

        mFeather = std::make_shared<Slider>(theme.slider);
        mFeather->setRange(0, 100); mFeather->setValue(50);
        mFeather->onChange = [this](double v) { if (mHasSelection && onFeather) onFeather(v / 100.0); };
        addChild(mFeather);

        auto spec = [this](const char *label, double mn, double mx, float LocalAdjust::*field) {
            return ParamPanel::Spec{label, mn, mx, 0, [this, field](double v) {
                                        mEditing.*field = (float)v;
                                        if (mHasSelection && onLocal) onLocal(mEditing);
                                    }};
        };
        // sub-sectioned local adjustments (like the Basic tab)
        std::vector<ParamPanel::Section> sections = {
            {"TONE", {
                spec("exposure", -5, 5, &LocalAdjust::exposure),
                spec("contrast", -100, 100, &LocalAdjust::contrast),
                spec("highlights", -100, 100, &LocalAdjust::highlights),
                spec("shadows", -100, 100, &LocalAdjust::shadows),
                spec("whites", -100, 100, &LocalAdjust::whites),
                spec("blacks", -100, 100, &LocalAdjust::blacks),
            }},
            {"COLOR", {
                spec("temp", -100, 100, &LocalAdjust::temp),
                spec("tint", -100, 100, &LocalAdjust::tint),
                spec("saturation", -100, 100, &LocalAdjust::saturation),
            }},
            {"PRESENCE", {
                spec("texture", -100, 100, &LocalAdjust::texture),
                spec("clarity", -100, 100, &LocalAdjust::clarity),
                spec("dehaze", -100, 100, &LocalAdjust::dehaze),
            }},
        };
        mLocal = std::make_shared<ParamPanel>("", theme, accent, sections);
        mLocal->setChrome(false);  // embedded: no nested panel box, just sections + sliders
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
        double y = kHeaderH + 6;

        // add-mask chips
        const double bw = (cw - 2 * kGap) / 3;
        for (int i = 0; i < 3; ++i)
        {
            auto &b = i == 0 ? mAddRadial : (i == 1 ? mAddLinear : mAddBrush);
            b->x.set(kPad + i * (bw + kGap)); b->y.set(y);
            b->width.set(bw); b->height.set(kBtnH);
        }
        y += kBtnH + kGap;

        // select combo (left, flex) + delete icon (right, square)
        const double delW = kComboH;
        mSelect->x.set(kPad); mSelect->y.set(y); mSelect->width.set(cw - delW - kGap); mSelect->height.set(kComboH);
        mDelete->x.set(kPad + cw - delW); mDelete->y.set(y); mDelete->width.set(delW); mDelete->height.set(kComboH);
        y += kComboH + kGap;

        // invert (left) + feather slider (right)
        const double invW = 66.0, labW = 42.0;
        mInvert->x.set(kPad); mInvert->y.set(y + (kRowH - 18) * 0.5); mInvert->width.set(invW); mInvert->height.set(18);
        mFeatherLabelX = kPad + invW + 6; mFeatherLabelY = y + kRowH * 0.5 + 4;
        const double fx = mFeatherLabelX + labW;
        mFeather->x.set(fx); mFeather->y.set(y + (kRowH - 14) * 0.5);
        mFeather->width.set(kPad + cw - fx); mFeather->height.set(14);
        y += kRowH + kGap;

        mLocal->x.set(0); mLocal->y.set(y);
        mLocal->width.set(w); mLocal->height.set(h - y);
        mLocal->layout(w, h - y);
    }

    void MaskPanel::scrollBy(double wheelDelta) { mLocal->scrollBy(wheelDelta); }

    void MaskPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), "MASK");  // gray body joining the tab
        t.setFill(palette::muted());
        t.drawText("feather", mFeatherLabelX, mFeatherLabelY, 10.0);
    }
}
}
