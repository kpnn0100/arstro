#include "ColorGradingPanel.h"
#include "../Chrome.h"
#include "../CosmoTheme.h"

namespace arstro
{
namespace cosmo
{
    using namespace artboard;

    namespace
    {
        constexpr double kHeaderH = 34.0;
        constexpr double kPad = 12.0;
        constexpr double kLabelW = 64.0;
        constexpr double kCtrlH = 16.0;
        constexpr double kRowH = 30.0;  // fixed row height: rows snap, they never stretch/collide
    }

    ColorGradingPanel::ColorGradingPanel(const Theme &theme, const Color &accent) : mAccent(accent)
    {
        width.set(300.0);
        height.set(300.0);

        // The row controls live under a clipped body (below the title bar), so
        // scrolling a tall row list can never draw a row over the chrome above it.
        mBody = std::make_shared<Segment>();
        mBody->clipToBounds = true;
        addChild(mBody);

        mRegionSel = std::make_shared<ComboBox>(theme.combo);
        mRegionSel->setOptions({"Shadows", "Midtones", "Highlights"});
        mRegionSel->setSelectedIndex(0);
        mRegionSel->onChange = [this](int r) { mRegion = r; loadRegion(); };
        mBody->addChild(mRegionSel);

        auto slider = [&](double mn, double mx, double def) {
            auto s = std::make_shared<Slider>(theme.slider);
            s->setRange(mn, mx); s->setValue(def); s->setDefault(def);
            mBody->addChild(s);
            return s;
        };
        mHue = slider(0, 360, 0);
        mSat = slider(0, 100, 0);
        mLum = slider(-100, 100, 0);
        mBalance = slider(-100, 100, 0);
        for (auto *s : {mHue.get(), mSat.get(), mLum.get()}) s->onChange = [this](double) { emitGrade(); };
        mBalance->onChange = [this](double v) { mState.balance = v; if (onBalance) onBalance(v); };

        mRemap = std::make_shared<TextToggle>("remap", accent);
        mRemap->onChange = [this](bool on) { mState.remapOn = on; emitRemap(); };
        mBody->addChild(mRemap);

        mSrc = slider(0, 360, 0);
        mRange = slider(0, 180, 30);
        mDst = slider(0, 360, 0);
        mStrength = slider(0, 100, 0);
        for (auto *s : {mSrc.get(), mRange.get(), mDst.get(), mStrength.get()})
            s->onChange = [this](double) { emitRemap(); };

        mRows = {
            {mRegionSel, "", false},
            {mHue, "hue", true}, {mSat, "sat", true}, {mLum, "lum", true},
            {mBalance, "balance", true},
            {mRemap, "", false},
            {mSrc, "src", true}, {mRange, "range", true}, {mDst, "dst", true}, {mStrength, "amount", true},
        };
    }

    void ColorGradingPanel::layout(double w, double h)
    {
        width.set(w);
        height.set(h);
        const int rows = (int)mRows.size();
        mNaturalH = kHeaderH + rows * kRowH + kPad;
        clampScroll();
        reflow();
    }

    void ColorGradingPanel::reflow()
    {
        const double w = width.value(), h = height.value();
        const int rows = (int)mRows.size();
        mBody->x.set(0.0); mBody->y.set(kHeaderH);
        mBody->width.set(w); mBody->height.set(h - kHeaderH > 0 ? h - kHeaderH : 0);

        // Rows snap to a fixed height (never stretched to fill h) so they can never
        // collide. Positions are relative to mBody's origin (already below the title
        // bar); scrollY shifts them within the clipped body only.
        for (int i = 0; i < rows; ++i)
        {
            const double top = i * kRowH - mScrollY;
            Row &r = mRows[i];
            r.baseY = top + kRowH * 0.5 + 3.0;
            const bool labeled = r.labeled;
            const double cx = labeled ? kLabelW : kPad;
            const double cw = w - cx - kPad;
            r.ctrl->x.set(cx);
            r.ctrl->width.set(cw > 20 ? cw : 20);
            if (labeled)
            {
                r.ctrl->y.set(top + (kRowH - kCtrlH) * 0.5);
                r.ctrl->height.set(kCtrlH);
            }
            else
            {
                // combo / toggle: a bit taller
                const double ch = 22.0;
                r.ctrl->y.set(top + (kRowH - ch) * 0.5);
                r.ctrl->height.set(ch);
            }
        }
    }

    void ColorGradingPanel::clampScroll()
    {
        const double maxScroll = mNaturalH - height.value();
        if (mScrollY < 0.0) mScrollY = 0.0;
        else if (maxScroll <= 0.0) mScrollY = 0.0;
        else if (mScrollY > maxScroll) mScrollY = maxScroll;
    }

    void ColorGradingPanel::scrollBy(double wheelDelta)
    {
        mScrollY -= wheelDelta * kRowH;
        clampScroll();
        reflow();  // scrolling must move the row controls themselves, not just the labels
    }

    void ColorGradingPanel::loadRegion()
    {
        mHue->setValue(mState.grade[mRegion][0]);
        mSat->setValue(mState.grade[mRegion][1]);
        mLum->setValue(mState.grade[mRegion][2]);
    }

    void ColorGradingPanel::emitGrade()
    {
        mState.grade[mRegion] = {mHue->value(), mSat->value(), mLum->value()};
        if (onGrade) onGrade(mRegion, mHue->value(), mSat->value(), mLum->value());
    }

    void ColorGradingPanel::emitRemap()
    {
        mState.remapSrc = mSrc->value();
        mState.remapRange = mRange->value();
        mState.remapDst = mDst->value();
        mState.remapStrength = mStrength->value();
        if (onRemap)
            onRemap(mState.remapOn, mSrc->value(), mRange->value(), mDst->value(), mStrength->value() / 100.0);
    }

    void ColorGradingPanel::setState(const State &s)
    {
        mState = s;
        loadRegion();
        mBalance->setValue(s.balance);
        mRemap->setOn(s.remapOn);
        mSrc->setValue(s.remapSrc);
        mRange->setValue(s.remapRange);
        mDst->setValue(s.remapDst);
        mStrength->setValue(s.remapStrength);
    }

    void ColorGradingPanel::onPaint(IRenderTarget &t) const
    {
        drawPanelChrome(t, width.value(), height.value(), "GRADE");
        // Labels are drawn here (not as children), so clip them to the same body
        // rect the row controls are clipped to -- otherwise a scrolled-off label
        // could still paint over the title bar.
        t.save();
        t.clipRect(0.0, kHeaderH, width.value(), height.value() - kHeaderH);
        t.setFill(palette::muted());
        for (const auto &r : mRows)
            if (r.labeled)
                t.drawText(r.label, 10.0, kHeaderH + r.baseY, 10.0);

        // A visible scrollbar -- otherwise there's no cue that a row list taller
        // than its tab can be reached at all (only that it's cut off).
        const double maxScroll = mNaturalH - height.value();
        if (maxScroll > 0.5)
        {
            const double viewport = height.value() - kHeaderH;
            double thumbH = viewport * viewport / (mNaturalH - kHeaderH);
            if (thumbH < 20.0) thumbH = 20.0;
            if (thumbH > viewport) thumbH = viewport;
            const double thumbY = kHeaderH + (mScrollY / maxScroll) * (viewport - thumbH);
            drawRoundedRect(t, Rect{width.value() - 6.0, kHeaderH, 4.0, viewport}, 2.0, Paint::filled(palette::surface()));
            drawRoundedRect(t, Rect{width.value() - 6.0, thumbY, 4.0, thumbH}, 2.0, Paint::filled(palette::faint()));
        }
        t.restore();
    }
}
}
