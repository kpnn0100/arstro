#pragma once
/*
 *  solaris_host — other makers' VST3 plugins, found (R-VST-6): where they are looked for, and each bundle read by
 *  `solaris-plughost --scan` in a process of its own, so a module that crashes or hangs while being read loses only
 *  itself (docs/discussion.md Round 9).
 */
#include "AppModel.h"
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_host
{
    /** SOLARIS_VST3_PATH (colon-separated), else ~/.vst3, /usr/lib/vst3, /usr/local/lib/vst3. */
    std::vector<std::string> pluginSearchPath();
    /** The machine's list of what a scan found: SOLARIS_PLUGINS, else the XDG data folder's solaris/plugins. */
    std::string pluginsListPath();
    /** The helper: SOLARIS_PLUGHOST, else the one this build made ("" when none). */
    std::string plughostPath();
    /** Every bundle (`*.vst3`) in `dirs`, three folders down, read by the helper — what it found, and a line per
     *  bundle that could not be read (crashed, hung, refused). False only when there is no helper. */
    bool scanPlugins(const std::vector<std::string> &dirs, std::vector<solaris::PluginModel> &out, std::vector<std::string> &problems,
                     std::string &err);
}
}
