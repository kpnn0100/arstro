/*
 *  solaris_ui — DevicePanel: one device's parameters, GENERATED from the registry (R-UI-5).
 *
 *  Nothing here knows a synth from a reverb. The model publishes every parameter of every device
 *  (`DeviceModel::params`: name, label, unit, range, default, choices, taper) and the panel draws a
 *  control per parameter, grouped by its name's prefix (`osc1.*`, `filter.*` → OSC1, FILTER): a
 *  number is cosmo's `SliderRow` (its opt-in formatter shows engineering units; a `logScale`
 *  parameter moves in ratios; an `integer` one in whole steps), a choice is a row that steps
 *  through its names. A parameter the DSP library adds tomorrow appears here with no UI code.
 *
 *  Every change is one command line — `set <dv>.<param>=<value>` (in its unit; a choice by name),
 *  `set <dv>.bypass=…`, `device remove <dv>` — so a script reaches everything the panel does.
 *
 *  A device's rows are built the first time it is shown and KEPT (a Segment never drops a child),
 *  in a page of its own; switching device cross-fades the pages (Interstellar's `FadePage`). The
 *  panel opens and closes as a fade with a short slide (150 / 120 ms) — a setter has no clock, so
 *  `show`/`hide` record intent and `advance` starts the tween. While the pointer is down `bind`
 *  does not re-seed the sliders: a gesture in flight outranks the model. The body scrolls when the
 *  rows outgrow it.
 */
#pragma once
#include "../Theme.h"
#include "AppModel.h"
#include "../../../interstellar/app/widgets/EasedScroll.h"
#include "../../../cosmo/widgets/HoverFade.h"
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo_v2 { class SliderRow; }
namespace solaris_ui
{
    class ParamPage;

    class DevicePanel : public artboard::Segment
    {
    public:
        static constexpr double kWidth = 292.5;   // space::u(90)
        static constexpr double kHeaderH = 48.75; // space::u(15)

        DevicePanel();
        /** Intent: show device `id` (another one cross-fades in); `hide` closes the panel. */
        void show(const std::string &deviceId);
        void hide();
        bool shown() const { return mWanted; }
        const std::string &device() const { return mDevice; }
        double appearAmount() const { return mAppear.value(); } // LIVE, for a test

        void bind(const solaris::AppModel &m, bool interacting);
        void layout();
        void advance(double nowMs) override;

        // what a test aims at (local)
        int rowCount() const;
        std::string rowParam(int i) const;
        artboard::Rect rowRect(int i) const;
        cosmo_v2::SliderRow *slider(const std::string &param) const;
        artboard::Rect bypassRect() const;
        artboard::Rect removeRect() const;
        artboard::Rect closeRect() const;
        double pageAmount(const std::string &deviceId) const; // a page's LIVE opacity
        /** Scroll (eased) so a parameter's row is in view. */
        void reveal(const std::string &param);

        std::function<bool(const std::string &line)> onCommand;

        bool hitTest(const artboard::Point &p) const override { return mWanted && Segment::hitTest(p); }

    protected:
        void onPaint(artboard::IRenderTarget &t) const override;
        bool handleGesture(const artboard::Gesture &g, const artboard::Point &local) override;
        bool hitTestSelf(const artboard::Point &p) const override { return mWanted && localBounds().contains(p); }

    private:
        ParamPage *page(const std::string &deviceId) const;
        ParamPage &build(const solaris::DeviceModel &d);
        const solaris::DeviceModel *find(const std::string &id) const;
        std::string ownerOf(const std::string &id) const; // the strip whose rack holds it

        solaris::AppModel mModel;
        std::string mDevice;
        std::map<std::string, std::shared_ptr<ParamPage>> mPages;
        bool mWanted = false, mApplied = false, mInteracting = false;
        double mNowMs = 0.0;
        artboard::AnimatedProperty mAppear{0.0}, mBypass{0.0};
        bool mBypassInit = false, mBypassLast = false;
        interstellar_v1::EasedScroll mScroll;
        cosmo_v2::HoverFade mHover;
    };
}
}
