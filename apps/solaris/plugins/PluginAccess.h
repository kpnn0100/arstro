/*
 *  solaris — the plugin's side of the editor's ParamAccess (R-VST-7): the VST3 controller, in
 *  engineering units. `Controller::editPerform` normalises with the ONE shared mapping
 *  (`normalizedFromValue`, REQ-device-7) and tells the host — so what the editor draws, what a host
 *  automates and what Solaris stores agree to the last digit. Header-only: the plugin and its tests share it.
 */
#pragma once
#include "InstrumentEditor.h"
#include "RegistryPlugin.h"

namespace arstro
{
namespace solaris_ui
{
    class ControllerAccess : public ParamAccess
    {
    public:
        explicit ControllerAccess(vst3::Controller &c) : mC(c) {}
        double value(int i) const override { return mC.plainValue(i); }
        void begin(int i) override { mC.editBegin(i); }
        void perform(int i, double v) override { mC.editPerform(i, v); }
        void end(int i) override { mC.editEnd(i); }

    private:
        vst3::Controller &mC;
    };
}
}
