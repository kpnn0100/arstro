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
#include "AppModel.h"
#include "Colour.h"
#include "Command.h"
#include "Event.h"
#include "FrameSelector.h"
#include "FrameSource.h"
#include "Rack.h"
#include "Raster.h"
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
    struct ResolvedTimeline;
    namespace render { class GradeEngine; class FrameCache; }

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
        /** A NAMED timeline at `t` — what render and export-still use (R-RENDER-1). */
        bool renderTimelineFrame(const NodeId &timeline, double t, int proxyEdge, Raster &out, bool *anyClip = nullptr);
        /** The effective grade of a rack object in a timeline: colour source (live rack or pin),
         *  the version's overrides, the group fold. */
        bool gradeFor(const NodeId &timeline, const NodeId &rackObj, EditParams &out, std::string &err);

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
        struct RenderCtx;
        struct PreviewWorker;

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
        bool resolved(const NodeId &timeline, ResolvedTimeline &out, std::string &err) const;
        NodeId currentTimeline() const;
        NodeId timelineRef(const std::string &s) const;

        // transport + render
        bool playheadCommand(const Command &c);
        bool renderCommand(const Command &c);
        bool exportStill(const Command &c);
        void pumpJobs();
        Source *source(RenderCtx &ctx, const std::string &media);
        bool decodeLayer(RenderCtx &ctx, const struct PlanLayer &l, Raster &out);
        bool planFrame(const NodeId &timeline, double t, int proxyEdge, FramePlan &out, bool *anyClip);
        bool planReferenceFrame(int proxyEdge, FramePlan &out);
        bool executePlan(RenderCtx &ctx, const FramePlan &plan, Raster &out);
        void previewLoop();
        bool gradeForBypassing(const NodeId &timeline, const NodeId &rackObj, const std::set<NodeId> &groupsOff,
                               EditParams &out, std::string &err);
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
        std::map<NodeId, std::string> mStoredPath;   // #rackobj → the path Cosmo stores (selector key)
        std::map<std::string, std::pair<ColourTree, std::map<NodeId, int>>> mPins;

        // transport
        bool mPlaying = false;
        double mPlayFromT = 0, mPlayFromMs = 0;

        // render path
        std::unique_ptr<render::FrameCache> mCache;
        std::unique_ptr<RenderCtx> mSync;            // the UI thread's decoders + grade engine
        render::GradeEngine *mGrade = nullptr;       // == mSync->grade (settings reach it)
        std::unique_ptr<PreviewWorker> mPreview;     // last member: stopped first
        std::vector<std::unique_ptr<Job>> mJobs;
        int mNextJob = 1;

        // edit history, clipboard, settings, presets
        std::vector<UndoEntry> mUndo, mRedo;
        EditParams mClipboard;
        bool mHasClipboard = false;
        std::string mClipboardFrom;
        SettingsModel mSettings;
        std::vector<PresetModel> mPresets;
    };
}
}
