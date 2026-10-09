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
        bool logScale = false;  // a control's taper: frequencies and times move in ratios
        bool integer = false;   // whole steps only (octave, voices)
        std::string formula;    // the formula driving it ("" = its own value plays) — R-AUTO-1
    };

    struct DeviceModel
    {
        std::string id, type, label;
        bool instrument = false, bypass = false, known = true; // known = this build's registry has the type
        std::vector<ParamModel> params;
        std::string lastChanged;                // the parameter last written, by anyone (R-WIN-2)
        bool takesSample = false;               // R-EDM-8: it plays a recorded sound (a sampler)
        std::string sample;                     // that sound, as stored (relative to the song); "" = none
    };

    struct AutoPointModel
    {
        double at = 0, value = 0;               // beats; the automation's unit
        std::string shape = "linear";           // linear | hold | smooth | bezier
        double speedIn = 0, inflIn = 33.333, speedOut = 0, inflOut = 33.333; // bezier (R-AUTO-10): unit per beat; %
    };

    struct AutomationModel
    {
        std::string id, name, unit, from;
        double min = 0, max = 1;
        std::vector<AutoPointModel> points;
        std::vector<std::string> usedBy;        // addresses whose formula reads it
        double now = 0;                         // its value at the playhead, by the engine's evaluator (R-AUTO-11)
    };

    struct BindingModel
    {
        std::string address, formula;
        std::vector<std::string> reads;         // automation ids and addresses it reads
        bool ok = true;                         // false = INERT (its own value plays); `problem` says why
        std::string problem;
        double live = 0;                        // R-MIX-16: its value at the HEARD position — the engine's, block by
                                                // block, while playing; `eval`'s at the transport when stopped
    };

    struct SendModel
    {
        std::string id, to;
        double gain = 0;   // dB
        bool pre = false;
        std::string gainFormula; // R-AUTO-1
        bool sidechain = false;  // R-MIX-15: into `to`'s KEY (its compressors' detectors), not its input
    };

    struct StripModel
    {
        std::string id, name, kind, mixer, out; // out resolved: "master", a strip id or a port id
        int order = 0, colour = -1;
        double gain = 0, pan = 0;               // dB, −1…+1
        std::string gainFormula, panFormula;    // R-AUTO-1: "" = the stored value plays
        bool mute = false, solo = false;
        bool audible = true;                    // false when muted or silenced by another strip's solo
        std::vector<SendModel> sends;
        std::vector<DeviceModel> devices;
        // "fed by" (R-MIX-8)
        float peak[2] = {0, 0};                 // the last played block's peaks, L/R (0 when stopped)
        int clipCount = 0;
        std::vector<std::string> fromLanes;     // lanes its clips are drawn on
        std::vector<std::string> fromStrips;    // strips whose output or a send lands here
        std::vector<std::string> targets;       // where its output or a send MAY go (R-MIX-4): later strips, master, out ports
        std::vector<std::string> keyTargets;    // where a sidechain KEY may go (R-MIX-15): the strips of `targets`
    };

    /** R-MIX-9: the matrix both faces draw — computed once, by the service. */
    struct MatrixModel
    {
        std::vector<std::string> columns;       // every strip some strip may or does reach (processing order), "master", out ports
    };

    struct MixerModel
    {
        std::string id, name;
        int order = 0;
        std::vector<std::string> strips;        // in processing order
    };

    struct LaneModel
    {
        std::string id, name;                    // name: what it SHOWS — its own, else its instrument's (R-LANE-3)
        int order = 0, colour = -1;
        std::string strip;                       // R-LANE-3: the instrument strip it is the track of; empty = plain
        bool ownName = true;                     // false: a track showing its strip's name
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
        std::string strip;                       // the strip its first clip plays through ("" = none yet)
        std::string instrument;                  // that strip's instrument type ("synth", "drums")
        std::vector<std::string> strips;         // R-BROWSE-4: every strip a clip of it plays through, in clip order
        std::string lastStrip;                   // … the one its newest clip plays through — where a drop off a track goes
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
        bool metronome = false, reducedMotion = false; // R-SET-3
        double metronomeLevel = -6, newBpm = 120;
        double auditionLevel = -6;              // dB: a sample previewed from the browser (R-EDM-9)
        std::string newSig = "4/4";
        bool showIds = false;                    // R-UI-11: every object's id beside its name
    };

    /** One entry of the DSP library's device registry — what the browser lists (R-BROWSE-1). */
    struct NoteNameModel
    {
        int note = 0;
        std::string name;                        // "Kick" — a kit's key, from the registry (REQ-device-6)
    };

    struct DeviceTypeModel
    {
        std::string name, label, kind;           // kind = instrument | effect
        std::vector<NoteNameModel> noteNames;    // a kit's keys; empty for a melodic instrument
        bool takesSample = false;                // R-EDM-8: it plays a recorded sound — give it one (`--sample`, `<dv>.sample`)
    };

    /** R-EDM-9: a sample heard from the browser, outside the song. */
    struct AuditionModel
    {
        std::string file;                        // as given (a path the host opens); "" = none
        bool playing = false;
        double progress = 0;                     // 0 … 1 of the file handed to the device
    };

    struct TransportModel
    {
        bool playing = false;
        double position = 0;                     // beats — what is HEARD (rendered minus the device's latency)
        double loopFrom = 0, loopTo = 0;         // beats; equal = no loop
        double latencyMs = 0;                    // the clock device's, while playing
        std::string device;                      // the clock device ("" = the system default)
        float masterPeak[2] = {0, 0};
    };

    struct AppModel
    {
        std::string screen = "home";             // home | project
        std::string projectPath, projectName;
        bool dirty = false;
        double bpm = 120, masterGain = 0;
        std::string masterGainFormula;           // R-AUTO-1
        std::string undoLabel, redoLabel;        // what `undo` / `redo` would take back ("" = nothing) — R-EDM-1
        int undoDepth = 0, redoDepth = 0;
        std::string sig = "4/4";
        int sampleRate = 48000;
        std::vector<std::string> masterOut;
        std::vector<MixerModel> mixers;
        std::vector<StripModel> strips;
        std::vector<DeviceModel> masterDevices;  // the master's rack
        std::vector<LaneModel> lanes;
        std::vector<ClipModel> clips;
        std::vector<AutomationModel> automations; // R-AUTO-4/9
        std::vector<BindingModel> bindings;       // R-AUTO-1/9, every formula, inert ones flagged
        std::vector<PatternModel> patterns;
        std::vector<PortModel> ports;
        MatrixModel matrix;                      // R-MIX-9: its columns, so `matrix print` and the GUI show every route alike
        double lengthBeats = 0;                  // the song's end (the last clip's)
        std::vector<std::string> audit;          // the last `audit`'s findings
        std::string lastError;
        std::vector<RecentModel> recents;        // Home's cards, newest first
        SettingsModel settings;
        std::vector<DeviceInfo> devices;         // from the last `devices list`
        BrowserModel browser;
        TransportModel transport;
        AuditionModel audition;
        std::vector<DeviceTypeModel> deviceTypes; // the registry, instruments first (R-DSP-2)
        long long revision = 0;                  // bumps on every change — NOT in the stable dump
    };
}
}
