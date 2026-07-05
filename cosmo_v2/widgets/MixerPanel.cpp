#include "MixerPanel.h"
#include "SectionHeader.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kPickerMT = 6.5, kPickerMB = 3.25, kPickerH = 22.75;  // mt-2, mb-1, ~py-1 row height
        const float kBandHue[MixerPanel::kBands] = {0, 45, 90, 135, 180, 225, 270, 315};
        const char *kBandNames[MixerPanel::kBands] = {"Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"};

        float sampleBand(const std::vector<std::pair<float, float>> &curve, float hueDeg)
        {
            for (const auto &pt : curve)
                if (std::abs(pt.first - hueDeg) < 0.5f) return pt.second;
            return 0.0f;
        }
    }

    MixerPanel::MixerPanel()
    {
        clipToBounds = true;
        mSubTabs = std::make_shared<SegmentedControl>(std::vector<std::string>{"Hue", "Sat", "Lum"});
        mSubTabs->containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        mSubTabs->idleSegBox = {Paint{}, radius::hairline()};
        mSubTabs->activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        mSubTabs->idleText = {palette::mutedForeground(), 10.0, font::sans()};
        mSubTabs->activeText = {palette::white(), 10.0, font::sans()};
        mSubTabs->padding = 2.0;
        mSubTabs->gap = 2.0;
        mSubTabs->height.set(kPickerH);
        mSubTabs->onChange = [this](int idx) { mSubChannel = idx; refreshBandValues(); };
        addChild(mSubTabs);

        for (int i = 0; i < kBands; ++i)
        {
            auto row = std::make_shared<SliderRow>(kBandNames[i], -100, 100, 0.0);
            row->onChange = [this, i](double v) {
                if (onBandChange) onBandChange(mSubChannel, kBandHue[i], (float)(v / 100.0));
            };
            addChild(row);
            mBandRows.push_back(row);
        }
    }

    void MixerPanel::setMixer(const std::array<std::vector<std::pair<float, float>>, 3> &mixer)
    {
        mMixer = mixer;
        refreshBandValues();
    }

    void MixerPanel::refreshBandValues()
    {
        for (int i = 0; i < kBands; ++i)
            mBandRows[i]->setValue(sampleBand(mMixer[mSubChannel], kBandHue[i]) * 100.0);
    }

    void MixerPanel::scrollBy(double delta)
    {
        const double maxScroll = std::max(0.0, mContentHeight - height.value());
        mScroll = std::min(maxScroll, std::max(0.0, mScroll - delta));
        layout();
    }

    void MixerPanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        double y = -mScroll + kPickerMT;
        mSubTabs->x.set(kPadX); mSubTabs->y.set(y); mSubTabs->width.set(innerW); mSubTabs->layout();
        y += kPickerH + kPickerMB;

        mHeaderY = y;
        y += kSectionHeaderHeight;
        for (auto &row : mBandRows)
        {
            row->x.set(kPadX); row->y.set(y); row->width.set(innerW); row->layout();
            y += SliderRow::kRowHeight;
        }
        mContentHeight = y + mScroll + 13.0;
    }

    void MixerPanel::onPaint(IRenderTarget &t) const
    {
        const double innerW = std::max(0.0, width.value() - 2 * kPadX);
        if (mHeaderY + kSectionHeaderHeight >= 0 && mHeaderY <= height.value())
        {
            const char *titles[3] = {"HUE", "SAT", "LUM"};
            drawSectionHeader(t, kPadX, mHeaderY, innerW, titles[mSubChannel]);
        }
    }
}
}
