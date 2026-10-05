/*
 *  interstellar_v1 — GradeInspector: the Grade tab's right column — COSMO'S OWN panels, linked.
 *
 *  "The tab that must feel exactly like cosmo, because it IS cosmo" (ui-brief §3). The histogram,
 *  the edit-stack tabs and the four panels behind them are cosmo's classes, compiled from
 *  the .cpp files in apps/cosmo/widgets/ (R-UI-5): `HistogramWidget`, `EditStackTabs`, `ParamPanel`,
 *  `StackPanel` + `MixerPanel` + `CurvePanel`, `GradePanel`, `XformPanel`. They draw purple-pink
 *  because the accent slot moved, not because anything was forked.
 *
 *  This class replicates what cosmo's `RightColumn` does for them — and nothing else, because
 *  RightColumn itself cannot be reused: it is built over `cosmo::CosmoService` (cosmo_core) and
 *  emits `cosmo::Command` structs. Here the same wiring has two different ends:
 *
 *   * IN:  `bind()` pushes `AppModel::gradeOwnParams` (what an edit changes) into the panels and
 *          `gradeParams - gradeOwnParams` as the green stacked reach / the faint reference curves
 *          — exactly RightColumn::syncToSlot's own/effective split, from the frozen model.
 *   * OUT: every callback becomes ONE text line, `set <bind>.<filter>.<key>=<value>`, with the
 *          same EditParamsIO keys and the same UI→engine unit conversions RightColumn and
 *          cosmo's EditCommands use (toEv, toKelvin, toTint, toRadiusPx; remap strength /100).
 *
 *  Mask is not here: the grade address grammar has no mask filter, and cosmo's MaskPanel needs its
 *  on-photo overlay to be usable — see NOTES.md. The white-balance eyedropper is dropped for the
 *  same reason (it samples a click on the photo).
 *
 *  Above the panels, the node's IMAGE PROCESSING list (R-FX-5, `PluginList`): Cosmo first — its row
 *  selected shows a Mix slider (the node's weight, which used to be a bar on every rack row,
 *  R-RACK-4 amended) and cosmo's own tabs; an effect's row selected cross-fades to its parameters
 *  (`EffectPanel`). Which row is selected is presentation; the switches, the mix and every slider
 *  are command lines.
 *
 *  States: no grade target → an eased card wash with a sentence naming what to do; the selected
 *  node BYPASSED → cosmo's own scrim-and-pill treatment; a change of selected node → the page
 *  cross-fades (cosmo's EditStackTabs page-swap idiom), since cosmo's sliders take a programmatic
 *  value without easing and a node switch would otherwise re-seat twenty rows in one frame.
 *  While the pointer is down in here, `bind` does not re-seed the panels: a gesture in flight
 *  outranks the model (gotcha 9).
 */
#pragma once
#include "../Theme.h"
#include "../AppHooks.h"
#include "../../../cosmo/widgets/HistogramWidget.h"
#include "../../../cosmo/widgets/EditStackTabs.h"
#include "../../../cosmo/widgets/ParamPanel.h"
#include "../../../cosmo/widgets/MixerPanel.h"
#include "../../../cosmo/widgets/CurvePanel.h"
#include "../../../cosmo/widgets/GradePanel.h"
#include "../../../cosmo/widgets/XformPanel.h"
#include "../../../cosmo/widgets/StackPanel.h"
#include "../../../cosmo/widgets/SliderRow.h"
#include "PluginList.h"
#include "ScopePanel.h"
#include "EffectPanel.h"
#include <functional>
#include <memory>
#include <string>

namespace arstro
{
namespace interstellar_v1
{
    class GradeInspector : public artboard::Segment
    {
    public:
        static constexpr int kTabBasicDetail = 0, kTabColor = 1, kTabGrade = 2, kTabXform = 3;

        GradeInspector();

        /** `interacting`: the pointer is down inside this column — do not re-seed the panels. */
        void bind(const interstellar::AppModel &m, bool interacting);
        /** The frame's scopes (R-UI-15) — the SCOPES panel where cosmo's histogram sat, which now
         *  hosts that histogram as one of its modes. */
        void setScopes(const ScopeData &d) { mScopes->setData(d); }
        void layout();

        std::shared_ptr<cosmo_v2::EditStackTabs> tabs() { return mTabs; }
        std::shared_ptr<cosmo_v2::ParamPanel> basicDetail() { return mBasicDetail; }
        std::shared_ptr<cosmo_v2::MixerPanel> mixer() { return mMixer; }
        std::shared_ptr<cosmo_v2::CurvePanel> curve() { return mCurve; }
        std::shared_ptr<cosmo_v2::GradePanel> gradePanel() { return mGrade; }
        std::shared_ptr<cosmo_v2::XformPanel> xform() { return mXform; }
        std::shared_ptr<ScopePanel> scopes() { return mScopes; }
        std::shared_ptr<PluginList> plugins() { return mPlugins; }
        std::shared_ptr<EffectPanel> effectPanel() { return mEffectPanel; }
        std::shared_ptr<cosmo_v2::SliderRow> cosmoMix() { return mCosmoMix; }
        /** The LIVE eased swap between Cosmo's panels (0) and the selected effect's (1). */
        double pluginFade() const { return mPluginFade.value(); }
        /** "+ Add" in the plugin list, at a WORLD rect: the screen opens the catalog menu. */
        std::function<void(artboard::Rect world)> onAddEffect;
        /** Right-click on a plugin row ("" = Cosmo) at a WORLD point. */
        std::function<void(const std::string &id, artboard::Point world)> onPluginContext;
        const std::string &target() const { return mBind; }
        double emptyAmount() const { return mEmptyAmt.value(); }
        double bypassAmount() const { return mBypassAmt.value(); }

        std::function<void(const std::string &line)> onCommand;

        void advance(double nowMs) override;

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        void onOverlay(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;

    private:
        using Fields = std::vector<std::pair<std::string, std::string>>;
        /** `set <bind>.<filter>.<k>=<v> …` — nothing leaves without a target. */
        void send(const char *filter, const Fields &fields);
        void syncPanels(const interstellar::AppModel &m);
        artboard::Rect stackRect() const;

        std::shared_ptr<ScopePanel> mScopes;
        std::shared_ptr<cosmo_v2::EditStackTabs> mTabs;
        std::shared_ptr<cosmo_v2::ParamPanel> mBasicDetail;
        std::shared_ptr<cosmo_v2::StackPanel> mColorTab;
        std::shared_ptr<cosmo_v2::MixerPanel> mMixer;
        std::shared_ptr<cosmo_v2::CurvePanel> mCurve;
        std::shared_ptr<cosmo_v2::GradePanel> mGrade;
        std::shared_ptr<cosmo_v2::XformPanel> mXform;
        std::shared_ptr<PluginList> mPlugins;
        std::shared_ptr<EffectPanel> mEffectPanel;
        std::shared_ptr<cosmo_v2::SliderRow> mCosmoMix;     // the node's weight — Cosmo's Mix
        double tabsTop() const;
        bool mEffectWanted = false, mEffectApplied = false;
        artboard::AnimatedProperty mPluginFade{0.0};
        const interstellar::AppModel *mLastModel = nullptr;

        std::string mBind;          // the selected node's bind name — the address prefix
        std::string mLastBind;
        int mQuarterTurns = 0;
        bool mHasTarget = false, mBypassed = false, mRackEmpty = true;
        bool mEmptyApplied = true, mBypassApplied = false, mInit = false;
        bool mSwapPending = false;
        artboard::AnimatedProperty mEmptyAmt{1.0}, mBypassAmt{0.0}, mSwap{1.0};
    };
}
}
