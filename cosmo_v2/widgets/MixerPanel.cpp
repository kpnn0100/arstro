#include "MixerPanel.h"
#include "Icons.h"
#include "../Theme.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kPickerMT = 6.5, kPickerMB = 6.5, kPickerH = 22.75;
        constexpr double kEditorMaxH = 200.0;
    }

    MixerPanel::MixerPanel()
    {
        clipToBounds = true;

        mSubTabs = std::make_shared<SegmentedControl>(std::vector<std::string>{"Hue", "Sat", "Lum"});
        mSubTabs->containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        mSubTabs->idleSegBox = {Paint{}, radius::hairline()};
        mSubTabs->activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        mSubTabs->edgeRadius = radius::control();  // first/last tabs hug the tray corners
        mSubTabs->idleText = {palette::mutedForeground(), 10.0, font::sans()};
        mSubTabs->activeText = {palette::white(), 10.0, font::sans()};
        mSubTabs->padding = 2.0;
        mSubTabs->gap = 2.0;
        mSubTabs->height.set(kPickerH);
        mSubTabs->onChange = [this](int idx) { showChannel(idx); };
        addChild(mSubTabs);

        for (int c = 0; c < 3; ++c)
        {
            mEditors[c] = std::make_shared<HueCurveEditor>();
            const int ch = c;
            mEditors[c]->onChange = [this, ch](const std::vector<CurvePoint> &pts) {
                if (onCurveChange) onCurveChange(ch, pts);
            };
            addChild(mEditors[c]);
        }
        mEditors[0]->setMappedHue(true);  // Hue channel: colour the line by output hue

        mResetBtn = std::make_shared<IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::refreshCw(t, r, c); });
        mResetBtn->idleColor = palette::mutedForeground();
        mResetBtn->activeColor = palette::foreground();
        mResetBtn->width.set(10.0 + 2 * 1.625);
        mResetBtn->height.set(10.0 + 2 * 1.625);
        mResetBtn->onClick = [this] { mEditors[mChannel]->reset(); };
        addChild(mResetBtn);

        showChannel(0);
    }

    void MixerPanel::showChannel(int channel)
    {
        mChannel = channel;
        for (int c = 0; c < 3; ++c) mEditors[c]->visible = (c == channel);
    }

    void MixerPanel::setMixer(const std::array<std::vector<CurvePoint>, 3> &mixer)
    {
        for (int c = 0; c < 3; ++c) mEditors[c]->setPoints(mixer[c]);
    }

    void MixerPanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        double y = kPickerMT;

        const double pickerW = std::max(0.0, innerW - mResetBtn->width.value() - 6.5);
        mSubTabs->x.set(kPadX); mSubTabs->y.set(y); mSubTabs->width.set(pickerW); mSubTabs->layout();
        mResetBtn->x.set(kPadX + pickerW + 6.5);
        mResetBtn->y.set(y + (kPickerH - mResetBtn->height.value()) * 0.5);
        y += kPickerH + kPickerMB;

        const double editorH = std::min(kEditorMaxH, std::max(0.0, height.value() - y - 13.0));
        for (auto &ed : mEditors)
        {
            ed->x.set(kPadX); ed->y.set(y);
            ed->width.set(innerW); ed->height.set(editorH);
        }
    }

    void MixerPanel::onPaint(IRenderTarget &) const {}
}
}
