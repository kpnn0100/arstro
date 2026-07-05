#include "MaskPanel.h"
#include "SectionHeader.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "UnitConversions.h"
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
        constexpr double kChipGap = 4.875, kChipH = 22.75, kChipMarginBottom = 9.75;
        constexpr double kInfoRowH = 9.75 + 8.125;  // swatch height + mb-2.5
        const char *kTypeNames[3] = {"Radial", "Linear", "Brush"};

        std::string maskLabel(const MaskParams &m, int index)
        {
            std::string s = "Mask " + std::to_string(index + 1) + " -- " + kTypeNames[m.type];
            if (m.inverted) s += ", Inverted";
            return s;
        }
    }

    MaskPanel::MaskPanel()
    {
        clipToBounds = true;
        for (int i = 0; i < 3; ++i)
        {
            auto chip = std::make_shared<PillButton>(kTypeNames[i]);
            chip->idleBox = {Paint::filledStroked(Color{0, 0, 0, 0}, palette::border(), 1.0), radius::control()};
            chip->idleText = {palette::mutedForeground(), 10.0, font::sans()};
            chip->height.set(kChipH);
            chip->onClick = [this, i] { if (onAddMask) onAddMask(i); };
            addChild(chip);
            mAddChips.push_back(chip);
        }

        mInvBtn = std::make_shared<PillButton>("Inv");
        mInvBtn->idleBox = {Paint{}, radius::hairline()};
        mInvBtn->idleText = {palette::mutedForeground(), 9.0, font::sans()};
        mInvBtn->width.set(estimateTextWidth("Inv", 9.0) + 2 * 3.25);
        mInvBtn->height.set(9.0 * 1.3 + 2 * 1.625);
        mInvBtn->onClick = [this] { if (onToggleInvert) onToggleInvert(); };
        addChild(mInvBtn);

        mTrashBtn = std::make_shared<IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::trash2(t, r, c); });
        mTrashBtn->idleColor = palette::mutedForeground();
        mTrashBtn->activeColor = palette::destructive();
        mTrashBtn->width.set(10.0 + 2 * 1.625);
        mTrashBtn->height.set(10.0 + 2 * 1.625);
        mTrashBtn->onClick = [this] { if (onDeleteMask) onDeleteMask(); };
        addChild(mTrashBtn);

        mFeather = std::make_shared<SliderRow>("Feather", 0, 100, 0.0);
        addChild(mFeather);
        mExposure = std::make_shared<SliderRow>("Exposure", -400, 400, 0.0);
        addChild(mExposure);
        mHighlights = std::make_shared<SliderRow>("Highlights", -100, 100, 0.0);
        addChild(mHighlights);
        mShadows = std::make_shared<SliderRow>("Shadows", -100, 100, 0.0);
        addChild(mShadows);
        mTemperature = std::make_shared<SliderRow>("Temperature", -100, 100, 0.0);
        addChild(mTemperature);
        mSaturation = std::make_shared<SliderRow>("Saturation", -100, 100, 0.0);
        addChild(mSaturation);
        mDehaze = std::make_shared<SliderRow>("Dehaze", 0, 100, 0.0);
        addChild(mDehaze);
    }

    void MaskPanel::setMasks(const std::vector<MaskParams> &masks, int selected)
    {
        mMasks = masks;
        mSelected = (selected >= 0 && selected < (int)masks.size()) ? selected : -1;
        const bool has = mSelected >= 0;
        mInvBtn->visible = mTrashBtn->visible = mFeather->visible = has;
        mExposure->visible = mHighlights->visible = mShadows->visible = has;
        mTemperature->visible = mSaturation->visible = mDehaze->visible = has;
        if (!has) return;

        const MaskParams &m = mMasks[mSelected];
        mFeather->setValue(m.feather * 100.0);
        mExposure->setValue(fromEv(m.adjust.exposure));
        mHighlights->setValue(m.adjust.highlights);
        mShadows->setValue(m.adjust.shadows);
        mTemperature->setValue(m.adjust.temp);
        mSaturation->setValue(m.adjust.saturation);
        mDehaze->setValue(m.adjust.dehaze);
    }

    void MaskPanel::scrollBy(double delta)
    {
        const double viewH = height.value();
        const double maxScroll = std::max(0.0, mContentHeight - viewH);
        mScroll = std::min(maxScroll, std::max(0.0, mScroll - delta));
        layout();
    }

    void MaskPanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        double y = -mScroll;

        mHeaderY[0] = y;
        y += kSectionHeaderHeight;  // "Add Mask" header
        const double chipW = (innerW - 2 * kChipGap) / 3.0;
        for (int i = 0; i < 3; ++i)
        {
            mAddChips[i]->x.set(kPadX + i * (chipW + kChipGap));
            mAddChips[i]->y.set(y);
            mAddChips[i]->width.set(chipW);
        }
        y += kChipH + kChipMarginBottom;

        if (mSelected >= 0)
        {
            mHeaderY[1] = y;
            y += kSectionHeaderHeight;  // "Mask N -- Type" header

            mInfoRowY = y;
            mInvBtn->x.set(w - kPadX - mTrashBtn->width.value() - 1.625 - mInvBtn->width.value());
            mInvBtn->y.set(mInfoRowY);
            mTrashBtn->x.set(w - kPadX - mTrashBtn->width.value());
            mTrashBtn->y.set(mInfoRowY);
            y += kInfoRowH;

            mFeather->x.set(kPadX); mFeather->y.set(y); mFeather->width.set(innerW); mFeather->layout();
            y += SliderRow::kRowHeight;

            mHeaderY[2] = y;
            y += kSectionHeaderHeight;  // "Tone"
            for (auto *row : {mExposure.get(), mHighlights.get(), mShadows.get()})
            {
                row->x.set(kPadX); row->y.set(y); row->width.set(innerW); row->layout();
                y += SliderRow::kRowHeight;
            }
            mHeaderY[3] = y;
            y += kSectionHeaderHeight;  // "Colour"
            for (auto *row : {mTemperature.get(), mSaturation.get()})
            {
                row->x.set(kPadX); row->y.set(y); row->width.set(innerW); row->layout();
                y += SliderRow::kRowHeight;
            }
            mHeaderY[4] = y;
            y += kSectionHeaderHeight;  // "Presence"
            mDehaze->x.set(kPadX); mDehaze->y.set(y); mDehaze->width.set(innerW); mDehaze->layout();
            y += SliderRow::kRowHeight;
        }
        mContentHeight = y + mScroll + 13.0;
    }

    void MaskPanel::onPaint(IRenderTarget &t) const
    {
        const double innerW = std::max(0.0, width.value() - 2 * kPadX);
        const double viewH = height.value();
        auto visible = [&](double y) { return y + kSectionHeaderHeight >= 0 && y <= viewH; };

        if (visible(mHeaderY[0])) drawSectionHeader(t, kPadX, mHeaderY[0], innerW, "Add Mask");
        if (mSelected < 0) return;

        if (visible(mHeaderY[1])) drawSectionHeader(t, kPadX, mHeaderY[1], innerW, maskLabel(mMasks[mSelected], mSelected));
        if (visible(mHeaderY[2])) drawSectionHeader(t, kPadX, mHeaderY[2], innerW, "Tone");
        if (visible(mHeaderY[3])) drawSectionHeader(t, kPadX, mHeaderY[3], innerW, "Colour");
        if (visible(mHeaderY[4])) drawSectionHeader(t, kPadX, mHeaderY[4], innerW, "Presence");

        if (mInfoRowY >= -kInfoRowH && mInfoRowY <= viewH)
        {
            const MaskParams &m = mMasks[mSelected];
            const Color primary = palette::primary();
            drawRoundedRect(t, Rect{kPadX, mInfoRowY + 1.0, 9.75, 9.75}, 1.0,
                            Paint::filledStroked(Color{primary.r, primary.g, primary.b, 0.5},
                                                  Color{primary.r, primary.g, primary.b, 0.6}, 1.0));
            std::string label = kTypeNames[m.type];
            if (m.inverted) label += ", Inverted";
            const Color fg = palette::foreground();
            t.setFill(Color{fg.r, fg.g, fg.b, 0.8});
            t.drawText(label, kPadX + 9.75 + 6.5, mInfoRowY + 9.75 * 0.5 + 1.0 + 10.0 * 0.35, 10.0, font::sans());
        }
    }
}
}
