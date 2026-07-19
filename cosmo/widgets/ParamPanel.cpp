#include "ParamPanel.h"
#include "SectionHeader.h"
#include <algorithm>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace { constexpr double kPadX = 9.75, kPadBottom = 13.0; }

    ParamPanel::ParamPanel(std::vector<Section> sections) : mSections(std::move(sections))
    {
        clipToBounds = true;
        for (const auto &section : mSections)
            for (const auto &spec : section.rows)
            {
                auto row = std::make_shared<SliderRow>(spec.label, spec.min, spec.max, 0.0);
                row->onChange = spec.onChange;
                if (spec.hasGradient) row->setTrackGradient(spec.gradLeft, spec.gradRight);
                addChild(row);
                mFlatRows.push_back(row);
            }
    }

    void ParamPanel::setValues(const std::vector<double> &values)
    {
        for (size_t i = 0; i < mFlatRows.size() && i < values.size(); ++i)
            mFlatRows[i]->setValue(values[i]);
    }

    void ParamPanel::setSubValues(const std::vector<double> &offsets)
    {
        // Per-row green stacked reach (the ancestor-group contribution), in the same
        // flattened Spec order as setValues (DR-EDIT-4).
        for (size_t i = 0; i < mFlatRows.size() && i < offsets.size(); ++i)
            mFlatRows[i]->setSubValueOffset(offsets[i]);
    }

    void ParamPanel::scrollBy(double delta)
    {
        const double viewH = height.value();
        const double maxScroll = std::max(0.0, mContentHeight - viewH);
        // Move the target only; advance() eases mScroll toward it and re-lays out.
        mScrollTarget = std::min(maxScroll, std::max(0.0, mScrollTarget - delta));
    }

    void ParamPanel::advance(double nowMs)
    {
        if (mScrollTarget != mScrollLastTarget)
        {
            mScroll.animateTo(mScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mScrollLastTarget = mScrollTarget;
        }
        const bool moving = mScroll.isAnimating();
        mScroll.update(nowMs);
        if (moving) layout();  // reposition rows at the animated scroll offset
        Segment::advance(nowMs);
    }

    void ParamPanel::layout()
    {
        const double w = width.value();
        double y = -mScroll.value();
        size_t rowIdx = 0;
        mSectionHeaderY.clear();
        for (const auto &section : mSections)
        {
            mSectionHeaderY.push_back(y);
            y += kSectionHeaderHeight;
            for (size_t i = 0; i < section.rows.size(); ++i, ++rowIdx)
            {
                auto &row = mFlatRows[rowIdx];
                row->x.set(kPadX);
                row->y.set(y);
                row->width.set(std::max(0.0, w - 2 * kPadX));
                row->layout();
                row->visible = (y + SliderRow::kRowHeight >= 0 && y <= height.value());
                y += SliderRow::kRowHeight;
            }
        }
        mContentHeight = y + mScroll.value() + kPadBottom;
    }

    void ParamPanel::onPaint(IRenderTarget &t) const
    {
        const double w = width.value() - 2 * kPadX;
        for (size_t i = 0; i < mSections.size(); ++i)
        {
            const double y = mSectionHeaderY[i];
            if (y + kSectionHeaderHeight < 0 || y > height.value()) continue;
            drawSectionHeader(t, kPadX, y, w, mSections[i].title);
        }
    }
}
}
