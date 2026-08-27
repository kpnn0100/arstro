/*
 *  cosmo_v2 by arstro — RightColumn: histogram + 7-tab strip + scrollable
 *  panel body + pinned action bar (App.tsx's right column, fixed 292px).
 *  Every control's callback leaves as a Command on `onCommand` (R-SVC-2) — a
 *  slider drag, a scripted `set exposure=1.1` and an agent on the control
 *  socket therefore travel one path and cannot diverge. The session reference
 *  that remains is the READ side (R-SVC-4 lets a view read what it renders),
 *  the direct-write fallback for a column with no service behind it (App::undo's
 *  arrangement, for App::undo's reason), and writeSelectedMask — the one surface
 *  no Command reaches yet, and the comment there says why.
 */
#pragma once
#include "../../../core/Artboard/include/artboard/artboard.h"
#include "../core/EditSession.h"
#include "../core/service/Command.h"
#include "../core/service/CosmoService.h"
#include "HistogramWidget.h"
#include "EditStackTabs.h"
#include "ParamPanel.h"
#include "MaskPanel.h"
#include "MixerPanel.h"
#include "CurvePanel.h"
#include "GradePanel.h"
#include "XformPanel.h"
#include "StackPanel.h"
#include "ActionBar.h"
#include <functional>
#include <memory>

namespace arstro
{
namespace cosmo_v2
{
    class RightColumn : public artboard::Segment
    {
    public:
        // Wider than the Figma mock's 292px so the edit-stack tab labels sit
        // comfortably centred (task point 5: "make the edit stack wider").
        static constexpr double kWidth = 324.0;
        // Merged edit-stack tabs (Basic+Detail, Mask, Mixer+Curve, Grade, Xform).
        static constexpr int kTabBasicDetail = 0, kTabMask = 1, kTabColor = 2, kTabGrade = 3, kTabXform = 4;

        /** S4c: the column reads the MODEL and emits Commands. It holds no `EditSession` at
         *  all — with no App above it (a shot rig, a test) it dispatches straight to the
         *  service, which is the same destination App's channel reaches. */
        explicit RightColumn(cosmo::CosmoService &svc);

        /** R-SVC-2: the column's outbound channel, mirroring `App::onCommand`. Every edit
         *  this panel makes leaves as a Command instead of being written into an EditParams,
         *  so the sliders, the CLI and the control socket are three views of one path — and
         *  every slider becomes addressable by name for free. App wires it to its own
         *  channel; see `emitCommand` for what happens when nobody does. */
        /** Returns whether the command was actually dispatched, so the fallback COMPOSES:
         *  this is a two-hop channel (RightColumn -> App -> service) and a `void` sink would
         *  report success merely because App's own channel had been set, dropping the command
         *  if App's was not wired in turn. The one place in the design where the unwired-
         *  fallback contract does not compose by itself. */
        std::function<bool(cosmo::Command)> onCommand;
        /** Emit `c` if a service is wired, and report whether it was. A false here is what
         *  sends each emitter down its direct-write fallback — the same bargain App::undo
         *  makes, because a widget tree built with no service behind it must still edit. */
        bool emitCommand(const cosmo::Command &c) const { return onCommand && onCommand(c); }

        /** Fires when the edit-stack tab changes, with the new index. The view above uses it
         *  to stop paying for histograms whose panel is not on screen (R-PREVIEW-6); it is a
         *  notification, not a request, so an unwired one is simply silent. */
        std::function<void(int)> onTabChanged;
        /** The crop aspect lock changed (w/h, or 0 for Free) — R-CROP-2. A notification, not a
         *  request: the panel owns which chip is lit, and the crop box on the photo needs to
         *  know so a corner drag keeps the shape. */
        std::function<void(double)> onAspectLockChange;
        /** R-WB-1: the white-balance eyedropper was armed or disarmed. The view above turns the
         *  next click on the photo into a `wb pick`; the panel only owns the button. */
        std::function<void(bool armed)> onWhiteBalancePickArmed;
        /** Turn the eyedropper off — the picker disarms after one sample, so the button follows. */
        void setWhiteBalancePickArmed(bool armed);
        /** The locked ratio right now, for a view that is created after the choice was made. */
        double aspectLock() const;

        std::shared_ptr<HistogramWidget> histogram() { return mHistogram; }
        std::shared_ptr<ActionBar> actionBar() { return mActionBar; }

        /** Push the current slot's params into whichever tab is showing (and
         *  the ones cached for a quick tab switch). Call after any selection
         *  or param change. */
        void syncToSlot();
        void scrollActivePanel(double delta);
        void layout();  // call after width/height changes

        /** R-BYPASS-4: while the edit target's filter is disabled the edit stack is
         *  covered by an eased dark scrim + a "FILTER DISABLED" pill. Pushed by
         *  App::syncControlsToSlot; the scrim opacity animates (R-G-1). */
        void setBypassed(bool on);
        bool bypassed() const { return mBypassed; }

        // ── on-photo mask overlay bridge (R-MASK) ──
        int activeTab() const;                          // edit-stack tab index
        bool maskTabActive() const;                     // true while the Mask tab is selected
        const MaskParams *selectedMaskParams() const;   // the mask being edited, or nullptr
        void writeSelectedMask(const MaskParams &m);    // overlay drag -> write geometry back + submit
        /** The on-photo crop box hands its rectangle back here (R-CROP-5), rather than going
         *  straight to `App::emitCommand`: this is the channel with the direct-to-service
         *  fallback, so it works in a rig or a shot where the host never wired App's own — and
         *  the crop belongs to the Xform panel's domain anyway, so the panel is kept in step. */
        void writeCrop(double x, double y, double w, double h);

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;  // card body (blends with active tab)
        /** The bypass scrim. Drawn in the overlay pass so it lands ON TOP of the tab
         *  strip + panel children (onPaint runs BEFORE children), clipped by hand to
         *  the edit-stack band so the histogram and the pinned action bar stay clear
         *  (R-BYPASS-4). A child's own overlay (an open ComboBox dropdown) still draws
         *  after ours and so stays readable. */
        void onOverlay(artboard::IRenderTarget &t) const override;
        void advance(double nowMs) override;

    private:
        /** The dimmed band: the tab strip + panel body, i.e. everything between the
         *  histogram and the action bar. */
        artboard::Rect editStackRect() const;

        // Push the effective (group-stacked) curves to the mixer/curve editors as their
        // faint green "final" reference — call after every curve/mixer edit so it tracks live.
        void refreshCurveReferences();

        /** A Command's payload: `key=value` pairs, already in ENGINE units. */
        using Fields = std::vector<std::pair<std::string, std::string>>;

        /** Emit `set <fields>` (R-SVC-2). `set` reaches every scalar, every curve, the
         *  mixer, grading and a mask APPEND, because EditParamsIO names them all
         *  (DR-SVC-2b) — which is why this column needs no command of its own. */
        void sendSet(Fields fields);
        /** Emit `mask set <index> <fields>`: addressing an EXISTING mask by index is the
         *  one thing the params codec cannot express, since `set mask=` appends. `whole` is
         *  the same edit as a complete mask, used only by the unwired fallback. */
        void sendMaskSet(int index, Fields fields, const MaskParams &whole);
        /** Emit `mask delete <index>`. */
        void sendMaskDelete(int index);

        /** The read seam. R-SVC-4 lets a view read the state it renders, and until S4c hands
         *  widgets an `AppModel` these two are how it reads it. */
        const EditParams *params() const;
        EditParams effectiveParams() const;

        cosmo::CosmoService &mSvc;   // S4c: the column holds the service and NOTHING else
        std::shared_ptr<HistogramWidget> mHistogram;
        std::shared_ptr<EditStackTabs> mTabs;
        std::shared_ptr<ParamPanel> mBasicDetail;  // merged Basic + Detail (one scrollable list)
        std::shared_ptr<MaskPanel> mMask;
        std::shared_ptr<StackPanel> mColorTab;     // merged Mixer + Curve (scrollable stack)
        std::shared_ptr<MixerPanel> mMixer;
        std::shared_ptr<CurvePanel> mCurve;
        std::shared_ptr<GradePanel> mGrade;
        std::shared_ptr<XformPanel> mXform;
        std::shared_ptr<ActionBar> mActionBar;
        int mSelectedMask = -1;
        bool mBypassed = false;                     // R-BYPASS-4 target state
        artboard::AnimatedProperty mDim{0.0};       // eased scrim opacity 0..1 (R-G-1)
        double mLastMs = 0.0;                       // frame clock, so setBypassed() can start a tween
    };
}
}
