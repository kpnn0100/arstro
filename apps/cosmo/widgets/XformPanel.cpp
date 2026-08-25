#include "XformPanel.h"
#include "SectionHeader.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "../Theme.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        constexpr double kPadX = 9.75;
        constexpr double kRotateRowH = 4.875 * 2 + 11.0 * 1.3;  // py-1.5 * 2 + line height
        constexpr double kAspectChipH = 1.625 * 2 + 10.0 * 1.3;
        const char *kAspectNames[XformPanel::kAspectCount] =
            {"Free", "1:1", "4:3", "16:9", "3:2", "5:4", "Custom"};
        // Custom's value is not a constant — it is whatever was typed (R-CROP-4) — so it reads
        // 0 here and `lockedRatio()` substitutes mCustomW/mCustomH.
        const double kAspectRatio[XformPanel::kAspectCount] =
            {0.0, 1.0, 4.0 / 3.0, 16.0 / 9.0, 3.0 / 2.0, 5.0 / 4.0, 0.0};
        constexpr double kCustomRowH = 20.0;

        BoxStyle outlineIdle() { return {Paint::filledStroked(Color{0, 0, 0, 0}, palette::border(), 1.0), radius::control()}; }
        TextStyle outlineText() { return {palette::mutedForeground(), 10.0, font::sans()}; }
    }

    XformPanel::XformPanel()
    {
        mMinus90 = std::make_shared<PillButton>("-90°");
        mPlus90 = std::make_shared<PillButton>("+90°");
        for (auto &b : {mMinus90, mPlus90})
        {
            b->idleBox = outlineIdle(); b->idleText = outlineText();
            b->height.set(kRotateRowH);
        }
        mMinus90->onClick = [this] { if (onQuarterTurn) onQuarterTurn(-1); };
        mPlus90->onClick = [this] { if (onQuarterTurn) onQuarterTurn(1); };
        addChild(mMinus90); addChild(mPlus90);

        mResetBtn = std::make_shared<IconButton>(
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::rotateCcw(t, r, c); });
        mResetBtn->idleColor = palette::mutedForeground();
        mResetBtn->activeColor = palette::foreground();
        mResetBtn->width.set(kRotateRowH); mResetBtn->height.set(kRotateRowH);
        mResetBtn->onClick = [this] { if (onResetRotation) onResetRotation(); };
        addChild(mResetBtn);

        for (int i = 0; i < kAspectCount; ++i)
        {
            auto chip = std::make_shared<PillButton>(kAspectNames[i]);
            chip->idleBox = outlineIdle();
            chip->idleBox.cornerRadius = radius::hairline();
            chip->activeBox = {Paint::filled(Color{palette::primary().r, palette::primary().g, palette::primary().b, 0.10}),
                                radius::hairline()};
            chip->activeBox.paint.hasStroke = true; chip->activeBox.paint.stroke = palette::primary(); chip->activeBox.paint.strokeWidth = 1.0;
            chip->idleText = outlineText();
            chip->activeText = {palette::primary(), 10.0, font::sans()};
            chip->height.set(kAspectChipH);
            chip->active = (i == mAspectSelected);
            chip->onClick = [this, i] {
                mAspectSelected = i;
                for (int k = 0; k < kAspectCount; ++k) mAspectChips[k]->active = (k == i);
                // R-CROP-2: FREE UNLOCKS, IT DOES NOT RESET. Picking Free used to run this same
                // code with r = 0, fall through to a full-frame rectangle, and throw the crop
                // away — so the one control whose job is "stop constraining me" was the one
                // that destroyed the work. It now changes the LOCK and nothing else.
                if (onAspectLockChange) onAspectLockChange(lockedRatio());
                if (i == kAspectFree) { layout(); return; }
                applySelectedRatio();
                layout();   // the Custom row appears/disappears with the selection
            };
            addChild(chip);
            mAspectChips.push_back(chip);
        }

        // R-CROP-4: the Custom ratio's two fields. Hidden (via `visible`) unless Custom is the
        // selected chip — `layout()` places them and sets that, so there is one place deciding
        // whether the row exists.
        {
            TextBoxStyle st;
            st.idle = {Paint::filledStroked(palette::input(), palette::border(), 1.0), radius::control()};
            st.focused = {Paint::filledStroked(palette::input(), palette::ring(), 1.0), radius::control()};
            st.text = {palette::foreground(), 10.0, font::mono()};        // a numeric, so mono
            st.placeholder = {palette::mutedForeground(), 10.0, font::mono()};
            st.caretColor = palette::primary();
            mCustomWField = std::make_shared<TextBox>(st);
            mCustomHField = std::make_shared<TextBox>(st);
            mCustomWField->text = "16";
            mCustomHField->text = "10";
            for (auto &f : {mCustomWField, mCustomHField})
            {
                f->height.set(kCustomRowH);
                f->visible = false;
                addChild(f);
            }
        }

        mFlipH = std::make_shared<PillButton>("Horizontal");
        mFlipV = std::make_shared<PillButton>("Vertical");
        mAutoHorizon = std::make_shared<PillButton>("Auto Horizon");
        mAutoGeometry = std::make_shared<PillButton>("Auto Geometry");
        for (auto &b : {mFlipH, mFlipV, mAutoHorizon, mAutoGeometry})
        {
            b->idleBox = outlineIdle(); b->idleText = outlineText();
            b->height.set(kRotateRowH);
            // Not wired: EditEngine has no flip or auto-horizon/geometry API yet
            // (only crop/rotate/quarterTurns) -- left inert rather than faking it.
            addChild(b);
        }
    }

    Rect XformPanel::aspectChipRect(int i) const
    {
        if (i < 0 || i >= (int)mAspectChips.size()) return Rect{0, 0, 0, 0};
        const auto &c = mAspectChips[i];
        return Rect{c->x.value(), c->y.value(), c->width.value(), c->height.value()};
    }

    double XformPanel::lockedRatio() const
    {
        if (mAspectSelected == kAspectFree) return 0.0;
        if (mAspectSelected == kAspectCustom)
            return (mCustomW > 0.0 && mCustomH > 0.0) ? mCustomW / mCustomH : 0.0;
        return kAspectRatio[mAspectSelected];
    }

    void XformPanel::applySelectedRatio()
    {
        const double r = lockedRatio();
        if (r <= 0.0) return;
        // R-CROP-1: against the PHOTO's shape, not a square. With no dimensions there is
        // nothing honest to compute, so do nothing rather than guess — the old code guessed and
        // turned a 16:9 request on a 3:2 photo into 16:10.7.
        const double nr = crop::normalisedRatio(r, mState.sourceWidth, mState.sourceHeight);
        if (nr <= 0.0) return;
        // R-CROP-3: fit the ratio to the crop ALREADY THERE, keeping its centre. A photographer
        // who has framed a shot and then asks for 16:9 wants their framing at 16:9, not a fresh
        // centred box over the whole photo.
        const Rect fitted = crop::applyRatio(
            Rect{mState.cropX, mState.cropY, mState.cropW, mState.cropH}, nr);
        mState.cropX = (float)fitted.x; mState.cropY = (float)fitted.y;
        mState.cropW = (float)fitted.w; mState.cropH = (float)fitted.h;
        if (onCropChange) onCropChange(fitted.x, fitted.y, fitted.w, fitted.h);
    }

    void XformPanel::advance(double nowMs)
    {
        // R-CROP-4: `TextBox` has no commit callback, so the two custom fields are polled — the
        // same thing HomeScreen's search box does. Applied only when the PARSED ratio actually
        // changes, so typing "1" on the way to "16" does not re-crop the photo on every
        // keystroke to a shape nobody asked for.
        if (mCustomWField && mCustomHField && mAspectSelected == kAspectCustom)
        {
            const double w = std::atof(mCustomWField->text.c_str());
            const double h = std::atof(mCustomHField->text.c_str());
            if (w > 0.0 && h > 0.0 && (w != mCustomW || h != mCustomH))
            {
                mCustomW = w; mCustomH = h;
                if (onAspectLockChange) onAspectLockChange(lockedRatio());
                applySelectedRatio();
            }
        }
        Segment::advance(nowMs);
    }

    void XformPanel::setState(const State &s)
    {
        mState = s;
        // Adopting a crop from elsewhere (a preset, an undo, the crop box on the photo) must not
        // silently change what the lock says — the chip row reports a MODE, and the mode is the
        // user's, not the rectangle's.
    }

    void XformPanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        double y = kSectionHeaderHeight;  // "Rotate" header at y=0

        const double btnGroupW = mMinus90->width.value() > 0 ? 0 : 0;  // computed below
        const double turnBtnW = estimateTextWidth("+90°", 10.0) + 2 * 6.5;
        const double resetW = kRotateRowH;
        const double gap = 6.5, innerGap = 3.25;
        const double readoutW = innerW - (turnBtnW * 2 + innerGap) - gap - resetW - gap;
        mReadoutRect = Rect{kPadX, y, std::max(0.0, readoutW), kRotateRowH};
        mMinus90->x.set(mReadoutRect.x + mReadoutRect.w + gap); mMinus90->y.set(y); mMinus90->width.set(turnBtnW);
        mPlus90->x.set(mMinus90->x.value() + turnBtnW + innerGap); mPlus90->y.set(y); mPlus90->width.set(turnBtnW);
        mResetBtn->x.set(mPlus90->x.value() + turnBtnW + gap); mResetBtn->y.set(y);
        y += kRotateRowH + 9.75;  // mb-3
        (void)btnGroupW;

        mAspectHeaderY = y; y += kSectionHeaderHeight;
        double cx = kPadX;
        const double chipGap = 3.25;
        for (auto &chip : mAspectChips)
        {
            const double cw = estimateTextWidth(chip->label(), 10.0) + 2 * 6.5;
            chip->x.set(cx); chip->y.set(y); chip->width.set(cw);
            cx += cw + chipGap;
        }
        y += kAspectChipH + 4.875;

        // R-CROP-4: the Custom row, only while Custom is picked. Its height is added to the
        // running `y` only when it is shown, so the panel below it does not leave a gap.
        {
            const bool show = mCustomRowVisible();
            mCustomRowY = y;
            const double colonW = estimateTextWidth(":", 10.0) + 6.5;
            const double fieldW = std::max(28.0, (innerW - colonW) * 0.5);
            mCustomWField->x.set(kPadX); mCustomWField->y.set(y); mCustomWField->width.set(fieldW);
            mCustomHField->x.set(kPadX + fieldW + colonW); mCustomHField->y.set(y);
            mCustomHField->width.set(fieldW);
            mCustomWField->visible = show;
            mCustomHField->visible = show;
            if (show) y += kCustomRowH;
        }
        y += 4.875;

        mFlipHeaderY = y; y += kSectionHeaderHeight;
        const double flipGap = 4.875, flipW = (innerW - flipGap) * 0.5;
        mFlipH->x.set(kPadX); mFlipH->y.set(y); mFlipH->width.set(flipW);
        mFlipV->x.set(kPadX + flipW + flipGap); mFlipV->y.set(y); mFlipV->width.set(flipW);
        y += kRotateRowH + 9.75;

        mAutoHeaderY = y; y += kSectionHeaderHeight;
        mAutoHorizon->x.set(kPadX); mAutoHorizon->y.set(y); mAutoHorizon->width.set(flipW);
        mAutoGeometry->x.set(kPadX + flipW + flipGap); mAutoGeometry->y.set(y); mAutoGeometry->width.set(flipW);
    }

    bool XformPanel::handleGesture(const Gesture &g, const Point &local)
    {
        const bool inReadout = mReadoutRect.contains(local);
        if (g.type == Gesture::Type::DragStart && inReadout)
        {
            mDraggingReadout = true; mDragStartX = local.x; mDragStartRotation = mState.rotation;
            return true;
        }
        if (g.type == Gesture::Type::Drag && mDraggingReadout)
        {
            const double deg = mDragStartRotation + (local.x - mDragStartX) * 0.15;
            mState.rotation = (float)std::clamp(deg, -45.0, 45.0);
            if (onRotationChange) onRotationChange(mState.rotation);
            return true;
        }
        if (g.type == Gesture::Type::Drop) { mDraggingReadout = false; return true; }
        return Segment::handleGesture(g, local);
    }

    void XformPanel::onPaint(IRenderTarget &t) const
    {
        const double innerW = std::max(0.0, width.value() - 2 * kPadX);
        drawSectionHeader(t, kPadX, 0.0, innerW, "Rotate");

        drawRoundedRect(t, mReadoutRect, radius::control(), Paint::filledStroked(palette::curvePlotBg(), palette::border(), 1.0));
        // Built from integer parts rather than a "%.1f" printf spec: %f's decimal
        // point follows the process's LC_NUMERIC locale (e.g. renders "0,0" under
        // a comma-decimal locale), and the design's "+1.5°" needs a literal period.
        const bool neg = mState.rotation < 0.0f;
        const float absRot = std::fabs(mState.rotation);
        const int whole = (int)absRot;
        const int tenths = (int)std::lround((absRot - whole) * 10.0f);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%s%d.%d\xC2\xB0", neg ? "-" : "+", whole, tenths);  // UTF-8 degree sign
        const std::string readout(buf);
        t.setFill(palette::primary());
        t.drawText(readout, mReadoutRect.x + (mReadoutRect.w - estimateTextWidth(readout, 11.0)) * 0.5,
                   mReadoutRect.y + mReadoutRect.h * 0.5 + 11.0 * 0.35, 11.0, font::mono());

        drawSectionHeader(t, kPadX, mAspectHeaderY, innerW, "Aspect Ratio");
        // R-CROP-4: the ":" between the two custom-ratio fields. Drawn here rather than as a
        // third child because it is a label, not a control — and it only exists while the row
        // does, so it follows the same `mCustomRowVisible()` test the fields do.
        if (mCustomRowVisible() && mCustomWField && mCustomHField)
        {
            const double cxColon = mCustomWField->x.value() + mCustomWField->width.value();
            const double gap = mCustomHField->x.value() - cxColon;
            t.setFill(palette::mutedForeground());
            t.drawText(":", cxColon + gap * 0.5 - estimateTextWidth(":", 10.0) * 0.5,
                       mCustomRowY + kCustomRowH * 0.5 + 10.0 * 0.35, 10.0, font::mono());
        }

        drawSectionHeader(t, kPadX, mFlipHeaderY, innerW, "Flip");
        drawSectionHeader(t, kPadX, mAutoHeaderY, innerW, "Auto");
    }
}
}
