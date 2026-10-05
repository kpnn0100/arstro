#include "ServiceInternal.h"
#include "ApiDoc.h"
#include "AppModelCodec.h"
#include "Arrange.h"
#include "ParamHash.h"
#include "ParamRegistry.h"
#include "Project.h"
#include "Schema.h"
#include "Versions.h"
#include "engine/EditParamsIO.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    std::string fileStem(const std::string &path)
    {
        std::string f = path;
        const auto slash = f.find_last_of('/');
        if (slash != std::string::npos) f = f.substr(slash + 1);
        const auto dot = f.find_last_of('.');
        return dot == std::string::npos || dot == 0 ? f : f.substr(0, dot);
    }

    double snapToFrame(double t, double fps)
    {
        if (!std::isfinite(t)) return 0.0;
        if (fps <= 0) return t;
        return std::round(t * fps) / fps;
    }

    namespace
    {
        bool parseDouble(const std::string &s, double &out)
        {
            if (s.empty()) return false;
            char *end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && *end == '\0' && std::isfinite(out);
        }

        std::string joinNames(const std::vector<std::string> &v)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
            return s;
        }

        std::string hex12(uint64_t h)
        {
            char buf[24];
            std::snprintf(buf, sizeof buf, "%012llx", (unsigned long long)(h & 0xffffffffffffULL));
            return buf;
        }

        bool readFile(const std::string &path, std::string &out)
        {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            std::ostringstream s;
            s << f.rdbuf();
            out = s.str();
            return true;
        }
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // construction, events, the model
    // ──────────────────────────────────────────────────────────────────────────────────────────

    InterstellarService::InterstellarService(cosmo::ThreadBudget &budget, Host host)
        : mBudget(budget), mHost(std::move(host)), mFrames(std::make_shared<FrameSelector>()), mRack(budget),
          mProject(new Project()), mCache(new render::FrameCache()), mSync(new RenderCtx())
    {
        mGrade = mSync->grade.get();
        if (mHost.asyncPreview)
        {
            mPreview.reset(new PreviewWorker());
            mPreview->thread = std::thread([this] { previewLoop(); });
            // R-PLAY-2: a few frames graded side by side; each worker owns a decoder set and an engine
            mAhead.reset(new AheadPool());
            size_t k = (size_t)std::clamp((int)std::thread::hardware_concurrency() / 6, 2, 4);
            if (const char *e = std::getenv("INTERSTELLAR_AHEAD_WORKERS")) k = (size_t)std::clamp(std::atoi(e), 1, 8);   // benches
            for (size_t i = 0; i < k; ++i)
            {
                mAhead->ctxs.emplace_back(new RenderCtx());
                mAhead->resetSources.push_back(0);
            }
            mAhead->cacheSrcs.resize(k);
            for (size_t i = 0; i < k; ++i) mAhead->threads.emplace_back([this, i] { aheadLoop(i); });
        }
        if (mHost.audioOut)
        {
            // R-AUD-6: the sound thread; it opens the output the first time there is something to hear
            mPlayer.reset(new AudioPlayer());
            mPlayer->out = mHost.audioOut();
            mPlayer->thread = std::thread([this] { playerLoop(); });
        }
        if (mHost.audioSource)
        {
            mPeaks.reset(new PeakStore());
            mPeaks->thread = std::thread([this] { peaksLoop(); });
        }
        if (mHost.rackDecoder)
        {
            auto make = mHost.rackDecoder;
            std::shared_ptr<const FrameSelector> sel = mFrames;
            mRack.setDecoderFactory([make, sel] { return make(sel); });
        }
        // Cosmo's own events are the rack's log; re-published so one stream carries everything.
        mRack.subscribe([this](const cosmo::Event &e) {
            if (e.kind == cosmo::Event::Kind::Error)
                emit(Event(EK::Info).with("text", "rack: " + cosmo::formatEvent(e)));
        });
        if (!mHost.presetDir.empty()) mRack.setPresetDir(mHost.presetDir);
        loadSettings();
        mSettings.gpuAvailable = mGrade->gpuAvailable();
        applySettingsNow();
        rescanPresets();
        loadRecents();
        refreshModel();
    }

    InterstellarService::~InterstellarService()
    {
        stopCache();   // the builder grades through mCache and the host: first
        if (mPlayer)
        {
            {
                std::lock_guard<std::mutex> l(mPlayer->mu);
                mPlayer->quit = true;
            }
            mPlayer->cv.notify_all();
            if (mPlayer->thread.joinable()) mPlayer->thread.join();
        }
        if (mPeaks)
        {
            {
                std::lock_guard<std::mutex> l(mPeaks->mu);
                mPeaks->quit = true;
            }
            mPeaks->cv.notify_all();
            if (mPeaks->thread.joinable()) mPeaks->thread.join();
        }
        if (mAhead)
        {
            {
                std::lock_guard<std::mutex> l(mAhead->mu);
                mAhead->stop = true;
            }
            mAhead->cv.notify_all();
            for (auto &t : mAhead->threads) if (t.joinable()) t.join();
        }
        if (mPreview)
        {
            {
                std::lock_guard<std::mutex> l(mPreview->mu);
                mPreview->stop = true;
            }
            mPreview->cv.notify_all();
            if (mPreview->thread.joinable()) mPreview->thread.join();
        }
    }

    const Project &InterstellarService::project() const { return *mProject; }

    void InterstellarService::emit(const Event &e)
    {
        for (const auto &s : mSinks) s(e);
    }

    bool InterstellarService::fail(const std::string &why)
    {
        mModel.lastError = why;
        emit(Event(EK::Error).with("why", why));
        return false;
    }

    bool InterstellarService::requireProject()
    {
        if (mOpen) return true;
        return fail("no project is open — `project open <path.isp>` or `project new <path.isp>`");
    }

    void InterstellarService::bumpFrame() { ++mModel.frameSeq; }

    void InterstellarService::markDirty()
    {
        mModel.dirty = true;
        ++mProjectRev;
    }

    bool InterstellarService::dispatchText(const std::string &line, std::string &err)
    {
        const Command c = parseCommand(line, err);
        if (!err.empty())
        {
            mModel.lastError = err;
            emit(Event(EK::CommandRejected).with("line", line).with("why", err));
            return false;
        }
        if (!c.valid()) return true;   // blank or comment
        const std::string before = mModel.lastError;
        mModel.lastError.clear();
        const bool ok = dispatch(c);
        if (!ok)
        {
            err = mModel.lastError.empty() ? "refused: " + line : mModel.lastError;
            emit(Event(EK::CommandRejected).with("line", line).with("why", err));
        }
        else if (mModel.lastError.empty())
            mModel.lastError = before.empty() ? std::string() : std::string();
        refreshModel();
        return ok;
    }

    bool InterstellarService::dispatch(const Command &c)
    {
        mOutput.clear();
        // One history across the rack and the project (ServiceEdit.cpp): the state around an
        // undoable command is captured, and a change to the rack's node set starts history over.
        std::unique_ptr<UndoState> before;
        if (mOpen && undoable(c.kind))
        {
            before.reset(new UndoState());
            captureState(*before);
        }
        const bool ok = dispatchInner(c);
        // the preview cache re-checks its frames after anything, and waits for the user to stop
        if (ok) ++mEpoch;
        if (ok && mOpen) pruneAnims();   // a node that went takes its curves (R-ANIM-1)
        if (ok && mOpen && before) retimeRamps();   // R-EDT-3: a ramped clip's length follows its curve, in the same undo step
        if (c.kind != CK::CacheBuild && c.kind != CK::CacheClear && c.kind != CK::Wait && c.kind != CK::StatePrint) mLastCommandMs = mNowMs;
        if (ok && structural(c.kind)) clearHistory();
        else if (ok && before) recordEdit(c, *before);
        refreshModel();
        return ok;
    }

    bool InterstellarService::dispatchInner(const Command &c)
    {
        bool ok = false;
        switch (c.kind)
        {
            case CK::None: return true;
            case CK::ProjectNew: ok = projectNew(c); break;
            case CK::ProjectOpen: ok = projectOpen(c.arg(0)); break;
            case CK::ProjectSave: ok = requireProject() && projectSave(c.arg(0)); break;
            case CK::ProjectClose: projectClose(); ok = true; break;

            case CK::RackImport: case CK::RackAdd: case CK::RackGroupNew: case CK::RackDuplicate:
            case CK::RackFrame: case CK::RackRename: case CK::RackSelect: case CK::RackRemove:
                ok = requireProject() && rackCommand(c);
                break;

            case CK::Set:
                ok = requireProject();
                for (const auto &f : c.fields)
                    if (ok) ok = setAddress(f.first, f.second);
                break;
            case CK::Get: ok = requireProject() && getAddress(c.arg(0), false, currentTimeline(), false); break;
            case CK::Eval:
            {
                if (!requireProject()) break;
                NodeId tl = currentTimeline();
                if (c.has("timeline") && (tl = timelineRef(c.flag("timeline"))).empty())
                {
                    ok = fail("no timeline named " + c.flag("timeline"));
                    break;
                }
                ok = getAddress(c.arg(0), true, tl, c.has("explain"));
                break;
            }
            case CK::Revert:
            {
                if (!requireProject()) break;
                NodeId tl = currentTimeline();
                if (c.has("timeline") && (tl = timelineRef(c.flag("timeline"))).empty())
                {
                    ok = fail("no timeline named " + c.flag("timeline"));
                    break;
                }
                ok = revertAddress(c.arg(0), tl);
                break;
            }

            case CK::TimelineNew: case CK::TimelineList: case CK::TimelineOpen: case CK::TimelinePin:
            case CK::TimelineUnpin: case CK::TimelineFreeze: case CK::TimelineThaw: case CK::TimelineRebase:
            case CK::TimelineDiff: case CK::TimelineDelete:
                ok = requireProject() && timelineCommand(c);
                break;

            case CK::TrackAdd: case CK::ClipAdd: case CK::ClipTrim: case CK::ClipSplit: case CK::ClipMove:
            case CK::ClipDelete: case CK::ClipRoll: case CK::ClipSlip: case CK::ClipSpeed: case CK::ClipSelect:
            case CK::TransitionAdd: case CK::MarkerAdd: case CK::FxAdd: case CK::FxDelete:
            case CK::AudioTrackAdd: case CK::AudioClipAdd: case CK::ClipCopy: case CK::ClipPaste:
                ok = requireProject() && arrangeCommand(c);
                break;

            case CK::EffectAdd: case CK::EffectRemove: case CK::EffectMove:
                ok = requireProject() && effectCommand(c);
                break;

            case CK::Playhead: case CK::Play: case CK::Pause:
                ok = requireProject() && playheadCommand(c);
                break;
            case CK::Render: case CK::RenderCancel: ok = requireProject() && renderCommand(c); break;
            case CK::ColourWorking:
            {
                // R-COLOR-3: where the grade happens. The media keep their interpretation (`<bind>.input`);
                // every frame re-plans through the new transforms, so nothing stale is shown (law 7)
                if (!requireProject()) break;
                const std::string w = c.arg(0);
                if (!render::colour::known(render::colour::workings(), w))
                {
                    ok = fail("colour working: rec709 or acescct, got " + w);
                    break;
                }
                mProject->colorspace = w;
                markDirty();
                bumpFrame();
                emit(Event(EK::ParamsChanged).with("address", "colorspace").with("value", w).with("target", "project"));
                ok = true;
                break;
            }
            case CK::CacheBuild: case CK::CacheClear: ok = requireProject() && cacheCommand(c); break;
            case CK::KeyAdd: case CK::KeyRemove: case CK::KeySet: case CK::KeyClear: ok = requireProject() && animCommand(c); break;
            case CK::KeyShift: case CK::KeyCopy: case CK::KeyPaste: ok = requireProject() && keysCommand(c); break;
            case CK::ExportStill: ok = requireProject() && exportStill(c); break;
            case CK::LutExport: ok = requireProject() && lutExport(c); break;
            case CK::InterchangeExport: case CK::InterchangeImport: ok = requireProject() && interchangeCommand(c); break;
            case CK::Shuttle: case CK::Mark: case CK::SourceView: case CK::SourcePlayhead: case CK::EditTarget:
            case CK::EditInsert: case CK::EditOverwrite: ok = requireProject() && editingCommand(c); break;
            case CK::Capture:
            {
                if (!requireProject()) break;
                if (!c.has("out")) { ok = fail("capture: --out <p.png> is required"); break; }
                if (!mHost.writeImage) { ok = fail("capture: no PNG writer installed"); break; }
                Raster r;
                std::string err;
                if (!captureFrame(c.flag("source"), r)) { ok = false; break; }
                if (!mHost.writeImage(c.flag("out"), r, err)) { ok = fail("capture: " + err); break; }
                mOutput = c.flag("out") + "\n";
                emit(Event(EK::RenderFinished).with("job", "capture").with("timeline", c.flag("source")).with("frames", 1).with("out", c.flag("out")));
                ok = true;
                break;
            }

            case CK::StatePrint:
            {
                refreshModel();
                ModelDumpOptions o;
                o.json = c.has("json");
                o.stable = c.has("stable");
                mOutput = formatModel(mModel, o);
                ok = true;
                break;
            }
            case CK::Api: mOutput = c.has("md") ? apiMarkdown() : apiJson(); ok = true; break;
            case CK::Lint: ok = requireProject() && lint(); break;
            case CK::Wait: ok = wait(c); break;
            case CK::Quit: mQuit = true; ok = true; break;

            case CK::Undo: case CK::Redo: case CK::SettingsSet: case CK::PresetImport:
                ok = editCommand(c);
                break;
            case CK::GradeCopy: case CK::GradePaste: case CK::RackUngroup: case CK::PresetApply: case CK::PresetSave:
                ok = requireProject() && editCommand(c);
                break;
        }
        return ok;
    }

    void InterstellarService::pump(double nowMs)
    {
        if (nowMs > mNowMs) mNowMs = nowMs;
        mRack.pump(mNowMs);
        if (mPreview)
        {
            const unsigned seq = mPreview->doneSeq.load();
            if (seq != mPreview->seenSeq)
            {
                mPreview->seenSeq = seq;
                bumpFrame();       // the requested monitor frame has landed: the view asks again
                refreshModel();
            }
        }
        if (mPending && !mRack.loading()) { finishRackLoad(); ++mEpoch; }

        if (mPlaying && mOpen) scheduleAhead();
        if (mPlaying && mOpen && mPreroll)
        {
            // pre-roll (R-PLAY-2): the clock starts when the first frames are graded, or after half a
            // second whatever happens — an editor's pre-roll, not a stall
            size_t ready = 0;
            if (mAhead)
            {
                std::lock_guard<std::mutex> l(mAhead->mu);
                for (const auto &kv : mAhead->done) ready += kv.second.first > mModel.playhead;
            }
            const size_t want = mAhead ? std::min<size_t>(3, mAhead->threads.size() + 1) : 0;
            if (ready >= want || mNowMs - mPrerollFromMs >= 500.0)
            {
                mPreroll = false;
                mPlayFromT = mModel.playhead;
                mPlayFromMs = mNowMs;
                startSound();   // R-AUD-6: the sound starts with the picture's clock
            }
        }
        if (mPlaying && mOpen && !mPreroll)
        {
            const double dur = timelineDuration(currentTimeline());
            syncSoundPlan();
            // R-AUD-6: when it is heard, the sound is the clock; otherwise the wall clock, as before
            double t = 0;
            if (!soundTime(t)) t = mPlayFromT + mShuttle * (mNowMs - mPlayFromMs) / 1000.0;   // R-EDT-2: at the shuttle's rate
            if ((dur > 0 && t >= dur && mShuttle > 0) || (mShuttle < 0 && t <= 0.0))
            {
                t = std::clamp(t, 0.0, dur > 0 ? dur : t);   // the end going forward, the start going back
                mPlaying = false;
                mShuttle = 0.0;
                mPlayEdge = 0;
                stopSound();
                emit(Event(EK::PlaybackChanged).with("playing", false));
            }
            const double snapped = snapToFrame(t, mProject->fps);
            if (snapped != mModel.playhead)
            {
                mModel.playhead = snapped;
                bumpFrame();
                refreshModel();
            }
        }
        pumpJobs();
        pumpPreviewCache();
    }

    void InterstellarService::resetPreview()
    {
        // A different project: the worker's decoders point at the old media, and the last frame
        // belongs to the old project. Dropped under the lock; the worker re-opens what it needs.
        if (mAhead)
        {
            std::lock_guard<std::mutex> l(mAhead->mu);
            mAhead->queue.clear();
            mAhead->done.clear();
            mAhead->doneFromCache.clear();
            for (auto &r : mAhead->resetSources) r = 1;
        }
        if (mPCache) mPCache->loaded = false;   // another project: its own index
        if (!mPreview) return;
        std::lock_guard<std::mutex> l(mPreview->mu);
        mPreview->pending.reset();
        mPreview->done = Raster{};
        mPreview->doneKey.clear();
        mPreview->resetSources = true;
    }

    bool InterstellarService::busy() const
    {
        if (mPending || mCacheForced) return true;
        for (const auto &j : mJobs)
            if (j->model.state == "queued" || j->model.state == "running") return true;
        return false;
    }

    bool InterstellarService::pumpUntilIdle(int maxMs)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(maxMs);
        while (busy())
        {
            if (std::chrono::steady_clock::now() >= deadline) return false;
            pump(mNowMs + 16.0);
            // Real time, because the rack decodes on a pool (Rack::pumpUntilLoaded's lesson).
            if (mPending || mCacheForced) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }

    NodeId InterstellarService::currentTimeline() const
    {
        if (!mOpen) return {};
        if (mProject->timeline(mProject->current)) return mProject->current;
        return mProject->timelines.empty() ? NodeId() : mProject->timelines.front().id;
    }

    NodeId InterstellarService::timelineRef(const std::string &s) const
    {
        const NodeId id = mProject->idForRef(s);
        return mProject->timeline(id) ? id : NodeId();
    }

    std::string InterstellarService::resolvePath(const std::string &p) const
    {
        if (p.empty()) return p;
        fs::path q(p);
        if (q.is_absolute() || mIspPath.empty()) return q.lexically_normal().string();
        return (fs::path(mIspPath).parent_path() / q).lexically_normal().string();
    }

    std::string InterstellarService::relativePath(const std::string &p) const
    {
        if (mIspPath.empty()) return p;
        const fs::path base = fs::absolute(fs::path(mIspPath)).parent_path();
        const fs::path abs = fs::absolute(fs::path(p)).lexically_normal();
        const fs::path rel = abs.lexically_relative(base);
        // Only relative when it stays inside the project folder; otherwise the absolute path is the
        // honest reference (a "../../.." chain breaks the moment the project is moved).
        if (rel.empty() || rel.begin()->string() == "..") return abs.string();
        return rel.string();
    }

    double InterstellarService::timelineDuration(const NodeId &tl) const
    {
        ResolvedTimeline R;
        std::string err;
        if (tl.empty() || !resolved(tl, R, err)) return 0;
        double d = 0;
        for (const auto &c : R.clips) d = std::max(d, c.end());
        for (const auto &a : R.audioClips) d = std::max(d, a.at + std::max(0.0, a.out - a.in));
        return d;
    }

    bool InterstellarService::resolved(const NodeId &tl, ResolvedTimeline &out, std::string &err) const
    {
        return resolve(*mProject, tl, out, err);
    }

    void InterstellarService::refreshModel()
    {
        AppModel &m = mModel;
        ++m.revision;
        m.screen = !mOpen ? (mPending && mPending->projectOpening ? Screen::Loading : Screen::Home)
                          : (mPending && mPending->projectOpening ? Screen::Loading : Screen::Edit);
        m.playing = mPlaying;
        if (!mOpen)
        {
            m.projectPath.clear();
            m.projectName.clear();
            m.dirty = false;
            m.rack.clear();
            m.selectedRack = -1;
            m.hasGradeTarget = false;
            m.gradeParams = EditParams{};
            m.gradeOwnParams = EditParams{};
            m.timelines.clear();
            m.currentTimeline.clear();
            m.tracks.clear();
            m.clips.clear();
            m.transitions.clear();
            m.markers.clear();
            m.selectedClip.clear();
            m.duration = 0;
            m.renders.clear();
            fillEditModel();
            return;
        }
        const Project &P = *mProject;
        m.projectPath = mIspPath;
        m.projectName = P.name;
        m.fps = P.fps;
        m.width = P.width;
        m.height = P.height;

        const NodeId cur = currentTimeline();
        ResolvedTimeline R;
        std::string err;
        const bool haveR = !cur.empty() && resolved(cur, R, err);

        // ── rack ──
        m.rack.clear();
        std::map<NodeId, int> usedBy;
        if (haveR)
            for (const auto &c : R.clips) usedBy[c.src]++;
        std::set<NodeId> overridden;
        for (const auto &g : P.grades)
            if (g.timeline == cur && !g.deltas.empty()) overridden.insert(g.node);

        // How many sources use each file: a variant shares its original's (R-RACK-5).
        std::map<std::string, int> filesUsed;
        for (const auto &ro : P.rackObjs)
            if (ro.kind != "group" && !ro.media.empty()) filesUsed[resolvePath(ro.media)]++;
        auto rackModelFor = [&](const RackObj *ro) {
            RackNodeModel r;
            if (ro)
            {
                r.rackObj = ro->id;
                r.bindName = ro->name;
                r.weight = ro->weight;
                r.media = ro->media;
                r.frame = ro->frame;
                r.input = ro->input.empty() ? std::string("rec709") : ro->input;
                r.lut = ro->lut;
                r.video = looksLikeVideo(ro->media);
                r.group = ro->kind == "group";
                r.usedBy = usedBy.count(ro->id) ? usedBy[ro->id] : 0;
                // The source's length, once something has opened it (select does; a load does not
                // open every file — that would be a stall per video at the end of a load).
                const auto src = mSync->sources.find(resolvePath(ro->media));
                if (src != mSync->sources.end() && src->second->ok)
                {
                    r.mediaFps = src->second->info.fps > 0 ? src->second->info.fps : 24.0;
                    r.mediaBitDepth = src->second->info.bitDepth;
                    r.timecode = src->second->info.timecode;
                    r.reel = src->second->info.reel;
                    r.mediaDuration = src->second->info.frames <= 1 ? 0.0 : (double)src->second->info.frames / r.mediaFps;
                }
                r.overridden = overridden.count(ro->id) > 0;
                if (!r.group && !ro->media.empty()) r.sharesMedia = filesUsed[resolvePath(ro->media)] - 1;
            }
            return r;
        };
        std::set<NodeId> listed;
        if (mRack.isOpen())
        {
            const auto &ns = mRack.model().nodes;
            for (const auto &n : ns)
            {
                const NodeId roId = rackObjOfCosmo(n.node);
                RackNodeModel r = rackModelFor(P.rackObj(roId));
                r.node = n.node;
                r.cosmoName = n.name;
                r.parent = n.parent < 0 ? -1 : mRack.indexOf(n.parent);
                r.depth = n.depth;
                r.group = n.group;
                r.bypass = n.bypass;
                r.pending = n.pending;
                r.failed = n.failed;
                if (r.bindName.empty()) r.bindName = "";   // unbound until a load completes
                m.rack.push_back(r);
                if (!roId.empty()) listed.insert(roId);
            }
        }
        // A #rackobj the rack does not have is OFFLINE — shown as missing, never dropped (R-RACK-7).
        for (const auto &ro : P.rackObjs)
            if (!listed.count(ro.id))
            {
                RackNodeModel r = rackModelFor(&ro);
                r.failed = true;
                m.rack.push_back(r);
            }

        mSelection.erase(std::remove_if(mSelection.begin(), mSelection.end(), [&](const NodeId &id) { return !P.rackObj(id); }),
                         mSelection.end());
        for (auto &r : m.rack)
            r.selected = !r.rackObj.empty() && std::find(mSelection.begin(), mSelection.end(), r.rackObj) != mSelection.end();
        m.selectedRack = -1;
        if (mRack.isOpen() && mRack.selectedNode() >= 0)
            for (size_t i = 0; i < m.rack.size(); ++i)
                if (m.rack[i].node == mRack.selectedNode()) m.selectedRack = (int)i;

        m.hasGradeTarget = false;
        m.gradeParams = EditParams{};
        m.gradeOwnParams = EditParams{};
        if (m.selectedRack >= 0 && !m.rack[(size_t)m.selectedRack].rackObj.empty() && !cur.empty())
        {
            ColourTree tree;
            std::map<NodeId, int> idx;
            std::string source, e2;
            const NodeId ro = m.rack[(size_t)m.selectedRack].rackObj;
            if (colourTreeFor(cur, tree, idx, source, e2) && idx.count(ro))
            {
                applyColourCurves(cur, tree, idx, sourceNow(ro));   // the panels show the curves where Grade stands (R-ANIM-3)
                for (auto &kv : idx) applyDeltas(tree[(size_t)kv.second].own, gradeDeltas(P, cur, kv.first));
                m.gradeOwnParams = tree[(size_t)idx[ro]].own;
                m.gradeParams = foldEditTarget(tree, idx[ro]);
                m.hasGradeTarget = true;
            }
        }
        m.sourceWidth = mRack.isOpen() ? mRack.model().sourceWidth : 0;
        m.sourceHeight = mRack.isOpen() ? mRack.model().sourceHeight : 0;

        // ── versions: base before derived, depth for the switcher ──
        m.timelines.clear();
        std::function<void(const NodeId &, int)> walk = [&](const NodeId &base, int depth) {
            std::vector<const Timeline *> kids;
            for (const auto &t : P.timelines)
                if (t.base == base) kids.push_back(&t);
            std::stable_sort(kids.begin(), kids.end(), [](const Timeline *a, const Timeline *b) {
                return a->order != b->order ? a->order < b->order : a->id < b->id;
            });
            for (const Timeline *t : kids)
            {
                TimelineModel tm;
                tm.id = t->id;
                tm.name = t->name;
                tm.base = t->base;
                tm.depth = depth;
                tm.colourPinned = t->pinned();
                tm.pinCommit = t->pinCommit();
                tm.cutFrozen = t->frozen();
                ResolvedTimeline tr;
                std::string e3;
                if (resolved(t->id, tr, e3)) tm.danglingDeltas = (int)tr.dangling.size();
                for (const auto &d : P.drops) tm.overrides += d.timeline == t->id;
                for (const auto &s : P.sets) tm.overrides += s.timeline == t->id;
                for (const auto &g : P.grades) tm.overrides += g.timeline == t->id;
                {
                    render::AudioPlan ap;
                    tm.hasSound = mHost.audioSource && planAudio(t->id, ap) && !ap.empty();
                }
                std::string e4;   // R-EDT-4: what the lane menu may offer to place here
                tm.placeable = t->id != P.current && !nestingRefused(P, P.current, t->id, e4);
                m.timelines.push_back(tm);
                if (m.timelines.size() <= P.timelines.size()) walk(t->id, depth + 1);
            }
        };
        walk(NodeId(), 0);
        // A timeline whose base is missing would be unreachable from the roots; list it anyway.
        for (const auto &t : P.timelines)
        {
            bool have = false;
            for (const auto &tm : m.timelines) have = have || tm.id == t.id;
            if (!have)
            {
                TimelineModel tm;
                tm.id = t.id;
                tm.name = t.name;
                tm.base = t.base;
                m.timelines.push_back(tm);
            }
        }
        m.currentTimeline = cur;

        // ── the resolved current timeline ──
        m.tracks.clear();
        m.clips.clear();
        m.transitions.clear();
        m.markers.clear();
        if (haveR)
        {
            auto prov = [&](const NodeId &id) {
                const auto it = R.provenance.find(id);
                return it == R.provenance.end() ? Provenance::Local : it->second;
            };
            for (const auto &t : R.tracks)
            {
                TrackModel tm;
                tm.id = t.id;
                tm.name = t.name;
                tm.audio = t.audio();
                tm.order = t.order;
                tm.mute = t.mute;
                tm.opacity = t.opacity;
                tm.gain = t.gain;
                tm.provenance = prov(t.id);
                m.tracks.push_back(tm);
            }
            for (const auto &t : R.audioTracks)
            {
                TrackModel tm;
                tm.id = t.id;
                tm.name = t.name;
                tm.audio = true;
                tm.order = 1000 + t.order;
                tm.mute = t.mute;
                tm.gain = t.gain;
                tm.provenance = prov(t.id);
                m.tracks.push_back(tm);
            }
            for (const auto &c : R.clips)
            {
                ClipModel cm;
                cm.id = c.id;
                cm.name = c.name;
                cm.track = c.track;
                cm.src = c.src;
                const RackObj *ro = P.rackObj(c.src);
                const Timeline *nt = ro ? nullptr : P.timeline(c.src);   // R-EDT-4
                cm.nested = nt != nullptr;
                cm.srcName = ro ? ro->name : nt ? nt->name : c.src;
                cm.at = c.at;
                cm.in = c.in;
                cm.out = c.out;
                cm.speed = c.speed;
                cm.duration = c.duration();
                cm.opacity = c.opacity;
                cm.offline = !nt && (!ro || cosmoNodeOf(ro->id) < 0);
                cm.provenance = prov(c.id);
                m.clips.push_back(cm);
            }
            for (const auto &a : R.audioClips)
            {
                ClipModel cm;
                cm.id = a.id;
                cm.name = a.name;
                cm.track = a.track;
                cm.src = a.src;
                cm.srcName = fileStem(a.src);
                cm.at = a.at;
                cm.in = a.in;
                cm.out = a.out;
                cm.duration = std::max(0.0, a.out - a.in);
                cm.gain = a.gain;
                cm.audio = true;
                cm.media = resolvePath(a.src);
                cm.offline = !fs::exists(cm.media);
                if (!cm.offline) requestPeaks(cm.media);   // R-AUD-7: its envelope, computed once
                cm.provenance = prov(a.id);
                m.clips.push_back(cm);
            }
            for (const auto &t : R.transitions)
                m.transitions.push_back({t.id, t.clipA, t.clipB, t.kind, t.dur});
            for (const auto &k : R.markers) m.markers.push_back({k.id, k.name, k.at, k.note});
        }
        bool selValid = false;
        for (const auto &c : m.clips) selValid = selValid || c.id == mSelectedClip;
        m.selectedClip = selValid ? mSelectedClip : NodeId();
        m.duration = 0;
        for (const auto &c : m.clips) m.duration = std::max(m.duration, c.at + c.duration);
        m.playheadFrame = (long long)std::llround(m.playhead * (P.fps > 0 ? P.fps : 24.0));
        m.frameWidth = P.width;
        m.frameHeight = P.height;

        m.renders.clear();
        for (const auto &j : mJobs) m.renders.push_back(j->model);
        fillEditModel();
    }

    void InterstellarService::fillEditModel()
    {
        AppModel &m = mModel;
        m.canUndo = !mUndo.empty();
        m.canRedo = !mRedo.empty();
        m.undoLabel = mUndo.empty() ? std::string() : mUndo.back().label;
        m.redoLabel = mRedo.empty() ? std::string() : mRedo.back().label;
        m.hasGradeClipboard = mHasClipboard;
        m.gradeClipboardFrom = mClipboardFrom;
        m.playbackEdge = mPlaying ? mPlayEdge : 0;
        m.playbackRate = mPlaying ? mPlayRate : 0.0;
        fillAnimModel(m);
        fillCacheModel(m);
        // ── the plugin stacks (R-FX-5) ──
        m.effects.clear();
        if (mProject && mOpen)
        {
            const Project &P = *mProject;
            std::vector<const Effect *> all;
            for (const auto &e : P.imageEffects) all.push_back(&e);
            std::stable_sort(all.begin(), all.end(), [](const Effect *a, const Effect *b) {
                return a->node != b->node ? a->node < b->node : a->order < b->order;
            });
            for (const Effect *e : all)
            {
                EffectModel em;
                em.id = e->id;
                em.node = e->node;
                if (const RackObj *ro = P.rackObj(e->node)) em.nodeBind = ro->name;
                em.type = e->type;
                em.order = e->order;
                em.enabled = e->enabled;
                em.mix = e->mix;
                if (const render::EffectTypeDef *def = render::effectType(e->type))
                {
                    em.label = def->label;
                    em.family = def->family;
                    for (const auto &d : def->params)
                    {
                        EffectParamModel pm{d.key, d.label, d.unit, d.def, d.def, d.min, d.max};
                        for (const auto &kv : e->unknown) if (kv.first == d.key) parseDouble(kv.second, pm.value);
                        em.params.push_back(pm);
                    }
                    for (const auto &fk : def->files)
                    {
                        em.fileKey = fk;
                        for (const auto &kv : e->unknown) if (kv.first == fk) em.file = kv.second;
                    }
                }
                else em.label = e->type + " (unknown to this build)";
                m.effects.push_back(std::move(em));
            }
        }
        if (m.effectTypes.empty())
            for (const auto &t : render::effectCatalog()) m.effectTypes.push_back({t.type, t.label, t.family});
        if (m.colourInputs.empty())
        {
            for (const auto &d : render::colour::inputs()) m.colourInputs.push_back({d.id, d.label});
            for (const auto &d : render::colour::outputs()) m.colourOutputs.push_back({d.id, d.label});
        }
        m.workingSpace = render::colour::known(render::colour::workings(), mProject->colorspace) ? mProject->colorspace : std::string("rec709");
        fillSoundModel(m);
        // R-EDT-1/2
        m.shuttle = mPlaying ? mShuttle : 0.0;
        m.markIn = mMarkIn;
        m.markOut = mMarkOut;
        m.sourceView = mSourceView;
        m.sourceIn = mSourceIn;
        m.sourceOut = mSourceOut;
        m.sourcePlayhead = mSourcePlayhead;
        m.sourceDuration = 0.0;
        if (!mSourceView.empty())
            if (const RackObj *sv = mProject->rackObj(mProject->idForRef(mSourceView)))
            {
                const auto it = mSync->sources.find(resolvePath(sv->media));
                if (it != mSync->sources.end() && it->second->ok && it->second->info.frames > 1)
                    m.sourceDuration = it->second->info.frames / (it->second->info.fps > 0 ? it->second->info.fps : mProject->fps);
            }
        m.targetTrack = mTargetTrack;
        m.hasClipClipboard = mHasClipClipboard;
        m.clipClipboardFrom = mHasClipClipboard && mClipClipboard ? mClipClipboard->name : std::string();
        m.settings = mSettings;
        m.settings.gpuInUse = mGrade->lastAccelerated();   // R-GPU-1: measured, the UI thread's last grade
        m.presets = mPresets;
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // project lifecycle
    // ──────────────────────────────────────────────────────────────────────────────────────────

    bool InterstellarService::projectNew(const Command &c)
    {
        const std::string path = c.arg(0);
        if (fs::exists(path)) return fail("project new: " + path + " already exists — `project open` it instead");
        if (mOpen) projectClose();

        auto P = std::make_unique<Project>();
        P->name = fileStem(path);
        P->id = "prj_" + P->name;
        if (c.has("fps"))
        {
            double fps = 0;
            if (!parseDouble(c.flag("fps"), fps) || fps <= 0 || fps > 240)
                return fail("project new: --fps must be a frame rate in (0, 240], got " + c.flag("fps"));
            P->fps = fps;
        }
        if (c.has("res"))
        {
            int w = 0, h = 0;
            if (std::sscanf(c.flag("res").c_str(), "%dx%d", &w, &h) != 2 || w < 16 || h < 16 || w > 16384 || h > 16384)
                return fail("project new: --res must be WxH, got " + c.flag("res"));
            P->width = w;
            P->height = h;
        }
        P->hasRack = true;
        P->rack.id = "rk_1";
        P->rack.path = fileStem(path) + ".cmp";
        NodeId main;
        std::string err;
        if (!newTimeline(*P, "main", NodeId(), main, err)) return fail("project new: " + err);
        P->current = main;

        mIspPath = fs::absolute(path).lexically_normal().string();
        mProject = std::move(P);
        const std::string cmp = resolvePath(mProject->rack.path);
        if (fs::exists(cmp)) return fail("project new: the rack " + cmp + " already exists — `rack import` it into a project");
        if (!mRack.newProject(cmp, err)) return fail("project new: " + err);
        mOpen = true;
        mNodeOf.clear();
        mPins.clear();
        mSync->sources.clear();
        resetPreview();
        mModel.playhead = 0;
        if (!projectSave(std::string())) return false;
        touchRecent(mIspPath);
        emit(Event(EK::ProjectOpened).with("path", mIspPath).with("timelines", 1).with("rack", cmp));
        emit(Event(EK::ScreenChanged).with("screen", "edit"));
        bumpFrame();
        return true;
    }

    bool InterstellarService::projectOpen(const std::string &path)
    {
        auto P = std::make_unique<Project>();
        std::string err;
        int repaired = 0;
        if (!P->load(path, err, &repaired)) return fail("project open: " + err);
        if (mOpen) projectClose();
        mIspPath = fs::absolute(path).lexically_normal().string();
        mProject = std::move(P);
        mOpen = true;
        mNodeOf.clear();
        mPins.clear();
        mSync->sources.clear();
        resetPreview();
        mCache->clear();
        mModel.playhead = 0;
        mModel.dirty = false;
        if (repaired > 0)
            emit(Event(EK::Info).with("text", "repaired " + std::to_string(repaired) + " non-finite value(s) to their defaults"));
        touchRecent(mIspPath);

        if (!mProject->hasRack)
        {
            emit(Event(EK::ProjectOpened).with("path", mIspPath).with("timelines", (int)mProject->timelines.size()).with("rack", ""));
            return true;
        }
        const std::string cmp = resolvePath(mProject->rack.path);
        ColourTree entries;
        if (!fs::exists(cmp) || !colourTreeFromCmp(cmp, entries, err))
        {
            // R-RACK-7: the project stays openable; every rack node reads as missing.
            emit(Event(EK::ProjectOpened).with("path", mIspPath).with("timelines", (int)mProject->timelines.size()).with("rack", cmp));
            return fail("the rack " + cmp + " is missing — every rack node is offline until `rack import` finds it");
        }
        mPending.reset(new PendingRack());
        mPending->kind = PendingRack::Kind::Open;
        mPending->projectOpening = true;
        bindFromCmp(entries);
        if (!mRack.beginOpen(cmp, err))
        {
            mPending.reset();
            return fail("project open: the rack refused: " + err);
        }
        emit(Event(EK::ScreenChanged).with("screen", "loading"));
        return true;
    }

    void InterstellarService::finishRackLoad()
    {
        std::unique_ptr<PendingRack> p = std::move(mPending);
        if (!p) return;
        std::vector<int> added;
        mRack.finishLoad(&added);

        if (p->kind == PendingRack::Kind::Add)
        {
            // Cosmo appends in the order it was handed the paths.
            for (size_t i = 0; i < added.size() && i < p->addedRackObjs.size(); ++i)
                mNodeOf[p->addedRackObjs[i]] = added[i];
            if (added.size() != p->addedRackObjs.size())
                emit(Event(EK::Error).with("why", "rack add: Cosmo added " + std::to_string(added.size()) + " of " +
                                                      std::to_string(p->addedRackObjs.size()) + " sources"));
            markDirty();
            for (const auto &ro : p->addedRackObjs) emit(Event(EK::RackChanged).with("what", "added").with("node", ro));
        }
        else
        {
            // `.cmp` entry i is live node i: Cosmo saves its tree depth-first and loads it in file
            // order, and treeRows walks the same order. Checked by count — a mismatch is reported.
            mNodeOf.clear();
            const auto &ns = mRack.model().nodes;
            if (ns.size() != p->entryRackObj.size())
                emit(Event(EK::Error).with("why", "rack: the .cmp lists " + std::to_string(p->entryRackObj.size()) +
                                                      " nodes but Cosmo loaded " + std::to_string(ns.size())));
            for (size_t i = 0; i < ns.size() && i < p->entryRackObj.size(); ++i)
                if (!p->entryRackObj[i].empty()) mNodeOf[p->entryRackObj[i]] = ns[i].node;
        }
        int failed = 0;
        for (const auto &n : mRack.model().nodes) failed += n.failed;
        emit(Event(EK::RackLoaded).with("images", mRack.imageCount()).with("failed", failed));
        if (p->projectOpening)
        {
            emit(Event(EK::ProjectOpened)
                     .with("path", mIspPath)
                     .with("timelines", (int)mProject->timelines.size())
                     .with("rack", mRack.path()));
            emit(Event(EK::ScreenChanged).with("screen", "edit"));
        }
        mCache->clear();
        bumpFrame();
        refreshModel();
    }

    bool InterstellarService::projectSave(const std::string &path)
    {
        std::string err, blocked;
        const bool rackBlocked = rackSaveBlocked(blocked);
        if (mRack.isOpen() && !mPending && !rackBlocked)
        {
            if (!mRack.save(err)) return fail("project save: the rack refused: " + err);
            syncRackObjNodes();
        }
        const std::string target = path.empty() ? mIspPath : fs::absolute(path).lexically_normal().string();
        if (target != mIspPath && fs::path(target).parent_path() != fs::path(mIspPath).parent_path())
        {
            // Save As into another folder: every reference is relative to the .isp, so each is
            // re-expressed against the new folder — the rack, the media, the audio. Otherwise the
            // copy opens with every source offline.
            auto rebase = [&](std::string &ref) {
                if (ref.empty()) return;
                const std::string abs = resolvePath(ref);
                const fs::path base = fs::path(target).parent_path();
                const fs::path rel = fs::path(abs).lexically_relative(base);
                ref = rel.empty() || rel.begin()->string() == ".." ? abs : rel.string();
            };
            if (mProject->hasRack) rebase(mProject->rack.path);
            for (auto &ro : mProject->rackObjs) rebase(ro.media);
            for (auto &a : mProject->audioClips) rebase(a.src);
        }
        if (!mProject->save(target, err)) return fail("project save: " + err);
        mIspPath = target;
        mModel.dirty = false;
        emit(Event(EK::ProjectSaved).with("path", target).with("cmp", rackBlocked ? std::string() : mRack.path()));
        // The .isp is saved; the rack is NOT, and saying so is the point — a silent partial save
        // would read as success while this session's colour edits stayed unsaved.
        if (rackBlocked) return fail("project save: saved the .isp but NOT the rack: " + blocked);
        return true;
    }

    void InterstellarService::projectClose()
    {
        const bool was = mOpen;
        mOpen = false;
        mPending.reset();
        mPlaying = false;
        mProject.reset(new Project());
        mIspPath.clear();
        mNodeOf.clear();
        mStoredPath.clear();
        mSelection.clear();
        mAnchor.clear();
        mPins.clear();
        mSync->sources.clear();
        resetPreview();
        mJobs.clear();
        mCache->clear();
        mSync->grade->releaseScratch();
        mSelectedClip.clear();
        mModel.playhead = 0;
        mFrames->clear();
        loadRecents();
        if (was)
        {
            emit(Event(EK::ProjectClosed));
            emit(Event(EK::ScreenChanged).with("screen", "home"));
        }
    }

    void InterstellarService::loadRecents()
    {
        mModel.recents.clear();
        if (mHost.recentsPath.empty()) return;
        std::ifstream f(mHost.recentsPath);
        std::string line;
        while (std::getline(f, line))
        {
            const auto tab = line.find('\t');
            const std::string path = line.substr(0, tab);
            if (path.empty() || !fs::exists(path)) continue;
            RecentModel r;
            r.path = path;
            r.lastOpened = tab == std::string::npos ? 0 : std::atoll(line.c_str() + tab + 1);
            Project p;
            std::string err;
            if (!p.load(path, err)) continue;
            r.name = p.name.empty() ? fileStem(path) : p.name;
            const fs::path dir = fs::path(path).parent_path();
            for (const auto &ro : p.rackObjs)
            {
                if (ro.kind == "group") continue;
                ++r.sourceCount;
                const fs::path m = fs::path(ro.media).is_absolute() ? fs::path(ro.media) : dir / ro.media;
                std::error_code ec;
                const auto sz = fs::file_size(m, ec);
                if (!ec) r.sizeBytes += (long long)sz;
                if (r.coverPath.empty() && !ec) r.coverPath = m.string();
            }
            mModel.recents.push_back(r);
        }
    }

    void InterstellarService::touchRecent(const std::string &path)
    {
        if (mHost.recentsPath.empty()) return;
        std::vector<std::string> lines;
        {
            std::ifstream f(mHost.recentsPath);
            std::string line;
            while (std::getline(f, line))
                if (line.substr(0, line.find('\t')) != path) lines.push_back(line);
        }
        std::error_code ec;
        fs::create_directories(fs::path(mHost.recentsPath).parent_path(), ec);
        std::ofstream f(mHost.recentsPath, std::ios::trunc);
        f << path << '\t' << (long long)std::time(nullptr) << '\n';
        for (size_t i = 0; i < lines.size() && i < 31; ++i) f << lines[i] << '\n';
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // the rack and its binding to #rackobj
    // ──────────────────────────────────────────────────────────────────────────────────────────

    int InterstellarService::cosmoNodeOf(const NodeId &ro) const
    {
        const auto it = mNodeOf.find(ro);
        return it == mNodeOf.end() ? -1 : it->second;
    }

    NodeId InterstellarService::rackObjOfCosmo(int node) const
    {
        for (const auto &kv : mNodeOf)
            if (kv.second == node) return kv.first;
        return {};
    }

    std::string InterstellarService::bindNameFor(const std::string &cosmoName) const
    {
        std::string f;
        double t = 0;
        splitFrameSelector(cosmoName, f, t);
        std::string stem = fileStem(f);
        for (char &ch : stem) ch = (char)std::tolower((unsigned char)ch);
        return mProject->freshName(stem.empty() ? "node" : stem);
    }

    void InterstellarService::bindFromCmp(const std::vector<ColourNode> &entries)
    {
        // Each `.cmp` entry to its #rackobj. First by `node=cn_<i>` (the entry index written at the
        // last save), validated by media for sources. Should that disagree — the `.cmp` was edited
        // in Cosmo meanwhile — fall back to media (by occurrence, so two variants of one file keep
        // their order) for sources and to group order for groups. An entry nobody claims becomes a
        // new #rackobj; a #rackobj nothing claims stays OFFLINE, never deleted (R-RACK-7).
        Project &P = *mProject;
        std::vector<NodeId> bound(entries.size());
        auto mediaOf = [&](const ColourNode &e) {
            std::string f;
            double t = 0;
            splitFrameSelector(e.imagePath, f, t);
            return resolvePath(f);
        };

        bool indexOk = true;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const std::string want = "cn_" + std::to_string(i);
            for (const auto &ro : P.rackObjs)
                if (ro.node == want)
                {
                    const bool kindOk = (ro.kind == "group") == entries[i].group;
                    const bool mediaOk = entries[i].group || resolvePath(ro.media) == mediaOf(entries[i]);
                    if (kindOk && mediaOk) bound[i] = ro.id;
                    else indexOk = false;
                }
        }
        if (!indexOk)
        {
            emit(Event(EK::Info).with("text", "rack: the .cmp changed outside Interstellar — re-binding by media"));
            bound.assign(entries.size(), NodeId());
            std::set<NodeId> taken;
            std::vector<const RackObj *> groups;
            for (const auto &ro : P.rackObjs)
                if (ro.kind == "group") groups.push_back(&ro);
            size_t nextGroup = 0;
            for (size_t i = 0; i < entries.size(); ++i)
            {
                if (entries[i].group)
                {
                    if (nextGroup < groups.size()) { bound[i] = groups[nextGroup]->id; taken.insert(bound[i]); }
                    ++nextGroup;
                    continue;
                }
                for (const auto &ro : P.rackObjs)
                    if (ro.kind != "group" && !taken.count(ro.id) && resolvePath(ro.media) == mediaOf(entries[i]))
                    {
                        bound[i] = ro.id;
                        taken.insert(ro.id);
                        break;
                    }
            }
        }
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (!bound[i].empty()) continue;
            RackObj ro;
            ro.id = P.freshId("ro_");
            ro.name = bindNameFor(entries[i].name);
            ro.kind = entries[i].group ? "group" : "source";
            if (!entries[i].group)
            {
                std::string f;
                double t = 0;
                splitFrameSelector(entries[i].imagePath, f, t);
                ro.media = relativePath(f);
                ro.frame = t;
            }
            ro.node = "cn_" + std::to_string(i);
            P.rackObjs.push_back(ro);
            bound[i] = ro.id;
            markDirty();
        }
        // The .isp's reference frame wins over the one baked into the stored path (R-RACK-3).
        mFrames->clear();
        mStoredPath.clear();
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].group)
                if (const RackObj *ro = P.rackObj(bound[i]))
                {
                    mFrames->set(entries[i].imagePath, ro->frame);
                    mStoredPath[ro->id] = entries[i].imagePath;
                }
        if (mPending) mPending->entryRackObj = bound;
    }

    void InterstellarService::syncRackObjNodes()
    {
        // `node=cn_<i>` = the node's index in the `.cmp` Cosmo just wrote: depth first, groups and
        // RESIDENT images only — Cosmo's save skips an image whose decode failed (D-2).
        int fileIndex = 0;
        std::map<int, int> indexOfNode;
        for (const auto &n : mRack.model().nodes)
            if (n.group || (!n.failed && !n.pending)) indexOfNode[n.node] = fileIndex++;
        for (auto &ro : mProject->rackObjs)
        {
            const int node = cosmoNodeOf(ro.id);
            const auto it = indexOfNode.find(node);
            const std::string want = it == indexOfNode.end() ? std::string() : "cn_" + std::to_string(it->second);
            if (ro.node != want) ro.node = want;
        }
    }

    bool InterstellarService::rackSaveBlocked(std::string &why) const
    {
        if (!mRack.isOpen()) return false;
        std::vector<std::string> offline;
        for (const auto &n : mRack.model().nodes)
            if (!n.group && n.failed)
            {
                const RackObj *ro = mProject->rackObj(rackObjOfCosmo(n.node));
                offline.push_back(ro ? ro->name : n.name);
            }
        if (offline.empty()) return false;
        why = "the rack has offline source(s) — " + joinNames(offline) +
              " — and Cosmo's save would delete them and their grades (D-2, Cosmo D-66). Relink the "
              "file(s), then retry";
        return true;
    }

    bool InterstellarService::rackCommand(const Command &c)
    {
        Project &P = *mProject;
        std::string err;
        auto rackObjRef = [&](const std::string &ref, RackObj *&out) {
            out = P.rackObj(P.idForRef(ref));
            if (out) return true;
            const auto near = nearest(ref, P.bindNames());
            return fail("no rack node named " + ref + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
        };
        if (mPending && c.kind != CK::RackSelect && c.kind != CK::RackRename)
            return fail("the rack is still loading — `wait rack.loaded` first");
        // Cosmo saves on every add (and `rack frame` saves to reload) — refuse before it can
        // delete an offline source (D-2).
        std::string blocked;
        if ((c.kind == CK::RackAdd || c.kind == CK::RackDuplicate) && rackSaveBlocked(blocked))
            return fail(std::string(specFor(c.kind)->verb) + ": " + blocked);

        switch (c.kind)
        {
            case CK::RackImport:
            {
                const std::string cmp = fs::absolute(c.arg(0)).lexically_normal().string();
                ColourTree entries;
                if (!colourTreeFromCmp(cmp, entries, err)) return fail("rack import: " + err);
                P.hasRack = true;
                if (P.rack.id.empty()) P.rack.id = "rk_1";
                P.rack.path = relativePath(cmp);
                mPending.reset(new PendingRack());
                mPending->kind = PendingRack::Kind::Reload;
                bindFromCmp(entries);
                if (!mRack.beginOpen(cmp, err)) { mPending.reset(); return fail("rack import: " + err); }
                markDirty();
                return true;
            }
            case CK::RackAdd:
            {
                if (!mRack.isOpen()) return fail("rack add: the project has no rack — `rack import <path.cmp>`");
                std::vector<std::string> stored;
                std::vector<NodeId> made;
                for (const auto &a : c.args)
                {
                    std::string file;
                    double t = 0;
                    splitFrameSelector(a, file, t);
                    const std::string abs = fs::absolute(file).lexically_normal().string();
                    if (!fs::exists(abs)) return fail("rack add: no such file: " + file);
                    stored.push_back(joinFrameSelector(abs, t));
                    RackObj ro;
                    ro.id = P.freshId("ro_");
                    ro.name = bindNameFor(abs);
                    ro.kind = "source";
                    ro.media = relativePath(abs);
                    ro.frame = t;
                    P.rackObjs.push_back(ro);   // before the next freshName, so names stay unique
                    made.push_back(ro.id);
                    mFrames->set(stored.back(), t);
                    mStoredPath[ro.id] = stored.back();
                }
                mPending.reset(new PendingRack());
                mPending->kind = PendingRack::Kind::Add;
                mPending->addedPaths = stored;
                mPending->addedRackObjs = made;
                if (!mRack.beginAdd(stored, err))
                {
                    mPending.reset();
                    for (const auto &id : made)
                        P.rackObjs.erase(std::remove_if(P.rackObjs.begin(), P.rackObjs.end(),
                                                        [&](const RackObj &r) { return r.id == id; }),
                                         P.rackObjs.end());
                    return fail("rack add: " + err);
                }
                markDirty();
                return true;
            }
            case CK::RackGroupNew:
            {
                std::string why;
                const std::string typed = c.arg(0);
                const std::string bind = typed.empty() ? std::string("group") : typed;
                std::vector<int> members;
                std::string list = c.flag("nodes");
                // No --nodes: the selection (Shift/Ctrl-clicked), else the Grade target — Cosmo's
                // "Group Selection".
                if (list.empty())
                    for (const auto &id : mSelection)
                        if (const RackObj *r = P.rackObj(id)) list += (list.empty() ? "" : ",") + r->name;
                if (list.empty() && mRack.selectedNode() >= 0) list = rackObjOfCosmo(mRack.selectedNode());
                std::stringstream ss(list);
                std::string one;
                while (std::getline(ss, one, ','))
                {
                    RackObj *ro = nullptr;
                    if (!rackObjRef(one, ro)) return false;
                    const int node = cosmoNodeOf(ro->id);
                    if (node < 0) return fail("rack group new: " + ro->name + " is offline");
                    members.push_back(node);
                }
                if (members.empty()) return fail("rack group new: name the members with --nodes a,b");
                int group = -1;
                if (!mRack.groupNodes(typed.empty() ? std::string("Group") : typed, members, group, err))
                    return fail("rack group new: " + err);
                RackObj ro;
                ro.id = P.freshId("ro_");
                ro.name = bindNameFor(bind);
                ro.kind = "group";
                P.rackObjs.push_back(ro);
                mNodeOf[ro.id] = group;
                mSelection = {ro.id};
                mAnchor = ro.id;
                markDirty();
                bumpFrame();
                emit(Event(EK::RackChanged).with("what", "grouped").with("node", ro.id));
                return true;
            }
            case CK::RackDuplicate:
            {
                RackObj *src = nullptr;
                if (!rackObjRef(c.arg(0), src)) return false;
                if (src->kind == "group") return fail("rack duplicate: " + src->name + " is a group — duplicate a source");
                const int node = cosmoNodeOf(src->id);
                EditParams own;
                if (node < 0 || !mRack.ownParams(node, own)) return fail("rack duplicate: " + src->name + " is offline");
                const RackObj copyOfSrc = *src;
                const std::string stored = joinFrameSelector(resolvePath(copyOfSrc.media), copyOfSrc.frame);
                std::vector<int> added;
                if (!mRack.addSources({stored}, err, &added) || added.size() != 1) return fail("rack duplicate: " + err);
                // Every key of the source's own grade in ONE set — one history step in Cosmo.
                std::vector<std::pair<std::string, std::string>> fields;
                std::istringstream in(serializeParams(own));
                std::string line;
                while (std::getline(in, line))
                {
                    const auto eq = line.find('=');
                    if (eq != std::string::npos) fields.emplace_back(line.substr(0, eq), line.substr(eq + 1));
                }
                if (!mRack.setParams(added[0], fields, err)) return fail("rack duplicate: " + err);
                RackObj ro = copyOfSrc;
                ro.id = P.freshId("ro_");
                ro.name = c.has("name") ? c.flag("name") : P.freshName(copyOfSrc.name + "_v");
                ro.node.clear();
                ro.unknown.clear();
                ro.notes = Notes{};
                std::string why;
                if (!Project::nameIsLegal(ro.name, why)) return fail("rack duplicate: " + why);
                if (P.nameIsTaken(ro.name)) return fail("rack duplicate: the name " + ro.name + " is taken");
                P.rackObjs.push_back(ro);
                mNodeOf[ro.id] = added[0];
                mStoredPath[ro.id] = stored;
                // its own copy of the original's plugin stack, each with a fresh id (R-FX-5)
                {
                    std::vector<Effect> copies;
                    for (const auto &fx : P.imageEffects)
                        if (fx.node == copyOfSrc.id) copies.push_back(fx);
                    for (auto &fx : copies)
                    {
                        const NodeId was = fx.id;
                        fx.id = P.freshId("ef_");
                        fx.node = ro.id;
                        fx.notes = Notes{};
                        P.imageEffects.push_back(fx);
                        copyAnims(was, fx.id, 0.0);   // and its curves (R-ANIM-1)
                    }
                    copyAnims(copyOfSrc.id, ro.id, 0.0);
                }
                // The new variant is the selection and the Grade target, so it is in view (the
                // strip and the tree follow the target) and the next edit lands on IT.
                mSelection = {ro.id};
                mAnchor = ro.id;
                if (!mRack.select(added[0], err)) return fail("rack duplicate: " + err);
                markDirty();
                bumpFrame();
                emit(Event(EK::RackChanged).with("what", "duplicated").with("node", ro.id));
                return true;
            }
            case CK::RackFrame:
            {
                RackObj *ro = nullptr;
                if (!rackObjRef(c.arg(0), ro)) return false;
                if (!looksLikeVideo(ro->media)) return fail("rack frame: " + ro->name + " is a still — it has one frame");
                double t = 0;
                if (!parseDouble(c.flag("at"), t) || t < 0) return fail("rack frame: --at must be a time in seconds");
                // Land on a frame of the SOURCE (a 30p clip in a 24p project has 30p frames).
                const Source *src = source(*mSync, resolvePath(ro->media));
                ro->frame = snapToFrame(t, src && src->ok && src->info.fps > 0 ? src->info.fps : mProject->fps);
                // Nothing reloads. Every pixel Interstellar shows of a source — the Grade monitor,
                // the filmstrip, a render — comes from its own frame source at this time; Cosmo's
                // slot holds the frame decoded at load and only its own preview reads it. So the
                // selector table learns the new frame (the next rack load decodes it) and no
                // parameter, node or path moves (R-RACK-3). The first build of this saved and
                // re-opened the whole rack — every source re-decoded on each choice.
                const auto stored = mStoredPath.find(ro->id);
                if (stored != mStoredPath.end()) mFrames->set(stored->second, ro->frame);
                markDirty();
                bumpFrame();
                emit(Event(EK::RackChanged).with("what", "reframed").with("node", ro->id));
                return true;
            }
            case CK::RackRename:
            {
                RackObj *ro = nullptr;
                if (!rackObjRef(c.arg(0), ro)) return false;
                const std::string id = ro->id;
                if (!P.rename(id, c.arg(1), err)) return fail("rack rename: " + err);
                markDirty();
                emit(Event(EK::RackChanged).with("what", "renamed").with("node", id));
                return true;
            }
            case CK::RackSelect:
            {
                RackObj *ro = nullptr;
                if (!rackObjRef(c.arg(0), ro)) return false;
                const int node = cosmoNodeOf(ro->id);
                if (node < 0) return fail("rack select: " + ro->name + " is offline");
                const NodeId id = ro->id;
                bool target = true;
                if (c.has("range") && !mAnchor.empty())
                {
                    // Shift-click: every row from the anchor to here, in the order the tree shows.
                    int a = -1, b = -1;
                    for (size_t i = 0; i < mModel.rack.size(); ++i)
                    {
                        if (mModel.rack[i].rackObj == mAnchor) a = (int)i;
                        if (mModel.rack[i].rackObj == id) b = (int)i;
                    }
                    if (a < 0 || b < 0) mSelection = {id};
                    else
                    {
                        mSelection.clear();
                        for (int i = std::min(a, b); i <= std::max(a, b); ++i)
                            if (!mModel.rack[(size_t)i].rackObj.empty() && !mModel.rack[(size_t)i].failed)
                                mSelection.push_back(mModel.rack[(size_t)i].rackObj);
                    }
                }
                else if (c.has("add"))
                {
                    // Ctrl-click: toggle. Taking a node OUT leaves the Grade target where it was.
                    const auto it = std::find(mSelection.begin(), mSelection.end(), id);
                    if (it != mSelection.end()) { mSelection.erase(it); target = false; }
                    else mSelection.push_back(id);
                    mAnchor = id;
                }
                else
                {
                    mSelection = {id};
                    mAnchor = id;
                }
                if (target && !mRack.select(node, err)) return fail("rack select: " + err);
                // Open the source (no decode) so its length reaches the model — the reference-frame
                // slider spans the WHOLE source (R-RACK-3).
                if (target && !ro->media.empty() && looksLikeVideo(ro->media)) source(*mSync, resolvePath(ro->media));
                std::string names;
                for (const auto &s2 : mSelection)
                    if (const RackObj *r = P.rackObj(s2)) names += (names.empty() ? "" : ",") + r->name;
                emit(Event(EK::SelectionChanged).with("rack", names).with("clip", mSelectedClip));
                bumpFrame();
                return true;
            }
            case CK::RackRemove:
            {
                RackObj *ro = nullptr;
                if (!rackObjRef(c.arg(0), ro)) return false;
                if (ro->kind == "group") return fail("rack remove: " + ro->name + " is a group — `rack ungroup` it");
                std::vector<std::string> users;
                for (const auto &cl : P.clips)
                    if (cl.src == ro->id) users.push_back(cl.name);
                if (!users.empty()) return fail("rack remove: " + ro->name + " is used by " + joinNames(users) + " — delete those clips first");
                const NodeId id = ro->id;
                const int node = cosmoNodeOf(id);
                if (node >= 0 && !mRack.removeNode(node, err)) return fail("rack remove: " + err);
                mNodeOf.erase(id);
                mStoredPath.erase(id);
                mSelection.erase(std::remove(mSelection.begin(), mSelection.end(), id), mSelection.end());
                P.grades.erase(std::remove_if(P.grades.begin(), P.grades.end(), [&](const TlGrade &g) { return g.node == id; }), P.grades.end());
                P.effects.erase(std::remove_if(P.effects.begin(), P.effects.end(), [&](const Fx &f) { return f.node == id; }), P.effects.end());
                P.imageEffects.erase(std::remove_if(P.imageEffects.begin(), P.imageEffects.end(), [&](const Effect &f) { return f.node == id; }),
                                     P.imageEffects.end());
                P.rackObjs.erase(std::remove_if(P.rackObjs.begin(), P.rackObjs.end(), [&](const RackObj &r) { return r.id == id; }), P.rackObjs.end());
                markDirty();
                bumpFrame();
                emit(Event(EK::RackChanged).with("what", "removed").with("node", id));
                return true;
            }
            default: return fail("not a rack command");
        }
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // colour: the source (live rack or pin), the version's overrides, the fold
    // ──────────────────────────────────────────────────────────────────────────────────────────

    std::string InterstellarService::pinsDir() const
    {
        const fs::path p(mIspPath);
        return (p.parent_path() / (p.stem().string() + ".pins")).string();
    }

    bool InterstellarService::colourTreeFor(const NodeId &tl, ColourTree &tree, std::map<NodeId, int> &indexOf,
                                            std::string &source, std::string &err)
    {
        indexOf.clear();
        for (const NodeId &t : mProject->chain(tl))
        {
            const Timeline *x = mProject->timeline(t);
            if (x && x->pinned())
            {
                source = "pin@" + x->pinCommit();
                return pinSnapshot(x->pinCommit(), tree, indexOf, err);
            }
        }
        source = "rack";
        if (!mRack.isOpen()) { err = "the project has no rack"; return false; }
        if (!mRack.colourTree(tree)) { err = "the rack's params are not read yet"; return false; }
        for (const auto &kv : mNodeOf)
        {
            const int i = mRack.indexOf(kv.second);
            if (i >= 0) indexOf[kv.first] = i;
        }
        return true;
    }

    bool InterstellarService::pinSnapshot(const std::string &commit, ColourTree &tree, std::map<NodeId, int> &indexOf,
                                          std::string &err)
    {
        auto it = mPins.find(commit);
        if (it == mPins.end())
        {
            const std::string base = pinsDir() + "/" + commit;
            std::pair<ColourTree, std::map<NodeId, int>> snap;
            if (!colourTreeFromCmp(base + ".cmp", snap.first, err))
            {
                err = "the pin " + commit + " has no snapshot (" + base + ".cmp)";
                return false;
            }
            std::ifstream f(base + ".map");
            std::string ro;
            int idx = -1;
            while (f >> ro >> idx) snap.second[ro] = idx;
            it = mPins.emplace(commit, std::move(snap)).first;
        }
        tree = it->second.first;
        indexOf = it->second.second;
        return true;
    }

    bool InterstellarService::takePin(const NodeId &tl, std::string &commit, std::string &err)
    {
        (void)tl;
        if (!mRack.isOpen()) { err = "the project has no rack to pin"; return false; }
        if (rackSaveBlocked(err)) return false;   // a pin saves the rack first (D-2)
        if (!mRack.save(err)) return false;
        syncRackObjNodes();
        std::string bytes;
        if (!readFile(mRack.path(), bytes)) { err = "cannot read " + mRack.path(); return false; }
        const std::string curves = colourCurveText();   // a pin freezes the rack's curves too (R-ANIM-5, R-VER-3)
        commit = hex12(render::fnv1a64(curves.empty() ? bytes : bytes + "\n#curves\n" + curves));
        std::error_code ec;
        fs::create_directories(pinsDir(), ec);
        const std::string base = pinsDir() + "/" + commit;
        if (!fs::exists(base + ".cmp"))
        {
            std::ofstream f(base + ".cmp", std::ios::binary);
            f << bytes;
            if (!f) { err = "cannot write the pin snapshot " + base + ".cmp"; return false; }
        }
        if (!curves.empty() && !fs::exists(base + ".anim"))
        {
            std::ofstream f(base + ".anim");
            f << curves;
            if (!f) { err = "cannot write the pin's curves " + base + ".anim"; return false; }
        }
        // Which #rackobj is which snapshot entry — Interstellar's data, never colour.
        std::ofstream m(base + ".map", std::ios::trunc);
        for (const auto &ro : mProject->rackObjs)
            if (ro.node.rfind("cn_", 0) == 0) m << ro.id << ' ' << ro.node.substr(3) << '\n';
        mPins.erase(commit);
        return true;
    }

    bool InterstellarService::gradeFor(const NodeId &tl, const NodeId &rackObj, EditParams &out, std::string &err, double srcT)
    {
        ColourTree tree;
        std::map<NodeId, int> idx;
        std::string source;
        if (!colourTreeFor(tl, tree, idx, source, err)) return false;
        const auto it = idx.find(rackObj);
        if (it == idx.end()) { err = "rack node " + rackObj + " is not in the " + source; return false; }
        // R-ANIM: each node's own value at this source time, then the version's deltas on top
        if (srcT >= 0) applyColourCurves(tl, tree, idx, srcT);
        // Overrides go on each node's OWN params, so a group override stacks onto its members.
        for (const auto &kv : idx) applyDeltas(tree[(size_t)kv.second].own, gradeDeltas(*mProject, tl, kv.first));
        out = foldRender(tree, it->second);
        return true;
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // addresses: set / get / eval / revert
    // ──────────────────────────────────────────────────────────────────────────────────────────

    namespace
    {
        struct Address
        {
            std::string name;     // first segment
            std::string rest;     // everything after the first dot
        };

        Address splitAddress(std::string a)
        {
            for (const char *prefix : {"rack:", "clip:", "track:", "fx:", "aclip:"})
                if (a.rfind(prefix, 0) == 0) a = a.substr(std::strlen(prefix));
            Address out;
            const auto dot = a.find('.');
            out.name = a.substr(0, dot);
            out.rest = dot == std::string::npos ? std::string() : a.substr(dot + 1);
            return out;
        }

        std::vector<std::string> fieldsOf(ParamOwner o)
        {
            std::vector<std::string> v;
            for (const auto &d : paramDefs())
                if (d.owner == o) v.push_back(d.key);
            return v;
        }

        std::string clipField(const Clip &c, const std::string &k)
        {
            if (k == "at") return canonicalNumber(c.at);
            if (k == "in") return canonicalNumber(c.in);
            if (k == "out") return canonicalNumber(c.out);
            if (k == "speed") return canonicalNumber(c.speed);
            if (k == "opacity") return canonicalNumber(c.opacity);
            if (k == "blend") return c.blend;
            if (k == "fit") return c.fit;
            if (k == "name") return c.name;
            if (k == "geom.x") return canonicalNumber(c.geom.x);
            if (k == "geom.y") return canonicalNumber(c.geom.y);
            if (k == "geom.scale") return canonicalNumber(c.geom.scale);
            if (k == "geom.rotation") return canonicalNumber(c.geom.rotation);
            if (k == "geom.anchor.x") return canonicalNumber(c.geom.anchorX);
            if (k == "geom.anchor.y") return canonicalNumber(c.geom.anchorY);
            if (k == "geom.crop.x") return canonicalNumber(c.geom.cropX);
            if (k == "geom.crop.y") return canonicalNumber(c.geom.cropY);
            if (k == "geom.crop.w") return canonicalNumber(c.geom.cropW);
            if (k == "geom.crop.h") return canonicalNumber(c.geom.cropH);
            return std::string();
        }

        std::string trackField(const Track &t, const std::string &k)
        {
            if (k == "opacity") return canonicalNumber(t.opacity);
            if (k == "blend") return t.blend;
            if (k == "mute") return t.mute ? "1" : "0";
            if (k == "gain") return canonicalNumber(t.gain);
            if (k == "name") return t.name;
            return std::string();
        }
    }

    bool InterstellarService::setAddress(const std::string &address, const std::string &value)
    {
        Project &P = *mProject;
        const Address a = splitAddress(address);
        std::string err;
        const NodeId tl = currentTimeline();

        if (a.name == "project")
        {
            const ParamDef *d = ownerField(ParamOwner::Project, a.rest);
            if (!d) return fail("project has no field `" + a.rest + "` (it has: " + joinNames(fieldsOf(ParamOwner::Project)) + ")");
            if (a.rest == "name") P.name = value;
            else
            {
                double v = 0;
                if (!parseDouble(value, v)) return fail("project." + a.rest + " needs a number, got `" + value + "`");
                P.masterGain = v;
            }
            markDirty();
            emit(Event(EK::ParamsChanged).with("address", address).with("value", value).with("target", "project"));
            return true;
        }

        const NodeId id = P.idForRef(a.name);
        const NodeKind kind = P.kindOf(id);
        if (kind == NodeKind::None)
        {
            const auto near = nearest(a.name, P.bindNames());
            return fail("no node named `" + a.name + "`" + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
        }

        {
            // R-ANIM-3: a parameter with a curve is keyed at the current time, not set
            bool handled = false;
            if (!setAnimated(address, value, handled)) return false;
            if (handled) return true;
        }

        if (kind == NodeKind::RackObj)
        {
            RackObj *ro = P.rackObj(id);
            const auto dot = a.rest.find('.');
            if (dot != std::string::npos)
            {
                // ── a COLOUR address: <bind>.<filter>.<key> ──
                const std::string filter = a.rest.substr(0, dot), key = a.rest.substr(dot + 1);
                const ParamDef *d = cosmoKey(key);
                if (!d)
                {
                    std::vector<std::string> keys;
                    for (const auto &p : paramDefs())
                        if (p.owner == ParamOwner::Cosmo && p.filter == filter) keys.push_back(p.key);
                    const auto near = nearest(key, keys.empty() ? fieldsOf(ParamOwner::Cosmo) : keys);
                    return fail("`" + key + "` is not a colour key" + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
                }
                if (d->filter != filter)
                    return fail("`" + key + "` is under " + d->filter + ", not " + filter + ": " + ro->name + "." + d->filter + "." + key);
                const Timeline *t = P.timeline(tl);
                if (t && t->pinned())
                    return fail("timeline " + t->name + " is pinned at " + t->pinCommit() +
                                " — its colour is read-only (R-VER-3); unpin it, or edit its base");
                const int node = cosmoNodeOf(ro->id);

                if (!t || t->base.empty())
                {
                    // A root timeline: write THROUGH to the hosted Cosmo project (R-RACK-2).
                    if (node < 0) return fail(ro->name + " is offline — its grade cannot be edited until it is found");
                    if (!mRack.setParam(node, key, value, err)) return fail(ro->name + "." + filter + "." + key + ": " + err);
                    markDirty();
                    bumpFrame();
                    emit(Event(EK::ParamsChanged).with("address", ro->name + "." + filter + "." + key).with("value", value).with("target", "rack"));
                    return true;
                }
                // A derived version: the value becomes this version's override — a delta on the
                // colour source's own value, so a later base edit still arrives (R-VER-2).
                if (d->kind != ParamKind::Scalar && d->kind != ParamKind::Int)
                {
                    const Timeline *base = P.timeline(t->base);
                    return fail("a version's colour override is a number added to the rack's value, and `" + key + "` is a " +
                                kindName(d->kind) + " — edit it on " + (base ? base->name : t->base) +
                                ", or `rack duplicate " + ro->name + "` for a second look (R-RACK-5)");
                }
                double v = 0;
                if (!parseDouble(value, v)) return fail(key + " needs a number, got `" + value + "`");
                ColourTree tree;
                std::map<NodeId, int> idx;
                std::string source;
                if (!colourTreeFor(tl, tree, idx, source, err)) return fail(err);
                const auto it = idx.find(ro->id);
                if (it == idx.end()) return fail(ro->name + " is not in the " + source);
                double baseValue = 0;
                paramScalar(tree[(size_t)it->second].own, key, baseValue);
                // an animated key: the delta is against the curve where Grade stands (R-ANIM-5)
                if (!pinCurvesFor(tl)) baseValue = curveAt(ro->id, filter + "." + key, sourceNow(ro->id), baseValue);
                // EditParams are floats: a delta finer than float precision is noise, and noise in
                // a committed file is a diff nobody made (0.8 - 0.5 = 0.30000000000000004).
                double delta = v - baseValue;
                {
                    char buf[32];
                    std::snprintf(buf, sizeof buf, "%.7g", delta);
                    delta = std::strtod(buf, nullptr);
                }
                const bool ok = std::fabs(delta) < 1e-9 ? clearGrade(P, tl, ro->id, key, err)
                                                        : setGrade(P, tl, ro->id, key, delta, err);
                if (!ok) return fail(err);
                markDirty();
                bumpFrame();
                emit(Event(EK::ParamsChanged).with("address", ro->name + "." + filter + "." + key).with("value", value).with("target", "version"));
                return true;
            }
            // ── Interstellar's own fields on a rack node ──
            if (a.rest == "weight")
            {
                double v = 0;
                if (!parseDouble(value, v) || v < 0 || v > 1) return fail(ro->name + ".weight is 0..1, got `" + value + "`");
                ro->weight = v;
            }
            else if (a.rest == "bypass")
            {
                if (value != "0" && value != "1") return fail(ro->name + ".bypass is 0 or 1");
                const int node = cosmoNodeOf(ro->id);
                if (node < 0) return fail(ro->name + " is offline");
                if (!mRack.setBypass(node, value == "1", err)) return fail(err);
            }
            else if (a.rest == "input")
            {
                // R-COLOR-2: what the media is — a source's interpretation, not a grade (law 1 holds)
                if (ro->kind == "group") return fail(ro->name + " is a group — an input transform belongs to a source");
                if (!render::colour::known(render::colour::inputs(), value))
                {
                    std::string ids;
                    for (const auto &d : render::colour::inputs()) ids += std::string(ids.empty() ? "" : ", ") + d.id;
                    return fail(ro->name + ".input is one of " + ids + ", got `" + value + "`");
                }
                ro->input = value;
            }
            else if (a.rest == "lut")
            {
                // R-COLOR-5: refused unless the file reads — a half-read LUT is a wrong picture
                if (ro->kind == "group") return fail(ro->name + " is a group — an input LUT belongs to a source");
                if (value.empty() || value == "none") ro->lut.clear();
                else
                {
                    std::string why;
                    if (!loadLut(resolvePath(value), why)) return fail(ro->name + ".lut: " + why);
                    ro->lut = value;
                }
            }
            else if (a.rest == "frame")
            {
                Command c;
                c.kind = CK::RackFrame;
                c.args = {ro->name};
                c.flags = {{"at", value}};
                return rackCommand(c);
            }
            else
            {
                std::vector<std::string> f = fieldsOf(ParamOwner::RackObj);
                return fail(ro->name + " has no field `" + a.rest + "` (rack nodes have: " + joinNames(f) +
                            ", and colour keys as <filter>.<key>)");
            }
            markDirty();
            bumpFrame();
            emit(Event(EK::ParamsChanged).with("address", address).with("value", value).with("target", "rackobj"));
            return true;
        }

        if (kind == NodeKind::Effect)
        {
            // ── a plugin's own fields (R-FX-5): on/off, mix, and its type's parameters ──
            Effect *e = P.effect(id);
            const render::EffectTypeDef *def = render::effectType(e->type);
            double v = 0;
            if (a.rest == "enabled")
            {
                if (value != "0" && value != "1") return fail(e->id + ".enabled is 0 or 1");
                e->enabled = value == "1";
            }
            else if (a.rest == "mix")
            {
                if (!parseDouble(value, v) || v < 0 || v > 1) return fail(e->id + ".mix is 0..1, got `" + value + "`");
                e->mix = v;
            }
            else if (def && std::find(def->files.begin(), def->files.end(), a.rest) != def->files.end())
            {
                // a file the plugin reads (a LUT, R-COLOR-5): refused unless it reads
                const std::string stored = value == "none" ? std::string() : value;
                if (!stored.empty())
                {
                    std::string why;
                    if (!loadLut(resolvePath(stored), why)) return fail(address + ": " + why);
                }
                bool found = false;
                for (auto &kv : e->unknown)
                    if (kv.first == a.rest) { kv.second = stored; found = true; }
                if (!found) e->unknown.emplace_back(a.rest, stored);
            }
            else
            {
                const render::EffectParamDef *pd = nullptr;
                if (def) for (const auto &d : def->params) if (d.key == a.rest) pd = &d;
                if (!pd)
                {
                    std::vector<std::string> keys{"enabled", "mix"};
                    if (def) for (const auto &d : def->params) keys.push_back(d.key);
                    if (def) for (const auto &k : def->files) keys.push_back(k);
                    return fail(e->id + " (" + e->type + ") has no parameter `" + a.rest + "` (it has: " + joinNames(keys) + ")");
                }
                if (!parseDouble(value, v)) return fail(address + " needs a number, got `" + value + "`");
                if (v < pd->min || v > pd->max)
                    return fail(address + " is " + canonicalNumber(pd->min) + ".." + canonicalNumber(pd->max) + (pd->unit.empty() ? "" : " " + pd->unit) + ", got " + value);
                bool found = false;
                for (auto &kv : e->unknown)
                    if (kv.first == a.rest) { kv.second = canonicalNumber(v); found = true; }
                if (!found) e->unknown.emplace_back(a.rest, canonicalNumber(v));
            }
            markDirty();
            bumpFrame();
            emit(Event(EK::ParamsChanged).with("address", e->id + "." + a.rest).with("value", value).with("target", "effect"));
            return true;
        }

        if (kind == NodeKind::Clip || kind == NodeKind::Track || kind == NodeKind::AClip || kind == NodeKind::ATrack)
        {
            const ParamOwner owner = kind == NodeKind::Clip ? ParamOwner::Clip : kind == NodeKind::Track ? ParamOwner::Track
                                   : kind == NodeKind::ATrack ? ParamOwner::AudioTrack : ParamOwner::AudioClip;
            const ParamDef *d = ownerField(owner, a.rest);
            if (!d)
            {
                const auto near = nearest(a.rest, fieldsOf(owner));
                return fail(a.name + " has no field `" + a.rest + "`" + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
            }
            if (a.rest == "name")
            {
                if (!P.rename(id, value, err)) return fail(err);
            }
            else
            {
                if (d->kind != ParamKind::Text)
                {
                    double v = 0;
                    if (!parseDouble(value, v)) return fail(address + " needs a number, got `" + value + "`");
                }
                std::string key = a.rest;
                if (owner == ParamOwner::AudioClip && key == "fade")
                {
                    Fields kv = {{"fadeIn", value}, {"fadeOut", value}};
                    if (!setFields(P, tl, id, kv, err)) return fail(err);
                }
                else if (!setField(P, tl, id, key, value, err))
                    return fail(err);
            }
            markDirty();
            bumpFrame();
            emit(Event(EK::ParamsChanged).with("address", address).with("value", value).with("target", ownerName(owner)));
            return true;
        }

        if (kind == NodeKind::Fx)
        {
            Fx *fx = P.fx(id);
            double v = 0;
            if (!parseDouble(value, v)) return fail(address + " needs a number");
            if (a.rest == "radius")
            {
                if (v < 0 || v > 16) return fail("radius is 0..16 frames");
                fx->radius = (int)v;
            }
            else if (a.rest == "strength") fx->strength = std::clamp(v, 0.0, 1.0);
            else if (a.rest == "shutter") fx->shutter = std::clamp(v, 0.0, 360.0);
            else if (a.rest == "at") fx->at = std::max(0.0, v);
            else return fail(a.name + " has no field `" + a.rest + "` (fx have: " + joinNames(fieldsOf(ParamOwner::Fx)) + ")");
            markDirty();
            bumpFrame();
            emit(Event(EK::ParamsChanged).with("address", address).with("value", value).with("target", "fx"));
            return true;
        }
        return fail("`" + a.name + "` is a " + nodeKindName(kind) + ", which has no settable fields — see `api` §Addresses");
    }

    bool InterstellarService::getAddress(const std::string &address, bool evaluate, const NodeId &tl, bool explain)
    {
        Project &P = *mProject;
        const Address a = splitAddress(address);
        std::string err;
        std::ostringstream out;

        if (a.name == "project")
        {
            if (a.rest == "masterGain") out << address << '=' << canonicalNumber(P.masterGain) << '\n';
            else if (a.rest == "name") out << address << '=' << quoteIfNeeded(P.name) << '\n';
            else return fail("project has no field `" + a.rest + "`");
            mOutput = out.str();
            return true;
        }
        const NodeId id = P.idForRef(a.name);
        const NodeKind kind = P.kindOf(id);
        if (kind == NodeKind::Effect)
        {
            const Effect *e = P.effect(id);
            const render::EffectTypeDef *def = render::effectType(e->type);
            std::string v;
            if (a.rest == "enabled") v = e->enabled ? "1" : "0";
            else if (a.rest == "mix") v = canonicalNumber(e->mix);
            else if (a.rest == "type") v = e->type;
            else
            {
                const render::EffectParamDef *pd = nullptr;
                if (def) for (const auto &d : def->params) if (d.key == a.rest) pd = &d;
                for (const auto &kv : e->unknown) if (kv.first == a.rest) v = kv.second;
                if (v.empty() && pd) v = canonicalNumber(pd->def);
                if (v.empty() && def && std::find(def->files.begin(), def->files.end(), a.rest) != def->files.end()) v = "none";
                if (v.empty()) return fail(e->id + " (" + e->type + ") has no parameter `" + a.rest + "`");
            }
            if (P.animOf(e->id, a.rest)) v = canonicalNumber(curveAt(e->id, a.rest, sourceNow(e->node), 0.0));   // R-ANIM: now
            mOutput = e->id + "." + a.rest + "=" + v + "\n";
            return true;
        }
        if (kind == NodeKind::RackObj)
        {
            const RackObj *ro = P.rackObj(id);
            const auto dot = a.rest.find('.');
            if (dot == std::string::npos)
            {
                if (a.rest == "weight") out << address << '=' << canonicalNumber(ro->weight) << '\n';
                else if (a.rest == "frame") out << address << '=' << canonicalTime(ro->frame) << '\n';
                else if (a.rest == "input") out << address << '=' << (ro->input.empty() ? std::string("rec709") : ro->input) << '\n';
                else if (a.rest == "lut") out << address << '=' << (ro->lut.empty() ? std::string("none") : ro->lut) << '\n';
                else if (a.rest == "bypass")
                {
                    bool b = false;
                    for (const auto &n : mRack.model().nodes)
                        if (n.node == cosmoNodeOf(ro->id)) b = n.bypass;
                    out << address << '=' << (b ? 1 : 0) << '\n';
                }
                else return fail(ro->name + " has no field `" + a.rest + "`");
                mOutput = out.str();
                return true;
            }
            const std::string filter = a.rest.substr(0, dot), key = a.rest.substr(dot + 1);
            const ParamDef *d = cosmoKey(key);
            if (!d || d->filter != filter) return fail("`" + a.rest + "` is not a colour address (see `api` §Addresses)");
            ColourTree tree;
            std::map<NodeId, int> idx;
            std::string source;
            if (!colourTreeFor(tl, tree, idx, source, err)) return fail(err);
            const auto it = idx.find(ro->id);
            if (it == idx.end()) return fail(ro->name + " is not in the " + source + " (offline?)");
            applyColourCurves(tl, tree, idx, sourceNow(ro->id));   // an animated key reads where Grade stands
            std::string sourceOwn;
            paramText(tree[(size_t)it->second].own, key, sourceOwn);
            const auto deltas = gradeDeltas(P, tl, ro->id);
            double delta = 0;
            bool hasDelta = false;
            for (const auto &dd : deltas)
                if (dd.first == key) { delta = dd.second; hasDelta = true; }
            for (const auto &kv : idx) applyDeltas(tree[(size_t)kv.second].own, gradeDeltas(P, tl, kv.first));
            std::string value;
            if (!evaluate) paramText(tree[(size_t)it->second].own, key, value);
            else paramText(foldRender(tree, it->second), key, value);
            out << ro->name << '.' << filter << '.' << key << '=' << value << '\n';
            if (explain)
            {
                const Timeline *t = P.timeline(tl);
                out << "  timeline   " << (t ? t->name : tl) << '\n';
                out << "  source     " << source << "  own " << sourceOwn << '\n';
                if (const Anim *an = P.animOf(ro->id, filter + "." + key))
                    out << "  animated   " << P.keysOf(an->id).size() << " keys; at source time " << canonicalNumber(sourceNow(ro->id)) << '\n';
                if (hasDelta)
                {
                    NodeId from;
                    for (const NodeId &x : P.chain(tl))
                        if (from.empty())
                            if (const TlGrade *g = P.tlgrade(x, ro->id))
                                for (const auto &dd : g->deltas)
                                    if (dd.first == key) from = x;
                    const Timeline *ft = P.timeline(from);
                    out << "  override   " << (delta >= 0 ? "+" : "") << canonicalNumber(delta) << "  from "
                        << (ft ? ft->name : from) << '\n';
                }
                for (int p = tree[(size_t)it->second].parent; p >= 0; p = tree[(size_t)p].parent)
                {
                    std::string gv;
                    paramText(tree[(size_t)p].own, key, gv);
                    NodeId gro;
                    for (const auto &kv : idx)
                        if (kv.second == p) gro = kv.first;
                    const RackObj *g = P.rackObj(gro);
                    out << "  group      " << (g ? g->name : tree[(size_t)p].name) << "  own " << gv
                        << (tree[(size_t)p].bypass ? "  (bypassed — contributes nothing)" : "") << '\n';
                }
                if (tree[(size_t)it->second].bypass) out << "  bypassed   the node's own grade contributes nothing\n";
                out << "  effective  " << value << '\n';
            }
            mOutput = out.str();
            return true;
        }
        if (kind == NodeKind::Clip || kind == NodeKind::Track || kind == NodeKind::ATrack)
        {
            ResolvedTimeline R;
            if (!resolved(tl, R, err)) return fail(err);
            std::string value;
            bool found = false;
            if (kind == NodeKind::ATrack)
                for (const auto &t : R.audioTracks)
                    if (t.id == id)
                    {
                        found = true;
                        if (a.rest == "gain") value = canonicalNumber(t.gain);
                        else if (a.rest == "pan") value = canonicalNumber(t.pan);
                        else if (a.rest == "mute") value = t.mute ? "1" : "0";
                        else if (a.rest == "solo") value = t.solo ? "1" : "0";
                        else if (a.rest == "name") value = t.name;
                    }
            if (kind == NodeKind::Clip)
                for (const auto &c : R.clips)
                    if (c.id == id)
                    {
                        value = clipField(c, a.rest);
                        found = true;
                        if (P.animOf(id, a.rest))   // R-ANIM: the curve at the playhead, on the clip's own clock
                            value = canonicalNumber(curveAt(id, a.rest, std::clamp(c.in + (mModel.playhead - c.at) * c.speed, c.in, c.out), 0.0));
                    }
            if (kind == NodeKind::Track)
                for (const auto &t : R.tracks)
                    if (t.id == id) { value = trackField(t, a.rest); found = true; }
            if (!found) return fail(a.name + " is not in timeline " + tl);
            if (value.empty() && a.rest != "name") return fail(a.name + " has no field `" + a.rest + "`");
            out << address << '=' << quoteIfNeeded(value) << '\n';
            if (explain)
            {
                const auto it = R.provenance.find(id);
                out << "  provenance " << provenanceName(it == R.provenance.end() ? Provenance::Local : it->second) << '\n';
                if (P.tlset(tl, id)) out << "  this version overrides it (#tlset)\n";
            }
            mOutput = out.str();
            return true;
        }
        if (kind == NodeKind::Fx)
        {
            const Fx *fx = P.fx(id);
            if (a.rest == "radius") out << address << '=' << fx->radius << '\n';
            else if (a.rest == "strength") out << address << '=' << canonicalNumber(fx->strength) << '\n';
            else if (a.rest == "shutter") out << address << '=' << canonicalNumber(fx->shutter) << '\n';
            else if (a.rest == "at") out << address << '=' << canonicalTime(fx->at) << '\n';
            else return fail(a.name + " has no field `" + a.rest + "`");
            mOutput = out.str();
            return true;
        }
        const auto near = nearest(a.name, P.bindNames());
        return fail("no node named `" + a.name + "`" + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
    }

    bool InterstellarService::revertAddress(const std::string &address, const NodeId &tl)
    {
        Project &P = *mProject;
        const Timeline *t = P.timeline(tl);
        if (!t) return fail("no timeline " + tl);
        if (t->base.empty()) return fail("timeline " + t->name + " is a root — there is nothing for it to inherit");
        const Address a = splitAddress(address);
        const NodeId id = P.idForRef(a.name);
        std::string err;
        if (P.kindOf(id) == NodeKind::RackObj)
        {
            std::string key;
            const auto dot = a.rest.find('.');
            if (!a.rest.empty()) key = dot == std::string::npos ? a.rest : a.rest.substr(dot + 1);
            if (!clearGrade(P, tl, id, key, err)) return fail(err);
        }
        else if (TlSet *s = P.tlset(tl, id))
        {
            if (a.rest.empty())
                P.sets.erase(std::remove_if(P.sets.begin(), P.sets.end(),
                                            [&](const TlSet &x) { return x.timeline == tl && x.node == id; }),
                             P.sets.end());
            else
            {
                const std::string k = a.rest;
                s->fields.erase(std::remove_if(s->fields.begin(), s->fields.end(),
                                               [&](const std::pair<std::string, std::string> &f) { return f.first == k; }),
                                s->fields.end());
                if (s->fields.empty())
                    P.sets.erase(std::remove_if(P.sets.begin(), P.sets.end(),
                                                [&](const TlSet &x) { return x.timeline == tl && x.node == id; }),
                                 P.sets.end());
            }
        }
        else if (P.tldrop(tl, id))
            P.drops.erase(std::remove_if(P.drops.begin(), P.drops.end(),
                                         [&](const TlDrop &x) { return x.timeline == tl && x.node == id; }),
                          P.drops.end());
        else
            return fail("timeline " + t->name + " has no override on `" + address + "`");
        markDirty();
        bumpFrame();
        emit(Event(EK::ParamsChanged).with("address", address).with("value", "inherited").with("target", "version"));
        return true;
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // versions
    // ──────────────────────────────────────────────────────────────────────────────────────────

    bool InterstellarService::timelineCommand(const Command &c)
    {
        Project &P = *mProject;
        std::string err;
        if (c.kind == CK::TimelineList)
        {
            std::ostringstream out;
            for (const auto &t : mModel.timelines)
            {
                out << std::string((size_t)t.depth * 2, ' ') << t.name << "  (" << t.id << ")";
                if (t.id == currentTimeline()) out << "  *current";
                if (t.colourPinned) out << "  colour=pin@" << t.pinCommit;
                if (t.cutFrozen) out << "  cut=frozen";
                if (t.overrides) out << "  overrides=" << t.overrides;
                if (t.danglingDeltas) out << "  dangling=" << t.danglingDeltas;
                out << '\n';
            }
            mOutput = out.str();
            return true;
        }
        if (c.kind == CK::TimelineNew)
        {
            NodeId base;
            if (c.has("base") && (base = timelineRef(c.flag("base"))).empty()) return fail("no timeline named " + c.flag("base"));
            NodeId id;
            if (!newTimeline(P, c.arg(0), base, id, err)) return fail("timeline new: " + err);
            markDirty();
            emit(Event(EK::TimelineChanged).with("timeline", id).with("what", "created"));
            return true;
        }
        const NodeId tl = timelineRef(c.arg(0));
        if (tl.empty())
        {
            std::vector<std::string> names;
            for (const auto &t : P.timelines) names.push_back(t.name);
            const auto near = nearest(c.arg(0), names);
            return fail("no timeline named " + c.arg(0) + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
        }
        Timeline *t = P.timeline(tl);
        switch (c.kind)
        {
            case CK::TimelineOpen:
                P.current = tl;
                mModel.dirty = true;   // `current` is presentation, but it is saved
                bumpFrame();
                emit(Event(EK::TimelineOpened).with("timeline", tl).with("name", t->name));
                return true;
            case CK::TimelinePin:
            {
                if (t->pinned()) return fail("timeline " + t->name + " is already pinned at " + t->pinCommit());
                std::string commit = c.flag("commit");
                if (!commit.empty())
                {
                    if (!fs::exists(pinsDir() + "/" + commit + ".cmp")) return fail("timeline pin: no snapshot " + commit + " in " + pinsDir());
                }
                else if (!takePin(tl, commit, err))
                    return fail("timeline pin: " + err);
                // The pin freezes the BASE's colour, and the base's overrides are part of it: copy
                // every inherited override this version does not already have into its own, so the
                // pinned walk (which stops here) still sees them.
                if (!t->base.empty())
                    for (const auto &ro : P.rackObjs)
                    {
                        const auto mine = P.tlgrade(tl, ro.id);
                        for (const auto &d : gradeDeltas(P, t->base, ro.id))
                        {
                            bool own = false;
                            if (mine)
                                for (const auto &m : mine->deltas) own = own || m.first == d.first;
                            if (!own && !setGrade(P, tl, ro.id, d.first, d.second, err)) return fail("timeline pin: " + err);
                        }
                    }
                if (!pinColour(P, tl, commit, err)) return fail("timeline pin: " + err);
                markDirty();
                bumpFrame();
                emit(Event(EK::TimelineChanged).with("timeline", tl).with("what", "pinned@" + commit));
                return true;
            }
            case CK::TimelineUnpin:
                if (!unpinColour(P, tl, err)) return fail("timeline unpin: " + err);
                markDirty();
                bumpFrame();
                emit(Event(EK::TimelineChanged).with("timeline", tl).with("what", "unpinned"));
                return true;
            case CK::TimelineFreeze:
                if (!freezeCut(P, tl, err)) return fail("timeline freeze: " + err);
                markDirty();
                emit(Event(EK::TimelineChanged).with("timeline", tl).with("what", "frozen"));
                return true;
            case CK::TimelineThaw:
                if (!thawCut(P, tl, err)) return fail("timeline thaw: " + err);
                markDirty();
                bumpFrame();
                emit(Event(EK::TimelineChanged).with("timeline", tl).with("what", "thawed"));
                return true;
            case CK::TimelineRebase:
            {
                const bool dry = c.has("dry-run");
                const RebaseReport rep = rebase(P, tl, true, dry);
                std::string pin;
                if (!dry && t->pinned())
                {
                    // Advance the pin to the rack's present state (R-VER-4).
                    const std::string old = t->pinCommit();
                    std::string commit;
                    if (!takePin(tl, commit, err)) return fail("timeline rebase: " + err);
                    if (commit != old)
                    {
                        if (!unpinColour(P, tl, err) || !pinColour(P, tl, commit, err)) return fail("timeline rebase: " + err);
                        pin = old + "->" + commit;
                    }
                }
                std::vector<std::string> names;
                for (const auto &d : rep.dangling) names.push_back(d.target + " (" + d.why + ")");
                std::ostringstream out;
                out << (dry ? "would prune " : "pruned ") << rep.pruned << " delta(s)";
                if (!names.empty()) out << "; dangling: " << joinNames(names);
                if (!pin.empty()) out << "; pin " << pin;
                if (!rep.message.empty()) out << "\n" << rep.message;
                out << '\n';
                mOutput = out.str();
                if (!dry) markDirty();
                bumpFrame();
                std::vector<std::string> ids;
                for (const auto &d : rep.dangling) ids.push_back(d.target);
                emit(Event(EK::TimelineRebased)
                         .with("timeline", tl)
                         .with("pruned", rep.pruned)
                         .with("dangling", joinNames(ids))
                         .with("pin", pin));
                return true;
            }
            case CK::TimelineDiff:
                mOutput = diff(P, tl);
                return true;
            case CK::TimelineDelete:
            {
                for (const auto &o : P.timelines)
                    if (o.base == tl) return fail("timeline delete: " + o.name + " is based on " + t->name + " — delete or rebase it first");
                if (P.timelines.size() <= 1) return fail("timeline delete: a project keeps at least one timeline");
                // R-EDT-4: a timeline another one places as a clip is in use
                for (const auto &o : P.timelines)
                {
                    ResolvedTimeline R;
                    if (o.id == tl || !resolved(o.id, R, err)) continue;
                    for (const auto &x : R.clips)
                        if (x.src == tl) return fail("timeline delete: " + o.name + " places " + t->name + " as clip " + x.name + " — delete that clip first");
                }
                std::set<NodeId> tracks, clips;
                for (const auto &x : P.tracks)
                    if (x.timeline == tl) tracks.insert(x.id);
                for (const auto &x : P.audioTracks)
                    if (x.timeline == tl) tracks.insert(x.id);
                for (const auto &x : P.clips)
                    if (x.timeline == tl || tracks.count(x.track)) clips.insert(x.id);
                auto eraseIf = [](auto &v, auto pred) { v.erase(std::remove_if(v.begin(), v.end(), pred), v.end()); };
                eraseIf(P.tracks, [&](const Track &x) { return tracks.count(x.id) > 0; });
                eraseIf(P.audioTracks, [&](const ATrack &x) { return tracks.count(x.id) > 0; });
                eraseIf(P.clips, [&](const Clip &x) { return clips.count(x.id) > 0; });
                eraseIf(P.audioClips, [&](const AClip &x) { return x.timeline == tl || tracks.count(x.track) > 0; });
                eraseIf(P.transitions, [&](const Transition &x) { return x.timeline == tl || tracks.count(x.track) > 0; });
                eraseIf(P.markers, [&](const Marker &x) { return x.timeline == tl; });
                eraseIf(P.drops, [&](const TlDrop &x) { return x.timeline == tl; });
                eraseIf(P.sets, [&](const TlSet &x) { return x.timeline == tl; });
                eraseIf(P.grades, [&](const TlGrade &x) { return x.timeline == tl; });
                eraseIf(P.effects, [&](const Fx &x) { return clips.count(x.clip) > 0; });
                const std::string name = t->name;
                eraseIf(P.timelines, [&](const Timeline &x) { return x.id == tl; });
                if (P.current == tl) P.current = P.timelines.front().id;
                markDirty();
                bumpFrame();
                emit(Event(EK::TimelineChanged).with("timeline", tl).with("what", "deleted " + name));
                return true;
            }
            default: return fail("not a timeline command");
        }
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // arrangement
    // ──────────────────────────────────────────────────────────────────────────────────────────

    void InterstellarService::effectChain(const NodeId &roId, std::vector<render::EffectRun> &out, std::string &key, double srcT) const
    {
        out.clear();
        key.clear();
        const Project &P = *mProject;
        // the node, then its ancestor groups (inner first), by the rack's live tree
        std::vector<NodeId> chain{roId};
        if (mRack.isOpen())
        {
            const auto &ns = mRack.model().nodes;
            int node = -1;
            for (const auto &kv : mNodeOf) if (kv.first == roId) node = kv.second;
            int guard = (int)ns.size() + 1;
            while (node >= 0 && guard-- > 0)
            {
                int parent = -1;
                for (const auto &n : ns) if (n.node == node) parent = n.parent;
                if (parent < 0) break;
                for (const auto &kv : mNodeOf) if (kv.second == parent) chain.push_back(kv.first);
                node = parent;
            }
        }
        for (const auto &owner : chain)
        {
            std::vector<const Effect *> stack;
            for (const auto &e : P.imageEffects) if (e.node == owner && e.enabled && e.mix > 0.0) stack.push_back(&e);
            std::stable_sort(stack.begin(), stack.end(), [](const Effect *a, const Effect *b) { return a->order < b->order; });
            for (const Effect *e : stack)
            {
                const render::EffectTypeDef *def = render::effectType(e->type);
                if (!def) continue;   // a plugin this build does not know: kept in the file, not run
                render::EffectRun r;
                r.type = e->type;
                r.mix = std::clamp(srcT >= 0 ? curveAt(e->id, "mix", srcT, e->mix) : e->mix, 0.0, 1.0);
                key += "|" + e->type + "@" + canonicalNumber(r.mix);
                for (const auto &d : def->params)
                {
                    double v = d.def;
                    for (const auto &kv : e->unknown)
                        if (kv.first == d.key) parseDouble(kv.second, v);
                    if (srcT >= 0) v = std::clamp(curveAt(e->id, d.key, srcT, v), d.min, d.max);   // R-ANIM: at this source time
                    r.p[d.key] = v;
                    key += ":" + canonicalNumber(v);
                }
                for (const auto &fk : def->files)
                {
                    // R-COLOR-5: the file, loaded here (the render path knows no paths) and keyed by version
                    std::string v;
                    for (const auto &kv : e->unknown) if (kv.first == fk) v = kv.second;
                    if (v.empty()) continue;
                    std::string why, stamp;
                    r.lut = loadLut(resolvePath(v), why, &stamp);
                    key += ":" + (r.lut ? stamp : std::string("unreadable"));
                }
                out.push_back(std::move(r));
            }
        }
    }

    bool InterstellarService::effectCommand(const Command &c)
    {
        Project &P = *mProject;
        auto stackOf = [&](const NodeId &node) {
            std::vector<Effect *> v;
            for (auto &e : P.imageEffects) if (e.node == node) v.push_back(&e);
            std::stable_sort(v.begin(), v.end(), [](const Effect *a, const Effect *b) { return a->order < b->order; });
            return v;
        };
        auto effectRef = [&](const std::string &ref, Effect *&out) {
            out = P.effect(P.idForRef(ref));
            if (out) return true;
            std::vector<std::string> ids;
            for (const auto &e : P.imageEffects) ids.push_back(e.id);
            const auto near = nearest(ref, ids);
            return fail("no effect " + ref + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
        };
        auto changed = [&](const char *what, const NodeId &id) {
            markDirty();
            bumpFrame();
            emit(Event(EK::RackChanged).with("what", what).with("node", id));
            return true;
        };
        switch (c.kind)
        {
            case CK::EffectAdd:
            {
                RackObj *ro = P.rackObj(P.idForRef(c.arg(0)));
                if (!ro)
                {
                    const auto near = nearest(c.arg(0), P.bindNames());
                    return fail("effect add: no rack node named " + c.arg(0) + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
                }
                const std::string type = c.flag("type");
                const render::EffectTypeDef *def = render::effectType(type);
                if (!def)
                {
                    std::vector<std::string> types;
                    for (const auto &t : render::effectCatalog()) types.push_back(t.type);
                    return fail("effect add: --type is one of " + joinNames(types) + (type.empty() ? std::string() : ", got " + type));
                }
                Effect e;
                e.id = P.freshId("ef_");
                e.node = ro->id;
                e.type = type;
                e.order = (int)stackOf(ro->id).size();
                for (const auto &d : def->params) e.unknown.emplace_back(d.key, canonicalNumber(d.def));
                P.imageEffects.push_back(e);
                mOutput = e.id + "\n";
                return changed("effect added", e.id);
            }
            case CK::EffectRemove:
            {
                Effect *e = nullptr;
                if (!effectRef(c.arg(0), e)) return false;
                const NodeId node = e->node, id = e->id;
                P.imageEffects.erase(std::remove_if(P.imageEffects.begin(), P.imageEffects.end(), [&](const Effect &x) { return x.id == id; }),
                                     P.imageEffects.end());
                int i = 0;
                for (Effect *x : stackOf(node)) x->order = i++;
                return changed("effect removed", id);
            }
            case CK::EffectMove:
            {
                Effect *e = nullptr;
                if (!effectRef(c.arg(0), e)) return false;
                const std::string ts = c.flag("to");   // flag() returns by value: keep it alive for `end`
                char *end = nullptr;
                long to = std::strtol(ts.c_str(), &end, 10);
                if (!c.has("to") || ts.empty() || !end || *end) return fail("effect move: --to <index>, 0 = first after Cosmo");
                auto stack = stackOf(e->node);
                const NodeId id = e->id;
                stack.erase(std::remove(stack.begin(), stack.end(), e), stack.end());
                to = std::clamp<long>(to, 0, (long)stack.size());
                stack.insert(stack.begin() + to, e);
                for (size_t i = 0; i < stack.size(); ++i) stack[i]->order = (int)i;
                return changed("effect moved", id);
            }
            default:
                return fail("not an effect command");
        }
    }

    bool InterstellarService::arrangeCommand(const Command &c)
    {
        Project &P = *mProject;
        const NodeId tl = currentTimeline();
        if (tl.empty()) return fail("the project has no timeline — `timeline new main`");
        std::string err;
        const double fps = P.fps;
        auto time = [&](const char *flag, double &out, bool required = true) {
            if (!c.has(flag))
            {
                if (required) return fail(std::string("--") + flag + " is required: " + usageOf(*specFor(c.kind)));
                return true;
            }
            if (!parseDouble(c.flag(flag), out)) return fail(std::string("--") + flag + " needs seconds, got `" + c.flag(flag) + "`");
            out = snapToFrame(out, fps);   // R-TL-5: a cut between frames is a bug
            return true;
        };
        auto clipRef = [&](const std::string &ref, NodeId &out) {
            out = P.idForRef(ref);
            if (P.clip(out)) return true;
            std::vector<std::string> names;
            for (const auto &x : P.clips) names.push_back(x.name);
            const auto near = nearest(ref, names);
            return fail("no clip named " + ref + (near.empty() ? "" : " (did you mean: " + joinNames(near) + "?)"));
        };
        auto done = [&](const std::string &what, const NodeId &node) {
            markDirty();
            bumpFrame();
            emit(Event(EK::ArrangeChanged).with("timeline", tl).with("what", what).with("node", node));
            return true;
        };

        switch (c.kind)
        {
            case CK::TrackAdd:
            {
                const std::string kind = c.flag("kind", "video");
                if (kind != "video" && kind != "audio") return fail("track add: --kind is video or audio");
                NodeId id;
                if (!arrange::addTrack(P, tl, kind, c.flag("name"), id, err)) return fail("track add: " + err);
                return done("track added", id);
            }
            case CK::ClipAdd:
            {
                double in = 0, out = 0, at = 0;
                if (!time("in", in) || !time("at", at)) return false;
                const NodeId track = P.idForRef(c.flag("track"));
                if (!P.track(track)) return fail("clip add: no track named " + c.flag("track"));
                if (c.has("out")) { if (!time("out", out)) return false; }
                else
                {
                    // the rest of the source — what a drop from the source bin means (R-UI-14)
                    const RackObj *ro = P.rackObj(P.idForRef(c.flag("src")));
                    const Timeline *nt = ro ? nullptr : P.timeline(P.idForRef(c.flag("src")));
                    if (nt)
                    {
                        // R-EDT-4: a nested timeline — the rest of it, to where its last clip ends
                        out = snapToFrame(timelineDuration(nt->id), fps);
                        if (!(out > in)) return fail("clip add: --in is at or past the end of " + nt->name + (out > 0 ? "" : " (it is empty)"));
                    }
                    else if (!ro || ro->kind == "group") return fail("clip add: " + c.flag("src") + " is not a rack source or a timeline");
                    else if (!looksLikeVideo(ro->media)) out = in + 5.0;   // a still: an editor's default hold
                    else
                    {
                        auto *s = source(*mSync, resolvePath(ro->media));
                        if (!s || !s->ok || s->info.frames <= 0) return fail("clip add: " + ro->name + " cannot be opened to know its length — give --out");
                        out = snapToFrame((double)s->info.frames / (s->info.fps > 0 ? s->info.fps : fps), fps);
                    }
                    if (!nt && !(out > in)) return fail("clip add: --in is at or past the end of " + ro->name);
                }
                NodeId id;
                if (!arrange::addClip(P, tl, track, P.idForRef(c.flag("src")), in, out, at, c.flag("name"), id, err))
                    return fail("clip add: " + err);
                return done("clip added", id);
            }
            case CK::ClipTrim:
            {
                NodeId id;
                if (!clipRef(c.arg(0), id)) return false;
                if (!c.has("in") && !c.has("out")) return fail("clip trim: give --in <t> and/or --out <t> (source seconds)");
                // --in / --out are SOURCE points — the clip's own `in`/`out` fields, the same
                // spelling `set <clip>.in=` uses. The model's trim moves an edge to a TIMELINE time,
                // so each is converted through the clip as this version resolves it.
                auto trimTo = [&](const char *flag, arrange::Edge edge) {
                    double src = 0;
                    if (!parseDouble(c.flag(flag), src)) return fail(std::string("clip trim: --") + flag + " needs source seconds");
                    ResolvedTimeline R;
                    if (!resolved(tl, R, err)) return fail(err);
                    const Clip *cl = nullptr;
                    for (const auto &x : R.clips)
                        if (x.id == id) cl = &x;
                    if (!cl) return fail("clip trim: " + c.arg(0) + " is not in this timeline");
                    const double speed = cl->speed != 0 ? std::fabs(cl->speed) : 1.0;
                    const double t = snapToFrame(cl->at + (src - cl->in) / speed, fps);
                    if (!arrange::trim(P, tl, id, edge, t, err)) return fail("clip trim: " + err);
                    return true;
                };
                if (c.has("in") && !trimTo("in", arrange::Edge::Head)) return false;
                if (c.has("out") && !trimTo("out", arrange::Edge::Tail)) return false;
                return done("trimmed", id);
            }
            case CK::ClipSplit:
            {
                NodeId id, right;
                double t = 0;
                if (!clipRef(c.arg(0), id) || !time("at", t)) return false;
                if (!arrange::split(P, tl, id, t, right, err)) return fail("clip split: " + err);
                copyAnims(id, right, 0.0);   // both halves keep the animation, on the same footage frames (R-ANIM-1)
                return done("split", right);
            }
            case CK::ClipMove:
            {
                NodeId id;
                double t = 0;
                if (!clipRef(c.arg(0), id) || !time("at", t)) return false;
                NodeId track;
                if (c.has("track") && !P.track(track = P.idForRef(c.flag("track")))) return fail("clip move: no track named " + c.flag("track"));
                if (!arrange::move(P, tl, id, t, track, err)) return fail("clip move: " + err);
                return done("moved", id);
            }
            case CK::ClipDelete:
            {
                NodeId id;
                if (!clipRef(c.arg(0), id)) return false;
                if (!arrange::remove(P, tl, id, c.has("ripple"), err)) return fail("clip delete: " + err);
                if (mSelectedClip == id) mSelectedClip.clear();
                return done(c.has("ripple") ? "ripple deleted" : "deleted", id);
            }
            case CK::ClipRoll:
            {
                NodeId id;
                double t = 0;
                if (!clipRef(c.arg(0), id) || !time("at", t)) return false;
                ResolvedTimeline R;
                if (!resolved(tl, R, err)) return fail(err);
                const Clip *left = nullptr;
                for (const auto &x : R.clips)
                    if (x.id == id) left = &x;
                if (!left) return fail("clip roll: " + c.arg(0) + " is not in this timeline");
                NodeId right;
                for (const auto &x : R.clips)
                    if (x.track == left->track && std::fabs(x.at - left->end()) < 0.5 / fps) right = x.id;
                if (right.empty()) return fail("clip roll: " + c.arg(0) + " has no adjacent clip after it");
                if (!arrange::roll(P, tl, id, right, t, err)) return fail("clip roll: " + err);
                return done("rolled", id);
            }
            case CK::ClipSlip:
            {
                NodeId id;
                double by = 0;
                if (!clipRef(c.arg(0), id)) return false;
                if (!parseDouble(c.flag("by"), by)) return fail("clip slip: --by needs source seconds");
                if (!arrange::slip(P, tl, id, by, err)) return fail("clip slip: " + err);
                return done("slipped", id);
            }
            case CK::ClipSpeed:
            {
                NodeId id;
                double s = 0;
                if (!clipRef(c.arg(0), id)) return false;
                if (!parseDouble(c.arg(1), s) || s <= 0 || s > 8) return fail("clip speed: a speed in (0, 8], got `" + c.arg(1) + "`");
                if (!setField(P, tl, id, "speed", c.arg(1), err)) return fail("clip speed: " + err);
                return done("speed", id);
            }
            case CK::ClipCopy:
            {
                NodeId id;
                if (!clipRef(c.arg(0), id)) return false;
                ResolvedTimeline R;
                if (!resolved(tl, R, err)) return fail(err);
                for (const auto &x : R.clips)
                    if (x.id == id)
                    {
                        mClipClipboard = std::make_shared<Clip>(x);   // as THIS version sees it, overrides included
                        mClipClipboardAnims.clear();
                        for (const auto &an : P.anims)
                            if (an.node == id) mClipClipboardAnims.emplace_back(an.key, P.keysOf(an.id));
                        mHasClipClipboard = true;
                        emit(Event(EK::SelectionChanged).with("rack", "").with("clip", id));
                        return true;
                    }
                return fail("clip copy: " + c.arg(0) + " is not in this timeline");
            }
            case CK::ClipPaste:
            {
                if (!mHasClipClipboard) return fail("clip paste: nothing copied — `clip copy <clip>` first");
                double at = snapToFrame(mModel.playhead, fps);
                if (c.has("at") && !time("at", at)) return false;
                const Clip &cb = *mClipClipboard;
                NodeId track = cb.track;
                if (c.has("track") && !P.track(track = P.idForRef(c.flag("track")))) return fail("clip paste: no track named " + c.flag("track"));
                NodeId id;
                if (!arrange::addClip(P, tl, track, cb.src, cb.in, cb.out, at, "", id, err))
                    return fail("clip paste: " + err);
                for (const auto &ca : mClipClipboardAnims)   // its curves come with it (R-ANIM-1)
                {
                    Anim n;
                    n.id = P.freshId("an_");
                    n.node = id;
                    n.key = ca.first;
                    P.anims.push_back(n);
                    for (const auto &kk : ca.second)
                    {
                        AnimKey ak;
                        ak.anim = n.id;
                        ak.t = kk.t; ak.v = kk.v;
                        ak.in = anim::sideName(kk.in); ak.out = anim::sideName(kk.out);
                        ak.speedIn = kk.speedIn; ak.speedOut = kk.speedOut; ak.inflIn = kk.inflIn; ak.inflOut = kk.inflOut;
                        P.animKeys.push_back(ak);
                    }
                }
                // everything else the clip carries, through the schema — so a field added to a clip
                // later is pasted too, without this list knowing it
                Fields kv;
                const Clip fresh;
                static const std::set<std::string> placement{"id", "name", "track", "timeline", "order", "src", "at", "in", "out", "from"};
                for (const auto &f : schema::fields<Clip>())
                {
                    if (!f.editable || placement.count(f.key)) continue;
                    if (f.text && f.text(cb) != f.text(fresh)) kv.emplace_back(f.key, f.text(cb));
                    else if (f.num && f.num(cb) != f.num(fresh)) kv.emplace_back(f.key, canonicalNumber(f.num(cb)));
                }
                if (!kv.empty() && !setFields(P, tl, id, kv, err)) return fail("clip paste: " + err);
                mSelectedClip = id;
                return done("pasted", id);
            }
            case CK::ClipSelect:
            {
                NodeId id;
                if (!c.args.empty() && !clipRef(c.arg(0), id)) return false;
                mSelectedClip = id;
                emit(Event(EK::SelectionChanged).with("rack", "").with("clip", id));
                return true;
            }
            case CK::TransitionAdd:
            {
                const std::string between = c.flag("between");
                const auto comma = between.find(',');
                if (comma == std::string::npos) return fail("transition add: --between a,b names the outgoing and incoming clips");
                NodeId a, b;
                if (!clipRef(between.substr(0, comma), a) || !clipRef(between.substr(comma + 1), b)) return false;
                double dur = 0.5;
                if (c.has("dur") && (!parseDouble(c.flag("dur"), dur) || dur <= 0)) return fail("transition add: --dur needs seconds");
                const std::string kind = c.flag("kind", "dissolve");
                if (kind != "dissolve" && kind != "dip") return fail("transition add: --kind is dissolve or dip");
                NodeId id;
                if (!arrange::addTransition(P, tl, a, b, kind, snapToFrame(dur, fps), id, err)) return fail("transition add: " + err);
                return done("transition added", id);
            }
            case CK::MarkerAdd:
            {
                double at = 0;
                if (!time("at", at)) return false;
                std::string why;
                if (!Project::nameIsLegal(c.arg(0), why)) return fail("marker add: " + why);
                if (P.nameIsTaken(c.arg(0))) return fail("marker add: the name " + c.arg(0) + " is taken");
                Marker m;
                m.id = P.freshId("mk_");
                m.name = c.arg(0);
                m.timeline = tl;
                m.at = at;
                m.note = c.flag("note");
                P.markers.push_back(m);
                return done("marker added", m.id);
            }
            case CK::FxAdd:
            {
                const std::string type = c.flag("type");
                if (type != "denoise" && type != "blend" && type != "freeze")
                    return fail("fx add: --type is denoise, blend or freeze");
                Fx fx;
                fx.id = P.freshId("fx_");
                fx.type = type;
                if (type == "freeze")
                {
                    NodeId clip;
                    if (!c.has("clip")) return fail("fx add: freeze is editorial — it attaches to a --clip");
                    if (!clipRef(c.flag("clip"), clip)) return false;
                    fx.clip = clip;
                    double at = 0;
                    if (!time("at", at)) return false;
                    fx.at = at;
                }
                else
                {
                    // Denoise and blend belong to the FOOTAGE, like its grade (project-format §5).
                    if (!c.has("node")) return fail("fx add: " + type + " belongs to the footage — attach it to a rack --node");
                    const NodeId ro = P.idForRef(c.flag("node"));
                    const RackObj *r = P.rackObj(ro);
                    if (!r || r->kind == "group") return fail("fx add: --node must name a rack source, got " + c.flag("node"));
                    fx.node = ro;
                    double v = 0;
                    fx.radius = 1;
                    if (c.has("radius"))
                    {
                        if (!parseDouble(c.flag("radius"), v) || v < 0 || v > 16) return fail("fx add: --radius is 0..16 frames");
                        fx.radius = (int)v;
                    }
                    if (c.has("strength"))
                    {
                        if (!parseDouble(c.flag("strength"), v) || v < 0 || v > 1) return fail("fx add: --strength is 0..1");
                        fx.strength = v;
                    }
                    if (c.has("shutter"))
                    {
                        if (!parseDouble(c.flag("shutter"), v) || v <= 0 || v > 360) return fail("fx add: --shutter is (0, 360] degrees");
                        fx.shutter = v;
                    }
                }
                P.effects.push_back(fx);
                return done("fx added", fx.id);
            }
            case CK::FxDelete:
            {
                const NodeId id = P.idForRef(c.arg(0));
                if (!P.fx(id)) return fail("no effect " + c.arg(0));
                P.effects.erase(std::remove_if(P.effects.begin(), P.effects.end(), [&](const Fx &f) { return f.id == id; }),
                                P.effects.end());
                return done("fx deleted", id);
            }
            case CK::AudioTrackAdd:
            {
                ATrack a;
                a.id = P.freshId("atrk_");
                a.name = c.has("name") ? c.flag("name") : P.freshName("a" + std::to_string(P.audioTracks.size()));
                std::string why;
                if (!Project::nameIsLegal(a.name, why)) return fail("audio track add: " + why);
                if (P.nameIsTaken(a.name)) return fail("audio track add: the name " + a.name + " is taken");
                a.timeline = tl;
                for (const auto &x : P.audioTracks)
                    if (x.timeline == tl) a.order = std::max(a.order, x.order + 1);
                P.audioTracks.push_back(a);
                return done("audio track added", a.id);
            }
            case CK::AudioClipAdd:
            {
                const NodeId track = P.idForRef(c.flag("track"));
                const Track *vt = P.track(track);
                if (!P.audioTrack(track) && !(vt && vt->audio())) return fail("audio clip add: no audio track named " + c.flag("track"));
                std::string src = c.flag("src");
                if (src.empty()) return fail("audio clip add: --src names the audio file (or a rack source, for its sound)");
                // a rack source's own sound — a camera file's dialogue under its picture
                if (const RackObj *ro = P.rackObj(P.idForRef(src)); ro && !ro->media.empty() && !fs::exists(src)) src = resolvePath(ro->media);
                if (!fs::exists(src)) return fail("audio clip add: no such file: " + src);
                double at = 0, in = 0, out = 0, gain = 0, fade = 0;
                if (!time("at", at)) return false;
                if (!time("in", in, false)) return false;
                if (c.has("out")) { if (!time("out", out)) return false; }
                else
                {
                    // the file's own length, read through the host's decoder (R-AUD-5 amended)
                    std::unique_ptr<IAudioSource> probe = mHost.audioSource ? mHost.audioSource() : nullptr;
                    IAudioSource::Info info;
                    if (!probe || !probe->open(src, P.sampleRate > 0 ? P.sampleRate : 48000, info))
                        return fail("audio clip add: " + src + " has no sound this build can read — give --out (source seconds) to place it anyway");
                    out = info.duration;
                }
                if (out <= in) return fail("audio clip add: --out must be after --in");
                if (c.has("gain") && !parseDouble(c.flag("gain"), gain)) return fail("audio clip add: --gain needs dB");
                if (c.has("fade") && (!parseDouble(c.flag("fade"), fade) || fade < 0)) return fail("audio clip add: --fade needs seconds");
                AClip a;
                a.id = P.freshId("aclp_");
                a.name = P.freshName(fileStem(src));
                a.track = track;
                a.timeline = tl;
                a.src = relativePath(fs::absolute(src).string());
                a.at = at;
                a.in = in;
                a.out = out;
                a.gain = gain;
                a.fadeIn = a.fadeOut = fade;
                P.audioClips.push_back(a);
                return done("audio clip added", a.id);
            }
            default: return fail("not an arrangement command");
        }
    }

    // ──────────────────────────────────────────────────────────────────────────────────────────
    // transport, lint, wait
    // ──────────────────────────────────────────────────────────────────────────────────────────

    bool InterstellarService::playheadCommand(const Command &c)
    {
        const double fps = mProject->fps;
        if (c.kind == CK::Play)
        {
            if (mPlaying) return true;
            const double dur = timelineDuration(currentTimeline());
            if (dur > 0 && mModel.playhead >= dur - 0.5 / fps) mModel.playhead = 0;
            mPlaying = true;
            mShuttle = 1.0;   // R-EDT-2: play is the shuttle at 1× forward
            mPlayFromT = mModel.playhead;
            mPlayFromMs = mNowMs;
            mPlayEdge = mSettings.previewEdge > 0 ? std::min(mSettings.previewEdge, 960) : 960;   // stepped down or up by what the pool keeps up with
            mRateFromMs = mNowMs;
            mRateFromDone = mAhead ? mAhead->finished.load() : 0;
            mPlayRate = 0;
            mPreroll = mAhead != nullptr;
            mPrerollFromMs = mNowMs;
            if (!mPreroll) startSound();
            emit(Event(EK::PlaybackChanged).with("playing", true));
            return true;
        }
        if (c.kind == CK::Pause)
        {
            if (!mPlaying) return true;
            mPlaying = false;
            mShuttle = 0.0;
            stopSound();
            mPlayEdge = 0;   // the paused frame is graded at the full preview size again
            emit(Event(EK::PlaybackChanged).with("playing", false));
            return true;
        }
        const std::string a = c.arg(0);
        double t = mModel.playhead;
        if (a == "next-cut" || a == "prev-cut")
        {
            ResolvedTimeline R;
            std::string err;
            if (!resolved(currentTimeline(), R, err)) return fail(err);
            std::vector<double> cuts = {0.0};
            for (const auto &x : R.clips)
            {
                cuts.push_back(x.at);
                cuts.push_back(x.end());
            }
            std::sort(cuts.begin(), cuts.end());
            const double eps = 0.5 / fps;
            if (a == "next-cut")
            {
                double best = t;
                for (double x : cuts)
                    if (x > t + eps) { best = x; break; }
                t = best;
            }
            else
            {
                double best = 0;
                for (double x : cuts)
                    if (x < t - eps) best = x;
                t = best;
            }
        }
        else
        {
            double v = 0;
            const bool rel = !a.empty() && (a[0] == '+' || a[0] == '-');
            if (!parseDouble(a, v)) return fail("playhead: a time in seconds, +<dt>, -<dt>, next-cut or prev-cut, got `" + a + "`");
            t = rel ? t + v : v;
        }
        t = std::max(0.0, snapToFrame(t, fps));
        mModel.playhead = t;
        if (mPlaying)
        {
            mPlayFromT = t;
            mPlayFromMs = mNowMs;
            if (mSoundClock) { stopSound(); startSound(); }   // a seek while playing: the sound jumps with it
        }
        else soundGrain(t);   // R-AUD-6: a scrub plays a short grain where it lands
        bumpFrame();
        emit(Event(EK::PlayheadMoved).with("t", t).with("frame", (long long)std::llround(t * fps)));
        return true;
    }

    bool InterstellarService::lint()
    {
        std::ostringstream out;
        int offline = 0, dangling = 0, refused = 0;
        for (const auto &r : mModel.rack)
            if (r.failed)
            {
                ++offline;
                out << "offline   " << (r.bindName.empty() ? r.cosmoName : r.bindName) << "  " << r.media << '\n';
            }
        for (const auto &t : mProject->timelines)
        {
            ResolvedTimeline R;
            std::string err;
            if (!resolved(t.id, R, err))
            {
                ++refused;
                out << "refused   timeline " << t.name << ": " << err << '\n';
                continue;
            }
            for (const auto &d : R.dangling)
            {
                ++dangling;
                out << "dangling  " << t.name << "  " << d.delta << " → " << d.target << "  (" << d.why << ")\n";
            }
        }
        for (const auto &u : mProject->unrenderable())
        {
            ++refused;
            out << "refused   " << u << '\n';
        }
        for (const auto &id : nestingCycles(*mProject))   // R-EDT-4: only a hand-edited .isp gets here
        {
            ++refused;
            const Timeline *t = mProject->timeline(id);
            out << "refused   timeline " << (t ? t->name : id) << " contains itself through its nested clips — the clip that closes the loop shows nothing\n";
        }
        for (const auto &fx : mProject->effects)
        {
            int temporal = 0;
            for (const auto &o : mProject->effects)
                if (!o.node.empty() && o.node == fx.node && o.type != "freeze") ++temporal;
            if (temporal > 1 && fx.type != "freeze")
            {
                ++refused;
                out << "refused   " << fx.id << ": only the first temporal effect on a source renders in v1\n";
            }
        }
        mOutput = out.str().empty() ? std::string("clean\n") : out.str();
        emit(Event(EK::LintReport).with("offline", offline).with("dangling", dangling).with("refused", refused));
        return true;
    }

    bool InterstellarService::wait(const Command &c)
    {
        const std::string cond = c.arg(0);
        if (cond != "rack.loaded" && cond != "render.done" && cond != "frame.ready" && cond != "cache.done")
            return fail("wait: rack.loaded, render.done, frame.ready or cache.done, got `" + cond + "`");
        int ms = 120000;
        if (c.has("timeout"))
        {
            const std::string v = c.flag("timeout");
            ms = v.size() > 2 && v.compare(v.size() - 2, 2, "ms") == 0 ? std::atoi(v.c_str())
                 : !v.empty() && v.back() == 's'                       ? std::atoi(v.c_str()) * 1000
                                                                       : std::atoi(v.c_str());
        }
        auto holds = [&] {
            if (cond == "rack.loaded") return !mPending;
            if (cond == "cache.done") return !mCacheForced;
            if (cond == "render.done")
            {
                for (const auto &j : mJobs)
                    if (j->model.state == "queued" || j->model.state == "running") return false;
                return true;
            }
            return true;
        };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
        while (!holds())
        {
            if (std::chrono::steady_clock::now() >= deadline) return fail("wait " + cond + ": timed out after " + std::to_string(ms) + " ms");
            pump(mNowMs + 16.0);
            if (mPending) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return true;
    }
}
}
