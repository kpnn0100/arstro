/*
 *  solaris_host — the machine: folders and audio devices (R-SVC-4, R-BROWSE-1, R-DEV-1).
 *
 *  `listDir` lists a folder for the browser — sub-folders, audio files (by extension: what FFmpeg
 *  reads), songs (.slp) — hidden entries left out. `listDevices` asks the sound server (the
 *  PulseAudio API, which PipeWire also serves, R-DEV-8) for every output and input, monitors of
 *  outputs left out. `machinePaths` says where the settings and recents files live (XDG).
 */
#pragma once
#include "AppModel.h"
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_host
{
    bool listDir(const std::string &path, std::vector<solaris::BrowserEntry> &out, std::string &err);
    bool listDevices(std::vector<solaris::DeviceInfo> &out, std::string &err);
    /** The settings and recents files (created folders included); SOLARIS_SETTINGS / SOLARIS_RECENTS override. */
    void machinePaths(std::string &settings, std::string &recents);
}
}
