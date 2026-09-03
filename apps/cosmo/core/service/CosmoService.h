/*
 *  Arstro cosmo_core — CosmoService: the application, with no front end attached.
 *
 *  R-SVC-1. Commands in, AppModel + Events out (R-SVC-2/3), and nothing above it holds
 *  behaviour. The GTK window, `cosmo-cc` and the shot renderer are three views of one of
 *  these.
 *
 *  ── Two things about the shape, both deliberate ────────────────────────────────────
 *
 *  **It borrows the session, it does not own it (yet).** `CosmoService(EditSession&, …)`.
 *  App has owned `mSession` since before any of this existed, and reparenting that
 *  ownership in the same step as introducing the service would mean rewriting App and the
 *  service at once, with no working state in between. So S2 wraps the existing session and
 *  the GUI keeps rendering from it; S4 moves ownership in and App becomes a view holding a
 *  `CosmoService&`. The CLI already constructs its own session and never involves App.
 *
 *  **It never blocks and never starts a thread on its own initiative** (R-SVC-6).
 *  `pump(nowMs)` drains finished decode results, refreshes the model and emits events. A
 *  GTK timeout calls it at frame rate, a CLI loop calls it as fast as it likes, a test
 *  calls it at a fixed 16 ms tick and gets the same sequence every run. The threads that do
 *  exist — the decode pool and the render worker — are sized by ThreadBudget (R-SVC-10) and
 *  are the same ones as before.
 *
 *  ── What it does not do ───────────────────────────────────────────────────────────
 *
 *  No file dialogs: a Command carries a path, so choosing one stays a host concern. No
 *  logging and no getenv, per the cosmo_core rule — it emits Events and the host writes
 *  them, which under R-SVC-5 is the same thing. No encoding: `Export` renders and hands
 *  bytes to an injected writer, because a codec in cosmo_core would break Android/WASM.
 */
#pragma once
#include "../EditSession.h"
#include "../ProjectLoader.h"
#include "../ThreadBudget.h"
#include "AppModel.h"
#include "Command.h"
#include "Event.h"
#include <functional>
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    class CosmoService
    {
    public:
        using EventSink = std::function<void(const Event &)>;
        /** Injected so cosmo_core carries no codec (R-SVC-7). Returns false + an error on
         *  failure; the service turns that into an ExportProgress/Error event. */
        using ImageWriter = std::function<bool(const std::string &outPath, const std::string &sourcePath,
                                               const uint8_t *rgba, int w, int h, std::string &err)>;

        /** **The service owns the session** (S4c). It borrowed one through S2/S3 so the GUI
         *  could keep working while the migration ran; owning it is what lets the frame path
         *  live here too, which is what D-21 needed. A front end now constructs the service
         *  first and hands it to the view, rather than the other way round. */
        explicit CosmoService(ThreadBudget &budget);

        // ── wiring the host supplies once ──
        /** The host's decoder, used for the project load AND — since R-MEM-2 — to re-decode
         *  a slot the engine evicted. Installing it here wires both, so there can be no
         *  second, unbudgeted decode path (the D-41 shape). */
        /** Install a real segmentation model (R-AISEG-15). The FACTORY, not the model: opening
         *  one means reading a file and loading a shared library, and `cosmo_core` may do
         *  neither — the same reason `setDecoderFactory` exists and takes the same shape. Called
         *  before the first render; a factory that returns nullptr (no model installed, runtime
         *  missing, manifest broken) leaves the built-in classifier answering, which is not an
         *  error state but the ordinary one. */
        void setSegmenterFactory(std::function<std::unique_ptr<ISegmenter>()> f);
        /** Which segmenter is answering: "built-in", or the installed model's name. In the
         *  model because a photographer looking at a mask needs to know which of two very
         *  different things produced it, and a script asserting on a mask needs to know that its
         *  answer depends on what is installed. */
        void setDecoderFactory(ProjectLoader::DecoderFactory f);
        /** Runs on every decode worker before it works — the OpenMP pin (R-CPU-2c, D-12). */
        void setWorkerInit(std::function<void()> f) { mWorkerInit = std::move(f); }
        void setImageWriter(ImageWriter w) { mWriter = std::move(w); }
        void subscribe(EventSink s) { mSinks.push_back(std::move(s)); }
        /** Put the persisted preferences in force and into the model (R-SETTINGS-4). The host
         *  calls this once at startup with what `AppSettings::load()` returned, instead of
         *  applying the pieces itself — D-15: it used to push the percentage into the budget
         *  and the edge into the session and never tell the service, so `model().settings`
         *  reported defaults while `model().budget` reported the truth. Two halves of one
         *  answer disagreeing is worse than either being wrong. */
        void applySettings(const AppSettings &s);

        // ── the whole interface (R-SVC-2/3) ──
        /** Apply a command. False = rejected; an Error event carries why and it lands in
         *  `model().lastError`. */
        bool dispatch(const Command &c);
        /** Parse then dispatch. A blank or `#`-commented line is a successful no-op, so a
         *  script file reads naturally. */
        bool dispatchText(const std::string &line, std::string &err);
        /** Drain finished work, refresh the model, emit events. Never blocks. */
        void pump(double nowMs);
        const AppModel &model() const { return mModel; }

        /** Pick up the newest preview, if one landed since the last call. The view calls this
         *  instead of `RenderService::tryAcquire` — there must be exactly ONE caller of that,
         *  because it moves the frame out, and making it the service is what lets a headless
         *  front end see frames at all (D-21). */
        bool takeFrame(RenderService::Frame &out);

        // ── transitional ──
        /** The GUI still renders from the session directly for everything the model does not
         *  carry yet. Every use is a line still to delete; the count is tracked in PROGRESS. */
        EditSession &session() { return mSession; }
        bool loadActive() const { return mLoader.active(); }
        /** True once a Quit command has been dispatched, so a CLI loop knows to stop. */
        bool quitRequested() const { return mQuit; }

        /** Re-derive the view-model from the session and bump `revision` (R-SVC-12).
         *
         *  For the call sites that still mutate `EditSession` directly instead of sending a
         *  Command — `App.cpp` has ~95 session calls left, a dozen of them mutations: selecting
         *  a node, navigating into a group, jumping in history, applying a preset. Those changed
         *  the MODEL without the view-model being re-derived, so the view then re-read a snapshot
         *  still describing the PREVIOUS edit target and pushed its values back into the panels.
         *  That is why selecting a different image did not change the curve (D-35).
         *
         *  A bridge, deliberately named for what it is: every one of those sites should become a
         *  Command, and then this disappears. Until they do, a direct mutation must announce
         *  itself, and one call here is cheaper than fifteen ways to forget. */
        void refreshFromSession() { refreshModel(); }

    private:
        void emit(const Event &e);
        void emit(Event::Kind k, const std::string &text = std::string(), int a = 0, int b = 0, double ms = 0,
                  int c = 0);
        /** R-PREVIEW-3: if a gesture has gone quiet and the frame on screen is coarse, ask
         *  for the next level up. Called once per `pump`; never blocks. */
        void maybeRefine(double nowMs);
        /** R-AISEG-20: publish a running detection's progress into the model, and — once it has
         *  an answer — write the regions it found onto the mask that asked (R-AISEG-21). Called
         *  once per `pump`; polls, never waits. */
        void pumpDetect();

        /** How long a gesture must be silent before the walk starts, ms. See maybeRefine
         *  for why it is this short. */
        static constexpr double kSettleMs = 80.0;
        /** ...and how long a gesture that is still nominally ON may be silent before the
         *  walk refines anyway. A safety net, not a mechanism: a front end that sends
         *  `gesture on` and then forgets `gesture off` would otherwise leave the photo
         *  coarse forever, which is a worse failure than one wasted render. Long enough
         *  (500 ms) that no real drag ever reaches it. */
        static constexpr double kStuckMs = 500.0;
        /** When the last INTERACTIVE render was asked for, or -1 if none yet. */
        double mLastInteractiveMs = -1.0;
        /** The level a refinement step is waiting on, or -1 when no step is in flight. */
        int mRefinePendingLevel = -1;
        /** True between `gesture on` and `gesture off`. */
        bool mGestureActive = false;
        /** The last clock the caller passed to `pump`. `dispatch` has no time of its own
         *  (R-SVC-6: the caller owns the clock), and a command lands between pumps, so this
         *  is at most one frame stale — which is far inside kSettleMs. */
        double mNowMs = 0.0;
        bool fail(const std::string &why);
        void refreshModel();
        void refreshRecents();
        bool startProjectLoad(const std::string &path, std::vector<EditSession::WorkspaceEntry> entries,
                              bool saveOnFinish);
        bool applySetFields(const Command &c);
        /** R-WB-1: sample the photo where the user clicked, solve for the temperature and tint
         *  that make it neutral, and apply them through the ordinary params path. */
        bool pickWhiteBalance(const Command &c);
        /** R-INFO: read a photo's metadata into the model, on demand. */
        bool readMetadata(const Command &c);
        /** The picker's averaging radius, in proxy pixels — big enough to swamp sensor noise,
         *  small enough that clicking a small neutral patch does not drag its neighbours in. */
        static constexpr int kWbPickRadius = 4;
        /** The range the temperature and tint sliders expose (`UnitConversions::toKelvin` maps
         *  -100..100 onto these), so a picked value is always one the user can see and drag away
         *  from. The top is `kelvinToRgbGain`'s own limit — it clamps `w` to 2.0, so 19500 K is
         *  the warmest gain the engine can express and a wider slider would be dead at the end.
         *  It used to be 10000 K, which clamped away half of a real shade correction (D-54). */
        static constexpr double kWbKelvinMin = 2000.0, kWbKelvinMax = 19500.0;
        static constexpr double kWbTintMin = -150.0, kWbTintMax = 150.0;

        bool applySettingsFields(const Command &c);
        bool runExport(const Command &c);

        EditSession mSession;          // owned as of S4c
        ThreadBudget &mBudget;
        ProjectLoader mLoader;
        ProjectLoader::DecoderFactory mMakeDecoder;
        std::function<void()> mWorkerInit;
        ImageWriter mWriter;
        std::vector<EventSink> mSinks;

        AppModel mModel;
        std::vector<int> mNodeOf;      // entry index -> tree node, for the running load
        std::vector<std::string> mEntryNames;      // by entry index; `entries` is moved into the loader
        std::vector<std::size_t> mStartedScratch;  // reused per pump, so a claim costs no allocation
        std::vector<ProjectLoader::EntryProgress> mProgressScratch;
        std::string mLoadPath;
        bool mSaveOnFinish = false;
        bool mQuit = false;
        RenderService::Frame mFrame;   // the newest acquired preview, awaiting takeFrame()
        bool mFrameWaiting = false;
    };
}
}
