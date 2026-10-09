/*
 *  solaris — a registry device type as the model's DeviceModel (R-UI-5, R-VST-7).
 *
 *  ONE mapping from the DSP library's description of a device (its `DeviceType`: every parameter's
 *  name, label, unit, range, default, choices, taper) to what a UI draws (`DeviceModel`): the service
 *  calls it for every device in a song, and the plugins' own editor for the device they wrap — so a
 *  Basic Synth looks the same in Solaris's window and in another host's (law 2: the registry is the only
 *  description of a parameter). It depends on the DSP library and the model's plain structs, nothing more.
 */
#pragma once
#include "AppModel.h"
#include "device/Device.h"
#include <vector>

namespace arstro
{
namespace solaris
{
    /** Every parameter of `t` at `values` (one per spec parameter, engineering units; a missing one is its
     *  default), each with its stored text. The id, bypass and sample are the caller's. */
    DeviceModel deviceModelOf(const DeviceType &t, const std::vector<double> &values);
}
}
