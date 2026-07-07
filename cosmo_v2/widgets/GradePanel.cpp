#include "GradePanel.h"
#include "SectionHeader.h"
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
        constexpr double kPickerMT = 6.5, kPickerMB = 3.25, kPickerH = 22.75;
        constexpr double kEnableRowH = 10.0 * 1.3 + 6.5;  // approx, matches "mb-2" spacing
    }

    GradePanel::GradePanel()
    {
        clipToBounds = true;
        mRegionPicker = std::make_shared<SegmentedControl>(std::vector<std::string>{"Shadows", "Midtones", "Highlights"});
        mRegionPicker->containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        mRegionPicker->idleSegBox = {Paint{}, radius::hairline()};
        mRegionPicker->activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        mRegionPicker->edgeRadius = radius::control();
        mRegionPicker->idleText = {palette::mutedForeground(), 10.0, font::sans()};
        mRegionPicker->activeText = {palette::white(), 10.0, font::sans()};
        mRegionPicker->padding = 2.0; mRegionPicker->gap = 2.0;
        mRegionPicker->height.set(kPickerH);
        mRegionPicker->onChange = [this](int idx) { mRegion = idx; pushRegionValues(); };
        addChild(mRegionPicker);

        auto onWheelChange = [this](int) {
            mState.grade[mRegion] = {(float)mHue->value(), (float)mSat->value(), (float)mLum->value()};
            if (onRegionChange) onRegionChange(mRegion, mHue->value(), mSat->value(), mLum->value());
        };
        mHue = std::make_shared<SliderRow>("Hue", 0, 360, 0.0);
        mHue->onChange = [onWheelChange](double) { onWheelChange(0); };
        addChild(mHue);
        mSat = std::make_shared<SliderRow>("Saturation", 0, 100, 0.0);
        mSat->onChange = [onWheelChange](double) { onWheelChange(0); };
        addChild(mSat);
        mLum = std::make_shared<SliderRow>("Luminance", -100, 100, 0.0);
        mLum->onChange = [onWheelChange](double) { onWheelChange(0); };
        addChild(mLum);

        mBalance = std::make_shared<SliderRow>("Balance", -100, 100, 0.0);
        mBalance->onChange = [this](double v) { if (onBalanceChange) onBalanceChange(v); };
        addChild(mBalance);

        mRemapToggle = std::make_shared<ToggleSwitch>(sharedTheme().toggle);
        mRemapToggle->width.set(22.75); mRemapToggle->height.set(14.0);
        mRemapToggle->onChange = [this](bool on) { if (onRemapEnableChange) onRemapEnableChange(on); };
        addChild(mRemapToggle);

        auto onRemap = [this](double) {
            if (onRemapChange) onRemapChange(mSource->value(), mRange->value(), mTarget->value(), mStrength->value());
        };
        mSource = std::make_shared<SliderRow>("Source", 0, 360, 0.0);
        mSource->onChange = onRemap; addChild(mSource);
        mRange = std::make_shared<SliderRow>("Range", 0, 180, 0.0);
        mRange->onChange = onRemap; addChild(mRange);
        mTarget = std::make_shared<SliderRow>("Target", 0, 360, 0.0);
        mTarget->onChange = onRemap; addChild(mTarget);
        mStrength = std::make_shared<SliderRow>("Strength", 0, 100, 0.0);
        mStrength->onChange = onRemap; addChild(mStrength);
    }

    void GradePanel::pushRegionValues()
    {
        const GradeWheel &g = mState.grade[mRegion];
        mHue->setValue(g.hue); mSat->setValue(g.sat); mLum->setValue(g.lum);
    }

    void GradePanel::setState(const State &s)
    {
        mState = s;
        pushRegionValues();
        mBalance->setValue(mState.balance);
        mRemapToggle->setOn(mState.remapEnable);
        mSource->setValue(mState.remapSrc);
        mRange->setValue(mState.remapRange);
        mTarget->setValue(mState.remapDst);
        mStrength->setValue(mState.remapStrength * 100.0);
    }

    void GradePanel::scrollBy(double delta)
    {
        const double maxScroll = std::max(0.0, mContentHeight - height.value());
        mScrollTarget = std::min(maxScroll, std::max(0.0, mScrollTarget - delta));
    }

    void GradePanel::advance(double nowMs)
    {
        if (mScrollTarget != mScrollLastTarget)
        {
            mScroll.animateTo(mScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mScrollLastTarget = mScrollTarget;
        }
        const bool moving = mScroll.isAnimating();
        mScroll.update(nowMs);
        if (moving) layout();
        Segment::advance(nowMs);
    }

    void GradePanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        double y = -mScroll.value() + kPickerMT;
        mRegionPicker->x.set(kPadX); mRegionPicker->y.set(y); mRegionPicker->width.set(innerW); mRegionPicker->layout();
        y += kPickerH + kPickerMB;

        mRegionHeaderY = y; y += kSectionHeaderHeight;
        for (auto *row : {mHue.get(), mSat.get(), mLum.get()})
        {
            row->x.set(kPadX); row->y.set(y); row->width.set(innerW); row->layout();
            y += SliderRow::kRowHeight;
        }

        mBalanceHeaderY = y; y += kSectionHeaderHeight;
        mBalance->x.set(kPadX); mBalance->y.set(y); mBalance->width.set(innerW); mBalance->layout();
        y += SliderRow::kRowHeight;

        mRemapHeaderY = y; y += kSectionHeaderHeight;
        mEnableRowY = y;
        mRemapToggle->x.set(w - kPadX - mRemapToggle->width.value());
        mRemapToggle->y.set(y + (kEnableRowH - mRemapToggle->height.value()) * 0.5);
        y += kEnableRowH;

        for (auto *row : {mSource.get(), mRange.get(), mTarget.get(), mStrength.get()})
        {
            row->x.set(kPadX); row->y.set(y); row->width.set(innerW); row->layout();
            y += SliderRow::kRowHeight;
        }
        mContentHeight = y + mScroll.value() + 13.0;
    }

    void GradePanel::onPaint(IRenderTarget &t) const
    {
        const double innerW = std::max(0.0, width.value() - 2 * kPadX);
        const double viewH = height.value();
        auto visible = [&](double y) { return y + kSectionHeaderHeight >= 0 && y <= viewH; };
        const char *regionNames[3] = {"Shadows", "Midtones", "Highlights"};
        if (visible(mRegionHeaderY)) drawSectionHeader(t, kPadX, mRegionHeaderY, innerW, regionNames[mRegion]);
        if (visible(mBalanceHeaderY)) drawSectionHeader(t, kPadX, mBalanceHeaderY, innerW, "Balance");
        if (visible(mRemapHeaderY)) drawSectionHeader(t, kPadX, mRemapHeaderY, innerW, "Hue Remap");

        if (mEnableRowY + kEnableRowH >= 0 && mEnableRowY <= viewH)
        {
            t.setFill(palette::mutedForeground());
            t.drawText("Enable", kPadX, mEnableRowY + kEnableRowH * 0.5 + 10.0 * 0.35, 10.0, font::sans());
        }
    }
}
}
