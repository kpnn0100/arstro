/*
 *  solaris_ui — DevicePanel: one device's parameters, GENERATED from the registry — the content of
 *  its window (R-UI-5, R-WIN-1…3).
 *
 *  Nothing here knows a synth from a reverb. The model publishes every parameter of every device
 *  (`DeviceModel::params`: name, label, unit, range, default, choices, taper, its formula) and the
 *  panel draws a row per parameter, grouped by its name's prefix (`osc1.*`, `filter.*` → OSC1,
 *  FILTER): a number is cosmo's `SliderRow` (its opt-in formatter shows engineering units; a
 *  `logScale` parameter moves in ratios; an `integer` one in whole steps), a choice is a row that
 *  steps through its names. A parameter the DSP library adds tomorrow appears here with no UI code.
 *
 *  It is the parameter LIST the brief asks for (R-WIN-2): each row's value column says what decides
 *  it — its number, or `auto au_1` (an automation), `= ch_2.gain` (a link), `= <formula>` — and the
 *  row the model says changed LAST (`DeviceModel::lastChanged`, written by any `set`, from anywhere)
 *  is lit, the light easing from row to row. A right-click offers the shared parameter menu
 *  (`ParamMenu`, R-WIN-3, R-UI-11): Create Automation, Formula…, Clear Binding, Reset to Default, Copy
 *  Address / Value / as Formula — the same menu the dock's faders, pans and sends offer. With View ›
 *  Show IDs every row's full address (`dv_1.filter.cutoff`) fades in over its label. Every change is ONE
 *  command line: `set <dv>.<param>=…`, `auto create`, `bind clear`, `set <dv>.bypass=…`, `device remove <dv>`.
 *
 *  The rows are built once, the first time the model has the device, and kept (a Segment never
 *  drops a child). While the pointer is down `bind` does not re-seed the sliders: a gesture in
 *  flight outranks the model. The body scrolls when the rows outgrow it (`reveal` eases to one).
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/ContextMenu.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2 { class SliderRow; }
namespace solaris_ui
{
    class ParamBody;
    class ParamIds;
    class AnalogFace;

    class DevicePanel : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 35.75;  // space::u(11)
        static constexpr double kValueW = 91.0;    // space::u(28): "auto au_12", "= ch_2.gain", "12.5 kHz"
        static constexpr double kOfflineH = 29.25; // space::barH(): a plugin's offline band — why, and Reload (R-VST-6)
        static constexpr double kReadingsH = 71.5; // space::u(22): a device that measures — its note, its needle, the rest (R-FX-17)

        explicit DevicePanel(std::string deviceId);
        const std::string &device() const { return mDevice; }
        bool present() const { return mPresent; }  // the model still has the device

        void bind(const solaris::AppModel &m, bool interacting);
        void layout();
        void advance(double nowMs) override;

        // what a test aims at (local)
        int rowCount() const;
        std::string rowParam(int i) const;
        artboard::Rect rowRect(int i) const;
        cosmo_v2::SliderRow *slider(const std::string &param) const;
        std::string readout(const std::string &param) const;   // the value column's text
        double litAmount(const std::string &param) const;      // the last-change light, LIVE
        artboard::Rect bypassRect() const;
        artboard::Rect removeRect() const;
        artboard::Rect rollRect() const;                       // an instrument's "Piano Roll" (R-ROLL-1)
        artboard::Rect presetsRect() const;                    // R-PRESET-1: "Presets" — its type's, and Save as…
        // R-VST-6 (N26): another maker's plugin — its list (no face), "Editor" in the Presets place (its own window;
        // lit, eased, while open), and offline a band under the header saying why, with Reload (eased in and out)
        bool isPlugin() const;
        artboard::Rect editorRect() const;
        artboard::Rect reloadRect() const;
        double editorAmount() const { return mEditorLit.value(); } // LIVE
        double offlineAmount() const { return mOffline.value(); }  // LIVE
        bool takesSample() const;                              // a sampler: it plays a recorded sound (R-EDM-8)
        std::string sampleText() const;                        // what its header says of that sound
        void setDropHint(bool on) { mDropWant = on; }          // a sample hovering over it: outlined, eased
        double dropAmount() const { return mDrop.value(); }    // LIVE
        void reveal(const std::string &param);
        /** A right-click at a window point: true when it lands on a parameter row (its menu opened).
         *  The App offers right-clicks here first — a row's slider would otherwise swallow it. */
        bool contextClick(artboard::Point world);
        /** R-UI-12: an instrument shows its analog face (sections, knobs, switches, screens) instead of the list.
         *  Opt-out only for a test of the list; an effect always has the list. */
        bool analogFace = true;
        AnalogFace *face() const { return mFace.get(); }
        /** A parameter's menu (R-WIN-3) — what a row or a knob offers. */
        void openParamMenuFor(const solaris::ParamModel &spec, int choice, artboard::Point world);

        /** The song's controls in the header — On/Bypassed, Remove, Piano Roll. Off in a plugin's own editor
         *  (R-VST-7), where the panel is the whole instrument: its header names it instead. Opt-in, default on. */
        bool songControls = true;
        /** Room under the header an embedder fills (a kit's pads in a plugin's editor, R-VST-7); the rows begin
         *  below it. Opt-in, default 0: Solaris's windows lay out exactly as before. */
        double band = 0.0;
        double bodyTop() const { return kHeaderH + band + mOffline.value() * kOfflineH + readingsH(); } // the bands push the rows
        /** R-FX-17 (Round 12): a device that MEASURES (its type's readings — a tuner) shows them in a band under the
         *  header — the note large, a needle for the cents easing to each reading, the rest small; drawn from the model
         *  only. Its height is the type's: a type's readings never change. */
        double readingsH() const;
        artboard::Rect readingsRect() const;
        std::string noteText() const;     // the band's large text: "A4", or a dash with no pitch
        std::string readingsText() const; // its small line: "446.0 Hz · +23 ct · −9.0 dB", "no pitch · −72.0 dB"
        double needleCents() const { return mNeedle.value(); }     // LIVE: eased toward each reading
        double pitchedAmount() const { return mPitched.value(); }  // LIVE: 1 while it hears a pitch, eased
        /** Scroll, eased, so the group whose parameters are `<prefix>.*` begins at the top (a pad picked). */
        void revealGroup(const std::string &prefix);

        std::function<bool(const std::string &line)> onCommand;
        std::function<void(std::vector<cosmo_v2::ContextMenu::Item> items, artboard::Point world)> onMenu;
        std::function<void(const std::string &current, artboard::Point world, std::function<void(const std::string &)> done)> onRename;
        std::function<void(const std::string &patternId)> onOpenPattern;
        std::function<void(const std::string &text)> onCopy;    // the host's clipboard (R-UI-11)
        std::function<double()> idsAmount;                      // View › Show IDs, eased by the screen (R-UI-11)
        double idsShown() const { return idsAmount ? idsAmount() : 0.0; }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return localBounds().contains(p); }

    private:
        friend class ParamBody;
        const solaris::DeviceModel *model() const;
        void build(const solaris::DeviceModel &d);
        void openParamMenu(int row, artboard::Point world);

        std::string mDevice, mOwner, mStrip;
        bool mRollPending = false; // "Piano Roll" on a strip with no pattern: open the one `clip add` makes
        solaris::AppModel mModel;
        bool mPresent = false, mBuilt = false, mInteracting = false;
        std::shared_ptr<ParamBody> mBody;
        std::shared_ptr<ParamIds> mIds;     // over the rows: each row's address, faded in by Show IDs
        std::shared_ptr<AnalogFace> mFace;  // an instrument's face, in the rows' place (R-UI-12)
        artboard::AnimatedProperty mBypass{0.0};
        bool mBypassInit = false, mBypassLast = false;
        interstellar_v1::EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
        artboard::AnimatedProperty mDrop{0.0};
        bool mDropWant = false, mDropLast = false;
        artboard::AnimatedProperty mOffline{0.0}, mEditorLit{0.0};
        bool mPluginInit = false, mOfflineLast = false, mEditorLast = false;
        artboard::AnimatedProperty mNeedle{0.0}, mPitched{0.0}; // R-FX-17
        double mNeedleLast = 0.0;
        bool mReadInit = false, mPitchedLast = false;
    };
}
}
