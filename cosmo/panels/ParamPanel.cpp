#include "ParamPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;   // panel title bar
        constexpr double kPad = 12.0;
        constexpr double kLabelW = 84.0;
        constexpr double kSliderH = 14.0;
        constexpr double kSectionH = 26.0;  // section sub-header row (room for a divider)
        constexpr double kSliderRowH = 28.0;  // fixed row height: rows snap, they never stretch/collide
    }

    ParamPanel::ParamPanel(const std::string &title, const Theme &theme,
                           const Color &accent, const std::vector<Section> &sections)
        : mTitle(title), mAccent(accent)
    {
        // The sliders live under a clipped body (below the title bar), so scrolling
        // a tall section list can never draw a row over the chrome above it.
        mBody = std::make_shared<Segment>();
        mBody->clipToBounds = true;
        addChild(mBody);

        for (const auto &sec : sections)
        {
            mItems.push_back({true, sec.title, -1, 0.0});
            for (const auto &s : sec.specs)
            {
                auto sl = std::make_shared<Slider>(theme.slider);
                sl->setRange(s.min, s.max);
                sl->setValue(s.def);
                sl->setDefault(s.def);  // click jumps to the cursor (spring-smoothed); double-click resets
                if (s.hasGradient) sl->setTrackGradient(s.gradLeft, s.gradRight);
                sl->onChange = s.onChange;
                mItems.push_back({false, s.label, (int)mSliders.size(), 0.0});
                mSliders.push_back(sl);
                mBody->addChild(sl);
            }
        }
        width.set(300.0);
        height.set(360.0);
    }

    void ParamPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        int nHeaders = 0, nSliders = 0;
        for (const auto &it : mItems) (it.header ? nHeaders : nSliders) += 1;
        mTop = mDrawChrome ? kHeaderH : 6.0;  // no title bar when embedded
        mNaturalH = mTop + nHeaders * kSectionH + nSliders * kSliderRowH + kPad;
        clampScroll();
        reflow();
    }

    void ParamPanel::reflow()
    {
        const double w = width.value(), h = height.value();
        mBody->x.set(0.0); mBody->y.set(mTop);
        mBody->width.set(w); mBody->height.set(h - mTop > 0 ? h - mTop : 0);

        // Rows snap to a fixed height (never stretched to fill h) so they can never
        // collide. Positions are relative to mBody's origin (i.e. already below the
        // title bar); scrollY shifts them within the clipped body only.
        double y = -mScrollY;
        for (auto &it : mItems)
        {
            if (it.header)
            {
                it.baseY = y + kSectionH * 0.5 + 3.0;
                y += kSectionH;
            }
            else
            {
                it.baseY = y + kSliderRowH * 0.5 + 3.0;
                auto &sl = mSliders[it.sliderIndex];
                sl->x.set(kLabelW);
                sl->y.set(y + (kSliderRowH - kSliderH) * 0.5);
                sl->width.set(w - kLabelW - kPad > 20 ? w - kLabelW - kPad : 20);
                sl->height.set(kSliderH);
                y += kSliderRowH;
            }
        }
    }

    void ParamPanel::clampScroll()
    {
        const double maxScroll = mNaturalH - height.value();  // mNaturalH includes mTop; so does height()
        if (mScrollY < 0.0) mScrollY = 0.0;
        else if (maxScroll <= 0.0) mScrollY = 0.0;
        else if (mScrollY > maxScroll) mScrollY = maxScroll;
    }

    void ParamPanel::scrollBy(double wheelDelta)
    {
        mScrollY -= wheelDelta * kSliderRowH;  // one wheel notch ~= one row
        clampScroll();
        reflow();  // scrolling must move the sliders themselves, not just the labels
    }

    void ParamPanel::setValues(const std::vector<double> &values)
    {
        for (size_t i = 0; i < values.size() && i < mSliders.size(); ++i)
            mSliders[i]->setValue(values[i]);
    }

    void ParamPanel::onPaint(IRenderTarget &t) const
    {
        if (mDrawChrome)
            drawPanelChrome(t, width.value(), height.value(), mTitle);
        const double w = width.value();

        // Item labels are drawn here (not as children), so clip them to the same
        // body rect the sliders are clipped to -- otherwise a scrolled-off label
        // could still paint over the title bar.
        t.save();
        t.clipRect(0.0, mTop, w, height.value() - mTop);
        bool firstHeader = true;
        for (const auto &it : mItems)
        {
            const double y = mTop + it.baseY;  // baseY is body-relative; text draws in panel space
            if (it.header)
            {
                // A hairline rule above each section (except the first) separates groups
                // cleanly; a short accent tick + faux-bold ink label names it.
                if (!firstHeader)
                {
                    const double ly = y - 16.0;
                    t.setStroke(palette::line(), 1.0);
                    t.beginPath(); t.moveTo(10.0, ly); t.lineTo(w - kPad, ly); t.strokePath();
                }
                firstHeader = false;
                drawRoundedRect(t, Rect{10.0, y - 8.0, 3.0, 10.0}, 1.5, Paint::filled(mAccent));  // accent tick
                t.setFill(palette::ink());
                for (double ox : {0.0, 0.4})  // faux-bold section title
                    t.drawText(it.label, 19.0 + ox, y, 10.5);
            }
            else
            {
                t.setFill(palette::muted());
                t.drawText(it.label, 12.0, y, 10.0);
            }
        }

        // A visible scrollbar -- otherwise there's no cue that a section list taller
        // than its tab can be reached at all (only that it's cut off).
        const double maxScroll = mNaturalH - height.value();
        if (maxScroll > 0.5)
        {
            const double viewport = height.value() - mTop;
            double thumbH = viewport * viewport / (mNaturalH - mTop);
            if (thumbH < 20.0) thumbH = 20.0;
            if (thumbH > viewport) thumbH = viewport;
            const double thumbY = mTop + (mScrollY / maxScroll) * (viewport - thumbH);
            drawRoundedRect(t, Rect{w - 6.0, mTop, 4.0, viewport}, 2.0, Paint::filled(palette::surface()));
            drawRoundedRect(t, Rect{w - 6.0, thumbY, 4.0, thumbH}, 2.0, Paint::filled(palette::faint()));
        }
        t.restore();
    }
}
}
