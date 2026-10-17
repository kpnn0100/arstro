/*
 *  solaris_core — SolarisService: THE application (R-SVC-1, arstro.rule §1).
 *
 *  One way in: a text line in the grammar (`dispatchText`), parsed by the table in Command.cpp.
 *  One way out: the `AppModel` (plain data, refreshed after every change) and the `Event` stream
 *  (every line also the log). `output()` is what the last command printed — `get`, `audit`,
 *  `matrix print`, `state print`, `api`.
 *
 *  The service carries no codec, no device API and no OS path (R-SVC-4): decoding a file, writing
 *  a WAV, listing a folder and the settings/recents files are the host's, injected as `Host`. A
 *  host that leaves one out gets a refusal naming it, never a silent no-op.
 *
 *  Every edit is all-or-nothing: the project is copied before a mutating command, the command
 *  runs, the result is validated (R-FMT-4, R-MIX-4), and a failure restores the copy and refuses
 *  with the validation's sentence — a refused command changes nothing.
 */
#pragma once
#include "AppModel.h"
#include "AudioIn.h"
#include "AudioOut.h"
#include "Command.h"
#include "Event.h"
#include "MixGraph.h"
#include "MidiIn.h"
#include "MidiOut.h"
#include "Surface.h"
#include "Player.h"
#include "PluginDevice.h"
#include "PluginHost.h"
#include "Auditioner.h"
#include "NotePreview.h"
#include "Project.h"
#include "Settings.h"
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct CompileResult;

    class SolarisService
    {
    public:
        struct Host
        {
            /** Decode `path` to PCM at `rate` (1 or 2 channels). False + `err` if it cannot. */
            std::function<bool(const std::string &path, int rate, engine::Pcm &out, std::string &err)> decodeAudio;
            /** Write `channels` (equal lengths) as a WAV at `rate`; bits 16 / 24 = PCM (x·2^(bits−1), clamped), 32 = float. */
            std::function<bool(const std::string &path, const std::vector<std::vector<float>> &channels, int rate,
                               int bits, std::string &err)> writeWav;
            /** R-EXPORT-1: write them as `format` — flac (bits 16 / 24), mp3, ogg (Vorbis), opus — at `kbps` for the
             *  lossy ones. Empty = this build has no encoders (the core holds no codec, R-SVC-4). */
            std::function<bool(const std::string &path, const std::vector<std::vector<float>> &channels, int rate,
                               const std::string &format, int bits, int kbps, std::string &err)> writeAudio;
            /** The entries of a folder (no "." / ".."). False + `err` if it cannot be listed. */
            std::function<bool(const std::string &path, std::vector<BrowserEntry> &out, std::string &err)> listDir;
            /** The machine's audio devices, inputs and outputs. */
            std::function<bool(std::vector<DeviceInfo> &out, std::string &err)> listDevices;
            /** A stream to the clock device (R-PLAY-1). Empty = this build cannot play. */
            std::function<std::unique_ptr<IAudioOut>()> audioOut;
            /** R-DEV-9: a stream through a named driver ("pipewire"); null when the build has none — `audioOut` (PulseAudio)
             *  is the fallback. */
            std::function<std::unique_ptr<IAudioOut>(const std::string &driver)> audioOutFor;
            /** R-MIDI-2: this machine's MIDI inputs, and a listener to them. Empty = this build has no MIDI input. */
            std::function<bool(std::vector<MidiPort> &out, std::string &err)> listMidiInputs;
            std::function<std::unique_ptr<IMidiIn>()> midiIn;
            /** R-CTRL-2: a MIDI output, for a control surface's faders, lights and text. Empty = none. */
            std::function<std::unique_ptr<IMidiOut>()> midiOut;
            /** R-REC-1: a stream from a capture device — what an armed audio strip monitors and records. Empty = this
             *  build cannot record audio. */
            std::function<std::unique_ptr<IAudioIn>()> audioIn;
            /** … through a named driver ("pipewire"), as `audioOutFor`; null when the build has none — `audioIn` the fallback. */
            std::function<std::unique_ptr<IAudioIn>(const std::string &driver)> audioInFor;
            std::string settingsPath; // the machine's settings file ("" = not persisted)
            std::string recentsPath;  // the recent songs, one path per line ("" = not persisted)
            std::string presetsPath;        // R-PRESET-1: the user's presets folder ("" = none can be saved)
            std::string factoryPresetsPath; // … the factory's, shipped with the app (never written)
            std::string templatesPath;        // R-PRESET-2: the user's song templates folder ("" = none can be saved)
            std::string factoryTemplatesPath; // … the factory's, shipped with the app (never written)
            std::string libraryPath;          // R-PRESET-3: the sample library — tags and favourites ("" = not kept)
            /** R-VST-6: every VST3 bundle where this machine keeps them, read out of process — what was found, and a
             *  line per bundle that could not be read. Empty = this build hosts no plugins. */
            std::function<bool(std::vector<PluginModel> &out, std::vector<std::string> &problems, std::string &err)> scanPlugins;
            std::string pluginsPath;          // … the machine's list of what was found ("" = not kept)
            /** R-VST-6: one instance of class `uid` in `bundle`, started where it cannot take the song with it (the
             *  host's: a process of its own) — null and why when it cannot be. Empty = this build plays no plugins. */
            std::function<std::unique_ptr<IPluginInstance>(const std::string &bundle, const std::string &uid, double rate, int maxBlock,
                                                           std::string &err)> loadPlugin;
        };

        explicit SolarisService(Host host = Host());
        ~SolarisService();
        SolarisService(const SolarisService &other);   // a copy never plays (getAddress probes on one)

        bool dispatchText(const std::string &line, std::string &err);
        bool dispatch(const Command &c, std::string &err);

        const AppModel &model() const { return mModel; }
        /** Bring the transport and meters into the model, free engines the player handed back. A
         *  host calls it on a timer while playing; `wait` calls it; it emits nothing. */
        void pump();
        bool auditionCommand(const Command &c, std::string &err);
        bool previewCommand(const Command &c, std::string &err);  // R-ROLL-7: `note preview`
        const std::string &output() const { return mOutput; }
        void subscribe(std::function<void(const Event &)> sink) { mSinks.push_back(std::move(sink)); }
        /** The open document (null on Home) — for tests and the host's title bar. */
        const Project *project() const { return mOpen ? &mProject : nullptr; }

    private:
        // dispatch groups (ServiceEdit.cpp, ServiceModel.cpp, ServiceRender.cpp)
        bool projectCommand(const Command &c, std::string &err);
        bool setAddress(const std::string &address, const std::string &value, std::string &stored, std::string &err);
        bool getAddress(const std::string &address, std::string &value, std::string &err) const;
        bool mixCommand(const Command &c, std::string &err);
        bool clipCommand(const Command &c, std::string &err);
        bool render(const Command &c, std::string &err);
        bool machineCommand(const Command &c, std::string &err);
        bool transportCommand(const Command &c, std::string &err);
        /** R-DEV-9: the clock device through the setting's driver, PulseAudio the fallback; `driver` says which. */
        std::unique_ptr<IAudioOut> openOutput(int rate, int block, std::string &driver, std::string &err);
        // R-MIDI-2: the keyboard — its ports, the armed strip, the input monitor (ServiceInput.cpp)
        bool inputCommand(const Command &c, std::string &err);
        bool ensureMidi(std::string &err);          // the inputs open, as `midiInputs` names them
        void startMonitor();                         // armed and stopped: the strip heard through the song's mix
        void stopMonitor();
        void drainMidi();                            // no consumer runs: what waited is stale
        int armedIn(const std::vector<std::string> &strips) const;
        bool autoCommand(const Command &c, std::string &err);   // ServiceAuto.cpp
        // R-SVC-8 (ServiceCompose.cpp): notes in bulk and by name, steps, pattern edits; the read-back views
        bool composeCommand(const Command &c, std::string &err);
        bool arrangeCommand(const Command &c, std::string &err); // markers (R-ARR-1), time insert / delete (R-ARR-2)
        bool timeCommand(const Command &c, std::string &err);    // R-ARR-2
        bool groupCommand(const Command &c, std::string &err);   // R-ARR-3: lane groups, a group moved, clips moved together
        bool tempoCommand(const Command &c, std::string &err);   // R-TEMPO-1 (ServiceArrange.cpp): the tempo map's points
        // R-PRESET-1 (ServicePresets.cpp): the presets of each device type, the user's and the factory's
        bool presetCommand(const Command &c, std::string &err);
        bool applyPreset(const std::string &dv, const std::string &name, std::string &err);
        std::string presetFile(const std::string &type, const std::string &name, bool factory) const;
        void scanPresets();
        std::vector<PresetModel> mPresets;
        bool mPresetsScanned = false;
        // R-PRESET-2 (ServicePresets.cpp): song templates — the user's and the factory's
        bool templateCommand(const Command &c, std::string &err);
        bool templateSong(const std::string &name, Project &out, std::string &err);
        void scanTemplates();
        std::vector<TemplateModel> mTemplates;
        bool mTemplatesScanned = false;
        // R-PRESET-3 (ServiceLibrary.cpp): the machine's sample library — path → its tags (sorted, lower case)
        bool libraryCommand(const Command &c, std::string &err);
        bool searchSamples(const std::string &folder, const std::string &text, bool favourites, std::string &err);
        void loadLibrary();
        bool saveLibrary(std::string &err);
        std::map<std::string, std::vector<std::string>> mLibrary;
        bool mLibraryLoaded = false;
        // R-VST-6 (ServicePlugins.cpp): other makers' plugins — the machine's list of what a scan found
        bool pluginsCommand(const Command &c, std::string &err);
        void loadPlugins();
        std::vector<PluginModel> mPlugins;
        bool mPluginsLoaded = false;
        // … and those a song plays: a connection per device, made on first need and kept across engine swaps (a render
        // and a note preview make their own); `vst3` devices' type is their connection's
        const PluginModel *findPlugin(const std::string &idOrName, std::string &err);
        bool pluginNode(const std::string &idOrName, DeviceNode &out, std::string &err);
        std::shared_ptr<PluginConnection> startPlugin(const DeviceNode &d, int rate, const std::string &state);
        std::shared_ptr<PluginConnection> livePlugin(const DeviceNode &d);
        /** The device's type: the registry's, or a plugin's from its connection (null when neither). */
        const DeviceType *typeOf(const DeviceNode &d);
        /** Who plays each `vst3` device of a compiled song: the live connections (Live); or new ones at the song's
         *  rate, from where the live ones stand — for another stream, with a budget (Own: a monitor, a note preview), or
         *  for a render, waited for (Render). */
        enum class PluginUse { Live, Own, Render };
        std::vector<std::pair<std::string, std::shared_ptr<PluginConnection>>> attachPlugins(CompileResult &cr, const Project &p, PluginUse use);
        void prunePlugins();             // connections of devices no longer in the song
        void capturePluginStates();      // each playing plugin's state into its node (before the song is written)
        bool pluginReload(const Command &c, std::string &err);
        DeviceModel pluginFacts(const DeviceNode &d, DeviceModel m);
        bool pluginEditor(const Command &c, std::string &err);
        /** What the plugins said on their own (`pump`): their editors' edits become `set` lines — applied when no
         *  command is running (else before the next one), and never sent back to the plugin they came from. */
        void gatherPluginEdits();
        void applyPendingEdits(); // a plugin editor's lines (not echoed back to it) and the MIDI controls' (R-CTRL-1)
        std::vector<std::string> mPluginEdits;
        std::set<std::string> mFromPlugin; // addresses a plugin itself set, while its line lands: not echoed
        int mDepth = 0;                    // commands running (a `wait` pumps from inside one)
        std::map<std::string, std::shared_ptr<PluginConnection>> mPluginLive;
        // R-EDIT-1 (ServiceAudioEdit.cpp): audio clips cut, joined, sliced — a cut is `splitClip`, which slice repeats
        bool audioEditCommand(const Command &c, std::string &err);
        // R-PERF-3: a strip's own sound rendered — frozen into it, or bounced onto a new strip
        bool freezeCommand(const Command &c, std::string &err);
        bool renderOwnSound(const Project &p, const std::string &strip, std::vector<std::vector<float>> &out, std::string &err);
        /** Beside the song: Bounces/<stem> <n>.wav, the next number no file there (and no clip) has, 32-bit float —
         *  the path written ("" refused: err says why). `clip consolidate` and `strip bounce` share it. */
        std::string writeBounce(const std::string &stem, const std::vector<std::vector<float>> &ch, std::string &err);
        bool splitClip(const std::string &id, double at, std::string &made, std::string &err);
        bool sliceToKit(const Clip &clip, double step, std::string &err);
        /** R-WARP-1: a stretch key's sound — its file's span (from `base`) stretched at `rate` by the DSP library. */
        static std::shared_ptr<const engine::Pcm> stretched(const std::string &key, const std::function<std::shared_ptr<const engine::Pcm>(const std::string &)> &base, int rate);
        /** R-TEMPO-1: a beat's sample in the song, and a sample's beat — through its tempo map (b·spb for one tempo). */
        double sampleOfBeat(double beat) const;
        double beatOfSample(double sample) const;
        /** R-TEMPO-1: the tempo at `beat`, and the change in effect there ('' before the first). */
        void tempoAt(double beat, double &bpm, std::string &id) const;
        /** R-WARP-1: a clip's own tempo heard in its span (the DSP library's estimate); 0 when none is. */
        double tempoOf(const Clip &c);
        bool consolidateClips(const Command &c, std::string &err);
        // R-REC-1: the capture — opened for the armed audio strip's input port; whether its monitoring is heard now;
        // the take landed as a WAV beside the song and a clip of it
        std::unique_ptr<IAudioIn> openInput(const Strip &s, int &channels, std::string &err);
        bool monitorHears(const Strip &s, bool playing, bool recording) const;
        void attachInput(Player &pl, const std::vector<std::string> &strips, bool playing, bool recording);
        bool commitAudioTake(Player::Take take, double outLatency, double inLatency, long long graphLatency, std::string &err);
        /** A marker's id → its beat, and its section's end — `transport seek|loop <mk>`. False: not a marker. */
        bool markerSpan(const std::string &id, double &from, double &to) const;
        bool readCommand(const Command &c, std::string &err);
        /** A pitch as written — a number, a name (C4 = 60) or a pad of the kit that plays `pattern`. */
        bool pitchOf(const std::string &text, const std::string &pattern, int &midi, std::string &err) const;
        /** "warning: …" naming notes that start at or after the pattern's end ("" = none). */
        std::string pastEndWarning(const Pattern &pt) const;
        std::string growToHold(Pattern &pt);          // R-ROLL-9: the pattern grows to the bar holding its last note's end; returns what it said
        std::string lsText() const;
        bool showText(const std::string &id, std::string &out, std::string &err) const;
        bool historyCommand(const Command &c, std::string &err); // undo / redo / history / history jump
        bool midiCommand(const Command &c, std::string &err);    // pattern import / export, project export-midi (R-MIDI-1)
        // R-SAFE: autosave and recovery
        static double steadySeconds();
        static std::string autosavePath(const std::string &song);
        static bool recoveryOf(const std::string &song, double &ageSeconds);
        bool autosave();                          // the unsaved song, written beside it now (atomically)
        void dropAutosave(const std::string &song);
        double mLastAutosave = 0.0;               // steady seconds of the last write (or of opening)
        std::string mRecovery;                    // the open song's autosave on offer (its song's path), "" = none
        double mRecoveryAge = -1.0;
        std::string labelOf(const Command &c) const;
        bool evalCommand(const Command &c, std::string &err);
        double songEndBeats() const;
        std::vector<std::string> readersOf(const std::string &name) const; // addresses whose formula reads `name`
        void dropBindingsOf(const std::set<std::string> &nodes);         // the formulas driving these nodes go
        void liveUpdate(const Command &c);            // after an edit while playing: live messages or an engine swap
        bool buildLive(std::unique_ptr<engine::Engine> &out, std::string &err);
        void stopPlayer();
        void endClipLoop();                           // R-ROLL-6: the song's loop back
        void followClipLoop();                        // … the looped clip moved, resized or gone
        void loadMachine();
        bool saveSettings(std::string &err);
        void touchRecent(const std::string &path);
        void refreshModel();
        void evaluateLive(double beat);               // bindings[].live at `beat`, by the engine's evaluator (R-MIX-16)
        void refreshNow(); // automations[].now at the playhead (R-AUTO-11)
        std::vector<std::string> audit() const;
        std::string matrixText(bool json) const;

        // helpers
        std::string resolvePath(const std::string &src) const;      // a clip's src → a path the host opens
        std::string relativePath(const std::string &file) const;    // a file → what a clip stores
        std::shared_ptr<const engine::Pcm> pcmFor(const std::string &src);
        /** R-INST-9: a definition's text (an SFZ) for a path as the song stores it — the file it read in `resolved`. */
        bool textFor(const std::string &src, std::string &text, std::string &resolved);
        /** R-EDM-8: a sampler's sound — resolved as a clip's src is, and refused if it cannot be read. */
        bool resolveSample(const std::string &given, std::string &stored, std::string &err, bool definition = false);
        std::string defaultOutFor(const std::string &mixerId) const; // "Main": the bus leaving the nearest later mixer, else its own
        std::string firstMixer() const;
        std::string secondMixer() const;
        void emit(const Event &e);
        void changed(const std::string &what, const std::string &node);
        bool requireOpen(std::string &err) const;

        Host mHost;
        Project mProject;
        bool mOpen = false;
        std::string mPath;
        AppModel mModel;
        std::string mOutput;
        std::vector<std::function<void(const Event &)>> mSinks;
        std::vector<Event> mPending; // an edit's events, emitted only once the whole command has landed
        bool mBindingsTouched = false; // a formula or an automation changed: playback needs a new engine
        // R-HIST-1: the history is a TREE of the song as each edit left it, keyed by an id that is never reused
        struct HistNode { Project project; std::string label; int parent = -1; std::vector<int> kids; };
        std::map<int, HistNode> mHist;   // ascending id = the order they were made
        int mHistCur = -1, mHistNext = 0;
        std::string mCoalesce;           // the newest node's merge key: `set` of the same addresses (R-EDM-1)
        static constexpr size_t kHistory = 200;
        void historyReset(const std::string &rootLabel); // "" = no song: no tree
        void historyAdd(const std::string &label);       // the song now, a child of the current node
        void historyPrune();
        std::map<std::string, std::string> mLastChanged; // device id → the parameter last written (R-WIN-2)
        std::vector<engine::Curve> mNowCurves;           // model.automations[i] compiled: its `now` at the playhead (R-AUTO-11)
        double mNowSpb = 1.0;                            // … at this many samples per beat
        std::map<std::string, std::shared_ptr<const engine::Pcm>> mPcm; // by resolved path, at mPcmRate
        std::set<std::string> mOffline;                                 // resolved paths that would not decode
        int mPcmRate = 0;
        std::vector<std::string> mAudit, mLastRenderPeaks;
        std::set<std::string> mClipLatch;  // R-METER-2: strips that reached full scale since `meters clear`
        bool mMasterClip = false;          // … and the master
        bool mPipewireDown = false;        // R-DEV-9: PipeWire would not open this session — PulseAudio until the driver is set again
        std::map<std::string, double> mDriftOf;           // R-DEV-2: each follower's drift as last measured (a play starts locked)
        std::map<std::string, std::string> mFollowerDown; // R-DEV-1: followers this play could not open, and why
        // R-MIDI-2: the queue the keyboard's thread fills and the running player (or monitor) drains; created by ensureMidi
        std::shared_ptr<LockFreeQueue<MidiEvent>> mMidiQ;
        // R-CTRL-1 (ServiceControl.cpp): controllers and MIDI Machine Control, for the service (its `pump`), not a strip
        std::shared_ptr<LockFreeQueue<MidiEvent>> mCtlQ;
        std::string mMapLearning;            // the address a `map learn` waits to bind
        bool mControlTried = false;          // the input was asked to open for the mappings (said once if it cannot)
        double mCpuHeld = 0, mCpuHeldAt = 0; // R-PERF-1: the worst block, held a second
        std::vector<std::string> mControlLines; // what controls moved and keys pressed, as lines — landed as an editor's
        bool mapCommand(const Command &c, std::string &err);
        void controlMidi();
        bool controlValue(const std::string &address, double travel, std::string &text);
        bool mapInert(const std::string &address) const;
        std::shared_ptr<std::atomic<long long>> mMidiCount = std::make_shared<std::atomic<long long>>(0);
        std::unique_ptr<IMidiIn> mMidiIn;
        std::string mMidiOpenFor;          // the `midiInputs` (and the surface's port it leaves out) it was opened with
        std::string midiFor() const { return mSettings.midiInputs + (mSettings.surface != "none" ? "|" + mSettings.surfacePort : std::string()); }
        // R-CTRL-2 (ServiceSurface.cpp): a control surface on its own port — its codec, what it was last sent, its bank
        void surfacePump();
        void startSurface();
        void stopSurface();
        SurfaceState surfaceState() const;
        std::unique_ptr<SurfaceCodec> mSurfaceCodec;
        std::unique_ptr<IMidiIn> mSurfaceIn;
        std::unique_ptr<IMidiOut> mSurfaceOut;
        std::shared_ptr<LockFreeQueue<MidiEvent>> mSurfaceQ;
        std::string mSurfaceFor = "none|", mSurfaceWhy; // the settings it was started for; why it is not connected
        SurfaceState mSurfaceShown;
        std::array<bool, kSurfaceStrips + 1> mSurfaceTouched{};
        bool mSurfaceAll = true;
        int mSurfaceBank = 0;                // the first of its eight strips, in processing order
        double mSurfacePing = 0.0;
        std::string mArmed;                // the armed strip ("" none) — the session's, not the song's
        std::unique_ptr<Player> mMonitor;
        std::vector<std::string> mMonitorStrips;
        std::vector<MidiPortModel> mMidiPorts; // what `midi devices` last listed
        // R-MIDI-2: a recording — what the player kept, where it began, how it lands
        bool mRecording = false, mOverdub = false;
        bool mRecordingAudio = false;          // R-REC-1: the take is the armed audio strip's input
        std::string mInputErr;                 // R-REC-1: why the last input could not be opened
        double mPunchFrom = 0, mPunchTo = 0;   // R-REC-1: beats; equal = no punch (the session's)
        std::string mRecordStrip;
        double mRecordFrom = 0;                // beats: where the song began recording
        std::vector<Player::Played> mTaken;
        bool commitRecording(long long stoppedAt, std::string &err); // at the stop: the take into a clip (an edit)
        long long mCountInFrames = 0;          // a recording's count-in, for the play it starts
        Settings mSettings;
        std::vector<std::string> mRecents;
        std::vector<RecentModel> mRecentCards; // parsed from the files: rebuilt only when stale
        bool mRecentsStale = true;
        std::vector<DeviceInfo> mDevices;
        BrowserModel mBrowser;
        std::unique_ptr<Player> mPlayer;
        std::unique_ptr<Auditioner> mAudition;   // R-EDM-9: the browser's preview, its own stream
        std::unique_ptr<NotePreview> mPreview;   // R-ROLL-7: a note heard through a strip, its own stream
        std::vector<std::string> mLiveStrips;                         // engine strip index → strip id
        std::map<std::string, std::pair<int, int>> mLiveDevices;      // device id → (strip index, rack index)
        std::vector<std::string> mLiveBinds;                          // engine bind index → the address it drives
        std::vector<std::pair<std::string, int>> mLiveReadings;       // R-FX-17: the engine's devices that measure, in its
                                                                      // readingValues order — id and how many readings
        std::vector<double> mLiveReadingValues;
        int mLiveGen = 0;                                             // bumps with every engine built to play
        int mPlayGen = 0;                                             // … the one a play started with (transport.swaps)
        bool mMono = false;                                           // R-MIX-20: the mono check — the listener's
        std::vector<double> mLiveValues;                              // the player's, read in `pump`
        std::map<std::string, int> mLiveLatency;                      // device id → its latency in the playing engine (R-MIX-17)
        std::map<std::string, std::shared_ptr<Device>> mLatencyProbes; // per type: what its stored values make its latency
        int latencyOf(const DeviceNode &d);
        double mPosition = 0, mLoopFrom = 0, mLoopTo = 0;             // beats
        double mCue = 0;                                              // R-TIME-7: where the user last put the time bar (a seek, or where play began)
        std::string mClipLoop;                                        // R-ROLL-6: the clip `transport play --clip` loops ('' = the song's loop)
        double mSongLoopFrom = 0, mSongLoopTo = 0;                    // … and the song's loop, given back when it ends
    };
}
}
