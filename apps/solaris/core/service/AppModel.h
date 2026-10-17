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

    /** R-UI-12: a parameter group — the prefix before a parameter's dot ("" for none) — and its title. */
    struct ParamGroupModel
    {
        std::string prefix, label;
    };

    /** R-FX-17 (Round 12): what a device measures — its registry's description and its value while playing. */
    struct ReadingModel
    {
        std::string name, label, unit;          // the registry's: "frequency", "Frequency", "Hz"
        double value = 0;                       // as heard now, while playing; 0 stopped
    };

    struct DeviceModel
    {
        std::string id, type, label;
        bool instrument = false, bypass = false, known = true; // known = this build's registry has the type
        std::vector<ParamModel> params;
        std::string lastChanged;                // the parameter last written, by anyone (R-WIN-2)
        bool takesSample = false;               // R-EDM-8: it plays a recorded sound (a sampler)
        bool takesDefinition = false;           // R-INST-9: … its "sample" an instrument's definition (an .sfz) naming its sounds
        std::string sample;                     // that sound, as stored (relative to the song); "" = none
        std::vector<ParamGroupModel> groups;    // R-UI-12: its parameter groups' titles, the registry's (REQ-device-10)
        std::string plugin;                     // R-VST-6: type vst3 — the plugin's class id (its name is `label`)
        bool offline = false;                   // … not playing: never started, crashed, overran — an effect passes dry,
        std::string why;                        //    an instrument is silent; and why (`device reload` starts it again)
        bool editor = false;                    // … its own editor window is open (`device editor`, N26)
        std::vector<ReadingModel> readings;     // R-FX-17: what it measures (a tuner's pitch); empty for most
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
        double loop = 0, at = 0, length = 0;    // R-AUTO-12: beats — its first `loop` repeat from `at` for `length` (0: on)
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
        std::string input;                      // R-REC-1: the input port it monitors and records ("" none)
        std::string monitor = "auto";           // R-REC-1: auto | on | off
        bool audible = true;                    // false when muted or silenced by another strip's solo
        std::vector<SendModel> sends;
        std::vector<DeviceModel> devices;
        // "fed by" (R-MIX-8)
        float peak[2] = {0, 0};                 // the last played block's peaks, L/R (0 when stopped)
        float rms[2] = {0, 0};                  // R-METER-2: RMS over 300 ms, L/R
        float hold[2] = {0, 0};                 // the held peak: the most of 1.5 s, then falling 20 dB/s
        bool clip = false;                      // reached full scale since `meters clear` (a latch)
        float cpu = 0;                          // R-PERF-1: its share of the audio thread's time (0 … 1 of a block), while playing
        std::string frozen;                     // R-PERF-3: the file it plays instead of its own work ("" not frozen)
        std::string vca;                        // R-MIX-19: the VCA it follows ("" none)
        double vcaGain = 0;                     // … the dB that VCA adds to it (0 for none)
        std::vector<std::string> members;       // … on a VCA: the strips that follow it, in processing order
        bool phase = false;                     // R-MIX-20: its polarity inverted
        double width = 100;                     // … its width, % (0 … 200)
        bool soloSafe = false;                  // … a bus no solo silences
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
        std::string group;                       // R-ARR-3: the lane group it is in ("" none)
    };

    /** R-ARR-3: a named group of lanes, side by side; folded, one row on the timeline. */
    struct LaneGroupModel
    {
        std::string id, name;
        bool folded = false;
        std::vector<std::string> lanes;          // its lanes, in order
    };

    struct ClipModel
    {
        std::string id, name, track, lane, kind; // kind = audio | note
        std::string src, pattern;
        double at = 0, length = 0;               // beats; length RESOLVED (file span at the tempo, or the pattern's)
        double offset = 0;                       // R-LAUNCH-1: a note clip's start, beats into its pattern
        double in = 0, out = 0;                  // seconds (audio)
        double gain = 0, fadeIn = 0, fadeOut = 0;
        bool loop = false;
        bool offline = false;                    // the file could not be read (audio)
        int linked = 1;                          // clips playing the same pattern, this one included
        std::vector<std::string> takes;          // R-REC-2: a loop recording's passes (files), 1-based in the comp
        struct CompModel { int take = 1; double from = 0, to = 0; };
        std::vector<CompModel> comp;             // R-REC-2: which take plays where; elsewhere the last
        std::vector<CompModel> plays;            // … resolved: runs covering the clip, each its take (what is heard)
        bool reverse = false;                    // R-EDIT-1: its span played backwards (audio)
        double fadeInHeard = 0, fadeOutHeard = 0; // R-EDIT-1: its fades grown over an overlap on its lane (beats)
        bool warp = false;                       // R-WARP-1: it follows the song's tempo
        double bpm = 0, pitch = 0;               // R-WARP-1: its own tempo (0 unknown); semitones it is moved
    };

    struct NoteModel
    {
        int pitch = 60, vel = 100;
        double at = 0, length = 0.25;
        double chance = 1;                       // R-ROLL-11: the odds it sounds in a repetition, 0 … 1
    };

    /** R-MIDI-3: a point of a pattern's expression lane. */
    struct ExprPointModel
    {
        std::string lane;                        // bend | cc0 … cc127 (cc1 the mod wheel)
        double at = 0, value = 0;
    };

    struct PatternModel
    {
        std::string id, name;
        double length = 4;
        int clips = 0;
        std::vector<NoteModel> notes;
        std::vector<ExprPointModel> expr;        // R-MIDI-3: sorted by (lane, at)
        std::string strip;                       // the strip its first clip plays through ("" = none yet)
        std::string instrument;                  // that strip's instrument type ("synth", "drums")
        std::vector<std::string> strips;         // R-BROWSE-4: every strip a clip of it plays through, in clip order
        std::string lastStrip;                   // … the one its newest clip plays through — where a drop off a track goes
        std::string scale;                       // R-ROLL-11: "D dorian" — placed and moved notes land on it; "" none
        int scaleMask = 0;                       // … its pitch classes, bit k for pitch class k (C = 0); 0 none
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
        std::string search;                      // R-PRESET-3: what was searched ('' a folder listed; `favourites`)
    };

    /** R-PRESET-3: a sound in the machine's library — its tags (lower case, sorted), whether it is a favourite. */
    struct LibraryEntry
    {
        std::string path;
        std::vector<std::string> tags;           // `favourite` not among them: that is `favourite`
        bool favourite = false;
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
        bool recovery = false;                   // R-SAFE-2: an autosave newer than it — unsaved edits to recover
    };

    /** One keyboard action and its keys on this machine (R-SET-4) — a front end looks a pressed combo up here. */
    struct KeyActionModel
    {
        std::string id, area, label;             // "transport.play" · "Transport" · what the sheet shows
        std::vector<std::string> keys, defaults; // canonical combos ("Ctrl+Shift+Z"); keys empty = no key
    };

    /** R-CTRL-1: a MIDI control (`<n>` or `<ch>/<n>`) and the number it moves. */
    struct MapModel
    {
        std::string cc, address;
        bool inert = false; // the open song has no such address (or no song is open)
    };

    /** R-CTRL-2: the control surface, as it is now (the session's). */
    struct SurfaceModel
    {
        std::string protocol = "none";           // none | mcu | hui (the setting)
        bool connected = false;
        std::string why;                         // why it is not ("" when it is, or none is set)
        int bank = 0;                            // the first strip of its eight, 0-based in processing order
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
        int autosave = 60;                       // R-SAFE-1: seconds; 0 = off
        std::string driver = "pulse";             // R-DEV-9: pulse | pipewire
        std::string midiInputs = "all";          // R-MIDI-2: all | none | port names
        std::string recordQuantize = "off";      // R-MIDI-2
        int countIn = 0;
        std::vector<KeyActionModel> keys;        // R-SET-4: every shortcut, in the sheet's order
        std::vector<MapModel> mappings;          // R-CTRL-1: the MIDI controls and what they move
        std::string surface = "none";            // R-CTRL-2: none | mcu | hui
        std::string surfacePort;                 // … its port, in and out
        std::string threads = "auto";            // R-PERF-2: auto | 1 … 16
    };

    /** R-ARR-1: a named place in the song, and where its section ends (computed: its length, else the next marker). */
    struct MarkerModel
    {
        std::string id, name, note;
        double at = 0, length = 0, end = 0;      // beats; length 0 = a point
    };

    /** R-VER: a version of the song — its base resolved, its changes on top (docs/project-format.md §9b). */
    struct VersionModel
    {
        std::string id, name;
        std::string base = "main";               // an earlier version's id, or "main"
        bool frozen = false;                     // R-VER-3: reads its snapshot; an edit while open is refused
        int changes = 0, own = 0, dangling = 0;  // its deltas; nodes only it has; deltas whose node its base dropped
        std::string problem;                     // why it does not stand as a song ("" when it does)
    };

    /** R-LAUNCH-1: a row of the session grid. */
    struct SceneModel
    {
        std::string id, name;
        int order = 0;
    };

    /** R-LAUNCH-1: a cell of it — what a strip plays when launched in that scene. */
    struct SlotModel
    {
        std::string id, name, scene, strip;
        std::string pattern, src;                // one of them
        double length = 0;                       // beats a loop lasts — the pattern's (or the file span's) when not set
        double gain = 0;                         // dB
    };

    /** R-VIDEO-1: a point of the picture on the song's beats — where the lane draws a thumbnail. */
    struct VideoThumbModel
    {
        double beat = 0, time = 0;               // the song's beat; the picture's seconds there
    };

    /** R-VIDEO-1: the song's picture — and where it is at the heard position. */
    struct VideoModel
    {
        std::string id, src;                     // "" id: no picture
        double offset = 0;                       // seconds into the song its first frame shows
        double duration = 0, fps = 0;            // as the host read it
        int width = 0, height = 0;
        bool offline = false;                    // the host cannot read it
        double time = -1;                        // the picture's seconds at the heard position (−1: outside it)
        std::string path;                        // the file as the host opens it (resolved against the song's folder)
        double at = 0, end = 0;                  // the beats it starts and ends on — through the tempo map
        std::vector<VideoThumbModel> thumbs;     // a point every second of it (fewer for a long one), through the tempo map
    };

    /** R-COLLECT-1: a file the song names that cannot be read — and what uses it. */
    struct MissingModel
    {
        std::string path;                        // as the song names it
        std::vector<std::string> users;          // ids; a version's prefixed "vr_n:"
    };

    /** R-TEMPO-1: a change of the song's tempo — and where it falls in time (computed through the map). */
    struct TempoModel
    {
        std::string id;
        double at = 0, bpm = 120;                // beats; beats a minute
        bool ramp = false;                       // reached gliding from the tempo before
        double seconds = 0;                      // where it is heard
    };

    /** R-PRESET-1: a preset of a device type — the user's, or the factory's shipped with the app. */
    struct PresetModel
    {
        std::string type, name;
        bool factory = false;
    };

    /** R-VST-6: another maker's VST3 plugin a scan found — its class's id (UID), its name and maker, instrument or
     *  effect, the bundle it is in. */
    struct PluginModel
    {
        std::string id, name, vendor, kind, path;
    };

    /** R-PRESET-2: a song template — the user's, or the factory's shipped with the app. */
    struct TemplateModel
    {
        std::string name;
        bool factory = false;
    };

    /** R-ROLL-11: a word the grammar takes, and what it means — the roll's menus offer what the service accepts. */
    struct VocabModel
    {
        std::string name, label;
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
        bool takesDefinition = false;            // R-INST-9: … an instrument's definition (an .sfz), the same way
    };

    /** R-EDM-9: a sample heard from the browser, outside the song. */
    struct AuditionModel
    {
        std::string file;                        // as given (a path the host opens); "" = none
        bool playing = false;
        double progress = 0;                     // 0 … 1 of the file handed to the device
    };

    /** R-DEV-5: a device the song plays through, while it plays (R-DEV-1: the clock, and each follower). */
    struct AudioDeviceModel
    {
        std::string id;                          // as the settings name it ("" = the system default)
        std::string role;                        // clock | follower | input (on another device, held to the clock)
        double latencyMs = 0;                    // from a frame rendered to its being heard — every device the same
                                                 //   (R-DEV-4) but one that is silent
        double driftPpm = 0;                     // a follower's or an input's crystal against the clock's: + it runs fast
        long long xruns = -1;                    // its device's underruns (−1: the driver cannot count them)
        long long starved = 0;                   // a follower's blocks with too little to play (the clock stalled)
        long long dropped = 0;                   // … the clock's pushes it had no room for (its device stalled)
        std::string problem;                     // why it is silent ("" it plays)
    };

    /** R-LAUNCH-1: a strip in the session — what it plays from the grid, and what will land next. */
    struct LaunchModel
    {
        std::string strip;
        bool session = false;                    // its launch has landed: it plays `slot` ("" nothing), not the arrangement
        std::string slot;
        std::string next;                        // what lands on `at`: a slot's id, "stop", "back" — "" nothing
        double at = 0;                           // beats
        double progress = 0;                     // 0 … 1 through the playing slot's loop
    };

    struct TransportModel
    {
        bool playing = false;
        double position = 0;                     // beats — what is HEARD (rendered minus the device's latency)
        double loopFrom = 0, loopTo = 0;         // beats; equal = no loop
        double punchFrom = 0, punchTo = 0;       // R-REC-1: beats a recording keeps; equal = all of it (the session's)
        std::string loopClip;                    // the clip `transport play --clip` loops ('' = the song's loop) — R-ROLL-6
        double cue = 0;                          // beats: where the time bar was last put; `transport stop --return` goes back there — R-TIME-7
        double tempo = 120;                      // R-TEMPO-1: the tempo where the time bar is (the map's)
        std::string tempoId;                     // … the change in effect there ('' before the first: the song's bpm)
        double latencyMs = 0;                    // the clock device's, while playing
        std::string device;                      // the clock device ("" = the system default)
        float masterPeak[2] = {0, 0};
        float masterRms[2] = {0, 0};             // R-METER-2, as a strip's
        float masterHold[2] = {0, 0};
        bool masterClip = false;
        std::string driver;                      // R-DEV-9: what the clock device plays through now ("" stopped)
        bool mono = false;                       // R-MIX-20: the mono check — the speakers get mono (the listener's, never a render)
        std::vector<LaunchModel> launched;       // R-LAUNCH-1: the strips in the session (or about to be), by strip id
        int swaps = 0;                           // engines built and swapped in since play — an edit the live path could not carry
        long long xruns = -1;                    // R-DEV-5: underruns since play; −1 = this driver cannot count them
        float cpu = 0, cpuPeak = 0;              // R-PERF-1: the audio thread's load (a block's work over its duration),
                                                 //   smoothed; its worst block in the last second (over 1: a missed deadline)
        bool recording = false;                  // R-MIDI-2: `transport record` — the armed strip's keyboard kept
        bool countingIn = false;                 // … its count-in still clicking
        int recorded = 0;                        // notes struck that will land (a count-in's are not)
        std::vector<AudioDeviceModel> devices;   // R-DEV-5: while playing, the clock first; none stopped
    };

    /** R-METER-1: the master as measured while playing (BS.1770-4) — kept after a stop, so a played song's
     *  integrated loudness can be read; −144 = nothing measured. */
    struct LoudnessModel
    {
        double momentary = -144, shortTerm = -144, integrated = -144; // LUFS: 400 ms, 3 s, gated since play / clear
        double truePeak = -144;                  // dBTP: the most since play / `meters clear`
        double correlation = 0;                  // −1 … +1, over 300 ms
        std::vector<double> spectrum;            // dB re a full-scale sine, 61 bands: band b at 1000·2^((b−34)/6) Hz
    };

    /** One node of the song's history tree (R-HIST-2): the song as an edit left it. */
    struct HistoryNodeModel
    {
        int id = 0, parent = -1;                 // `history jump <id>`; -1 = the root
        std::string label;                       // what made it ("set ch_2.gain", "New song")
    };

    /** R-MIDI-2: a MIDI input this machine has. */
    struct MidiPortModel
    {
        std::string id, name;
        bool listening = false;                  // Solaris is subscribed to it now
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
        std::vector<HistoryNodeModel> history;   // R-HIST-2: the song's history tree, oldest first
        std::string recovery;                    // R-SAFE-2: the open song has an autosave newer than it ("" = no) — its song's path
        double recoveryAge = -1;                 // … how old, seconds
        int historyCurrent = -1;                 // the node the song is now
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
        LoudnessModel loudness;                  // R-METER-1
        std::string armed;                       // R-MIDI-2: the strip the keyboard plays ("" none) — the session's
        std::string mapLearning;                 // R-CTRL-1: the address a `map learn` waits to bind ('' none) — the session's
        SurfaceModel surface;                    // R-CTRL-2
        std::vector<MidiPortModel> midiDevices;  // `midi devices`: this machine's MIDI inputs
        long long midiMessages = 0;              // messages the keyboard sent since Solaris started (an activity light)
        AuditionModel audition;
        std::vector<DeviceTypeModel> deviceTypes; // the registry, instruments first (R-DSP-2)
        std::vector<VocabModel> chords, scales, grooves; // R-ROLL-11: `--chord` qualities ("" major), `pattern scale`'s, `--groove`'s
        std::vector<MarkerModel> markers;        // R-ARR-1: by time
        std::vector<TempoModel> tempos;          // R-TEMPO-1: the tempo map's changes, by time (the start's is `bpm`)
        std::vector<PresetModel> presets;        // R-PRESET-1: every preset, by type then name (the machine's — any screen)
        std::vector<TemplateModel> templates;    // R-PRESET-2: every song template, by name (the machine's — any screen)
        std::vector<LibraryEntry> library;       // R-PRESET-3: every tagged sound, by path (the machine's — any screen)
        std::vector<PluginModel> plugins;        // R-VST-6: the VST3 plugins the last scan found (the machine's)
        std::vector<LaneGroupModel> groups;      // R-ARR-3: in the order of their first lanes
        std::vector<VersionModel> versions;      // R-VER: in creation order, each after its base
        std::string version;                     // R-VER: the open one's id, or "main" ("" with no song)
        std::vector<MissingModel> missing;       // R-COLLECT-1: files the song names that cannot be read (every version's)
        std::vector<SceneModel> scenes;          // R-LAUNCH-1: the session grid's rows, by order
        std::vector<SlotModel> slots;            // … its cells, by scene order then processing order
        std::string launchQuantize = "bar";      // R-LAUNCH-1: where a launch lands — bar | beat | none
        VideoModel video;                        // R-VIDEO-1: the song's picture ("" id: none)
        long long revision = 0;                  // bumps on every change — NOT in the stable dump
    };
}
}
