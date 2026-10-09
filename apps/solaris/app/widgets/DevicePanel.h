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

    class DevicePanel : public artboard::Segment
    {
    public:
        static constexpr double kHeaderH = 35.75;  // space::u(11)
        static constexpr double kValueW = 91.0;    // space::u(28): "auto au_12", "= ch_2.gain", "12.5 kHz"

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
        bool takesSample() const;                              // a sampler: it plays a recorded sound (R-EDM-8)
        std::string sampleText() const;                        // what its header says of that sound
        void setDropHint(bool on) { mDropWant = on; }          // a sample hovering over it: outlined, eased
        double dropAmount() const { return mDrop.value(); }    // LIVE
        void reveal(const std::string &param);
        /** A right-click at a window point: true when it lands on a parameter row (its menu opened).
         *  The App offers right-clicks here first — a row's slider would otherwise swallow it. */
        bool contextClick(artboard::Point world);

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
        artboard::AnimatedProperty mBypass{0.0};
        bool mBypassInit = false, mBypassLast = false;
        interstellar_v1::EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
        artboard::AnimatedProperty mDrop{0.0};
        bool mDropWant = false, mDropLast = false;
    };
}
}
