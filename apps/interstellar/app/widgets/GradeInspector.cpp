#include "GradeInspector.h"
#include "KeyState.h"
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
        // cosmo's XformPanel laid out top to bottom: four section headers (27.95), the rotate row, the
        // ratio chips, the Custom ratio row when it shows (20), the flip and auto rows, their gaps and
        // one ladder-3 pad under the last row — the height it needs, so the page can scroll to its end
        constexpr double kXformStackH = 260.0;
        // R-UI-16a: the room the sliders must keep (a section header and about six rows). Under it the
        // IMAGE PROCESSING list folds to its selected row.
        constexpr double kMinPageH = 150.0;
        constexpr double kPillH = 19.0, kPillPx = 9.0, kPillPadX = 9.0;
        constexpr double kDimAlpha = 0.62;   // cosmo's bypass scrim strength
        constexpr double kNotchPx = 1.0;     // gesture deltas already arrive in pixels
    }

    GradeInspector::GradeInspector()
    {
        clipToBounds = true;
        mScopes = std::make_shared<ScopePanel>();
        addChild(mScopes);

        mTabs = std::make_shared<EditStackTabs>();
        mTabs->tabHeight = 27.0;

        // A control → one `set` line. `basic` / `detail` split exactly where cosmo's own Basic
        // and Detail sections split; the keys and the unit conversions are RightColumn's.
        auto set = [this](const char *filter, const char *key, double (*toEngine)(double) = nullptr) {
            mRowKeys.push_back(std::string(filter) + "." + key);   // row i's colour key, for its mark (R-ANIM-9)
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
        // R-ANIM-9: every row's mark — marks it to animate (an offset: nothing on screen changes), or, on a
        // marked one, its menu (the mode, Remove Animation)
        mBasicDetail->setKeyColumn([this](int row) {
            if (mBind.empty() || row < 0 || row >= (int)mRowKeys.size() || row >= (int)mRowStates.size()) return;
            const std::string address = mBind + "." + mRowKeys[(size_t)row];
            if (mRowStates[(size_t)row] == 0) { if (onCommand) onCommand(keys::mark(address)); return; }
            if (auto r = mBasicDetail->row(row); r && onMarkMenu)
            {
                const Rect k = r->keyRect();
                onMarkMenu(address, r->worldTransform().apply(Point{k.x + k.w * 0.5, k.bottom()}));
            }
        });
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
        // R-UI-16a: cosmo's Xform page has no scroll of its own (in cosmo the column is always tall
        // enough); here it sits in cosmo's StackPanel, as Mixer/Curve does, so a short column scrolls it
        mXformTab = std::make_shared<StackPanel>();
        mXformTab->addItem(mXform, kXformStackH, [this] { mXform->layout(); });
        mTabs->addPage("Xform", mXformTab);
        addChild(mTabs);

        // R-FX-5: the node's plugin stack, Cosmo first; the selected row decides the panel below
        mPlugins = std::make_shared<PluginList>();
        mPlugins->onCommand = [this](const std::string &l) { if (onCommand) onCommand(l); };
        mPlugins->onSelect = [this](const std::string &id) {
            mEffectWanted = !id.empty();
            if (mLastModel) mEffectPanel->bind(*mLastModel, id, false);   // the panel shows it at once
        };
        mPlugins->onAdd = [this](Rect r) { if (onAddEffect) onAddEffect(r); };
        mPlugins->onRowContext = [this](const std::string &id, Point w) { if (onPluginContext) onPluginContext(id, w); };
        addChild(mPlugins);
        mEffectPanel = std::make_shared<EffectPanel>();
        mEffectPanel->onCommand = [this](const std::string &l) { if (onCommand) onCommand(l); };
        mEffectPanel->onMarkMenu = [this](const std::string &a, Point w) { if (onMarkMenu) onMarkMenu(a, w); };
        mEffectPanel->visible = false;
        addChild(mEffectPanel);
        // Cosmo's Mix is the node's weight (R-RACK-4, amended): one slider here, not a bar per row
        mCosmoMix = std::make_shared<SliderRow>("Mix", 0.0, 100.0, 100.0);
        mCosmoMix->onChange = [this](double v) {
            if (!mBind.empty() && onCommand) onCommand("set " + cmd::quote(mBind + ".weight=" + cmd::num(v / 100.0)));
        };
        addChild(mCosmoMix);
        // R-UI-18a: LAST, so it takes the click above the pages it covers
        mPinPill = std::make_shared<StatePill>();
        mPinPill->onAction = [this] { if (!mPinTimeline.empty() && onCommand) onCommand("timeline unpin " + cmd::quote(mPinTimeline)); };
        mPinPill->opacity.set(0.0);
        addChild(mPinPill);
    }

    double GradeInspector::tabsTop() const
    {
        return scopeHeight() + mPlugins->wantedHeight() + SliderRow::kRowHeight + 13.0;   // the Mix row and its gaps
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
        // R-UI-18a: a pinned version's colour is read-only (R-VER-3) — the PAGES take no input and say
        // why; the tab bar stays live, so a pinned grade can still be read
        mPinned = false;
        for (const auto &tl : m.timelines)
            if (tl.id == m.currentTimeline && tl.colourPinned)
            {
                mPinned = valid;
                mPinTimeline = tl.id;
                mPinPill->setText("COLOUR PINNED" + (tl.pinCommit.empty() ? std::string() : " @" + tl.pinCommit.substr(0, 7)), "Unpin");
            }
        for (Segment *page : {(Segment *)mBasicDetail.get(), (Segment *)mColorTab.get(), (Segment *)mGrade.get(), (Segment *)mXformTab.get()})
            page->enabled = !mPinned;
        mLastModel = &m;   // the service's model outlives every frame; a selection re-binds from it
        mScopes->setSourceBits(valid ? m.rack[(size_t)m.selectedRack].mediaBitDepth : 0);
        mPlugins->bind(m);
        mEffectWanted = !mPlugins->selected().empty();
        mEffectPanel->bind(m, mPlugins->selected(), interacting);
        if (valid && !interacting) mCosmoMix->setValue(m.rack[(size_t)m.selectedRack].weight * 100.0);
        if (!valid) return;
        // the marks follow the model even mid-gesture: they say what is animated, not a value
        {
            const std::string ro = m.rack[(size_t)m.selectedRack].rackObj;
            mRowStates.assign(mRowKeys.size(), 0);
            for (size_t i = 0; i < mRowKeys.size(); ++i) mRowStates[i] = keys::animOf(m, ro, mRowKeys[i]) ? 2 : 0;
            mBasicDetail->setKeyStates(mRowStates);
        }
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
        const double top = tabsTop();
        return Rect{0, top, width.value(), std::max(0.0, height.value() - top)};
    }

    void GradeInspector::layout()
    {
        // scopes · the plugin list (its height eases with its rows) · then Cosmo's Mix and tabs
        // OR the selected effect's panel, cross-faded through one live amount
        const double w = width.value(), h = height.value();
        // R-UI-16a: the scopes take a share of the column, and the list folds to its selected row when
        // the whole of it would leave the sliders less than kMinPageH — derived every layout from the
        // room there is (design rule R4), and eased by the list itself
        const double scopeH = scopeHeight();
        mPlugins->setFolded(h - scopeH - mPlugins->fullHeight() - (SliderRow::kRowHeight + 13.0) - mTabs->tabHeight < kMinPageH);
        mScopes->x.set(0.0); mScopes->y.set(0.0); mScopes->width.set(w); mScopes->height.set(scopeH);
        mScopes->layout();
        const double listY = scopeH, listH = mPlugins->wantedHeight();
        mPlugins->x.set(0.0); mPlugins->y.set(listY); mPlugins->width.set(w); mPlugins->height.set(listH);
        const double f = mPluginFade.value();
        const double below = listY + listH;
        mCosmoMix->x.set(9.75); mCosmoMix->y.set(below + 6.5);
        mCosmoMix->width.set(std::max(0.0, w - 19.5)); mCosmoMix->height.set(SliderRow::kRowHeight);
        mCosmoMix->layout();
        mCosmoMix->opacity.set(1.0 - f);
        mCosmoMix->visible = f < 0.999;
        const double tabsY = tabsTop();
        mTabs->x.set(0.0); mTabs->y.set(tabsY);
        mTabs->width.set(w); mTabs->height.set(std::max(0.0, h - tabsY));
        mTabs->opacity.set(1.0 - f);
        mTabs->visible = f < 0.999;
        mTabs->layoutPages();
        mBasicDetail->layout(); mColorTab->layout(); mGrade->layout(); mXformTab->layout();
        mColorTab->scrollBy(0.0); mXformTab->scrollBy(0.0);   // re-clamp: a taller column must not leave a gap under the last row
        mEffectPanel->x.set(0.0); mEffectPanel->y.set(below);
        mEffectPanel->width.set(w); mEffectPanel->height.set(std::max(0.0, h - below));
        mEffectPanel->opacity.set(f);
        mEffectPanel->visible = f > 0.001;
        mEffectPanel->layout();
        // R-UI-18a: the pin pill over the top of Cosmo's page — under the bypass pill when both show,
        // through the bypass amount so it slides rather than jumps
        const double pa = mPinAmt.value() * (1.0 - mEmptyAmt.value()) * (1.0 - f);
        mPinPill->x.set(0.0); mPinPill->width.set(w);
        mPinPill->y.set(tabsY + mTabs->tabHeight + 8.0 + (kPillH + 6.5) * mBypassAmt.value());
        mPinPill->height.set(StatePill::kHeight);
        mPinPill->opacity.set(pa);
        mPinPill->visible = pa > 0.001 && mPinPill->y.value() + StatePill::kHeight <= h;   // an overlay is unclipped: never past the column
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
            case kTabXform: mXformTab->scrollBy(d); break;   // R-UI-16a: every page scrolls
            default: break;
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
            mPinAmt.set(mPinned ? 1.0 : 0.0);
            mPinApplied = mPinned;
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
        if (mPinned != mPinApplied)
        {
            mPinAmt.animateTo(mPinned ? 1.0 : 0.0, motion::kScrollMs, Easing::EaseInOutCubic, nowMs);   // the bypass scrim's own easing
            mPinApplied = mPinned;
        }
        mPinAmt.update(nowMs);
        if (mSwapPending)
        {
            mSwap.set(0.0);
            mSwap.animateTo(1.0, motion::kScrollMs, Easing::EaseOutCubic, nowMs);
            mSwapPending = false;
        }
        if (mEffectWanted != mEffectApplied)
        {
            mPluginFade.animateTo(mEffectWanted ? 1.0 : 0.0, motion::kCrossFadeMs, Easing::EaseOutCubic, nowMs);
            mEffectApplied = mEffectWanted;
        }
        mPluginFade.update(nowMs);
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
        const Rect stack = stackRect();
        const Rect page{stack.x, stack.y + mTabs->tabHeight, stack.w, std::max(0.0, stack.h - mTabs->tabHeight)};
        const double cosmoShown = 1.0 - mPluginFade.value();   // the Cosmo page's washes go with it
        const Rect band{0, scopeHeight(), width.value(), std::max(0.0, height.value() - scopeHeight())};
        t.save();
        t.clipRect(0, 0, width.value(), height.value());
        // a different node selected: the page re-seats under a card wash that eases away
        const double sw = (1.0 - mSwap.value()) * cosmoShown;
        if (sw > 0.001)
        {
            Color c = palette::card(); c.a *= sw * 0.9;
            drawRoundedRect(t, page, 0.0, Paint::filled(c));
        }
        // the selected node is bypassed: cosmo's own scrim + pill (R-BYPASS-4's look)
        const double ba = mBypassAmt.value() * (1.0 - mEmptyAmt.value()) * cosmoShown;
        // …or the version's colour is pinned (R-UI-18a): ONE scrim for both, never two stacked
        const double pin = mPinAmt.value() * (1.0 - mEmptyAmt.value()) * cosmoShown;
        if (std::max(ba, pin) > 0.001 && page.h > 0.0)
            drawRoundedRect(t, page, 0.0, Paint::filled(surface::scrim(kDimAlpha * std::max(ba, pin))));
        if (ba > 0.001 && page.h > 0.0)
        {
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
            // an empty project is said ONCE, in the monitor (R-UI-21d): here the column is simply quiet
            const std::string a = mRackEmpty ? "" : "Nothing selected to grade.";
            const std::string b = mRackEmpty ? "" : "Pick a source or a group in the rack.";
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
