#pragma once
/*
 *  solaris_host — one plugin instance played out of process (R-VST-6): `solaris-plughost --serve` started with a
 *  control socket, a shared block and two eventfds; the block handed over and waited for within its budget. The
 *  helper dying or overrunning takes the instance offline for good (a new one is `device reload`'s).
 */
#include "PluginHost.h"
#include <memory>
#include <string>

namespace arstro
{
namespace solaris_host
{
    /** Start `uid` of `bundle` at `rate`, blocks up to `maxBlock` — null (and why) when it cannot be started. */
    std::unique_ptr<solaris::IPluginInstance> loadPlugin(const std::string &bundle, const std::string &uid, double rate, int maxBlock, std::string &err);
}
}
