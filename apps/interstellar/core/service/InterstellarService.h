/*
 *  interstellar_core — InterstellarService: the whole application without a window (R-SVC-1).
 *
 *      dispatch(Command) · dispatchText(line) · pump(nowMs) · model() · renderFrame(t)
 *
 *  The GUI, `interstellar-cc` and an agent are peers over this one object. It owns:
 *   - the RACK — a hosted CosmoService, the only colour authority (R-RACK);
 *   - the PROJECT — the `.isp` document, its timelines/versions and arrangement (R-VER, R-TL);
 *   - the RENDER PATH — active set → source volume → grade → composite (R-VOL, R-FX, R-RENDER).
 *
 *  **Addresses route by owner** (`set <address>=<value>`, docs/API.md §Addresses): a rack colour key
 *  writes THROUGH to Cosmo on a root timeline and becomes the current version's `#tlgrade` on a
 *  derived one; a pinned version refuses colour, naming its pin.
 *
 *  **A pin is a byte copy of the `.cmp`**, content-addressed (`pin@<commit>`, the commit being a
 *  hash of the bytes), stored beside the project in `<stem>.pins/`. It is read back through Cosmo's
 *  own static reader and folded by the one fold (Colour.h). A pin never becomes a second authority:
 *  it is a frozen historical copy the user asked for, read-only by construction (R-VER-3).
 *
 *  The core carries no codec and no OS paths (R-SCOPE-3): the host injects every one through `Host`.
 */
#pragma once
#include "Anim.h"
#include "AppModel.h"
#include "Colour.h"
#include "Command.h"
#include "Event.h"
#include "FrameSelector.h"
#include "Effects.h"
#include "AudioOut.h"
#include "AudioSource.h"
#include "FrameSource.h"
#include "Rack.h"
#include "Raster.h"
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class Project;
    struct Clip;
    struct AnimKey;
    struct ResolvedTimeline;
    namespace render {
        struct AudioPlan; class GradeEngine; class FrameCache; }

    class InterstellarService
    {
    public:
        struct Host
        {
            /** Cosmo's image decoder. Handed the frame selector so a video path decodes the frame
             *  the `.isp` names, not the one baked into the path (R-RACK-3). */
            std::function<std::unique_ptr<cosmo::IImageDecoder>(std::shared_ptr<const FrameSelector>)> rackDecoder;
            /** A source for the TIMELINE: video frames, or a still as a one-frame source (R-VOL-6). */
            std::function<std::unique_ptr<IFrameSource>()> frameSource;
            /** A decoder for a file's SOUND, read at the mix rate as stereo (R-AUD-5 amended); unset
             *  = the timeline is silent (renders carry no audio, playback is picture only). */
            std::function<std::unique_ptr<IAudioSource>()> audioSource;
            /** The machine's sound output (R-AUD-6); unset = playback is picture only (tests, the CLI). */
            std::function<std::unique_ptr<IAudioOut>()> audioOut;
            /** R-MEDIA-1: whether a vendor RAW decoder (an SDK build) is installed for `ext` ("r3d",
             *  "braw", "ari"); unset = none is, and such a file is refused naming its SDK. */
            std::function<bool(const std::string &ext)> hasVendorDecoder;
            /** An encoder for `path`, chosen by its extension (h264 .mp4, prores .mov). */
            std::function<std::unique_ptr<IFrameWriter>()> frameWriter;
            /** A PNG writer, for stills and png sequences. */
            std::function<bool(const std::string &path, const Raster &frame, std::string &err)> writeImage;
            /** The recents index; "" = none (tests). */
            std::string recentsPath;
            /** Engine settings file (cpuPercent, threads, previewEdge, useGpu, uiScale); "" = not
             *  persisted. */
            std::string settingsPath;
            /** The `.apf` preset library; "" = presets unavailable. */
            std::string presetDir;
            /** Render the MONITOR on a worker thread: `renderFrame` returns at once with the last
             *  finished frame and the requested one lands later (frameSeq rises). A window wants
             *  this (D-5); a test or a script wants the default, synchronous answer. */
            bool asyncPreview = false;
        };

        using EventSink = std::function<void(const Event &)>;

        InterstellarService(cosmo::ThreadBudget &budget, Host host);
        ~InterstellarService();

        void subscribe(EventSink s) { mSinks.push_back(std::move(s)); }

        bool dispatch(const Command &c);
        /** Parse and dispatch one line; a refusal fills `err` and emits `command.rejected`. */
        bool dispatchText(const std::string &line, std::string &err);
        /** Advance the clock: rack loads, playback, render jobs. Never blocks. */
        void pump(double nowMs);
        /** Something is in flight (a rack load, a render). A CLI pumps until this is false. */
        bool busy() const;
        /** Pump against the wall clock until idle or `maxMs`; false on timeout. */
        bool pumpUntilIdle(int maxMs = 120000);

        const AppModel &model() const { return mModel; }
        /** Text a query command produced (`get`, `eval`, `timeline list`, `state print`, `api`, …). */
        const std::string &output() const { return mOutput; }
        bool quitRequested() const { return mQuit; }

        /** The current timeline composited at `t`; `proxyEdge` = 0 for full resolution. When no clip
         *  is live at `t` and a rack node is selected, its reference frame graded — so Grade is
         *  usable before anything is cut. */
        bool renderFrame(double t, int proxyEdge, Raster &out);
        /** One rack source (bind name or id) graded as the open version resolves it, at source time
         *  `t` (`t < 0` = its reference frame), fitted to `proxyEdge` — Grade's monitor and the
         *  reference-frame slider's live preview (R-UI-3, R-RACK-3). Asynchronous like renderFrame
         *  under `asyncPreview`. */
        bool renderSourceFrame(const std::string &bind, double t, int proxyEdge, Raster &out);
        /** What the monitor shows, at FULL resolution, now (R-UI-11): `bind` names a source (its
         *  reference frame, Grade), "" = the current timeline at the playhead. For the host's
         *  clipboard copy; `capture --out` saves the same pixels. */
        bool captureFrame(const std::string &bind, Raster &out);
        /** R-PLAY-2, for benches and tests: the monitor's frames while playing that came ready from
         *  the read-ahead pool (hits) or not (misses), and the long edge playback grades at. */
        struct PlaybackStats
        {
            long long hits = 0, misses = 0;   // the frame due was ready / was not
            int edge = 0;
            double rate = 0;                  // frames the pool finished per second
            long long shown = 0;              // frames handed to the monitor while playing
            double lagFrames = 0;             // how far behind the playhead the shown picture was, summed
            long long fromCache = 0;          // shown frames decoded from the preview cache (R-PLAY-1)
        };
        /** R-PLAY-1: the long edge the preview cache is built at — playback's best size, under Preview quality. */
        int cacheEdge() const;
        PlaybackStats playbackStats() const { return {mAheadHits, mAheadMisses, mPlayEdge, mPlayRate, mAheadShown, mAheadLag, mCacheShown}; }
        /** A NAMED timeline at `t` — what render and export-still use (R-RENDER-1). */
        bool renderTimelineFrame(const NodeId &timeline, double t, int proxyEdge, Raster &out, bool *anyClip = nullptr, bool deep = false);
        /** R-AUD-7: a file's waveform envelope — the peak of |L|,|R| per 1/perSecond s — once computed
         *  (false until then; `model().peaksEpoch` rises when one lands). Thread-safe. */
        bool audioPeaks(const std::string &media, std::vector<float> &peaks, double &perSecond);
        /** The effective grade of a rack object in a timeline: colour source (live rack or pin),
         *  the version's overrides, the group fold. */
        bool gradeFor(const NodeId &timeline, const NodeId &rackObj, EditParams &out, std::string &err, double srcT = -1.0);

        Rack &rack() { return mRack; }
        const Project &project() const;
        std::shared_ptr<FrameSelector> frameSelector() const { return mFrames; }

    private:
        struct Source;
        struct Job;
        struct PendingRack;
        struct UndoState;
        struct UndoEntry;
        struct FramePlan;
        friend struct PlanLayer;   // R-EDT-4: a layer may hold a nested timeline's plan
        struct RenderCtx;
        struct PreviewWorker;
        struct AheadPool;
        struct AudioPlayer;   // R-AUD-6/8: the sound thread (ServiceAudio.cpp)
        struct PeakStore;     // R-AUD-7: waveform envelopes (ServiceAudio.cpp)
        struct PreviewCache;

        void emit(const Event &e);
        bool dispatchInner(const Command &c);
        void resetPreview();
        void fillEditModel();
        bool fail(const std::string &why);
        bool requireProject();
        void refreshModel();
        void bumpFrame();
        void markDirty();

        // project lifecycle
        bool projectNew(const Command &c);
        bool projectOpen(const std::string &path);
        bool projectSave(const std::string &path);
        void projectClose();
        void finishRackLoad();
        void loadRecents();
        void touchRecent(const std::string &path);

        // rack
        /** True (and `why` filled) when making Cosmo save now would DELETE an offline source and
         *  its grade — Cosmo's save skips every image without a slot (D-2, Cosmo D-66). */
        bool rackSaveBlocked(std::string &why) const;
        bool rackCommand(const Command &c);
        void bindFromCmp(const std::vector<ColourNode> &entries);
        void rebindAfterLoad();
        void syncRackObjNodes();
        int cosmoNodeOf(const NodeId &rackObj) const;
        NodeId rackObjOfCosmo(int cosmoNode) const;
        std::string bindNameFor(const std::string &cosmoName) const;
        std::string resolvePath(const std::string &p) const;
        std::string relativePath(const std::string &p) const;

        // colour
        bool colourTreeFor(const NodeId &timeline, ColourTree &tree, std::map<NodeId, int> &indexOf,
                           std::string &source, std::string &err);
        bool pinSnapshot(const std::string &commit, ColourTree &tree, std::map<NodeId, int> &indexOf, std::string &err);
        std::string pinsDir() const;
        bool takePin(const NodeId &timeline, std::string &commit, std::string &err);

        // addresses
        bool setAddress(const std::string &address, const std::string &value);
        bool getAddress(const std::string &address, bool evaluate, const NodeId &timeline, bool explain);
        bool revertAddress(const std::string &address, const NodeId &timeline);

        // versions + arrangement
        bool timelineCommand(const Command &c);
        bool arrangeCommand(const Command &c);
        bool effectCommand(const Command &c);
        /** The plugins a source's pixels pass through after Cosmo (R-FX-5): its own enabled effects
         *  in order, then each ancestor group's, inner first — and a cache-key string of all of it. */
        void effectChain(const NodeId &roId, std::vector<render::EffectRun> &out, std::string &key, double srcT = -1.0) const;
        bool resolved(const NodeId &timeline, ResolvedTimeline &out, std::string &err) const;
        NodeId currentTimeline() const;
        NodeId timelineRef(const std::string &s) const;

        // transport + render
        bool playheadCommand(const Command &c);
        bool renderCommand(const Command &c);
        bool exportStill(const Command &c);
        bool lutExport(const Command &c);   // R-COLOR-6 (ServiceRender.cpp)
        bool interchangeCommand(const Command &c);   // R-XCH (ServiceInterchange.cpp)
        bool editingCommand(const Command &c);       // R-EDT-1/2 (ServiceEditing.cpp)
        /** R-EDT-3: the speed ramp of a clip, or null when its speed is a constant (ServiceEditing.cpp). */
        const anim::Ramp *rampFor(const Clip &c) const;
        void retimeRamps();
        /** R-EDT-5: the top-most clip under `t` showing an angle of a placed timeline — or, when
         *  `anyNested`, failing that the top-most placing a timeline at all. "" = none. */
        NodeId multicamAt(const ResolvedTimeline &R, double t, bool anyNested) const;
        /** R-EDT-5: a timeline's angles — its video tracks, by order (angle k = element k-1). */
        std::vector<NodeId> anglesOf(const NodeId &timeline) const;
        void pumpJobs();
        Source *source(RenderCtx &ctx, const std::string &media);
        bool decodeLayer(RenderCtx &ctx, const struct PlanLayer &l, Raster &out, bool deep = false);
        bool planFrame(const NodeId &timeline, double t, int proxyEdge, FramePlan &out, bool *anyClip);
        /** R-EDT-4: `stack` = the timelines being planned around this one (a nested clip plans its
         *  timeline here, without the view transform — it is a picture in the working space). */
        bool planFrameIn(const NodeId &timeline, double t, int proxyEdge, FramePlan &out, bool *anyClip, std::vector<NodeId> &stack,
                         int angle = 0);   // R-EDT-5: > 0 = only that video track (a multicam clip's angle)
        // R-AUD-5 (amended), R-AUD-9 — ServiceAudio.cpp
        bool planAudio(const NodeId &timeline, render::AudioPlan &out);
        bool planAudioIn(const NodeId &timeline, render::AudioPlan &out, std::vector<NodeId> &stack);
        IAudioSource *audioSourceFor(RenderCtx &ctx, const std::string &media, int rate);
        IAudioSource *audioSourceIn(std::map<std::string, std::unique_ptr<IAudioSource>> &cache, const std::string &media, int rate);
        void startSound();                 // playback heard from the playhead (R-AUD-6)
        void stopSound();
        void soundGrain(double t);         // a scrub's short grain
        bool soundTime(double &t);         // the audio clock: what is heard now, once it runs
        void syncSoundPlan();              // an edit while playing reaches the ear
        void playerLoop();
        void peaksLoop();
        void requestPeaks(const std::string &media);
        void fillSoundModel(AppModel &m);
        bool planReferenceFrame(int proxyEdge, FramePlan &out);
        bool planSourceFrame(const NodeId &rackObj, double t, int proxyEdge, FramePlan &out);
        bool present(FramePlan &&plan, Raster &out);
        bool executePlan(RenderCtx &ctx, const FramePlan &plan, Raster &out, bool remember = true, bool deep = false);
        void previewLoop();
        // R-PLAY-2: while playing, grade the frames after the playhead in parallel
        void aheadLoop(size_t worker);
        void scheduleAhead();
        int playEdge(int requested) const;
        // R-ANIM: keyframes (ServiceAnim.cpp)
        struct AnimTarget
        {
            NodeId node;
            std::string key, cosmoKey, address, owner;   // owner: rack | effect | clip
            bool clipClock = false, inherited = false, shape = false;   // shape: a curve, a wheel, a crop (R-ANIM-6)
            double now = 0, lo = -1e300, hi = 1e300, staticValue = 0;
        };
        struct PinCurve
        {
            NodeId node;
            std::string key;
            std::vector<anim::Key> keys;
            std::vector<anim::ShapeKey> shapes;
        };
        using PinCurves = std::vector<PinCurve>;
        double sourceNow(const NodeId &roId) const;
        bool animTarget(const std::string &address, AnimTarget &out, std::string &why);
        double staticValue(const AnimTarget &t);
        double animatedValue(const AnimTarget &t);
        NodeId rootOf(const NodeId &tl) const;
        bool curveEditable(const AnimTarget &t);
        bool upsertKey(const AnimTarget &t, double at, double v, const anim::Key *shape, const std::string *text = nullptr);
        std::string staticText(const AnimTarget &t);
        bool writeStaticText(const AnimTarget &t, const std::string &text);
        bool writeStatic(const AnimTarget &t, double v);
        bool setAnimated(const std::string &address, const std::string &value, bool &handled);
        bool animCommand(const Command &c);
        bool keysCommand(const Command &c);   // key shift | copy | paste (R-ANIM-7)
        struct KeyClip
        {
            NodeId node;
            std::string key;
            bool shape = false;
            std::vector<AnimKey> keys;   // times relative to the clipboard's earliest
        };
        std::vector<KeyClip> mKeyClipboard;
        void pruneAnims();
        void copyAnims(const NodeId &from, const NodeId &to, double shift);
        void applyColourCurves(const NodeId &tl, ColourTree &tree, const std::map<NodeId, int> &idx, double srcT);
        double curveAt(const NodeId &node, const std::string &key, double t, double fallback) const;
        std::string colourCurveText() const;
        const PinCurves *pinCurvesFor(const NodeId &tl);
        void fillAnimModel(AppModel &m);
        std::map<std::string, PinCurves> mPinCurves;
        std::vector<std::pair<std::string, std::vector<anim::Key>>> mClipClipboardAnims;   // the copied clip's curves
        // R-PLAY-1: the graded preview cache (ServiceCache.cpp)
        bool cacheCommand(const Command &c);
        void pumpPreviewCache();
        void cacheLoop();
        void stopCache();
        bool cacheLookup(long long frame, const FramePlan &atCacheEdge, std::string &file, long long &index, int &w, int &h) const;
        void fillCacheModel(AppModel &m) const;
        bool gradeForBypassing(const NodeId &timeline, const NodeId &rackObj, const std::set<NodeId> &groupsOff,
                               EditParams &out, std::string &err, double srcT = -1.0);
        double timelineDuration(const NodeId &timeline) const;

        bool lint();
        bool wait(const Command &c);

        // edit / settings / presets (ServiceEdit.cpp)
        bool editCommand(const Command &c);
        static bool undoable(Command::Kind k);
        static bool structural(Command::Kind k);
        void captureState(UndoState &out) const;
        bool applyState(const UndoState &s, std::string &err);
        void recordEdit(const Command &c, const UndoState &before);
        void clearHistory();
        bool settingsCommand(const Command &c);
        void loadSettings();
        void applySettingsNow();
        bool saveSettings() const;
        void rescanPresets();

        cosmo::ThreadBudget &mBudget;
        Host mHost;
        std::shared_ptr<FrameSelector> mFrames;
        Rack mRack;
        std::unique_ptr<Project> mProject;
        std::string mIspPath;
        bool mOpen = false;
        std::unique_ptr<PendingRack> mPending;

        AppModel mModel;
        std::vector<EventSink> mSinks;
        std::string mOutput;
        bool mQuit = false;
        double mNowMs = 0;
        unsigned mProjectRev = 0;
        NodeId mSelectedClip;

        // rack binding: #rackobj id ↔ live Cosmo node id
        std::map<NodeId, int> mNodeOf;
        std::map<NodeId, std::string> mStoredPath;
        std::vector<NodeId> mSelection;              // #rackobj ids, in click order (R-RACK-8)
        NodeId mAnchor;                              // the last plain/added click: a range starts here   // #rackobj → the path Cosmo stores (selector key)
        std::map<std::string, std::pair<ColourTree, std::map<NodeId, int>>> mPins;

        // transport
        bool mPlaying = false;
        double mPlayFromT = 0, mPlayFromMs = 0;

        // render path
        std::unique_ptr<render::FrameCache> mCache;
        std::unique_ptr<RenderCtx> mSync;            // the UI thread's decoders + grade engine
        render::GradeEngine *mGrade = nullptr;       // == mSync->grade (settings reach it)
        std::unique_ptr<PreviewWorker> mPreview;     // last member: stopped first
        std::unique_ptr<AheadPool> mAhead;           // stopped in the destructor, before mPreview
        std::unique_ptr<PreviewCache> mPCache;       // made when first wanted; stopped first in the destructor
        std::unique_ptr<AudioPlayer> mPlayer;        // R-AUD-6: made when the host has a sound output
        std::unique_ptr<PeakStore> mPeaks;           // R-AUD-7: made when the host can decode sound
        bool mSoundClock = false;                    // the audio clock drives the playhead now
        // R-EDT-1/2: the shuttle rate, the marks, the source viewer, the target track
        double mShuttle = 0.0;
        double mMarkIn = -1.0, mMarkOut = -1.0;
        std::string mSourceView;
        double mSourceIn = -1.0, mSourceOut = -1.0, mSourcePlayhead = 0.0;
        NodeId mTargetTrack;
        mutable std::map<NodeId, std::pair<std::string, anim::Ramp>> mRamps;   // R-EDT-3: by clip, with what built it
        unsigned mSoundSeq = ~0u;                    // the frame sequence the player's plan was made at
        unsigned mEpoch = 0;                         // rises on every command and rack load: the cache re-checks
        double mLastCommandMs = -1e9;                // the cache builds when the user has stopped for a moment
        bool mCacheForced = false;                   // `cache build`: now, whatever the idle rule says
        long long mCacheShown = 0;
        std::atomic<bool> mGpuWanted{false};         // R-GPU-1: the grade on Cosmo's GPU backend (read by every render thread)
        bool mLastFromCache = false;                 // the last frame shown while playing came from the cache
        int mPlayEdge = 0;                           // the long edge playback grades at now (0 = not playing)
        int mLastMonitorEdge = 0;                    // the edge the monitor last asked for
        bool mPreroll = false;                       // Play pressed: the clock waits for the first frames
        double mPrerollFromMs = 0;
        double mRateFromMs = 0;                      // the read-ahead rate's window
        long long mRateFromDone = 0;
        double mPlayRate = 0;                        // frames the pool finished per second, last window
        long long mAheadHits = 0, mAheadMisses = 0, mAheadShown = 0;
        double mAheadLag = 0;
        std::set<long long> mAheadPlanned;           // frames already handed to the pool at the current edge
        int mAheadPlannedEdge = 0;
        unsigned mAheadPlannedRevision = ~0u;
        std::vector<std::unique_ptr<Job>> mJobs;
        int mNextJob = 1;

        // edit history, clipboard, settings, presets
        std::vector<UndoEntry> mUndo, mRedo;
        EditParams mClipboard;
        bool mHasClipboard = false;
        std::string mClipboardFrom;
        // `clip copy` (R-TL-6): the clip as the current version resolved it when copied
        bool mHasClipClipboard = false;
        std::shared_ptr<Clip> mClipClipboard;   // a pointer: the header carries no Project.h
        SettingsModel mSettings;
        std::vector<PresetModel> mPresets;
    };
}
}
