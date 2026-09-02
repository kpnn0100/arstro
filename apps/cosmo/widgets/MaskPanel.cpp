#include "MaskPanel.h"
#include "SectionHeader.h"
#include "../../../core/ImageProcessing/src/analysis/Segmenter.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "UnitConversions.h"
#include "../Theme.h"
#include <algorithm>
#include <cctype>
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
        // R-MASK-6 / R-AISEG-10: "Draw" rather than "Path", "Detect" rather than "Semantic" —
        // each chip names what the user DOES with it, as the first three do, not the data
        // structure underneath.
        constexpr int kTypeCount = 5;
        const char *kTypeNames[kTypeCount] = {"Radial", "Linear", "Brush", "Draw", "Detect"};

        /** What each subject is actually found by (R-AISEG-11). Not decoration: a photographer
         *  who knows the sky detector keys on SMOOTHNESS understands at once why it declined a
         *  textured blue awning, and reaches for Sensitivity or Inv instead of concluding the
         *  feature is broken. Hair's line is the one R-AISEG-2 insists on — the reliability of
         *  the five is not equal and the UI may not pretend it is. */
        const char *kSubjectCaption[(int)SemanticSubject::Count] = {
            "Blue or bright, smooth, high in the frame.",
            "Warm mid-tones with red over green — faces and hands.",
            "Green with real saturation, anywhere in the frame.",
            "Blue-cyan, low in the frame, a little texture.",
            "Dark, muted and textured — may take fabric too."};

        std::string subjectLabel(int subject)
        {
            const int s = (subject >= 0 && subject < (int)SemanticSubject::Count) ? subject : 0;
            std::string n = semanticSubjectName((SemanticSubject)s);
            if (!n.empty()) n[0] = (char)std::toupper((unsigned char)n[0]);
            return n;
        }

        std::string maskLabel(const MaskParams &m, int index)
        {
            const int t = m.type >= 0 && m.type < kTypeCount ? m.type : 0;
            std::string s = kTypeNames[t] + std::string(" ") + std::to_string(index + 1);
            // A drawn mask says how far along it is: three points is the threshold where it
            // starts to render, and a panel that stayed silent about that would leave the user
            // wondering why nothing happened (R-MASK-6).
            if (t == MaskParams::Path)
                s += m.path.size() < 3 ? " (" + std::to_string(m.path.size()) + " pts)"
                                       : " (" + std::to_string(m.path.size()) + ")";
            // A detect mask says WHAT it is looking for, for the same reason: the difference
            // between two of them in the list is their subject and nothing else.
            if (t == MaskParams::Semantic) s += " (" + subjectLabel(m.subject) + ")";
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
        for (int i = 0; i < kTypeCount; ++i)
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
            [](IRenderTarget &t, const Rect &r, const Color &c) { icon::deleteBin(t, r, c); });
        mTrashBtn->idleColor = palette::mutedForeground();
        mTrashBtn->activeColor = palette::destructive();
        mTrashBtn->width.set(kSelectRowH);
        mTrashBtn->height.set(kSelectRowH);
        mTrashBtn->onClick = [this] { if (onDeleteMask) onDeleteMask(); };
        addChild(mTrashBtn);

        // R-AISEG-10..12. A SegmentedControl and not a ComboBox: the subject set is small,
        // fixed and worth seeing all at once — the same reasoning as Hue/Sat/Lum — and its
        // highlight already slides (R-G-1) where a popup would have to escape the block's clip.
        mDetect = std::make_shared<DetectBlock>();
        mDetect->visible = false;
        addChild(mDetect);

        std::vector<std::string> subjects;
        for (int i = 0; i < (int)SemanticSubject::Count; ++i) subjects.push_back(subjectLabel(i));
        mSubject = std::make_shared<SegmentedControl>(subjects);
        mSubject->containerBox = {Paint::filledStroked(palette::segmentedBg(), palette::border(), 1.0), radius::control()};
        mSubject->idleSegBox = {Paint{}, radius::hairline()};
        mSubject->activeSegBox = {Paint::filled(palette::primary()), radius::hairline()};
        mSubject->edgeRadius = radius::control();
        mSubject->idleText = {palette::mutedForeground(), 10.0, font::sans()};
        mSubject->activeText = {palette::white(), 10.0, font::sans()};
        mSubject->height.set(DetectBlock::kPickerH);
        mSubject->onChange = [this](int i) {
            if (mDetect) mDetect->caption = kSubjectCaption[i < 0 || i >= (int)SemanticSubject::Count ? 0 : i];
            if (onSubjectChange) onSubjectChange(i);
        };
        mDetect->addChild(mSubject);

        mSensitivity = std::make_shared<SliderRow>("Sensitivity", 0, 100, 50.0);
        mSensitivity->onChange = [this](double v) { if (onSensitivityChange) onSensitivityChange(v / 100.0); };
        mDetect->addChild(mSensitivity);

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
        if (!has) mDetectTarget = 0.0;
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
        // The block's TARGET, not its height: `advance` starts the tween, because a setter with
        // no `nowMs` cannot (R-G-1). Setting the height here is exactly the snap R-AISEG-12 is
        // about, and it would look correct in every still frame.
        mDetectTarget = (m.type == MaskParams::Semantic) ? 1.0 : 0.0;
        mSubject->setSelectedImmediate(m.subject);   // programmatic sync must not fire onChange
        mDetect->caption = kSubjectCaption[(m.subject >= 0 && m.subject < (int)SemanticSubject::Count)
                                               ? m.subject : 0];
        mSensitivity->setValue(m.sensitivity * 100.0);
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
        mScrollTarget = std::min(maxScroll, std::max(0.0, mScrollTarget - delta));
    }

    void MaskPanel::advance(double nowMs)
    {
        if (mScrollTarget != mScrollLastTarget)
        {
            mScroll.animateTo(mScrollTarget, 180.0, Easing::EaseOutCubic, nowMs);
            mScrollLastTarget = mScrollTarget;
        }
        // R-AISEG-12: the Detect block opens and closes, it does not appear. Started here and
        // not in the setter, because a setter has no clock.
        if (mDetectTarget != mDetectLastTarget)
        {
            mDetectOpen.animateTo(mDetectTarget, 200.0, Easing::EaseOutCubic, nowMs);
            mDetectLastTarget = mDetectTarget;
        }
        const bool opening = mDetectOpen.isAnimating();
        mDetectOpen.update(nowMs);
        const bool moving = mScroll.isAnimating();
        mScroll.update(nowMs);
        if (moving || opening) layout();
        Segment::advance(nowMs);
    }

    Rect MaskPanel::addChipRect(int i) const
    {
        if (i < 0 || i >= (int)mAddChips.size()) return Rect{0, 0, 0, 0};
        const auto &c = mAddChips[(std::size_t)i];
        return Rect{c->x.value(), c->y.value(), c->width.value(), c->height.value()};
    }

    void MaskPanel::layout()
    {
        const double w = width.value(), innerW = std::max(0.0, w - 2 * kPadX);
        double y = -mScroll.value();

        mHeaderY[0] = y;
        y += kSectionHeaderHeight;  // "Add Mask" header
        const double chipW = (innerW - (kTypeCount - 1) * kChipGap) / (double)kTypeCount;
        for (int i = 0; i < kTypeCount; ++i)
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

            // The Detect block: full-height children inside a container whose HEIGHT eases, so
            // the rows slide out from under Feather at their real positions and everything below
            // moves in step. Nothing is ever drawn compressed — that is what the clip buys, and
            // it is why this is a container rather than an opacity fade (R-AISEG-12).
            const double open = mDetectOpen.value();
            const double blockH = DetectBlock::kHeaderH + DetectBlock::kPickerH +
                                  DetectBlock::kPickerMB + DetectBlock::kCaptionH +
                                  SliderRow::kRowHeight;
            mDetect->x.set(0.0);
            mDetect->y.set(y);
            mDetect->width.set(w);
            mDetect->height.set(open * blockH);
            mDetect->visible = open > 0.004;
            {
                double by = DetectBlock::kHeaderH;
                mSubject->x.set(kPadX); mSubject->y.set(by); mSubject->width.set(innerW);
                mSubject->layout();
                by += DetectBlock::kPickerH + DetectBlock::kPickerMB + DetectBlock::kCaptionH;
                mSensitivity->x.set(kPadX); mSensitivity->y.set(by);
                mSensitivity->width.set(innerW); mSensitivity->layout();
            }
            y += open * blockH;

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
        mContentHeight = y + mScroll.value() + 13.0;
    }

    void DetectBlock::onPaint(IRenderTarget &t) const
    {
        constexpr double kPadX = 9.75;
        const double innerW = std::max(0.0, width.value() - 2 * kPadX);
        // `clipToBounds` clips the CHILD SUBTREE only — `Segment::renderContent` calls onPaint
        // before it installs the clip, deliberately, so a widget can draw a ring or a shadow
        // outside its own box. Here that meant the caption stayed on screen at every
        // intermediate height while the picker beside it was correctly clipped away, which
        // looked exactly like a bug and was one. So this pass clips itself.
        t.save();
        t.clipRect(0.0, 0.0, width.value(), height.value());
        // "Detect (colour & texture)", not "AI Subject" (R-AISEG-11). The header is where the
        // honesty belongs: it is read once, by everyone, before the first click — and a label
        // claiming more than the code does is worse than a plain one.
        drawSectionHeader(t, kPadX, 0.0, innerW, "Detect (colour & texture)");
        // What THIS subject is found by. 9px muted, on the type ramp's caption size, sitting in
        // the gap the layout already reserves for it.
        const double y = kHeaderH + kPickerH + kPickerMB + kCaptionH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText(caption, kPadX, y + 9.0 * 0.35, 9.0, font::sans());
        t.restore();
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
