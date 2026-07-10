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
        const char *kAspectNames[XformPanel::kAspectCount] = {"Free", "1:1", "4:3", "16:9", "3:2", "5:4"};
        const double kAspectRatio[XformPanel::kAspectCount] = {0.0, 1.0, 4.0 / 3.0, 16.0 / 9.0, 3.0 / 2.0, 5.0 / 4.0};

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
                const double r = kAspectRatio[i];
                double w = 1.0, h = 1.0;
                // Approximation: without the source image's real pixel dimensions
                // plumbed into this panel, the target ratio is applied against a
                // normalized square (crop coords are 0..1 of the framed image) --
                // exact for a square source, directionally correct otherwise.
                if (r > 0.0) { if (r >= 1.0) h = 1.0 / r; else w = r; }
                const double x = (1.0 - w) * 0.5, y = (1.0 - h) * 0.5;
                mState.cropX = (float)x; mState.cropY = (float)y; mState.cropW = (float)w; mState.cropH = (float)h;
                if (onCropChange) onCropChange(x, y, w, h);
            };
            addChild(chip);
            mAspectChips.push_back(chip);
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

    void XformPanel::setState(const State &s) { mState = s; }

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
        y += kAspectChipH + 9.75;

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
        drawSectionHeader(t, kPadX, mFlipHeaderY, innerW, "Flip");
        drawSectionHeader(t, kPadX, mAutoHeaderY, innerW, "Auto");
    }
}
}
