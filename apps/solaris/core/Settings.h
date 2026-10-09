/*
 *  solaris_core — Settings: the MACHINE's, never a song's (R-SET-2, R-DEV-3).
 *
 *  A song names logical ports ("Main", "Phones"); this file says which device and channels each
 *  port is on THIS machine, which devices are in use, the rate new songs start at, the buffer, and
 *  the sample folders the browser lists first. The host gives the path; the text format is the
 *  suite's `key = value`, one per line, a folder per `folder` line, a port per `port.<name>` line.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct Settings
    {
        int sampleRate = 48000;                 // new songs start here; the clock device's preferred rate
        int bufferSize = 256;                   // frames per device write
        std::string output;                     // the clock device's id ("" = the system default)
        std::string input;
        std::vector<std::string> folders;       // R-SET-1: the browser's quick-access list, in order
        std::vector<std::pair<std::string, std::string>> ports; // port name → "<device id>:<first channel>"
        // R-SET-3: the rest of the sheet
        bool metronome = false;                 // clicks while playing (R-TIME-4), never in a render
        double metronomeLevel = -6.0;           // dB, −40 … 6
        double newBpm = 120.0;                  // what `project new` starts at without --bpm
        std::string newSig = "4/4";             // … and without --sig
        bool reducedMotion = false;             // the UI's tweens collapse (design rule §2.6), with the OS's

        std::string text() const;
        static Settings parse(const std::string &text);
        /** The device a port name maps to on this machine ("" = the clock device). */
        std::string portDevice(const std::string &portName) const;
    };
}
}
