#include "RightColumn.h"
#include "../EditCommands.h"
#include "../Theme.h"
#include "Icons.h"
#include "TextMetrics.h"
#include "UnitConversions.h"
#include <algorithm>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace cosmo_v2
{
    using namespace artboard;

    namespace
    {
        // Fixed content heights for the merged Mixer/Curve stack tab: Mixer's editor
        // caps at ~200px + its picker; Curve is header + picker + a 164px plot.
        constexpr double kMixerStackH = 250.0;
        constexpr double kCurveStackH = 235.0;

        // R-BYPASS-4 "filter disabled" scrim over the edit stack.
        constexpr double kDimMs = 180.0;        // fade in/out duration
        constexpr double kDimAlpha = 0.62;      // scrim strength at rest
        constexpr double kPillH = 19.0;         // the FILTER DISABLED pill
        constexpr double kPillTop = 8.0;        // its inset below the TAB STRIP (not the band top:
                                                //  sitting it at the band top would cover the tab
                                                //  labels -- siblings snap, they don't stack)
        constexpr double kPillFontPx = 9.0;
        constexpr double kPillPadX = 9.0;
        const char *kPillLabel = "FILTER DISABLED";

        // ── Command payloads (R-SVC-2) ──────────────────────────────────────────────
        // A Command is text (R-SVC-5) and this column is no longer the only view that sends
        // one: the touch shell sends the same commands, so the formatting moved to
        // EditCommands.h and both shells call it (R-TOUCH-1). What is left here is the part
        // that IS this column's own — when to send, and where to route it.
        using editcmd::adjustFields;
        using editcmd::maskBlob;
        using editcmd::num;
        using editcmd::pointsStr;
    }

    RightColumn::RightColumn(cosmo::CosmoService &svc)
        : mSvc(svc)
    {
        clipToBounds = true;
        width.set(kWidth);

        mHistogram = std::make_shared<HistogramWidget>();
        addChild(mHistogram);

        mTabs = std::make_shared<EditStackTabs>();
        // Forward the tab change outward. The column itself has no use for it — the pages
        // are already swapped by EditStackTabs — but the view above needs it to stop paying
        // for histograms whose panel is not visible (R-PREVIEW-6, T0.4).
        mTabs->onChange = [this](int index) { if (onTabChanged) onTabChanged(index); };
        mTabs->tabHeight = 27.0;

        // R-SVC-2: the one funnel all 23 slider rows go through, and what leaves it is a
        // Command, not a write. That is why a row now names its EditParams FIELD — the names
        // EditParamsIO uses, so `set` from a slider reaches exactly what a `.cmp` reaches —
        // and, where the mock's track is on its own scale, the conversion into engine units:
        // the command has to carry what the engine stores, not where the thumb sits. A drag
        // therefore becomes ~60 tiny text parses a second, which is not a cost worth avoiding.
        auto set = [this](const char *field, double (*toEngine)(double) = nullptr) {
            return [this, field, toEngine](double v) {
                sendSet({{field, num(toEngine ? toEngine(v) : v)}});
            };
        };

        std::vector<ParamPanel::Section> basicSections = {
            {"TONE", {
                {"Exposure", -400, 400, set("exposure", toEv)},
                {"Contrast", -100, 100, set("contrast")},
                {"Highlights", -100, 100, set("highlights")},
                {"Shadows", -100, 100, set("shadows")},
                {"Whites", -100, 100, set("whites")},
                {"Blacks", -100, 100, set("blacks")},
            }},
            // R-WB-1: the eyedropper rides on the COLOUR header, which is the section
            // Temperature and Tint live in — a tool belongs to the scope it acts on.
            {"COLOUR", {
                // Temperature / Tint tracks carry a colour ramp so the drag
                // direction reads as the colour it pushes toward (task point 6):
                // temperature cool-blue -> warm-amber, tint green -> magenta.
                {"Temperature", -100, 100, set("temp", toKelvin),
                 true, Color::rgba(74, 132, 232), Color::rgba(240, 178, 84)},
                {"Tint", -100, 100, set("tint", toTint),
                 true, Color::rgba(88, 196, 118), Color::rgba(206, 104, 196)},
                {"Vibrance", -100, 100, set("vibrance")},
                {"Saturation", -100, 100, set("saturation")},
            },
             ParamPanel::Section::Action::Pipette, /*toggles=*/true, false,
             [this](bool armed) { if (onWhiteBalancePickArmed) onWhiteBalancePickArmed(armed); }},
            {"PRESENCE", {
                {"Texture", -100, 100, set("texture")},
                {"Clarity", -100, 100, set("clarity")},
                {"Dehaze", 0, 100, set("dehaze")},
            }},
            {"EFFECTS", {
                {"Grain Amount", 0, 100, set("grainAmount")},
                {"Grain Size", 0, 100, set("grainSize")},
            }},
        };
        std::vector<ParamPanel::Section> detailSections = {
            {"SHARPENING", {
                {"Amount", 0, 150, set("sharpenAmount")},
                {"Radius", 5, 30, set("sharpenRadius", toRadiusPx)},
                {"Masking", 0, 100, set("sharpenMasking")},
            }},
            {"NOISE REDUCTION", {
                {"Luminance", 0, 100, set("nrLuminance")},
                {"Colour", 0, 100, set("nrColor")},
            }},
            {"LENS", {
                {"Distortion", -100, 100, set("lensDistortion")},
                {"Defringe", 0, 100, set("lensCA")},
                {"Vignette", -100, 100, set("lensVignette")},
            }},
        };
        // Merge Basic + Detail into one scrollable tab: all sections stacked in order.
        for (auto &sec : detailSections) basicSections.push_back(std::move(sec));
        mBasicDetail = std::make_shared<ParamPanel>(std::move(basicSections));
        mTabs->addPage("Basic/Detail", mBasicDetail);

        mMask = std::make_shared<MaskPanel>();
        mMask->onAddMask = [this](int type) {
            // `set mask=<blob>` APPENDS (DR-SVC-2b) — the one mask operation the params codec
            // expresses — so the new mask is the last one, and selecting it is view state
            // (R-SVC-4), decided here rather than asked for.
            MaskParams m;
            m.type = type;
            sendSet({{"mask", maskBlob(m)}});
            if (const EditParams *p = params()) mSelectedMask = (int)p->masks.size() - 1;
            syncToSlot();
        };
        mMask->onToggleInvert = [this] {
            if (const MaskParams *sel = selectedMaskParams())
            {
                MaskParams next = *sel;
                next.inverted = !next.inverted;
                sendMaskSet(mSelectedMask, {{"inverted", next.inverted ? "1" : "0"}}, next);
                syncToSlot();
            }
        };
        mMask->onDeleteMask = [this] {
            if (!selectedMaskParams()) return;
            sendMaskDelete(mSelectedMask);
            // Which mask is focused afterwards is the panel's business, not the service's.
            if (const EditParams *p = params())
                mSelectedMask = p->masks.empty() ? -1 : std::min(mSelectedMask, (int)p->masks.size() - 1);
            syncToSlot();
        };
        mMask->onSelectMask = [this](int i) {
            // No command: nothing about the photo changes. mSelectedMask is pure view state
            // (R-SVC-4), which is why it survives the migration untouched.
            if (const EditParams *p = params(); p && i >= 0 && i < (int)p->masks.size())
            {
                mSelectedMask = i;
                mMask->setMasks(p->masks, mSelectedMask);
            }
        };
        // Writing feather / adjust gives the mask a non-identity LocalAdjust so the
        // engine's applyMaskStack stops skipping it -> the mask visibly renders. No
        // setMasks() here (would fight a live drag); it re-syncs on select/add/slot.
        mMask->onFeatherChange = [this](double f) {
            if (const MaskParams *sel = selectedMaskParams())
            {
                MaskParams next = *sel;
                next.feather = (float)f;
                sendMaskSet(mSelectedMask, {{"feather", num(f)}}, next);
            }
        };
        // R-AISEG-27: the button starts the detection, and that is all the view does about it.
        // A `Command` and not a call into the session (R-SVC-2), so a script reaches it the same
        // way; the answer comes back through `AppModel::detect` and the mask's own regions, which
        // `setDetectStatus` below reads on every model refresh.
        mMask->onDetect = [this] {
            if (mSelectedMask < 0) return;
            cosmo::Command c;
            c.kind = cosmo::Command::Kind::MaskDetect;
            c.index = mSelectedMask;
            if (emitCommand(c)) return;
            mSvc.dispatch(c);   // unwired: the service is right here (see sendMaskSet)
        };
        mMask->onSensitivityChange = [this](double v) {
            if (const MaskParams *sel = selectedMaskParams())
            {
                MaskParams next = *sel;
                next.sensitivity = (float)v;
                // No setMasks(): it would fight a live drag, exactly as the feather slider's
                // comment says two callbacks above.
                sendMaskSet(mSelectedMask, {{"sensitivity", num(v)}}, next);
            }
        };
        mMask->onAdjustChange = [this](const LocalAdjust &a) {
            if (const MaskParams *sel = selectedMaskParams())
            {
                MaskParams next = *sel;
                next.adjust = a;
                sendMaskSet(mSelectedMask, adjustFields(a), next);
            }
        };
        mTabs->addPage("Mask", mMask);

        mMixer = std::make_shared<MixerPanel>();
        mMixer->onCurveChange = [this](int channel, std::vector<CurvePoint> pts) {
            // No new command needed: EditParamsIO names the mixer curves, so `set mixer0=…`
            // already reaches them (DR-SVC-2b).
            sendSet({{"mixer" + std::to_string(channel), pointsStr(pts)}});
            refreshCurveReferences();  // the green "final" tracks the edit live
        };

        mCurve = std::make_shared<CurvePanel>();
        mCurve->onCurveChange = [this](int channel, CurvePanel::Points pts) {
            // EditParamsIO's names: the master is `curve`, the three channels curveR/G/B.
            static const char *kChannelKey[3] = {"curveR", "curveG", "curveB"};
            sendSet({{channel == 0 ? "curve" : kChannelKey[channel - 1], pointsStr(pts)}});
            refreshCurveReferences();
        };

        // Merge Mixer + Curve into one scrollable tab (Mixer above, Curve below).
        mColorTab = std::make_shared<StackPanel>();
        mColorTab->addItem(mMixer, kMixerStackH, [this] { mMixer->layout(); });
        mColorTab->addItem(mCurve, kCurveStackH, [this] { mCurve->layout(); });
        mTabs->addPage("Mixer/Curve", mColorTab);

        mGrade = std::make_shared<GradePanel>();
        mGrade->onRegionChange = [this](int region, double hue, double sat, double lum) {
            sendSet({{"grade" + std::to_string(region), num(hue) + "," + num(sat) + "," + num(lum)}});
        };
        mGrade->onBalanceChange = [this](double v) { sendSet({{"balance", num(v)}}); };
        mGrade->onRemapEnableChange = [this](bool on) { sendSet({{"remapEnable", on ? "1" : "0"}}); };
        mGrade->onRemapChange = [this](double src, double range, double dst, double strength) {
            // The wheel hands strength in UI units (0..100); the command carries the engine's
            // 0..1, because a command is engine-side by construction.
            sendSet({{"remapSrc", num(src)},
                     {"remapRange", num(range)},
                     {"remapDst", num(dst)},
                     {"remapStrength", num(strength / 100.0)}});
        };
        mTabs->addPage("Grade", mGrade);

        mXform = std::make_shared<XformPanel>();
        mXform->onRotationChange = [this](double deg) { sendSet({{"rotation", num(deg)}}); };
        // R-CROP-2/3: the lock is a MODE, so it goes out as a notification rather than as an
        // edit. The crop box on the photo is the thing that needs it.
        mXform->onAspectLockChange = [this](double r) { if (onAspectLockChange) onAspectLockChange(r); };
        mXform->onQuarterTurn = [this](int dir) {
            // The button says "turn by dir"; the command says what the value BECOMES, so the
            // panel reads the current one first. syncToSlot() then pushes it back, which is
            // what the old setState() call did.
            if (const EditParams *p = params())
            {
                sendSet({{"quarterTurns", num(((p->quarterTurns + dir) % 4 + 4) % 4)}});
                syncToSlot();
            }
        };
        mXform->onResetRotation = [this] { sendSet({{"rotation", "0"}}); syncToSlot(); };
        mXform->onCropChange = [this](double x, double y, double w, double h) {
            sendSet({{"crop", num(x) + "," + num(y) + "," + num(w) + "," + num(h)}});
        };
        mTabs->addPage("Xform", mXform);

        addChild(mTabs);

        mActionBar = std::make_shared<ActionBar>();
        addChild(mActionBar);
    }

    // ── R-SVC-2: the way out ──────────────────────────────────────────────────────
    //
    // Each of these builds a Command, offers it to the service, and only writes for itself
    // if nobody is listening. The fallback is App::undo's bargain and exists for App::undo's
    // reason: a column built with no service behind it must still edit. These three, the two
    // read accessors under them, and writeSelectedMask are the whole of what still names the
    // session in this file.

    void RightColumn::sendSet(Fields fields)
    {
        cosmo::Command c = editcmd::set(std::move(fields));
        if (emitCommand(c)) return;
        // No App above us (a shot rig, a test): dispatch straight to the service we already
        // hold. Before S4c this branch wrote to the session, because an unwired column had
        // nothing else to reach — it does now, so the widget layer touches no session at all.
        mSvc.dispatch(c);
    }

    void RightColumn::sendMaskSet(int index, Fields fields, const MaskParams &whole)
    {
        cosmo::Command c = editcmd::maskSet(index, std::move(fields));
        if (emitCommand(c)) return;
        mSvc.dispatch(c);   // unwired: the service is right here (see sendSet)
    }

    void RightColumn::sendMaskDelete(int index)
    {
        cosmo::Command c = editcmd::maskDelete(index);
        if (emitCommand(c)) return;
        mSvc.dispatch(c);   // unwired: the service is right here (see sendSet)
    }

    // ── the way in: reading what we render (R-SVC-4) ──────────────────────────────
    // S4c: both reads come from the MODEL now, so this widget no longer knows an EditSession
    // exists except on the unwired fallback path. `hasEditTarget` carries what a null
    // `curParams()` used to say — a value model cannot hand back a pointer (R-SVC-3).
    const EditParams *RightColumn::params() const
    {
        const cosmo::AppModel &m = mSvc.model();
        return m.hasEditTarget ? &m.ownParams : nullptr;
    }
    EditParams RightColumn::effectiveParams() const { return mSvc.model().params; }

    void RightColumn::syncToSlot()
    {
        const EditParams *p = params();
        if (!p) return;
        // Basic sections then Detail sections, in the merged panel's flattened order.
        auto flat = [](const EditParams &q) {
            return std::vector<double>{
                fromEv(q.exposure), q.contrast, q.highlights, q.shadows, q.whites, q.blacks,
                fromKelvin(q.temp), fromTint(q.tint), q.vibrance, q.saturation,
                q.texture, q.clarity, q.dehaze, q.grainAmount, q.grainSize,
                q.sharpenAmount, fromRadiusPx(q.sharpenRadius), q.sharpenMasking,
                q.nrLuminance, q.nrColor,
                q.lensDistortion, q.lensCA, q.lensVignette,
            };
        };
        const EditParams eff = effectiveParams();  // own + ancestor groups (the "final" values)
        const std::vector<double> own = flat(*p);
        mBasicDetail->setValues(own);
        // Green stacked reach: how much ancestor groups add on top of each own value
        // (effective - own, in slider units). Zero (no groups) hides it (DR-EDIT-4).
        const std::vector<double> effv = flat(eff);
        std::vector<double> offsets(own.size(), 0.0);
        for (size_t i = 0; i < own.size(); ++i) offsets[i] = effv[i] - own[i];
        mBasicDetail->setSubValues(offsets);

        mSelectedMask = p->masks.empty() ? -1 : std::min(mSelectedMask < 0 ? 0 : mSelectedMask, (int)p->masks.size() - 1);
        // R-AISEG-15: which segmenter is answering, from the model. A mask from a network and
        // a mask from a hue band are not the same claim, and the panel is where the difference
        // has to be visible.
        mMask->setSegmenter(mSvc.model().segmenter);
        mMask->setMasks(p->masks, mSelectedMask);
        // R-AISEG-26/27/28. The count comes off the MASK and the rest off the last detection —
        // deliberately, because after a project is reopened nothing has been detected this
        // session while the mask is exactly what it was, and a panel that read both from the
        // report would say "No detection yet" about a mask that plainly has a boundary on the
        // photo.
        {
            const cosmo::DetectModel &d = mSvc.model().detect;
            DetectStatus st;
            st.ranForThisMask = d.maskIndex == mSelectedMask;
            st.running = d.active && st.ranForThisMask;
            st.fraction = d.fraction;
            st.stage = d.stage;
            st.handled = !st.ranForThisMask || d.handled;
            st.coverage = d.coverage;
            if (mSelectedMask >= 0 && mSelectedMask < (int)p->masks.size())
                st.regions = (int)p->masks[mSelectedMask].regions.size();
            mMask->setDetectStatus(st);
        }
        mMixer->setMixer(p->mixer);
        mCurve->setCurves(p->curve, p->curveChannel);
        refreshCurveReferences();  // the effective ("final") curves, drawn faint behind
        GradePanel::State gs;
        gs.grade = p->grade; gs.balance = p->balance; gs.remapEnable = p->remapEnable;
        gs.remapSrc = p->remapSrc; gs.remapRange = p->remapRange; gs.remapDst = p->remapDst; gs.remapStrength = p->remapStrength;
        mGrade->setState(gs);
        // R-CROP-1: the panel needs the PHOTO's shape to turn a pixel ratio into a normalised
        // crop. It comes from the model rather than being guessed here, and a slot with no
        // dimensions yet reports {0,0}, which the panel reads as "nothing honest to compute".
        XformPanel::State xs;
        xs.rotation = p->rotation; xs.quarterTurns = p->quarterTurns;
        xs.cropX = p->cropX; xs.cropY = p->cropY; xs.cropW = p->cropW; xs.cropH = p->cropH;
        xs.sourceWidth = mSvc.model().sourceWidth;
        xs.sourceHeight = mSvc.model().sourceHeight;
        mXform->setState(xs);
    }

    void RightColumn::refreshCurveReferences()
    {
        // The "final" curve = the edit target's own curve composed with its ancestor
        // groups' (effectiveEditParams). Recomputed here so it tracks every live edit.
        const EditParams eff = effectiveParams();
        mMixer->setReference(eff.mixer);
        mCurve->setReferenceCurves(eff.curve, eff.curveChannel);
    }

    int RightColumn::activeTab() const { return mTabs->selectedIndex(); }
    double RightColumn::aspectLock() const { return mXform ? mXform->lockedRatio() : 0.0; }
    void RightColumn::setWhiteBalancePickArmed(bool armed)
    {
        if (mBasicDetail) mBasicDetail->setSectionActionArmed("COLOUR", armed);
    }
    bool RightColumn::maskTabActive() const { return mTabs->selectedIndex() == kTabMask; }

    const MaskParams *RightColumn::selectedMaskParams() const
    {
        const EditParams *p = params();
        if (!p || mSelectedMask < 0 || mSelectedMask >= (int)p->masks.size()) return nullptr;
        return &p->masks[mSelectedMask];
    }

    void RightColumn::writeCrop(double x, double y, double w, double h)
    {
        // ONE key, `crop=x,y,w,h` — the blob `EditParamsIO` reads and writes. There is no
        // `cropX` key, and four separate keys are rejected with "no field matched", silently:
        // a drag then looks like it does nothing at all. Sent through `sendSet` for the same
        // reason `writeSelectedMask` is — it is the path with the direct-to-service fallback.
        sendSet({{"crop", num(x) + "," + num(y) + "," + num(w) + "," + num(h)}});
        // Keep the panel's own copy in step, so its readouts and a following ratio pick work
        // from what is actually on screen rather than from what was there before the drag.
        XformPanel::State st;
        if (const EditParams *p = params())
        {
            st.rotation = p->rotation; st.quarterTurns = p->quarterTurns;
        }
        st.cropX = (float)x; st.cropY = (float)y; st.cropW = (float)w; st.cropH = (float)h;
        st.sourceWidth = mSvc.model().sourceWidth;
        st.sourceHeight = mSvc.model().sourceHeight;
        mXform->setState(st);
    }

    void RightColumn::writeSelectedMask(const MaskParams &m)
    {
        // The on-photo overlay hands back the FULL current mask each frame — the dragged
        // geometry plus the panel-owned fields — so this writes the whole thing back. It
        // needed `dabs` in the command set to be expressible at all (a Brush drag's only
        // product is dabs); `set mask=` could not stand in, because it appends and would move
        // the mask to the end of a stack whose order is what it renders.
        //
        // No setMasks() afterwards: that would fight a live drag. The mask count is unchanged,
        // so the panel stays in sync without being re-pushed.
        if (mSelectedMask < 0) return;
        cosmo::Command c;
        c.kind = cosmo::Command::Kind::MaskSet;
        c.index = mSelectedMask;
        c.fields = editcmd::maskFields(m);
        if (emitCommand(c)) return;
        mSvc.dispatch(c);   // unwired: the service is right here (see sendSet)
    }

    void RightColumn::scrollActivePanel(double delta)
    {
        switch (mTabs->selectedIndex())
        {
            case kTabBasicDetail: mBasicDetail->scrollBy(delta); break;
            case kTabMask:        mMask->scrollBy(delta); break;
            case kTabColor:       mColorTab->scrollBy(delta); break;
            case kTabGrade:       mGrade->scrollBy(delta); break;
            default: break;  // Xform is fixed-height (no scroll)
        }
    }

    void RightColumn::layout()
    {
        const double w = width.value(), h = height.value();
        mHistogram->x.set(0.0); mHistogram->y.set(0.0);
        mHistogram->width.set(w);

        const double actionBarH = ActionBar::kHeight;
        const double tabsY = HistogramWidget::kHeight;
        const double tabsH = h - tabsY - actionBarH;
        mTabs->x.set(0.0); mTabs->y.set(tabsY);
        mTabs->width.set(w); mTabs->height.set(std::max(0.0, tabsH));
        mTabs->layoutPages();  // sizes/positions/visibility of all pages

        // Each concrete panel lays out its own internals at the size the tab stack gave
        // it; the merged Mixer/Curve stack lays out its two children via callbacks.
        mBasicDetail->layout(); mMask->layout(); mColorTab->layout(); mGrade->layout(); mXform->layout();

        mActionBar->x.set(0.0); mActionBar->y.set(h - actionBarH);
        mActionBar->width.set(w);
        mActionBar->layout();
    }

    void RightColumn::onPaint(IRenderTarget &t) const
    {
        // The card surface behind the whole column (Figma's `bg-card`): the
        // panel body shows it directly and the active tab is filled with the same
        // card colour, so the highlighted tab blends into the editing section.
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(palette::card()));
    }

    // ── R-BYPASS-4: the "filter disabled" scrim ──

    void RightColumn::setBypassed(bool on)
    {
        if (mBypassed == on) return;
        mBypassed = on;
        // Eased, never a flip (R-G-1); AnimatedProperty collapses this to the end
        // state by itself under artboard::reducedMotion().
        mDim.animateTo(on ? 1.0 : 0.0, kDimMs, Easing::EaseInOutCubic, mLastMs);
    }

    void RightColumn::advance(double nowMs)
    {
        mLastMs = nowMs;
        mDim.update(nowMs);
        Segment::advance(nowMs);
    }

    Rect RightColumn::editStackRect() const
    {
        // Exactly the band the tab strip + panel body occupy: from the histogram's
        // bottom edge down to the top of the pinned action bar. The histogram and the
        // action bar are deliberately left undimmed (R-BYPASS-4).
        const double top = HistogramWidget::kHeight;
        const double bottom = std::max(top, height.value() - ActionBar::kHeight);
        return Rect{0.0, top, width.value(), bottom - top};
    }

    void RightColumn::onOverlay(IRenderTarget &t) const
    {
        const double a = mDim.value();
        if (a <= 0.001) return;
        const Rect band = editStackRect();
        if (band.h <= 0.0 || band.w <= 0.0) return;

        // Dark wash over the whole edit stack: the sliders/labels underneath stay
        // visible but read as inert, which is the point -- the values are still there,
        // they just are not being applied.
        drawRoundedRect(t, band, 0.0, Paint::filled(Color{0.02, 0.02, 0.02, kDimAlpha * a}));

        // A small, non-interactive pill naming the state, centred just BELOW the tab
        // strip so it never overlaps a tab label.
        const double tw = estimateTextWidth(kPillLabel, kPillFontPx);
        const double pw = tw + 2.0 * kPillPadX;
        const double pillY = band.y + mTabs->tabHeight + kPillTop;
        if (pillY + kPillH > band.y + band.h) return;   // too short to place it: scrim only
        const Rect pill{band.x + (band.w - pw) * 0.5, pillY, pw, kPillH};
        Color pillBg = palette::secondary(); pillBg.a *= a;
        Color pillBorder = palette::border(); pillBorder.a *= a;
        drawRoundedRect(t, pill, radius::control(), Paint::filledStroked(pillBg, pillBorder, 1.0));
        icon::ban(t, Rect{pill.x + kPillPadX - 2.0, pill.y + (kPillH - 9.0) * 0.5, 9.0, 9.0},
                  palette::whiteAlpha(0.55 * a), 1.1);
        // Near-white on the dark scrim rather than mutedForeground -- the wash sits
        // under it, so the muted grey would drop below a legible contrast (WCAG AA).
        t.setFill(palette::whiteAlpha(0.66 * a));
        t.drawText(kPillLabel, pill.x + kPillPadX + 11.0, pill.y + kPillH * 0.5 + 3.0, kPillFontPx, font::sansMedium());
    }
}
}
