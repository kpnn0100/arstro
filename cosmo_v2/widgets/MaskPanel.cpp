#include "MaskPanel.h"
#include "SectionHeader.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "UnitConversions.h"
#include "../Theme.h"
#include <algorithm>
#include <string>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kChipGap = 4.875, kChipH = 22.75, kChipMarginBottom = 9.75;
        constexpr double kSelectRowH = 22.75, kSelectRowMB = 8.125;
        const char *kTypeNames[3] = {"Radial", "Linear", "Brush"};

        std::string maskLabel(const MaskParams &m, int index)
        {
            std::string s = kTypeNames[m.type] + std::string(" ") + std::to_string(index + 1);
            if (m.inverted) s += " (inv)";
            return s;
        }

        ComboStyle maskComboStyle()
        {
            ComboStyle cs;
            cs.field = {Paint::filledStroked(palette::secondary(), palette::border(), 1.0), radius::control()};
            cs.popup = {Paint::filledStroked(palette::popover(), palette::border(), 1.0), radius::control()};
            cs.rowSelected = {Paint::filled(palette::primary()), 0.0};
            cs.text = {palette::foreground(), 10.0, font::sans()};
            cs.caretColor = palette::mutedForeground();
            return cs;
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

        mSelect = std::make_shared<ComboBox>(maskComboStyle());
        mSelect->rowHeight = 22.0;
        mSelect->height.set(kSelectRowH);
        mSelect->onChange = [this](int i) { if (onSelectMask) onSelectMask(i); };
        addChild(mSelect);

        mInvBtn = std::make_shared<PillButton>("Inv");
        mInvBtn->idleBox = {Paint::filledStroked(Color{0, 0, 0, 0}, palette::border(), 1.0), radius::control()};
        mInvBtn->idleText = {palette::mutedForeground(), 9.0, font::sans()};
        mInvBtn->width.set(estimateTextWidth("Inv", 9.0) + 2 * 6.5);
        mInvBtn->height.set(kSelectRowH);
        mInvBtn->onClick = [this] { if (onToggleInvert) onToggleInvert(); };
        addChild(mInvBtn);

        mTrashBtn = std::make_shared<IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::trash2(t, r, c); });
        mTrashBtn->idleColor = palette::mutedForeground();
        mTrashBtn->activeColor = palette::destructive();
        mTrashBtn->width.set(kSelectRowH);
        mTrashBtn->height.set(kSelectRowH);
        mTrashBtn->onClick = [this] { if (onDeleteMask) onDeleteMask(); };
        addChild(mTrashBtn);

        auto adjust = [this] { if (onAdjustChange) onAdjustChange(mEditing); };
        mFeather = std::make_shared<SliderRow>("Feather", 0, 100, 0.0);
        mFeather->onChange = [this](double v) { if (onFeatherChange) onFeatherChange(v / 100.0); };
        addChild(mFeather);
        mExposure = std::make_shared<SliderRow>("Exposure", -400, 400, 0.0);
        mExposure->onChange = [this, adjust](double v) { mEditing.exposure = (float)toEv(v); adjust(); };
        addChild(mExposure);
        mHighlights = std::make_shared<SliderRow>("Highlights", -100, 100, 0.0);
        mHighlights->onChange = [this, adjust](double v) { mEditing.highlights = (float)v; adjust(); };
        addChild(mHighlights);
        mShadows = std::make_shared<SliderRow>("Shadows", -100, 100, 0.0);
        mShadows->onChange = [this, adjust](double v) { mEditing.shadows = (float)v; adjust(); };
        addChild(mShadows);
        mTemperature = std::make_shared<SliderRow>("Temperature", -100, 100, 0.0);
        mTemperature->onChange = [this, adjust](double v) { mEditing.temp = (float)v; adjust(); };
        addChild(mTemperature);
        mSaturation = std::make_shared<SliderRow>("Saturation", -100, 100, 0.0);
        mSaturation->onChange = [this, adjust](double v) { mEditing.saturation = (float)v; adjust(); };
        addChild(mSaturation);
        mDehaze = std::make_shared<SliderRow>("Dehaze", 0, 100, 0.0);
        mDehaze->onChange = [this, adjust](double v) { mEditing.dehaze = (float)v; adjust(); };
        addChild(mDehaze);

        // No mask selected initially -> hide the per-mask controls.
        mSelect->visible = mInvBtn->visible = mTrashBtn->visible = mFeather->visible = false;
        mExposure->visible = mHighlights->visible = mShadows->visible = false;
        mTemperature->visible = mSaturation->visible = mDehaze->visible = false;
    }

    void MaskPanel::setMasks(const std::vector<MaskParams> &masks, int selected)
    {
        mMasks = masks;
        mSelected = (selected >= 0 && selected < (int)masks.size()) ? selected : -1;
        const bool has = mSelected >= 0;
        mSelect->visible = mInvBtn->visible = mTrashBtn->visible = mFeather->visible = has;
        mExposure->visible = mHighlights->visible = mShadows->visible = has;
        mTemperature->visible = mSaturation->visible = mDehaze->visible = has;
        if (!has) { mSelect->setOptions({}); return; }

        std::vector<std::string> opts;
        for (int i = 0; i < (int)mMasks.size(); ++i) opts.push_back(maskLabel(mMasks[i], i));
        mSelect->setOptions(opts);
        mSelect->setSelectedIndex(mSelected);

        const MaskParams &m = mMasks[mSelected];
        mEditing = m.adjust;
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
        const double maxScroll = std::max(0.0, mContentHeight - height.value());
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
            // Selector row: mask ComboBox + Inv + trash, centred on one line.
            mSelectRowY = y;
            mTrashBtn->x.set(w - kPadX - mTrashBtn->width.value());
            mTrashBtn->y.set(y);
            mInvBtn->x.set(mTrashBtn->x.value() - 4.875 - mInvBtn->width.value());
            mInvBtn->y.set(y);
            const double comboW = std::max(0.0, mInvBtn->x.value() - 4.875 - kPadX);
            mSelect->x.set(kPadX); mSelect->y.set(y); mSelect->width.set(comboW);
            y += kSelectRowH + kSelectRowMB;

            mFeather->x.set(kPadX); mFeather->y.set(y); mFeather->width.set(innerW); mFeather->layout();
            y += SliderRow::kRowHeight;

            mHeaderY[1] = y; y += kSectionHeaderHeight;  // "Tone"
            for (auto *row : {mExposure.get(), mHighlights.get(), mShadows.get()})
            {
                row->x.set(kPadX); row->y.set(y); row->width.set(innerW); row->layout();
                y += SliderRow::kRowHeight;
            }
            mHeaderY[2] = y; y += kSectionHeaderHeight;  // "Colour"
            for (auto *row : {mTemperature.get(), mSaturation.get()})
            {
                row->x.set(kPadX); row->y.set(y); row->width.set(innerW); row->layout();
                y += SliderRow::kRowHeight;
            }
            mHeaderY[3] = y; y += kSectionHeaderHeight;  // "Presence"
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
        if (visible(mHeaderY[1])) drawSectionHeader(t, kPadX, mHeaderY[1], innerW, "Tone");
        if (visible(mHeaderY[2])) drawSectionHeader(t, kPadX, mHeaderY[2], innerW, "Colour");
        if (visible(mHeaderY[3])) drawSectionHeader(t, kPadX, mHeaderY[3], innerW, "Presence");
    }
}
}
