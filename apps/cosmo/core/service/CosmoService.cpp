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

    // ── events ────────────────────────────────────────────────────────────────────────
    void CosmoService::emit(const Event &e)
    {
        if (e.kind == Event::Kind::Error || e.kind == Event::Kind::CommandRejected) mModel.lastError = e.text;
        for (auto &s : mSinks) if (s) s(e);
    }

    void CosmoService::emit(Event::Kind k, const std::string &text, int a, int b, double ms)
    {
        Event e;
        e.kind = k; e.text = text; e.a = a; e.b = b; e.ms = ms;
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
            m.load.total = (int)mLoader.total();
            m.load.workers = mLoader.workers();
        }

        m.budget.percent = mBudget.percent();
        m.budget.cores = mBudget.cores();
        m.budget.total = mBudget.total();
        m.budget.engineThreads = mBudget.engineThreads();
        m.budget.decodeWorkers = mBudget.decodeWorkers();
        m.budget.peakDecode = mBudget.peakDecode();

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
        emit(Event::Kind::ScreenChanged, screenName(mModel.screen));
        emit(Event::Kind::ProjectOpening, stemOf(path), 0, (int)entries.size());

        mSession.resetWorkspace();

        // R-LOADUX-1: the whole rack exists before a single pixel decodes, so entry parents
        // resolve here and an out-of-order arrival can never reparent the tree.
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

    void CosmoService::pump(double)
    {
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
            emit(Event::Kind::FrameReady, std::string(), mModel.frameSlot, mFrame.width);
        }

        if (!mLoader.active() && mLoader.total() == 0) return;

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
            emit(Event::Kind::LoadProgress, r.name, (int)mLoader.consumed(), (int)mLoader.total());
        }
        if (!any) return;

        if (mLoader.finished())
        {
            const int total = (int)mLoader.total();
            int decoded = 0;
            for (const NodeModel &n : mModel.nodes) if (!n.group && n.slot >= 0) ++decoded;

            mSession.finishWorkspaceLoad(mLoadPath);
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
        *p = probe;
        mSession.submit();

        std::string names;
        for (const auto &kv : c.fields) { if (!names.empty()) names += ","; names += kv.first; }
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
            else return fail("settings: unknown key " + k);
        }
        std::string tail;
        for (const auto &kv : c.fields) { if (!tail.empty()) tail += " "; tail += kv.first + "=" + kv.second; }
        emit(Event::Kind::SettingsChanged, tail);
        refreshModel();
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
        emit(Event::Kind::ExportFinished, std::string(), written, failures);
        refreshModel();
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
                if (!EditSession::readWorkspaceFile(c.path, entries))
                    return fail("cannot read project " + c.path);
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
                bool found = false;
                for (const NodeModel &n : mModel.nodes)
                    if (n.node == c.index)
                    {
                        found = true;
                        if (n.slot >= 0) mSession.selectImage(n.slot);
                        else mSession.selectNodeById(n.node);
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

            case Command::Kind::Set: { const bool ok = applySetFields(c); refreshModel(); return ok; }

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
                for (const auto &kv : c.fields)
                {
                    const std::string &k = kv.first;
                    const float v = (float)std::atof(kv.second.c_str());
                    const bool on = kv.second != "0" && kv.second != "false";
                    if (k == "feather") m.feather = v;
                    else if (k == "inverted") m.inverted = on;
                    else if (k == "type") m.type = std::atoi(kv.second.c_str());
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
                    else return fail("mask set: unknown field " + k);
                }
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

            // Both are front-end concerns: only the caller knows where to print, and only
            // the caller owns the loop a wait would spin. The service just refuses to guess.
            case Command::Kind::StatePrint:
            case Command::Kind::Wait: return true;

            case Command::Kind::Quit: mQuit = true; return true;
        }
        return fail("unhandled command");
    }
}
}
