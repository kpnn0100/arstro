#include "CosmoService.h"
#include "engine/EditParamsIO.h"
#include <cstdlib>
#include <filesystem>
#include <sstream>

namespace arstro
{
namespace cosmo
{
    namespace
    {
        std::string stemOf(const std::string &path)
        {
            return std::filesystem::path(path).stem().string();
        }
        std::string baseOf(const std::string &path)
        {
            const auto slash = path.find_last_of("/\\");
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }
    }

    CosmoService::CosmoService(ThreadBudget &budget) : mBudget(budget)
    {
        refreshRecents();
        refreshModel();
    }

    void CosmoService::setDecoderFactory(ProjectLoader::DecoderFactory f)
    {
        mMakeDecoder = std::move(f);

        // R-MEM-2: the same decoder that opens the project is what brings an evicted slot
        // back. Wiring it here rather than at the call site is deliberate — D-41 was a
        // decode path nobody remembered to budget, and a second one installed separately
        // would be the identical mistake with a new name.
        //
        // The lambda runs on the RENDER WORKER, so it may not touch the session: it is
        // handed a PATH, which RenderService kept alongside the slot for exactly this
        // reason, and it returns bytes. `mMakeDecoder` builds a decoder per call because
        // IImageDecoder implementations are stateless and that is what makes this
        // thread-safe without a lock (the same rule ProjectLoader's produce() follows).
        ProjectLoader::DecoderFactory make = mMakeDecoder;
        if (!make)
        {
            mSession.renderService().setSourceLoader(nullptr);
            return;
        }
        mSession.renderService().setSourceLoader(
            [make](const std::string &path, bool fullFidelity, std::vector<uint8_t> &rgba,
                   int &w, int &h) {
                auto dec = make();
                if (!dec) return false;
                DecodedImage img = dec->decodeFile(path, fullFidelity ? Fidelity::Full
                                                                      : Fidelity::Preview);
                if (!img.ok()) return false;
                rgba = std::move(img.rgba);
                w = img.width;
                h = img.height;
                return true;
            });
    }

    // ── events ────────────────────────────────────────────────────────────────────────
    void CosmoService::emit(const Event &e)
    {
        if (e.kind == Event::Kind::Error || e.kind == Event::Kind::CommandRejected) mModel.lastError = e.text;
        for (auto &s : mSinks) if (s) s(e);
    }

    void CosmoService::emit(Event::Kind k, const std::string &text, int a, int b, double ms, int c)
    {
        Event e;
        e.kind = k; e.text = text; e.a = a; e.b = b; e.ms = ms;
        e.c = c;
        emit(e);
    }

    bool CosmoService::fail(const std::string &why)
    {
        emit(Event::Kind::CommandRejected, why);
        return false;
    }

    // ── model ─────────────────────────────────────────────────────────────────────────
    void CosmoService::refreshRecents()
    {
        mModel.recents.clear();
        for (const RecentEntry &e : ProjectStore::recents())
        {
            RecentModel r;
            r.name = e.name; r.path = e.path; r.firstImagePath = e.firstImagePath;
            r.photoCount = e.photoCount; r.sizeBytes = e.sizeBytes; r.lastOpened = e.lastOpened;
            mModel.recents.push_back(std::move(r));
        }
    }

    void CosmoService::refreshModel()
    {
        // R-PREVIEW-1/2: the contract half of the pyramid — how many levels exist and what
        // an interactive frame is allowed to cost. Published from the render service rather
        // than duplicated, so there is one owner of the number (the same reason ThreadBudget
        // owns the CPU budget outright — R-SVC-10).
        // R-CROP-1: the selected photo's real shape, so a UI can turn a requested aspect
        // ratio into a normalised crop instead of assuming the photo is square.
        {
            int sw = 0, sh = 0;
            const int slot = mSession.currentSlot();
            if (slot >= 0) mSession.renderService().sourceSize(slot, sw, sh);
            mModel.sourceWidth = sw;
            mModel.sourceHeight = sh;
        }
        mModel.previewLevels = EditEngine::previewLevels();
        mModel.interactiveBudgetMs = mSession.renderService().interactiveBudgetMs();

        // Rebuilt wholesale rather than patched per command: the tree is small (hundreds of
        // nodes), and a snapshot that is always derived cannot drift from the session the way
        // incrementally-maintained mirror state does.
        AppModel &m = mModel;
        ++m.revision;

        m.nodes.clear();
        const std::vector<int> &sel = mSession.selection();
        for (const EditSession::TreeRow &r : mSession.treeRows())
        {
            NodeModel n;
            n.node = r.node; n.parent = r.parent; n.depth = r.depth;
            n.group = r.group; n.name = r.name; n.slot = r.slot;
            n.pending = r.pending; n.failed = r.failed; n.bypass = r.bypass;
            for (int s : sel) if (s == r.node) { n.selected = true; break; }
            m.nodes.push_back(std::move(n));
        }

        m.imageCount = mSession.imageCount();
        m.currentSlot = mSession.currentSlot();
        m.editGroup = mSession.editGroup();
        m.selectedNode = sel.empty() ? mSession.editTargetNode() : sel.front();
        m.params = mSession.effectiveEditParams();
        if (const EditParams *own = mSession.curParams()) { m.ownParams = *own; m.hasEditTarget = true; }
        else { m.ownParams = EditParams{}; m.hasEditTarget = false; }
        m.projectPath = mSession.workspacePath();
        if (!m.projectPath.empty()) m.projectName = stemOf(m.projectPath);

        m.history.canUndo = mSession.canUndo();
        m.history.canRedo = mSession.canRedo();
        if (const History *h = const_cast<EditSession &>(mSession).currentHistory())
        {
            m.history.nodes = (int)h->nodes.size();
            m.history.current = h->current;
            if (h->current >= 0 && h->current < (int)h->nodes.size())
                m.history.lastLabel = h->nodes[h->current].label;
        }
        else { m.history.nodes = 0; m.history.current = -1; m.history.lastLabel.clear(); }

        m.load.active = mLoader.active();
        if (m.load.active || mLoader.total() > 0)
        {
            m.load.done = (int)mLoader.consumed();
            m.load.started = (int)mLoader.started();
            m.load.partial = mLoader.partial();
            m.load.total = (int)mLoader.total();
            m.load.workers = mLoader.workers();
        }

        m.budget.percent = mBudget.percent();
        m.budget.cores = mBudget.cores();
        m.budget.total = mBudget.total();
        m.budget.engineThreads = mBudget.engineThreads();
        m.budget.decodeWorkers = mBudget.decodeWorkers();
        m.budget.peakDecode = mBudget.peakDecode();
        m.budget.residentBytes = mSession.renderService().residentBytes();   // R-MEM-4
        m.budget.rehydrations = mSession.renderService().rehydrations();

        m.gpuAvailable = mSession.gpuAvailable();
        m.gpuActive = mSession.useGpu() && m.gpuAvailable;
    }

    void CosmoService::applySettings(const AppSettings &s)
    {
        mModel.settings = s;
        mBudget.setPercent(s.cpuPercent);              // R-CPU-3
        mBudget.setExplicitEngineThreads(s.threads);   // R-CPU-2b
        mSession.setPreviewEdge(s.previewEdge);
        mSession.setUseGpu(s.useGpu);                  // no-op with no GPU backend (R-GPU-3)
        refreshModel();
    }

    // ── load ──────────────────────────────────────────────────────────────────────────
    bool CosmoService::startProjectLoad(const std::string &path,
                                        std::vector<EditSession::WorkspaceEntry> entries, bool saveOnFinish)
    {
        if (entries.empty()) return fail("project has no entries: " + path);
        if (!mMakeDecoder) return fail("no decoder factory installed (host must call setDecoderFactory)");

        mLoadPath = path;
        mSaveOnFinish = saveOnFinish;

        // ── Announce BEFORE touching the session, and the order is load-bearing (D-13) ──
        // A view's handler for these two runs synchronously, inside this call, and it is
        // entitled to clear its own state: the GTK host's ProjectOpening handler calls
        // App::resetWorkspace() so the open transition captures the outgoing editor rather
        // than fading in from nothing. If the pending tree were already built by then, that
        // handler would delete all of it — which is precisely what happened: 18 files decoded
        // and none attached, because every node in `mNodeOf` had been wiped.
        //
        // The rule this encodes: emit an event describing what is ABOUT to happen before the
        // state it describes exists, so a view that reacts by resetting cannot destroy work
        // the service has already done.
        mModel.screen = Screen::Loading;
        mModel.load.stage = "reading";
        mModel.load.started = 0;
        mModel.load.done = 0;
        mModel.load.total = (int)entries.size();
        emit(Event::Kind::ScreenChanged, screenName(mModel.screen));
        emit(Event::Kind::ProjectOpening, stemOf(path), 0, (int)entries.size());
        emit(Event::Kind::LoadStage, mModel.load.stage);

        mSession.resetWorkspace();

        // R-LOADUX-1: the whole rack exists before a single pixel decodes, so entry parents
        // resolve here and an out-of-order arrival can never reparent the tree.
        mEntryNames.clear();
        mEntryNames.reserve(entries.size());
        for (const auto &e : entries)
            mEntryNames.push_back(e.group ? e.name : baseOf(e.imagePath));

        mNodeOf.assign(entries.size(), 0);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto &e = entries[i];
            const int parent = (e.parent < 0 || e.parent >= (int)i) ? 0 : mNodeOf[e.parent];
            mNodeOf[i] = e.group ? mSession.addWorkspaceGroup(parent, e.name, e.params, e.history, e.bypass)
                                 : mSession.addPendingImage(parent, e.name);
        }

        const size_t n = entries.size();
        mLoader.start(std::move(entries), mBudget, mMakeDecoder, mWorkerInit);
        mModel.load.stage = "decoding";
        emit(Event::Kind::LoadStage, mModel.load.stage);
        emit(Event::Kind::Info,
             "load.started workers=" + std::to_string(mLoader.workers()) +
                 " engine=" + std::to_string(mBudget.engineThreads()) +
                 " budget=" + std::to_string(mBudget.total()) + " entries=" + std::to_string(n));
        refreshModel();
        return true;
    }

    bool CosmoService::takeFrame(RenderService::Frame &out)
    {
        if (!mFrameWaiting) return false;
        out = std::move(mFrame);
        mFrameWaiting = false;
        return true;
    }

    // ── R-PREVIEW-3: walk the level back up once the gesture stops ───────────────────
    //
    // A live gesture renders coarse so it can keep up (R-PREVIEW-1). The moment it stops,
    // the photo has to become sharp again — one level at a time, because every level is a
    // complete correct frame and a jump from 400 px to 1600 px is a long gap followed by a
    // single visible pop, which is what R-PREVIEW-4's cross-dissolve exists to avoid.
    //
    // "Stopped" is measured as SILENCE, not as a release event: the service is not told
    // when a finger lifts and should not be — a script driving `set` in a loop and a human
    // dragging a slider must behave identically (R-SVC-2). kSettleMs is the width of that
    // silence, and it is a compromise with a number attached: R-PREVIEW-3 gives the whole
    // walk 500 ms, three steps from the coarsest level cost roughly 40 + 85 + 340 ms on an
    // A733-class board, so the silence has to be short. 80 ms is ~2.5 interactive frames —
    // long enough that a pause mid-drag does not trigger a full render the next move throws
    // away, short enough to leave the walk its budget.
    void CosmoService::maybeRefine(double nowMs)
    {
        if (mModel.frameLevel <= 0) { mModel.refining = false; return; }
        if (mRefinePendingLevel >= 0) return;             // a step is already in flight
        if (mLastInteractiveMs < 0) return;               // no gesture has happened yet
        const double silence = nowMs - mLastInteractiveMs;
        // While the finger is still DOWN, do not refine at all — not even after a pause.
        // This is the whole reason `gesture` is a command rather than something inferred:
        // a full-level render cannot be cancelled once the worker has started it, so a
        // refinement begun during a mid-drag pause makes the NEXT move wait for it. On this
        // desktop that is 36 ms and invisible; on an A733-class board a level-0 render of a
        // real edit is over a second, and the photographer would feel exactly the lag this
        // requirement exists to remove. Measured with an 80 ms-paced drag through
        // `cosmo-cc --watch`, which oscillated 1,0,1,0 until this check existed.
        if (mGestureActive && silence < kStuckMs) return;
        if (!mGestureActive && silence < kSettleMs) return;
        const int next = mModel.frameLevel - 1;
        mRefinePendingLevel = next;
        mModel.refining = true;
        mSession.submitRefine(next);
    }

    void CosmoService::pump(double nowMs)
    {
        // R-PREVIEW-3: the settle-and-refine walk. `nowMs` used to be ignored here; the
        // clock is the caller's (R-SVC-6) and this is the first thing in the service that
        // needs it. Nothing blocks and nothing sleeps — a step is just another coalesced
        // render request, so a live run and a scripted one take the same path.
        mNowMs = nowMs;
        // The session's clock, which ONLY the two views used to advance (`App::render` and
        // `PhoneApp`). `History::record` coalesces edits that arrive close together — a whole
        // slider drag is one undoable step — and it reads that clock, so a front end that never
        // ticked it left it at 0 and **every edit in the session merged into one history node**.
        // Undo was therefore broken for `cosmo-cc`, for a script, and for the control socket, and
        // worked in the GUI only because a view happened to do the service's job (D-55).
        //
        // It belongs here: `pump` already owns the clock (R-SVC-6, the caller drives it), and
        // history is behaviour, not presentation (R-SVC-1).
        mSession.tick(nowMs);
        maybeRefine(nowMs);

        // Frames first, and UNCONDITIONALLY — this used to sit behind the load's early-return,
        // which is half of why nothing ever produced `frameSeq` (D-21). `tryAcquire` moves the
        // frame out, so exactly one owner may poll it; that owner is the service now, and the
        // view takes it via takeFrame(). A frame that arrives while one is still waiting simply
        // replaces it: RenderService coalesces upstream anyway, and a view that has not drawn
        // yet wants the newest, not a queue.
        RenderService::Frame f;
        if (mSession.renderService().tryAcquire(f) && f.width > 0)
        {
            mFrame = std::move(f);
            mFrameWaiting = true;
            mModel.frameSlot = mSession.currentSlot();
            mModel.frameWidth = mFrame.width;
            mModel.frameHeight = mFrame.height;
            ++mModel.frameSeq;
            ++mModel.revision;
            // The numbers this event claims to be about (§6 "emit the numbers you claim"): how
            // long the frame took, and whether a cold slot had to be re-decoded to produce it.
            // Without them a 250 ms hop and a 2537 ms one were the same line in the log, and the
            // user had to notice the difference by feel (D-44).
            // A step has landed (or a newer interactive request superseded it — either way
            // this level is now the truth), so the walk may take its next step.
            if (mRefinePendingLevel >= 0 && mFrame.level <= mRefinePendingLevel)
                mRefinePendingLevel = -1;
            mModel.frameLevel = mFrame.level;
            mModel.frameLevelEdge = mFrame.levelEdge;
            // R-CROP-1: a landing frame is proof the engine actually holds this slot. The
            // adds are QUEUED to the render worker, so `refreshModel` at the end of a load can
            // run before the worker has applied them and read {0,0} — which it did, and which
            // is why the dimensions are refreshed here as well as there rather than only there.
            {
                int sw = 0, sh = 0;
                if (mModel.frameSlot >= 0)
                    mSession.renderService().sourceSize(mModel.frameSlot, sw, sh);
                if (sw > 0 && sh > 0) { mModel.sourceWidth = sw; mModel.sourceHeight = sh; }
            }
            mModel.msPerMegapixel = mSession.renderService().msPerMegapixel();
            mModel.refining = mFrame.level > 0;
            // D-48: a NaN reaching a frame used to be a core dump. It is now a substituted
            // zero, which would be invisible — so it is named here, on the frame it happened
            // to, appended after the existing text so an older `expect` still matches.
            std::string note = mFrame.rehydrated ? "rehydrated" : std::string();
            if (mFrame.nonFinite > 0)
            {
                if (!note.empty()) note += ' ';
                note += "nonfinite=" + std::to_string(mFrame.nonFinite);
            }
            // R-AISEG-13: a mask whose region is COMPUTED has no geometry a front end could
            // print, so "the mask was found and here is how much of it there is" would
            // otherwise be visible only by looking at the photo. Appended after the existing
            // text, never inserted, so every `expect` written against this line still matches.
            if (!mFrame.maskOutlines.empty())
            {
                std::size_t loops = 0, points = 0;
                for (const auto &o : mFrame.maskOutlines)
                {
                    loops += o.loops.size();
                    for (const auto &l : o.loops) points += l.size();
                }
                if (!note.empty()) note += ' ';
                note += "outlines=" + std::to_string(mFrame.maskOutlines.size()) + "/" +
                        std::to_string(loops) + "/" + std::to_string(points);
            }
            emit(Event::Kind::FrameReady, note, mModel.frameSlot, mFrame.width, mFrame.ms,
                 mFrame.level);
        }

        if (!mLoader.active() && mLoader.total() == 0) return;

        // R-LOADUX-4: claims first. A worker announcing "I have started entry i" is the only
        // thing there is to say during the seconds a RAW decode takes, and without it the view
        // has no way to tell waiting from stalled (D-22).
        mStartedScratch.clear();
        mLoader.drainStarted(mStartedScratch);
        for (std::size_t i : mStartedScratch)
        {
            mModel.load.started = (int)mLoader.started();
            const std::string name = i < mEntryNames.size() ? mEntryNames[i] : std::string();
            // The model carries WHAT is loading, so a view composes its own label instead of
            // each front end inventing one (R-SVC-3).
            if (!name.empty()) mModel.load.status = name;
            emit(Event::Kind::EntryStarted, name, (int)i, mModel.load.started);
        }

        // Sub-image progress, coalesced by the loader to the newest value per entry (D-24).
        mProgressScratch.clear();
        mLoader.drainProgress(mProgressScratch);
        for (const ProjectLoader::EntryProgress &ep : mProgressScratch)
        {
            mModel.load.partial = mLoader.partial();
            mModel.load.entryStage = ep.stage;
            ++mModel.revision;
            Event e;
            e.kind = Event::Kind::EntryProgress;
            e.a = (int)ep.index;
            e.ms = ep.fraction * 100.0;
            e.text = ep.stage;
            emit(e);
        }

        bool any = false;
        ProjectLoader::Result r;
        while (mLoader.poll(r))
        {
            any = true;
            const int node = r.index < mNodeOf.size() ? mNodeOf[r.index] : -1;
            if (!r.group && node >= 0)
            {
                if (r.decoded)
                {
                    const int slot = mSession.attachImage(node, std::move(r.rgba), r.w, r.h,
                                                          r.imagePath, std::move(r.thumb));
                    if (slot >= 0)
                    {
                        mSession.applyParamsToSlot(slot, r.params, r.history);
                        mSession.setSlotBypass(slot, r.bypass);   // R-BYPASS-6
                        if (mSession.currentSlot() < 0) mSession.selectImage(slot);
                        emit(Event::Kind::EntryDecoded, r.name, (int)r.index, slot);
                    }
                }
                else
                {
                    mSession.markImageFailed(node);   // stops the spinner; reads as missing
                    emit(Event::Kind::EntryFailed, r.imagePath, (int)r.index);
                }
            }
            if (!r.name.empty()) mModel.load.status = r.name;
            emit(Event::Kind::LoadProgress, r.name, (int)mLoader.consumed(), (int)mLoader.total());
        }
        if (!any) return;

        if (mLoader.finished())
        {
            const int total = (int)mLoader.total();
            int decoded = 0;
            for (const NodeModel &n : mModel.nodes) if (!n.group && n.slot >= 0) ++decoded;

            mSession.finishWorkspaceLoad(mLoadPath);
            if (mSaveOnFinish)
            {
                mModel.load.stage = "saving";
                emit(Event::Kind::LoadStage, mModel.load.stage);
            }
            if (mSaveOnFinish && !mSession.saveWorkspaceAs(mLoadPath))
                emit(Event::Kind::Error, "could not write project " + mLoadPath);

            RecentEntry e;
            e.name = stemOf(mLoadPath);
            e.path = mLoadPath;
            e.photoCount = mSession.imageCount();
            e.lastOpened = 0;   // no clock in cosmo_core; the host stamps it (R-SVC-7)
            for (const NodeModel &n : mModel.nodes)
                if (!n.group && n.slot >= 0) { e.firstImagePath = mSession.sourcePathForSlot(n.slot); break; }
            ProjectStore::remember(e);
            refreshRecents();

            mModel.screen = Screen::Editor;
            mModel.load.stage.clear();
            mModel.load.entryStage.clear();
            mModel.load.partial = 0.0;
            emit(Event::Kind::LoadStage, std::string());
            refreshModel();
            // Counted after refreshModel so `decoded` reflects the final tree, not the
            // one-behind snapshot the loop was reading.
            decoded = 0;
            for (const NodeModel &n : mModel.nodes) if (!n.group && n.slot >= 0) ++decoded;
            emit(Event::Kind::ScreenChanged, screenName(mModel.screen));
            emit(Event::Kind::LoadFinished, std::string(), decoded, total);
            emit(Event::Kind::ProjectOpened, mLoadPath, 0, decoded);
            emit(Event::Kind::Info,
                 "load.peak decode=" + std::to_string(mBudget.peakDecode()) +
                     " engine=" + std::to_string(mBudget.engineThreads()) +
                     " budget=" + std::to_string(mBudget.total()));
            mLoader.stop();
        }
        else
            refreshModel();
    }

    // ── fields ────────────────────────────────────────────────────────────────────────
    bool CosmoService::readMetadata(const Command &c)
    {
        // The node, not the slot: a right-click names a node, and the menu is offered on group
        // rows too — answering "that has no file behind it" there is better than no answer.
        const int node = c.index >= 0 ? c.index : mModel.selectedNode;
        if (node < 0 || node >= (int)mSession.nodes().size())
            return fail("metadata: nothing selected");
        const EditSession::GNode &n = mSession.nodes()[(std::size_t)node];
        const int slot = n.slot;
        const std::string path = slot >= 0 ? mSession.sourcePathForSlot(slot) : std::string();
        if (path.empty())
            return fail(n.group ? "metadata: a group has no file behind it"
                                : "metadata: that photo's pixels are not loaded yet");
        std::string name = mSession.nameForSlot(slot);

        mModel.metadataNode = node;
        mModel.metadataName = name.empty() ? path : name;
        mModel.metadata.clear();

        // What the CALLER knows, first, and always — a file with no readable tags at all still
        // deserves a panel rather than an empty box. The NAME is not a row: it is
        // `metadataName`, which every surface shows as the heading, and a row repeating the
        // heading is a row that costs a line and says nothing.
        mModel.metadata.emplace_back("Path", path);
        {
            std::error_code ec;
            const auto bytes = std::filesystem::file_size(path, ec);
            if (!ec)
            {
                char buf[64];
                if (bytes >= 1024ull * 1024ull)
                    std::snprintf(buf, sizeof(buf), "%.1f MB", (double)bytes / (1024.0 * 1024.0));
                else
                    std::snprintf(buf, sizeof(buf), "%.0f KB", (double)bytes / 1024.0);
                mModel.metadata.emplace_back("File size", buf);
            }
        }

        // Then whatever the file itself says. Through the decoder seam, so no codec enters the
        // service (the same rule `setImageWriter` follows) — and via `readMetadata`, which does
        // not decode: the panel must work on a photo whose pixels have been evicted.
        bool haveDims = false;
        if (mMakeDecoder)
            if (auto dec = mMakeDecoder())
                for (auto &kv : dec->readMetadata(path).rows)
                {
                    if (kv.first == "Dimensions") haveDims = true;
                    mModel.metadata.emplace_back(kv.first, kv.second);
                }

        // Only as a fallback, and last: the engine knows the size of a photo whose pixels are
        // resident, but a row that appears or vanishes depending on whether a frame has landed
        // is not a property of the file (R-SVC-9) — so the file's own answer wins whenever the
        // decoder could give one, and this covers the formats that carry no header we read.
        if (!haveDims && mModel.sourceWidth > 0 && mModel.sourceHeight > 0 &&
            slot == mSession.currentSlot())
        {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%d x %d", mModel.sourceWidth, mModel.sourceHeight);
            mModel.metadata.emplace_back("Dimensions", buf);
        }

        ++mModel.revision;
        emit(Event::Kind::Info, "metadata " + std::to_string((int)mModel.metadata.size()) +
                                    " field(s) for " + mModel.metadataName);
        return true;
    }

    bool CosmoService::pickWhiteBalance(const Command &c)
    {
        // R-WB-1: "this pixel should be white". Three steps, and each one is somewhere it can be
        // reused: the engine samples, `color::solveNeutralWhiteBalance` solves, and the result is
        // applied through the SAME params path a slider uses — so the picker lands one history
        // entry, one event and one render, exactly like dragging the two sliders would.
        EditParams *p = mSession.curParams();
        if (!p) return fail("wb pick: nothing selected to edit");
        const int slot = mSession.currentSlot();
        if (slot < 0) return fail("wb pick: no image");

        const double x = std::atof(c.field("x").c_str());
        const double y = std::atof(c.field("y").c_str());
        if (!(x >= 0.0 && x <= 1.0 && y >= 0.0 && y <= 1.0))
            return fail("wb pick: x and y must be 0..1");

        // A box average, not a pixel: one pixel of a real photo is sensor noise, and a white
        // balance derived from it jumps when you click 1 px to the left.
        Pixel rgb[3] = {0, 0, 0};
        if (!mSession.renderService().sampleSourceLinear(slot, x, y, kWbPickRadius, rgb))
            return fail("wb pick: no pixels resident for this image yet");

        Pixel kelvin = 6500, tint = 0;
        // Clamped to the range the UI exposes, so the picker can never set a value the sliders
        // cannot show or the user cannot undo by dragging.
        if (!color::solveNeutralWhiteBalance(rgb[0], rgb[1], rgb[2],
                                            (Pixel)kWbKelvinMin, (Pixel)kWbKelvinMax,
                                            (Pixel)kWbTintMin, (Pixel)kWbTintMax, kelvin, tint))
            return fail("wb pick: that area is too dark to balance — pick somewhere brighter");

        p->temp = (float)kelvin;
        p->tint = (float)tint;
        mSession.submit();
        refreshModel();                       // D-34: before the emit
        emit(Event::Kind::ParamsChanged, "temp,tint");
        return true;
    }

    bool CosmoService::applySetFields(const Command &c)
    {
        EditParams *p = mSession.curParams();
        if (!p) return fail("set: nothing selected to edit");

        // Reuse the params text codec rather than growing a second key->field table: every
        // EditParams field already round-trips through it for .cosmo/.cmp/.apf, so `set`
        // accepts exactly the keys those files do and can never fall behind them.
        std::string text;
        for (const auto &kv : c.fields) text += kv.first + "=" + kv.second + "\n";
        EditParams probe = *p;
        if (!deserializeParams(text, probe)) return fail("set: no field matched: " + text);
        // D-36: reject a value that is not a finite number, BEFORE it reaches the engine.
        //
        // The engine is now hardened against a NaN reaching a LUT index (that was the crash),
        // but hardening only turns a crash into a wrong pixel. A NaN or an infinity in an
        // EditParams field is never something a photographer asked for — `set exposure=nan`,
        // `set exposure=1e40` whose 2^x overflows, a hand-edited preset, a fuzzed socket
        // client — so it is rejected here with a message, which is also what makes it
        // debuggable. Nothing legitimate is refused: every slider in the UI sends a finite
        // decimal, and a project FILE is treated more leniently on purpose (see
        // sanitizeParams) because refusing to open someone's work over one bad key would be
        // the worse failure.
        if (const char *whichField = firstNonFiniteParam(probe))
            return fail(std::string("set: ") + whichField + " is not a finite number");
        *p = probe;
        // R-PREVIEW-1: while a gesture is in flight, latency outranks resolution. Nothing
        // else about a `set` changes — same params, same history, same event — so a script
        // and a drag differ only in which pyramid level the frame came from.
        if (mGestureActive)
        {
            mLastInteractiveMs = mNowMs;
            mSession.submitInteractive();
        }
        else
        {
            mSession.submit();
        }

        std::string names;
        for (const auto &kv : c.fields) { if (!names.empty()) names += ","; names += kv.first; }
        // REFRESH BEFORE EMIT, and this is the one line the whole of D-34 was: a listener reads
        // the model *during* the event, and every other handler in this file already refreshes
        // first. Emitting first meant a `set` from the view was answered by the view being
        // re-seeded from the PRE-EDIT model — the widget's own edit thrown away and replaced by
        // the value it had just changed. Same family as D-13 (announce before mutating): an
        // event and the state it describes have to be consistent at the moment it is delivered.
        refreshModel();
        emit(Event::Kind::ParamsChanged, names);
        return true;
    }

    bool CosmoService::applySettingsFields(const Command &c)
    {
        AppSettings &s = mModel.settings;
        for (const auto &kv : c.fields)
        {
            const std::string &k = kv.first;
            const int v = std::atoi(kv.second.c_str());
            if (k == "cpuPercent") { s.cpuPercent = v; mBudget.setPercent(v); }
            else if (k == "threads") { s.threads = v; mBudget.setExplicitEngineThreads(v); }
            else if (k == "previewEdge") { s.previewEdge = v; mSession.setPreviewEdge(v); }
            else if (k == "useGpu") { s.useGpu = (kv.second != "0" && kv.second != "false"); mSession.setUseGpu(s.useGpu); }
            // The one settings key the service STORES and never acts on: the scale applies to
            // the view's layout, which R-SVC-3 forbids the service to know anything about. It
            // is in the grammar so a scale is scriptable and a shot of a scaled shell can be
            // rendered headlessly, and in the model so whichever front end owns a view can
            // read the value it must honour (R-SCALE-2).
            else if (k == "uiScale") s.uiScale = AppSettings::clampUiScale(v);
            // The second of those (R-TOUCH-6): WHICH shell a host draws is the view's business,
            // so the service stores the choice, puts it in the model and emits the change. That
            // is also what makes the touch shell scriptable from a desktop host.
            else if (k == "touchUi") s.touchUi = (kv.second != "0" && kv.second != "false");
            else return fail("settings: unknown key " + k);
        }
        std::string tail;
        for (const auto &kv : c.fields) { if (!tail.empty()) tail += " "; tail += kv.first + "=" + kv.second; }
        refreshModel();                       // D-34: before the emit, like every other handler
        emit(Event::Kind::SettingsChanged, tail);
        return true;
    }

    bool CosmoService::runExport(const Command &c)
    {
        if (!mWriter) return fail("no image writer installed (host must call setImageWriter)");
        std::vector<int> slots;
        for (const NodeModel &n : mModel.nodes) if (!n.group && n.slot >= 0) slots.push_back(n.slot);
        if (slots.empty()) return fail("export: no decoded images");

        mModel.exports = ExportModel{};
        mModel.exports.active = true;
        mModel.exports.total = (int)slots.size();
        mModel.exports.outDir = c.path;

        // Synchronous on purpose: the CLI wants a process that exits when the files are
        // written, and the GUI's animated batch export is a separate host-side concern that
        // S4 folds in. renderFull blocks the caller by design (RenderService's contract).
        int written = 0, failures = 0;
        for (int slot : slots)
        {
            int w = 0, h = 0;
            const uint8_t *rgba = mSession.exportFullResSlot(slot, w, h);
            const std::string src = mSession.sourcePathForSlot(slot);
            const std::string name = mSession.nameForSlot(slot);
            mModel.exports.name = name;
            if (!rgba || w <= 0 || h <= 0)
            {
                ++failures;
                emit(Event::Kind::Error, "export: render failed for " + name);
            }
            else
            {
                std::string err;
                const std::string out = c.path + "/" + name;
                if (mWriter(out, src, rgba, w, h, err)) ++written;
                else { ++failures; emit(Event::Kind::Error, "export: " + name + ": " + err); }
            }
            ++mModel.exports.done;
            emit(Event::Kind::ExportProgress, name, mModel.exports.done, mModel.exports.total);
        }
        mModel.exports.active = false;
        mModel.exports.failures = failures;
        refreshModel();                       // D-34: before the emit
        emit(Event::Kind::ExportFinished, std::string(), written, failures);
        return failures == 0;
    }

    // ── dispatch ──────────────────────────────────────────────────────────────────────
    bool CosmoService::dispatchText(const std::string &line, std::string &err)
    {
        const Command c = parseCommand(line, err);
        if (!c.valid()) return err.empty();   // blank/comment: a successful no-op
        return dispatch(c);
    }

    bool CosmoService::dispatch(const Command &c)
    {
        switch (c.kind)
        {
            case Command::Kind::None: return true;

            case Command::Kind::ProjectOpen:
            {
                std::vector<EditSession::WorkspaceEntry> entries;
                int repaired = 0;
                if (!EditSession::readWorkspaceFile(c.path, entries, &repaired))
                    return fail("cannot read project " + c.path);
                // D-36: repaired, not refused — but said out loud. A project that carried a
                // non-finite parameter opens with that field neutralised, and the log names
                // how many, so "my edit disappeared" has an answer in the journal.
                if (repaired > 0)
                    emit(Event::Kind::Info, "project had " + std::to_string(repaired) +
                                            " non-finite parameter value(s), neutralised");
                return startProjectLoad(c.path, std::move(entries), false);
            }
            case Command::Kind::ProjectNew:
                mSession.resetWorkspace();
                mModel.screen = Screen::Editor;
                if (!mSession.saveWorkspaceAs(c.path)) return fail("cannot write project " + c.path);
                refreshModel();
                emit(Event::Kind::ProjectSaved, c.path);
                emit(Event::Kind::ScreenChanged, screenName(mModel.screen));
                return true;
            case Command::Kind::ProjectSave:
            {
                const std::string path = c.path.empty() ? mSession.workspacePath() : c.path;
                if (path.empty()) return fail("project save: no path, and no project open");
                if (!mSession.saveWorkspaceAs(path)) return fail("cannot write project " + path);
                refreshModel();
                emit(Event::Kind::ProjectSaved, path);
                return true;
            }
            case Command::Kind::ProjectClose:
                mLoader.stop();
                mSession.resetWorkspace();
                mModel.screen = Screen::Home;
                refreshModel();
                emit(Event::Kind::ProjectClosed);
                emit(Event::Kind::ScreenChanged, screenName(mModel.screen));
                return true;

            case Command::Kind::Import:
            {
                std::vector<EditSession::WorkspaceEntry> entries;
                for (const std::string &p : c.paths)
                {
                    EditSession::WorkspaceEntry e;
                    e.imagePath = p;
                    e.name = baseOf(p);
                    entries.push_back(std::move(e));
                }
                // An import has no .cmp yet, so the file is written once every image lands.
                const std::string path = c.path.empty() ? mSession.workspacePath() : c.path;
                if (path.empty()) return fail("import: open or create a project first");
                return startProjectLoad(path, std::move(entries), true);
            }

            case Command::Kind::Select:
            {
                const bool add = c.name == "add";
                const bool range = c.name == "range";
                bool found = false;
                for (const NodeModel &n : mModel.nodes)
                    if (n.node == c.index)
                    {
                        found = true;
                        // `selectImage(slot)` is the plain single-select shortcut and cannot
                        // express a multi-select, so anything with a modifier — and anything
                        // still DECODING, which has no slot at all — goes by node id.
                        // R-LOADUX-2: selecting a pending photo moves the ring and leaves the
                        // stage alone until its pixels arrive, which is why this must work
                        // during a load rather than being ignored until the load ends.
                        if (n.slot >= 0 && !add && !range) mSession.selectImage(n.slot);
                        else mSession.selectNodeById(n.node, add, range);
                        break;
                    }
                if (!found) return fail("select: no node " + std::to_string(c.index));
                refreshModel();
                emit(Event::Kind::SelectionChanged, std::string(), mModel.selectedNode, mModel.currentSlot);
                return true;
            }
            case Command::Kind::SelectNext:
            case Command::Kind::SelectPrev:
            {
                // Over the flat tree, so "next" means the next decoded image anywhere in the
                // project rather than the next cell in whichever group happens to be open —
                // the order a filmstrip shows and the order a script means are the same here.
                std::vector<int> imageNodes;
                int at = -1;
                for (const NodeModel &n : mModel.nodes)
                    if (!n.group && n.slot >= 0)
                    {
                        if (n.slot == mModel.currentSlot) at = (int)imageNodes.size();
                        imageNodes.push_back(n.node);
                    }
                if (imageNodes.empty()) return fail("select: no decoded images");
                const int step = c.kind == Command::Kind::SelectNext ? 1 : -1;
                const int next = at < 0 ? (step > 0 ? 0 : (int)imageNodes.size() - 1) : at + step;
                if (next < 0 || next >= (int)imageNodes.size()) return fail("select: no image in that direction");
                if (!mSession.selectNodeById(imageNodes[next])) return fail("select: node vanished");
                refreshModel();
                emit(Event::Kind::SelectionChanged, std::string(), mModel.selectedNode, mModel.currentSlot);
                return true;
            }

            // applySetFields refreshes before it emits (D-34); refreshing again here would be
            // harmless but would leave two places deciding the order.
            case Command::Kind::Set: return applySetFields(c);
            case Command::Kind::WhiteBalancePick: return pickWhiteBalance(c);
            case Command::Kind::Metadata: return readMetadata(c);

            // R-PREVIEW-1. Turning a gesture OFF backdates the silence timer so the
            // settle-and-refine walk starts on the very next pump rather than kSettleMs
            // later: the front end has told us the finger is up, which is better
            // information than waiting to infer it.
            case Command::Kind::Gesture:
                mGestureActive = c.flag;
                if (!c.flag && mLastInteractiveMs >= 0) mLastInteractiveMs = mNowMs - kSettleMs;
                refreshModel();
                emit(Event::Kind::Info, c.flag ? "gesture on" : "gesture off");
                return true;

            case Command::Kind::Bypass:
                mSession.setBypassed(c.index, c.flag);
                refreshModel();
                emit(Event::Kind::ParamsChanged, "bypass");
                return true;

            case Command::Kind::GroupNew:
                if (mSession.selection().empty()) return fail("group new: nothing selected");
                mSession.createGroupFromSelection();
                if (!c.name.empty() && !mSession.selection().empty())
                    mSession.renameGroup(mSession.selection().front(), c.name);
                refreshModel();
                emit(Event::Kind::Info, "group.created name=" + c.name);
                return true;
            case Command::Kind::GroupUngroup:
                mSession.selectNodeById(c.index);
                mSession.ungroupSelected();
                refreshModel();
                emit(Event::Kind::Info, "group.ungrouped node=" + std::to_string(c.index));
                return true;

            case Command::Kind::GroupRename:
            {
                if (c.name.empty()) return fail("group rename: needs a name");
                int node = c.index;
                if (node < 0)
                {
                    // Bare form: the group being edited, else the selected node if it is one.
                    node = mSession.editGroup();
                    if (node < 0)
                        for (const NodeModel &n : mModel.nodes)
                            if (n.selected && n.group) { node = n.node; break; }
                    if (node < 0) return fail("group rename: no group selected");
                }
                bool found = false;
                for (const NodeModel &n : mModel.nodes) if (n.node == node && n.group) found = true;
                if (!found) return fail("group rename: node " + std::to_string(node) + " is not a group");
                mSession.renameGroup(node, c.name);
                refreshModel();
                emit(Event::Kind::Info, "group.renamed node=" + std::to_string(node) + " name=" + c.name);
                return true;
            }
            case Command::Kind::Delete:
            {
                // -1 means the current selection, so a shortcut and a script share one path.
                if (c.index >= 0 && !mSession.selectNodeById(c.index))
                    return fail("delete: no node " + std::to_string(c.index));
                if (mSession.selection().empty()) return fail("delete: nothing selected");
                const int removed = (int)mSession.selection().size();
                mSession.deleteSelected();
                refreshModel();
                emit(Event::Kind::Info, "deleted nodes=" + std::to_string(removed));
                emit(Event::Kind::SelectionChanged, std::string(), mModel.selectedNode, mModel.currentSlot);
                return true;
            }
            case Command::Kind::MaskSet:
            case Command::Kind::MaskDelete:
            {
                EditParams *p = mSession.curParams();
                if (!p) return fail("mask: nothing selected to edit");
                if (c.index < 0 || c.index >= (int)p->masks.size())
                    return fail("mask: no mask " + std::to_string(c.index) + " (have " +
                                std::to_string(p->masks.size()) + ")");
                if (c.kind == Command::Kind::MaskDelete)
                {
                    p->masks.erase(p->masks.begin() + c.index);
                    mSession.submit();
                    refreshModel();
                    emit(Event::Kind::ParamsChanged, "mask.deleted index=" + std::to_string(c.index));
                    return true;
                }
                // Addressing an existing mask by index is the ONE thing `set mask=<blob>`
                // cannot do — that appends. Everything else about a mask (and curves, the
                // mixer, grading, every scalar) is already reachable through `set`, because
                // EditParamsIO names it.
                MaskParams &m = p->masks[c.index];
                LocalAdjust &adj = m.adjust;
                bool sawType = false, sawSubject = false;
                for (const auto &kv : c.fields)
                {
                    const std::string &k = kv.first;
                    const float v = (float)std::atof(kv.second.c_str());
                    const bool on = kv.second != "0" && kv.second != "false";
                    if (k == "feather") m.feather = v;
                    else if (k == "inverted") m.inverted = on;
                    else if (k == "type") { m.type = std::atoi(kv.second.c_str()); sawType = true; }
                    else if (k == "cx") m.cx = v; else if (k == "cy") m.cy = v;
                    else if (k == "rx") m.rx = v; else if (k == "ry") m.ry = v;
                    else if (k == "x0") m.x0 = v; else if (k == "y0") m.y0 = v;
                    else if (k == "x1") m.x1 = v; else if (k == "y1") m.y1 = v;
                    else if (k == "adjust.exposure") adj.exposure = v;
                    else if (k == "adjust.contrast") adj.contrast = v;
                    else if (k == "adjust.highlights") adj.highlights = v;
                    else if (k == "adjust.shadows") adj.shadows = v;
                    else if (k == "adjust.whites") adj.whites = v;
                    else if (k == "adjust.blacks") adj.blacks = v;
                    else if (k == "adjust.temp") adj.temp = v;
                    else if (k == "adjust.tint") adj.tint = v;
                    else if (k == "adjust.saturation") adj.saturation = v;
                    else if (k == "adjust.texture") adj.texture = v;
                    else if (k == "adjust.clarity") adj.clarity = v;
                    else if (k == "adjust.dehaze") adj.dehaze = v;
                    else if (k == "dabs")
                    {
                        // `x:y:r:f;…`, the same third group `EditParamsIO::parseMask` reads out
                        // of a mask blob. Without this, a Brush mask was the one edit no command
                        // could express — its whole product is dabs — which left the on-photo
                        // overlay as the only surface in the app still writing to the session
                        // directly. `set mask=` cannot stand in: it APPENDS, so replacing mask i
                        // through it would move that mask to the end of a stack whose order is
                        // what it renders (R-SVC-2).
                        m.dabs.clear();
                        std::string tok;
                        std::istringstream ds(kv.second);
                        while (std::getline(ds, tok, ';'))
                        {
                            if (tok.empty()) continue;
                            float f[4] = {0, 0, 0.05f, 1.f};
                            std::string num;
                            std::istringstream ts(tok);
                            for (int n = 0; n < 4 && std::getline(ts, num, ':'); ++n)
                                f[n] = (float)std::atof(num.c_str());
                            m.dabs.push_back({f[0], f[1], f[2], f[3]});
                        }
                    }
                    else if (k == "subject")
                    {
                        // The NAME or the number, through the engine's one parser (R-SVC-5,
                        // R-AISEG-8) — `subject=sky` is readable a year later and `subject=0` is
                        // not, and a second table here would be the copy R-SVC-5 exists to
                        // forbid. A misspelling is REFUSED rather than silently taken as 0: the
                        // whole reason `parseSemanticSubject` returns a bool is that `skyy`
                        // landing on Sky would be a mask that quietly does the wrong thing.
                        arstro::SemanticSubject sub{};
                        if (!arstro::parseSemanticSubject(kv.second, sub))
                            return fail("mask set: unknown subject " + kv.second);
                        m.subject = (int)sub;
                        sawSubject = true;
                    }
                    else if (k == "sensitivity") m.sensitivity = v;
                    else if (k == "path")
                    {
                        // `x,y[,ix,iy,ox,oy];…` through the engine's OWN codec — the same one
                        // the curves and the mixer use (R-SVC-5). A path is the one part of a
                        // mask whose length is variable, so like `dabs` it cannot be reached by
                        // any per-scalar field, and `set mask=` cannot stand in because it
                        // appends rather than addresses (R-MASK-6).
                        m.path = parseCurvePoints(kv.second);
                    }
                    else return fail("mask set: unknown field " + k);
                }
                // Drawing a shape and leaving the type on Radial would render nothing and read
                // as a bug in the mask rather than in the caller — so a path arriving on its own
                // sets the type. Only when the caller did NOT say: the overlay sends the whole
                // mask every frame, `type` included, and a stale path left on a mask somebody
                // switched back to Radial must not silently convert it (R-MASK-6).
                if (!sawType && m.path.size() >= 3) m.type = MaskParams::Path;
                // A semantic mask has no geometry, so there is nothing to infer a type FROM —
                // `subject=` on a Radial mask is a caller that meant `type=4` and forgot, and
                // the alternative is a mask that renders an ellipse and reports a subject.
                // Same rule as the path's: only when the caller did not say.
                if (!sawType && sawSubject) m.type = MaskParams::Semantic;
                mSession.submit();
                refreshModel();
                emit(Event::Kind::ParamsChanged, "mask index=" + std::to_string(c.index));
                return true;
            }
            case Command::Kind::Undo:
                if (!mSession.canUndo()) return fail("nothing to undo");
                mSession.undo();
                refreshModel();
                emit(Event::Kind::HistoryChanged, mModel.history.lastLabel, mModel.history.current,
                     mModel.history.nodes);
                return true;
            case Command::Kind::Redo:
                if (!mSession.canRedo()) return fail("nothing to redo");
                mSession.redo();
                refreshModel();
                emit(Event::Kind::HistoryChanged, mModel.history.lastLabel, mModel.history.current,
                     mModel.history.nodes);
                return true;

            case Command::Kind::PresetApply:
                if (!mSession.applyPreset(c.name)) return fail("preset not found: " + c.name);
                refreshModel();
                emit(Event::Kind::ParamsChanged, "preset=" + c.name);
                return true;
            case Command::Kind::PresetSave:
                if (!mSession.savePreset(c.name)) return fail("could not save preset: " + c.name);
                emit(Event::Kind::Info, "preset.saved name=" + c.name);
                return true;

            case Command::Kind::Export: return runExport(c);
            case Command::Kind::SettingsSet: return applySettingsFields(c);

            case Command::Kind::Screen:
                if (c.name == "home") mModel.screen = Screen::Home;
                else if (c.name == "editor") mModel.screen = Screen::Editor;
                else return fail("screen: expected home|editor");
                refreshModel();
                emit(Event::Kind::ScreenChanged, screenName(mModel.screen));
                return true;

            // All three are front-end concerns: only the caller knows where to print, only
            // the caller owns the loop a wait would spin, and the Segment tree `ui dump`
            // walks is presentation the service is forbidden to know about (R-SVC-3). The
            // service just refuses to guess.
            case Command::Kind::StatePrint:
            case Command::Kind::UiDump:
            case Command::Kind::Wait: return true;

            case Command::Kind::Quit: mQuit = true; return true;
        }
        return fail("unhandled command");
    }
}
}
