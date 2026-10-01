#include "GradeInspector.h"
#include "CommandLine.h"
#include "TextFit.h"
#include "../../../cosmo/widgets/UnitConversions.h"
#include "../../../cosmo/widgets/Icons.h"
#include <algorithm>

namespace arstro
{
namespace interstellar_v1
{
    using namespace artboard;
    using namespace cosmo_v2;   // the panels and their unit conversions are cosmo's

    namespace
    {
        // cosmo's RightColumn constants for the merged Mixer/Curve stack, verbatim
        constexpr double kMixerStackH = 250.0;
        constexpr double kCurveStackH = 235.0;
        constexpr double kPillH = 19.0, kPillPx = 9.0, kPillPadX = 9.0;
        constexpr double kDimAlpha = 0.62;   // cosmo's bypass scrim strength
        constexpr double kNotchPx = 1.0;     // gesture deltas already arrive in pixels
    }

    GradeInspector::GradeInspector()
    {
        clipToBounds = true;
        mHistogram = std::make_shared<HistogramWidget>();
        addChild(mHistogram);

        mTabs = std::make_shared<EditStackTabs>();
        mTabs->tabHeight = 27.0;

        // A control → one `set` line. `basic` / `detail` split exactly where cosmo's own Basic
        // and Detail sections split; the keys and the unit conversions are RightColumn's.
        auto set = [this](const char *filter, const char *key, double (*toEngine)(double) = nullptr) {
            return [this, filter, key, toEngine](double v) {
                send(filter, {{key, cmd::num(toEngine ? toEngine(v) : v)}});
            };
        };
        std::vector<ParamPanel::Section> sections = {
            {"TONE", {
                {"Exposure", -400, 400, set("basic", "exposure", toEv)},
                {"Contrast", -100, 100, set("basic", "contrast")},
                {"Highlights", -100, 100, set("basic", "highlights")},
                {"Shadows", -100, 100, set("basic", "shadows")},
                {"Whites", -100, 100, set("basic", "whites")},
                {"Blacks", -100, 100, set("basic", "blacks")},
            }},
            {"COLOUR", {
                {"Temperature", -100, 100, set("basic", "temp", toKelvin),
                 true, Color::rgba(74, 132, 232), Color::rgba(240, 178, 84)},    // cosmo's own ramp ends
                {"Tint", -100, 100, set("basic", "tint", toTint),
                 true, Color::rgba(88, 196, 118), Color::rgba(206, 104, 196)},
                {"Vibrance", -100, 100, set("basic", "vibrance")},
                {"Saturation", -100, 100, set("basic", "saturation")},
            }},
            {"PRESENCE", {
                {"Texture", -100, 100, set("basic", "texture")},
                {"Clarity", -100, 100, set("basic", "clarity")},
                {"Dehaze", 0, 100, set("basic", "dehaze")},
            }},
            {"EFFECTS", {
                {"Grain Amount", 0, 100, set("basic", "grainAmount")},
                {"Grain Size", 0, 100, set("basic", "grainSize")},
            }},
            {"SHARPENING", {
                {"Amount", 0, 150, set("detail", "sharpenAmount")},
                {"Radius", 5, 30, set("detail", "sharpenRadius", toRadiusPx)},
                {"Masking", 0, 100, set("detail", "sharpenMasking")},
            }},
            {"NOISE REDUCTION", {
                {"Luminance", 0, 100, set("detail", "nrLuminance")},
                {"Colour", 0, 100, set("detail", "nrColor")},
            }},
            {"LENS", {
                {"Distortion", -100, 100, set("detail", "lensDistortion")},
                {"Defringe", 0, 100, set("detail", "lensCA")},
                {"Vignette", -100, 100, set("detail", "lensVignette")},
            }},
        };
        mBasicDetail = std::make_shared<ParamPanel>(std::move(sections));
        mTabs->addPage("Basic/Detail", mBasicDetail);

        mMixer = std::make_shared<MixerPanel>();
        mMixer->onCurveChange = [this](int channel, std::vector<CurvePoint> pts) {
            send("mixer", {{"mixer" + std::to_string(channel), cmd::points(pts)}});
        };
        mCurve = std::make_shared<CurvePanel>();
        mCurve->onCurveChange = [this](int channel, CurvePanel::Points pts) {
            static const char *kChannelKey[3] = {"curveR", "curveG", "curveB"};
            send("curve", {{channel == 0 ? "curve" : kChannelKey[channel - 1], cmd::points(pts)}});
        };
        mColorTab = std::make_shared<StackPanel>();
        mColorTab->addItem(mMixer, kMixerStackH, [this] { mMixer->layout(); });
        mColorTab->addItem(mCurve, kCurveStackH, [this] { mCurve->layout(); });
        mTabs->addPage("Mixer/Curve", mColorTab);

        mGrade = std::make_shared<GradePanel>();
        mGrade->onRegionChange = [this](int region, double hue, double sat, double lum) {
            send("grade", {{"grade" + std::to_string(region), cmd::num(hue) + "," + cmd::num(sat) + "," + cmd::num(lum)}});
        };
        mGrade->onBalanceChange = [this](double v) { send("grade", {{"balance", cmd::num(v)}}); };
        mGrade->onRemapEnableChange = [this](bool on) { send("grade", {{"remapEnable", on ? "1" : "0"}}); };
        mGrade->onRemapChange = [this](double src, double range, double dst, double strength) {
            send("grade", {{"remapSrc", cmd::num(src)}, {"remapRange", cmd::num(range)},
                           {"remapDst", cmd::num(dst)}, {"remapStrength", cmd::num(strength / 100.0)}});
        };
        mTabs->addPage("Grade", mGrade);

        mXform = std::make_shared<XformPanel>();
        mXform->onRotationChange = [this](double deg) { send("xform", {{"rotation", cmd::num(deg)}}); };
        mXform->onQuarterTurn = [this](int dir) {
            // the button says "turn by dir"; the line says what the value BECOMES
            send("xform", {{"quarterTurns", cmd::num(((mQuarterTurns + dir) % 4 + 4) % 4)}});
        };
        mXform->onResetRotation = [this] { send("xform", {{"rotation", "0"}}); };
        mXform->onCropChange = [this](double x, double y, double w, double h) {
            send("xform", {{"crop", cmd::num(x) + "," + cmd::num(y) + "," + cmd::num(w) + "," + cmd::num(h)}});
        };
        mTabs->addPage("Xform", mXform);
        addChild(mTabs);
    }

    void GradeInspector::send(const char *filter, const Fields &fields)
    {
        if (mBind.empty() || !onCommand) return;
        onCommand(cmd::gradeSet(mBind, filter, fields));
    }

    void GradeInspector::bind(const interstellar::AppModel &m, bool interacting)
    {
        const bool valid = m.hasGradeTarget && m.selectedRack >= 0 && m.selectedRack < (int)m.rack.size();
        mHasTarget = valid;
        mRackEmpty = m.rack.empty();
        mBind = valid ? m.rack[m.selectedRack].bindName : std::string();
        mBypassed = valid && m.rack[m.selectedRack].bypass;
        mTabs->enabled = valid;   // nothing to edit: the panels take no input (the wash says why)
        if (!valid) return;
        if (mBind != mLastBind)
        {
            if (!mLastBind.empty()) mSwapPending = true;   // a different node: cross-fade the page
            mLastBind = mBind;
            syncPanels(m);
            return;
        }
        if (!interacting) syncPanels(m);
    }

    void GradeInspector::syncPanels(const interstellar::AppModel &m)
    {
        const EditParams &p = m.gradeOwnParams;   // what an edit changes
        const EditParams &eff = m.gradeParams;    // the stacked reach (own + ancestor groups)
        mQuarterTurns = p.quarterTurns;
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
        const std::vector<double> own = flat(p), effv = flat(eff);
        std::vector<double> offsets(own.size(), 0.0);
        for (size_t i = 0; i < own.size(); ++i) offsets[i] = effv[i] - own[i];
        mBasicDetail->setValues(own);
        mBasicDetail->setSubValues(offsets);
        mMixer->setMixer(p.mixer);
        mMixer->setReference(eff.mixer);
        mCurve->setCurves(p.curve, p.curveChannel);
        mCurve->setReferenceCurves(eff.curve, eff.curveChannel);
        GradePanel::State gs;
        gs.grade = p.grade; gs.balance = p.balance; gs.remapEnable = p.remapEnable;
        gs.remapSrc = p.remapSrc; gs.remapRange = p.remapRange; gs.remapDst = p.remapDst; gs.remapStrength = p.remapStrength;
        mGrade->setState(gs);
        XformPanel::State xs;
        xs.rotation = p.rotation; xs.quarterTurns = p.quarterTurns;
        xs.cropX = p.cropX; xs.cropY = p.cropY; xs.cropW = p.cropW; xs.cropH = p.cropH;
        xs.sourceWidth = m.sourceWidth;
        xs.sourceHeight = m.sourceHeight;
        mXform->setState(xs);
    }

    Rect GradeInspector::stackRect() const
    {
        const double top = HistogramWidget::kHeight;
        return Rect{0, top, width.value(), std::max(0.0, height.value() - top)};
    }

    void GradeInspector::layout()
    {
        const double w = width.value(), h = height.value();
        mHistogram->x.set(0.0); mHistogram->y.set(0.0); mHistogram->width.set(w);
        const double tabsY = HistogramWidget::kHeight;
        mTabs->x.set(0.0); mTabs->y.set(tabsY);
        mTabs->width.set(w); mTabs->height.set(std::max(0.0, h - tabsY));
        mTabs->layoutPages();
        mBasicDetail->layout(); mColorTab->layout(); mGrade->layout(); mXform->layout();
    }

    bool GradeInspector::handleGesture(const Gesture &g, const Point &local)
    {
        if (g.type == Gesture::Type::Scroll)
        {
            // Cosmo's panels scroll through `scrollBy(+ = toward the top)`; nothing in them handles
            // the gesture, so it bubbles here and is routed to the page that is showing.
            const double d = -g.delta.y * kNotchPx;
            switch (mTabs->selectedIndex())
            {
            case kTabBasicDetail: mBasicDetail->scrollBy(d); break;
            case kTabColor: mColorTab->scrollBy(d); break;
            case kTabGrade: mGrade->scrollBy(d); break;
            default: break;   // Xform is fixed-height, as in cosmo
            }
            return true;
        }
        return Segment::handleGesture(g, local);
    }

    void GradeInspector::advance(double nowMs)
    {
        const bool empty = !mHasTarget;
        if (!mInit)
        {
            mEmptyAmt.set(empty ? 1.0 : 0.0);
            mEmptyApplied = empty;
            mBypassAmt.set(mBypassed ? 1.0 : 0.0);
            mBypassApplied = mBypassed;
            mInit = true;
        }
        if (empty != mEmptyApplied)
        {
            mEmptyAmt.animateTo(empty ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mEmptyApplied = empty;
        }
        if (mBypassed != mBypassApplied)
        {
            mBypassAmt.animateTo(mBypassed ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseInOutCubic, nowMs);   // cosmo's scrim easing
            mBypassApplied = mBypassed;
        }
        if (mSwapPending)
        {
            mSwap.set(0.0);
            mSwap.animateTo(1.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mSwapPending = false;
        }
        mEmptyAmt.update(nowMs);
        mBypassAmt.update(nowMs);
        mSwap.update(nowMs);
        Segment::advance(nowMs);
    }

    void GradeInspector::onPaint(IRenderTarget &t) const
    {
        drawRoundedRect(t, Rect{0, 0, width.value(), height.value()}, 0.0, Paint::filled(palette::card()));
        t.setStroke(palette::border(), 1.0);
        t.beginPath(); t.moveTo(0.5, 0); t.lineTo(0.5, height.value()); t.strokePath();
    }

    void GradeInspector::onOverlay(IRenderTarget &t) const
    {
        const Rect band = stackRect();
        const Rect page{band.x, band.y + mTabs->tabHeight, band.w, std::max(0.0, band.h - mTabs->tabHeight)};
        t.save();
        t.clipRect(0, 0, width.value(), height.value());
        // a different node selected: the page re-seats under a card wash that eases away
        const double sw = 1.0 - mSwap.value();
        if (sw > 0.001)
        {
            Color c = palette::card(); c.a *= sw * 0.9;
            drawRoundedRect(t, page, 0.0, Paint::filled(c));
        }
        // the selected node is bypassed: cosmo's own scrim + pill (R-BYPASS-4's look)
        const double ba = mBypassAmt.value() * (1.0 - mEmptyAmt.value());
        if (ba > 0.001 && page.h > 0.0)
        {
            drawRoundedRect(t, page, 0.0, Paint::filled(surface::scrim(kDimAlpha * ba)));
            const std::string label = "NODE BYPASSED";
            const double tw = t.measureText(label, kPillPx, font::sansMedium());
            const double pw = tw + 2 * kPillPadX + 11.0;
            const Rect pill{page.x + (page.w - pw) * 0.5, page.y + 8.0, pw, kPillH};
            Color bg = palette::secondary(); bg.a *= ba;
            Color bd = palette::border(); bd.a *= ba;
            drawRoundedRect(t, pill, radius::control(), Paint::filledStroked(bg, bd, 1.0));
            icon::ban(t, Rect{pill.x + kPillPadX - 2.0, pill.y + (kPillH - 9.0) * 0.5, 9.0, 9.0}, palette::whiteAlpha(0.55 * ba), 1.1);
            t.setFill(palette::whiteAlpha(0.66 * ba));
            t.drawText(label, pill.x + kPillPadX + 11.0, textfit::baseline(pill.y + kPillH * 0.5, kPillPx), kPillPx, font::sansMedium());
        }
        // nothing to grade: say what to do, over the whole stack
        const double ea = mEmptyAmt.value();
        if (ea > 0.001 && band.h > 0.0)
        {
            Color c = palette::card(); c.a *= ea;
            drawRoundedRect(t, band, 0.0, Paint::filled(c));
            const std::string a = mRackEmpty ? "Nothing to grade yet." : "Nothing selected to grade.";
            const std::string b = mRackEmpty ? "Add footage to the rack to start grading." : "Pick a source or a group in the rack.";
            Color f = palette::foreground(); f.a *= ea;
            Color mf = palette::mutedForeground(); mf.a *= ea;
            const double cy = band.y + band.h * 0.38;
            t.setFill(f);
            t.drawText(a, band.x + (band.w - t.measureText(a, 12.0, font::sansMedium())) * 0.5, cy, 12.0, font::sansMedium());
            t.setFill(mf);
            const std::string bb = textfit::ellipsize(t, b, band.w - 24.0, 11.0, font::sans());
            t.drawText(bb, band.x + (band.w - t.measureText(bb, 11.0, font::sans())) * 0.5, cy + 18.0, 11.0, font::sans());
        }
        t.restore();
    }
}
}
