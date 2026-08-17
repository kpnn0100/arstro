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

        CosmoService(EditSession &session, ThreadBudget &budget);

        // ── wiring the host supplies once ──
        void setDecoderFactory(ProjectLoader::DecoderFactory f) { mMakeDecoder = std::move(f); }
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

        // ── transitional, removed in S4 ──
        /** The GUI still renders from the session directly. Every use of this is a line S4
         *  has to delete; it exists so S2 can land without rewriting App in the same step. */
        EditSession &session() { return mSession; }
        bool loadActive() const { return mLoader.active(); }
        /** True once a Quit command has been dispatched, so a CLI loop knows to stop. */
        bool quitRequested() const { return mQuit; }

    private:
        void emit(const Event &e);
        void emit(Event::Kind k, const std::string &text = std::string(), int a = 0, int b = 0, double ms = 0);
        bool fail(const std::string &why);
        void refreshModel();
        void refreshRecents();
        bool startProjectLoad(const std::string &path, std::vector<EditSession::WorkspaceEntry> entries,
                              bool saveOnFinish);
        bool applySetFields(const Command &c);
        bool applySettingsFields(const Command &c);
        bool runExport(const Command &c);

        EditSession &mSession;
        ThreadBudget &mBudget;
        ProjectLoader mLoader;
        ProjectLoader::DecoderFactory mMakeDecoder;
        std::function<void()> mWorkerInit;
        ImageWriter mWriter;
        std::vector<EventSink> mSinks;

        AppModel mModel;
        std::vector<int> mNodeOf;      // entry index -> tree node, for the running load
        std::string mLoadPath;
        bool mSaveOnFinish = false;
        bool mQuit = false;
        unsigned mFrameSeq = 0;
    };
}
}
