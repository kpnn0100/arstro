#include "ParamPanel.h"
#include "Icons.h"   // icon::pipette — the white-balance picker's tool button (R-WB-1)
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
        // R-WB-1: one optional tool button per section, added AFTER the rows so it sits above
        // them in hit-test order — it overlaps no row, but a header button that lost to a slider
        // would be the kind of dead control D-32's family is made of.
        for (auto &section : mSections)
        {
            std::shared_ptr<IconButton> btn;
            if (section.action == Section::Action::Pipette)
            {
                // The Painter signature drops the stroke width, so the glyph is wrapped rather
                // than passed by name — its default weight matches the other header glyphs.
                btn = std::make_shared<IconButton>(
                    [](artboard::IRenderTarget &t, const artboard::Rect &b, const artboard::Color &c) {
                        icon::pipette(t, b, c);
                    });
                btn->idleColor = palette::mutedForeground();
                btn->activeColor = palette::primary();
                btn->onClick = [this, &section] {
                    if (section.toggles)
                    {
                        // Latching: the button IS the armed state, so the callback reports what
                        // it became rather than the caller having to track it.
                        const bool armed = !section.armed;
                        section.armed = armed;
                        if (section.onAction) section.onAction(armed);
                    }
                    else if (section.onAction)
                    {
                        section.onAction(true);
                    }
                };
                addChild(btn);
            }
            mSectionActions.push_back(btn);
        }
    }

    void ParamPanel::setSectionActionArmed(const std::string &sectionTitle, bool armed)
    {
        for (size_t i = 0; i < mSections.size(); ++i)
            if (mSections[i].title == sectionTitle)
            {
                mSections[i].armed = armed;
                if (i < mSectionActions.size() && mSectionActions[i])
                    mSectionActions[i]->active = armed;
                return;
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
        for (size_t si = 0; si < mSections.size(); ++si)
        {
            const auto &section = mSections[si];
            mSectionHeaderY.push_back(y);
            // R-WB-1: the section's tool button rides on its header row, right-aligned, and is
            // culled with it — a header scrolled off the top must not leave a button behind.
            if (si < mSectionActions.size() && mSectionActions[si])
            {
                auto &btn = mSectionActions[si];
                const double bs = kSectionHeaderHeight - 2.0;
                btn->width.set(bs); btn->height.set(bs);
                btn->x.set(std::max(0.0, w - kPadX - bs));
                btn->y.set(y + 1.0);
                btn->visible = (y + kSectionHeaderHeight >= 0 && y <= height.value());
                btn->active = mSections[si].armed;   // lit while armed (R-WB-1)
            }
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
