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

        /** What the detector is actually found by (R-AISEG-11). Not decoration: a photographer
         *  who knows it keys on red-over-green understands at once why it took a terracotta pot,
         *  and reaches for Sensitivity instead of concluding the feature is broken.
         *
         *  One line, because there is one subject (R-AISEG-25). The other four are withdrawn
         *  (R-AISEG-22) and their captions went with them; when they come back, each comes back
         *  with the sentence that says what IT keys on. */
        const char *kSkinCaption = "Warm mid-tones with red over green — faces and hands.";

        /** The stage names `Detection.cpp` publishes, in words a photographer reads (R-AISEG-27).
         *  The mapping is here and not in the engine because "colour" is the stable name an
         *  `expect` assertion and a log line match on, and "Reading colour" is a sentence in one
         *  language — the two must not be the same string. */
        std::string stageLabel(const std::string &stage)
        {
            if (stage == "preparing") return "Preparing";
            if (stage == "colour") return "Reading colour";
            if (stage == "regions") return "Finding regions";
            if (stage == "shapes") return "Cleaning up";
            if (stage == "outline") return "Tracing the outline";
            return "Detecting";                      // "queued", and anything added later
        }

        /** The five states of R-AISEG-26, in the order they can be told apart.
         *
         *  "Found nothing" and "not detected yet" being different sentences is the whole point:
         *  they ask for different things from the photographer — try another photo or move
         *  Sensitivity, versus press the button. A mask that covers nothing looks identical in
         *  both cases and this line is the only thing that separates them. */
        std::string detectStatusLine(const DetectStatus &d)
        {
            if (d.running) return stageLabel(d.stage) + "…";
            if (d.regions > 0)
            {
                const int pct = (int)(d.coverage * 100.0 + 0.5);
                std::string s = "Found " + std::to_string(d.regions) +
                                (d.regions == 1 ? " region" : " regions");
                // The percentage comes from the last detection, so it is absent after a reopen —
                // at which point the count is still true and a made-up percentage would not be.
                if (d.ranForThisMask) s += " · " + std::to_string(pct) + "% of the frame";
                return s;
            }
            if (!d.handled) return "No detector for this subject";
            if (d.ranForThisMask) return "Found nothing — try Sensitivity";
            return "No detection yet";
        }

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

        // R-AISEG-10..12/25/27. No subject picker: there is one subject, and a control with one
        // option looks like a choice, invites a click and does nothing. What the picker was
        // carrying — what this detector keys on — is in the header and the caption instead.
        mDetect = std::make_shared<DetectBlock>();
        mDetect->visible = false;
        mDetect->caption = kSkinCaption;
        addChild(mDetect);

        // The one thing in the block that DOES something. Styled as the accent action it is,
        // rather than as the outlined "Inv" beside it: this is the only control in the panel
        // that starts work rather than changing a number.
        mDetectBtn = std::make_shared<PillButton>("Detect");
        mDetectBtn->idleBox = {Paint::filled(palette::primary()), radius::control()};
        mDetectBtn->idleText = {palette::primaryForeground(), 10.0, font::sansMedium()};
        mDetectBtn->width.set(estimateTextWidth("Detect", 10.0) + 4 * 6.5);
        mDetectBtn->height.set(DetectBlock::kButtonH);
        mDetectBtn->onClick = [this] { if (onDetect) onDetect(); };
        mDetect->addChild(mDetectBtn);

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
        mSensitivity->setValue(m.sensitivity * 100.0);
        mFeather->setValue(m.feather * 100.0);
        mExposure->setValue(fromEv(m.adjust.exposure));
        mHighlights->setValue(m.adjust.highlights);
        mShadows->setValue(m.adjust.shadows);
        mTemperature->setValue(m.adjust.temp);
        mSaturation->setValue(m.adjust.saturation);
        mDehaze->setValue(m.adjust.dehaze);
    }

    void MaskPanel::setSegmenter(const std::string &name)
    {
        if (!mDetect) return;
        // "built-in" is not a name to show a photographer; it is the absence of one, and the
        // built-in's honest self-description is what it keys on.
        mDetect->header = (name.empty() || name == "built-in") ? "Detect skin (colour & texture)"
                                                              : "Detect skin (" + name + ")";
    }

    void MaskPanel::setDetectStatus(const DetectStatus &s)
    {
        mStatus = s;
        if (!mDetect) return;
        mDetect->status = detectStatusLine(s);
        // R-AISEG-20/27: one detection at a time is the SERVICE's rule, so a button that could
        // be pressed while one runs would be a control that lies. `enabled` is animated by the
        // framework (`disabledAmount`), so this is a fade and not a flip.
        if (mDetectBtn) mDetectBtn->enabled = !s.running;
        // TARGETS only. `advance` starts both tweens, because a setter has no clock — and a bar
        // that jumped to the fraction here would look right in every still frame and read as
        // three stalls in motion (R-G-1's compliance clause, met from the same direction again).
        mBarFillTarget = s.running ? s.fraction : (s.fraction > 0.0 ? 1.0 : 0.0);
        mBarShowTarget = s.running ? 1.0 : 0.0;
    }

    const std::string &MaskPanel::detectStatusText() const
    {
        static const std::string kNone;
        return mDetect ? mDetect->status : kNone;
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
        // R-AISEG-27: the bar EASES toward the service's fraction. The stages are uneven —
        // `colour` is a third of the work and `shapes` a tenth — so a bar that took each number
        // as it arrived would read as three stalls, while the same numbers eased read as
        // continuous progress, which is what they are. 220 ms, a little longer than the 180 ms
        // everything else uses, because it is deliberately catching up rather than responding.
        if (mBarFillTarget != mBarFillLast)
        {
            mBarFill.animateTo(mBarFillTarget, 220.0, Easing::EaseOutCubic, nowMs);
            mBarFillLast = mBarFillTarget;
        }
        if (mBarShowTarget != mBarShowLast)
        {
            // Slower out than in: the fill is still finishing when the fade starts, and the last
            // thing a photographer should see of a detection is a full bar, not one vanishing at
            // 70% — which reads as a failure.
            mBarShow.animateTo(mBarShowTarget, mBarShowTarget > 0.0 ? 120.0 : 300.0,
                               Easing::EaseOutCubic, nowMs);
            mBarShowLast = mBarShowTarget;
        }
        mBarFill.update(nowMs);
        mBarShow.update(nowMs);
        if (mDetect)
        {
            mDetect->barFill = mBarFill.value();
            mDetect->barShow = mBarShow.value();
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
            const double blockH = DetectBlock::kHeaderH + DetectBlock::kCaptionH +
                                  DetectBlock::kCaptionMB + SliderRow::kRowHeight +
                                  DetectBlock::kButtonH + DetectBlock::kButtonMB +
                                  DetectBlock::kBarH + DetectBlock::kBarMB;
            mDetect->x.set(0.0);
            mDetect->y.set(y);
            mDetect->width.set(w);
            mDetect->height.set(open * blockH);
            mDetect->visible = open > 0.004;
            {
                double by = DetectBlock::kHeaderH + DetectBlock::kCaptionH + DetectBlock::kCaptionMB;
                mSensitivity->x.set(kPadX); mSensitivity->y.set(by);
                mSensitivity->width.set(innerW); mSensitivity->layout();
                by += SliderRow::kRowHeight;
                mDetectBtn->x.set(kPadX); mDetectBtn->y.set(by);
                // The status text starts after the button and gets whatever is left, so R5 is a
                // measurement rather than a hope: the block tells the paint pass where the space
                // begins and the paint pass ellipsizes against it.
                mDetect->statusX = kPadX + mDetectBtn->width.value() + 8.125;
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

        // "Detect skin (colour & texture)", not "AI Subject" (R-AISEG-11/25). The header is where
        // the honesty belongs: it is read once, by everyone, before the first click — and a label
        // claiming more than the code does is worse than a plain one. It carries the SUBJECT now
        // that there is no picker, and the parenthesis says which of two very different things is
        // answering (R-AISEG-15): "colour & texture" is the built-in being honest about itself, a
        // model's own name replaces it when one is installed.
        drawSectionHeader(t, kPadX, 0.0, innerW, header);

        // What the detector is found by. 9px muted, on the type ramp's caption size.
        double y = kHeaderH + kCaptionH * 0.5;
        t.setFill(palette::mutedForeground());
        t.drawText(caption, kPadX, y + 9.0 * 0.35, 9.0, font::sans());

        // ── the status sentence (R-AISEG-26/28) ────────────────────────────────────────────
        // Beside the button, starting where `layout()` said the button ends, and ELLIPSIZED
        // against the space that is actually left rather than trusted to fit (R5). Measured with
        // the real primitive, not the estimate, because a sentence whose length varies with the
        // number in it is exactly where an estimate is wrong by a visible margin.
        const double rowY = kHeaderH + kCaptionH + kCaptionMB + SliderRow::kRowHeight;
        const double avail = std::max(0.0, width.value() - kPadX - statusX);
        std::string line = status;
        if (t.measureText(line, 9.0, font::sans()) > avail && avail > 0.0)
        {
            while (line.size() > 1 &&
                   t.measureText(line + "…", 9.0, font::sans()) > avail)
                line.erase(line.size() - 1);
            line += "…";
        }
        t.setFill(palette::mutedForeground());
        t.drawText(line, statusX, rowY + kButtonH * 0.5 + 9.0 * 0.35, 9.0, font::sans());

        // ── the progress bar (R-AISEG-27) ──────────────────────────────────────────────────
        // In a row that is ALWAYS laid out; what changes is its opacity. Growing the block
        // instead would move everything below it twice for one button press, and the panel
        // jumping the moment somebody presses a button is the opposite of feeling responsive.
        //
        // Drawn from `barShow`/`barFill`, both LIVE eased values written by MaskPanel::advance —
        // never from the service's fraction, which arrives in five uneven steps.
        if (barShow > 0.002)
        {
            const double barY = rowY + kButtonH + kButtonMB;
            const double r = kBarH * 0.5;
            drawRoundedRect(t, Rect{kPadX, barY, innerW, kBarH}, r,
                            Paint::filled(palette::whiteAlpha(0.10 * barShow)));
            const double fill = std::max(0.0, std::min(1.0, barFill)) * innerW;
            // Below 2r a rounded rect degenerates — the two caps overlap and the shape reads as
            // a dot that pops rather than a bar that starts. Nothing at all is the honest
            // picture of "it has not got anywhere yet".
            if (fill > kBarH)
            {
                drawRoundedRect(t, Rect{kPadX, barY, fill, kBarH}, r,
                                Paint::filled(palette::primaryAlpha(barShow)));
            }
        }
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
