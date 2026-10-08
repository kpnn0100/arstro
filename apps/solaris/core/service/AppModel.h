/*
 *  solaris_core — AppModel: what a front end can SEE (R-SVC-2). Plain data, no sound, no UI types.
 *
 *  A frozen contract with the UI: fields are ADDED, never renamed or removed. Every field is
 *  documented in `appModelFields()` (the API document's model section), and a test fails if the
 *  codec writes a field that table does not list — so the document cannot drift from the dump.
 *
 *  Computed things live here too, because a second front end would otherwise recompute them and
 *  get a different answer (arstro.rule §1): a strip's "fed by" (R-MIX-8), whether it is audible
 *  under the current solos, a clip's resolved length, how many clips share a pattern.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct ParamModel
    {
        std::string name, label, unit;
        std::string text;       // as stored: a number, or a choice's name
        double value = 0;       // engineering units; a choice = its index
        double min = 0, max = 1, def = 0;
        std::vector<std::string> choices;
    };

    struct DeviceModel
    {
        std::string id, type, label;
        bool instrument = false, bypass = false, known = true; // known = this build's registry has the type
        std::vector<ParamModel> params;
    };

    struct SendModel
    {
        std::string id, to;
        double gain = 0;   // dB
        bool pre = false;
    };

    struct StripModel
    {
        std::string id, name, kind, mixer, out; // out resolved: "master", a strip id or a port id
        int order = 0, colour = -1;
        double gain = 0, pan = 0;               // dB, −1…+1
        bool mute = false, solo = false;
        bool audible = true;                    // false when muted or silenced by another strip's solo
        std::vector<SendModel> sends;
        std::vector<DeviceModel> devices;
        // "fed by" (R-MIX-8)
        int clipCount = 0;
        std::vector<std::string> fromLanes;     // lanes its clips are drawn on
        std::vector<std::string> fromStrips;    // strips whose output or a send lands here
    };

    struct MixerModel
    {
        std::string id, name;
        int order = 0;
        std::vector<std::string> strips;        // in processing order
    };

    struct LaneModel
    {
        std::string id, name;
        int order = 0, colour = -1;
    };

    struct ClipModel
    {
        std::string id, name, track, lane, kind; // kind = audio | note
        std::string src, pattern;
        double at = 0, length = 0;               // beats; length RESOLVED (file span at the tempo, or the pattern's)
        double in = 0, out = 0;                  // seconds (audio)
        double gain = 0, fadeIn = 0, fadeOut = 0;
        bool loop = false;
        bool offline = false;                    // the file could not be read (audio)
        int linked = 1;                          // clips playing the same pattern, this one included
    };

    struct NoteModel
    {
        int pitch = 60, vel = 100;
        double at = 0, length = 0.25;
    };

    struct PatternModel
    {
        std::string id, name;
        double length = 4;
        int clips = 0;
        std::vector<NoteModel> notes;
    };

    struct PortModel
    {
        std::string id, name, dir;
        int channels = 2;
    };

    struct BrowserEntry
    {
        std::string name, path;
        std::string kind;                        // dir | audio | song | other
    };

    struct BrowserModel
    {
        std::string path;                        // the folder last browsed ("" = none)
        std::vector<BrowserEntry> entries;       // folders first, then by name
    };

    struct DeviceInfo
    {
        std::string id, name, dir;               // dir = out | in
        int channels = 2, rate = 48000;
    };

    struct RecentModel
    {
        std::string path, name;
        double bpm = 0, lengthBeats = 0;
        int strips = 0;
        bool missing = false;                    // the file is gone or no longer a song
    };

    struct SettingsModel
    {
        int sampleRate = 48000, bufferSize = 256;
        double latencyMs = 0;                    // what the buffer costs at the rate
        std::string output, input;
        std::vector<std::string> folders;
        std::vector<std::string> ports;          // "<name>=<device>:<channel>"
    };

    struct AppModel
    {
        std::string screen = "home";             // home | project
        std::string projectPath, projectName;
        bool dirty = false;
        double bpm = 120, masterGain = 0;
        std::string sig = "4/4";
        int sampleRate = 48000;
        std::vector<std::string> masterOut;
        std::vector<MixerModel> mixers;
        std::vector<StripModel> strips;
        std::vector<DeviceModel> masterDevices;  // the master's rack
        std::vector<LaneModel> lanes;
        std::vector<ClipModel> clips;
        std::vector<PatternModel> patterns;
        std::vector<PortModel> ports;
        double lengthBeats = 0;                  // the song's end (the last clip's)
        std::vector<std::string> audit;          // the last `audit`'s findings
        std::string lastError;
        std::vector<RecentModel> recents;        // Home's cards, newest first
        SettingsModel settings;
        std::vector<DeviceInfo> devices;         // from the last `devices list`
        BrowserModel browser;
        long long revision = 0;                  // bumps on every change — NOT in the stable dump
    };
}
}
