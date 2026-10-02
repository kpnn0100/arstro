/*
 *  InterstellarService — the frame path and the render queue.
 *
 *      resolved timeline → activeAt(t) → per clip: source frame (volume, temporal fx, freeze)
 *        → grade (GradeEngine, cached on the param hash) → grade weight → Layer → compose
 *
 *  Pure in (project, timeline, t, size): no wall clock, no hidden state that changes the pixels
 *  (R-RENDER-2). The caches are invisible by construction — the volume returns the same bytes for
 *  the same t whatever the access order (R-VOL-7), and the frame cache keys on everything that
 *  changes the graded pixels.
 */
#include "ServiceInternal.h"
#include "ActiveSet.h"
#include "Composite.h"
#include "ParamHash.h"
#include "Project.h"
#include "Versions.h"
#include "volume/TemporalOps.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <map>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        render::Blend blendOf(const std::string &s)
        {
            if (s == "multiply") return render::Blend::Multiply;
            if (s == "screen") return render::Blend::Screen;
            if (s == "overlay") return render::Blend::Overlay;
            if (s == "add") return render::Blend::Add;
            if (s == "subtract") return render::Blend::Subtract;
            if (s == "difference") return render::Blend::Difference;
            return render::Blend::Normal;
        }

        render::Fit fitOf(const std::string &s)
        {
            if (s == "cover") return render::Fit::Cover;
            if (s == "stretch") return render::Fit::Stretch;
            if (s == "none") return render::Fit::None;
            return render::Fit::Contain;
        }

        void outputSize(int w, int h, int proxyEdge, int &ow, int &oh)
        {
            ow = w;
            oh = h;
            const int longEdge = std::max(w, h);
            if (proxyEdge > 0 && longEdge > proxyEdge)
            {
                const double s = (double)proxyEdge / longEdge;
                ow = std::max(1, (int)std::lround(w * s));
                oh = std::max(1, (int)std::lround(h * s));
            }
        }

        /** The grade weight: a continuous bypass, ungraded → graded per pixel (R-RACK-4). */
        void mixWeight(const Raster &ungraded, Raster &graded, double w)
        {
            if (w >= 1.0 || ungraded.width != graded.width || ungraded.height != graded.height) return;
            const int k = (int)std::lround(std::clamp(w, 0.0, 1.0) * 256.0);
            uint8_t *g = graded.rgba.data();
            const uint8_t *u = ungraded.rgba.data();
            const size_t n = graded.rgba.size();
            for (size_t i = 0; i < n; ++i) g[i] = (uint8_t)((u[i] * (256 - k) + g[i] * k + 128) >> 8);
        }
    }

    InterstellarService::Source *InterstellarService::source(RenderCtx &ctx, const std::string &media)
    {
        auto it = ctx.sources.find(media);
        if (it != ctx.sources.end()) return it->second.get();
        auto s = std::make_unique<Source>();
        if (!mHost.frameSource) s->why = "no frame source installed (the host must provide one)";
        else
        {
            s->src = mHost.frameSource();
            if (!s->src || !s->src->open(media, s->info) || !s->info.valid())
                s->why = "cannot open " + media;
            else
            {
                Source *raw = s.get();
                VolumeExtent ext;
                ext.width = s->info.width;
                ext.height = s->info.height;
                ext.frames = std::max<long long>(1, s->info.frames);
                // 33 frames of 4K is 1.1 GB; the cap is the hard refusal, never an overshoot.
                const size_t cap = (size_t)ext.width * ext.height * 4 * VolumeView::kMaxWindow + 1;
                s->volume = std::make_unique<CachedVolume>(
                    ext,
                    [raw](long long f, FrameRGBA &out) {
                        Raster r;
                        r.rgba.swap(out.rgba);   // decode into the recycled buffer
                        const bool ok = raw->src->frameAt(f, r);
                        out.rgba.swap(r.rgba);
                        out.width = r.width;
                        out.height = r.height;
                        return ok;
                    },
                    cap);
                s->ok = true;
            }
        }
        Source *out = s.get();
        ctx.sources[media] = std::move(s);
        return out;
    }

    bool InterstellarService::decodeLayer(RenderCtx &ctx, const PlanLayer &l, Raster &out)
    {
        Source *s = source(ctx, l.media);
        if (!s || !s->ok) return false;
        const long long last = std::max<long long>(0, s->volume->extent().frames - 1);
        const long long frame = std::clamp<long long>(l.frame, 0, last);
        // v1 renders the FIRST temporal effect on a source; lint reports any after it.
        if (!l.fxType.empty() && l.fxRadius > 0 && s->volume->extent().frames > 1)
        {
            FrameRGBA f;
            bool ok = false;
            if (l.fxType == "denoise")
            {
                TemporalDenoise op;
                op.setRadius(l.fxRadius);
                op.setStrength((float)l.fxStrength);
                ok = renderTemporal(*s->volume, op, frame, f);
            }
            else
            {
                FrameBlend op;
                op.setRadius(l.fxRadius);
                ok = renderTemporal(*s->volume, op, frame, f);
            }
            if (!ok) return false;
            out.width = f.width;
            out.height = f.height;
            out.rgba.swap(f.rgba);
            return true;
        }
        VolumeView v;
        if (!s->volume->window(frame, frame, v) || v.frames != 1) return false;
        out.allocate(v.width, v.height);
        for (int y = 0; y < v.height; ++y)
            std::memcpy(out.rgba.data() + (size_t)y * v.width * 4, v.row(0, y), (size_t)v.width * 4);
        return true;
    }

    bool InterstellarService::gradeForBypassing(const NodeId &tl, const NodeId &rackObj, const std::set<NodeId> &groupsOff,
                                                EditParams &out, std::string &err)
    {
        ColourTree tree;
        std::map<NodeId, int> idx;
        std::string source;
        if (!colourTreeFor(tl, tree, idx, source, err)) return false;
        const auto it = idx.find(rackObj);
        if (it == idx.end()) { err = "rack node " + rackObj + " is not in the " + source; return false; }
        for (const auto &kv : idx)
        {
            applyDeltas(tree[(size_t)kv.second].own, gradeDeltas(*mProject, tl, kv.first));
            if (groupsOff.count(kv.first)) tree[(size_t)kv.second].bypass = true;   // Cosmo's bypass, exactly
        }
        out = foldRender(tree, it->second);
        return true;
    }

    namespace
    {
        void planKeyAppend(std::string &k, const PlanLayer &l)
        {
            char buf[256];
            std::snprintf(buf, sizeof buf, "|%lld|%016llx|%016llx|%.4f|%.4f|%d|%.3f,%.3f,%.4f,%.3f,%.3f,%.3f,%.4f,%.4f,%.4f,%.4f|%d|%.4f|%d|%d",
                          l.frame, (unsigned long long)render::hashParams(l.params),
                          (unsigned long long)(l.groupMix ? render::hashParams(l.paramsGroupsOff) : 0), l.groupWeight, l.weight,
                          l.edge, l.layer.geom.x, l.layer.geom.y, l.layer.geom.scale, l.layer.geom.rotation, l.layer.geom.anchorX,
                          l.layer.geom.anchorY, l.layer.geom.cropX, l.layer.geom.cropY, l.layer.geom.cropW, l.layer.geom.cropH,
                          (int)l.layer.fit, l.layer.opacity, (int)l.layer.blend, l.layer.dissolveWithPrevious ? 1 : 0);
            k += l.media + l.fxKey + buf;
        }
    }

    bool InterstellarService::planFrame(const NodeId &tl, double t, int proxyEdge, FramePlan &plan, bool *anyClip)
    {
        if (anyClip) *anyClip = false;
        plan = FramePlan{};
        if (!mOpen) return false;
        const Project &P = *mProject;
        ResolvedTimeline R;
        std::string err;
        if (!resolved(tl, R, err)) return fail("render: " + err);
        outputSize(P.width, P.height, proxyEdge, plan.width, plan.height);

        std::map<NodeId, const Track *> tracks;
        for (const auto &x : R.tracks) tracks[x.id] = &x;
        std::vector<render::ClipSpan> spans;
        std::map<NodeId, const Clip *> clips;
        for (const auto &c : R.clips)
        {
            const auto prov = R.provenance.find(c.id);
            if (prov != R.provenance.end() && prov->second == Provenance::Dangling) continue;   // shown, never rendered
            const auto tr = tracks.find(c.track);
            if (tr == tracks.end() || tr->second->audio() || tr->second->mute) continue;
            render::ClipSpan s;
            s.id = c.id;
            s.trackOrder = tr->second->order;
            s.at = c.at;
            s.in = c.in;
            s.out = c.out;
            s.speed = c.speed;
            spans.push_back(s);
            clips[c.id] = &c;
        }
        std::vector<render::TransitionSpan> trs;
        for (const auto &x : R.transitions)
        {
            render::TransitionSpan s;
            s.clipA = x.clipA;
            s.clipB = x.clipB;
            s.dur = x.dur;
            s.linear = x.kind == "dissolve";
            trs.push_back(s);
        }
        const auto active = render::activeAt(spans, trs, t, P.fps);

        // The colour tree once per frame, for the group weights (D-7).
        ColourTree tree;
        std::map<NodeId, int> idx;
        std::string colourSource;
        const bool haveTree = colourTreeFor(tl, tree, idx, colourSource, err);
        std::map<int, NodeId> roOfIndex;
        for (const auto &kv : idx) roOfIndex[kv.second] = kv.first;

        for (const auto &a : active)
        {
            const Clip *c = clips[a.id];
            const RackObj *ro = c ? P.rackObj(c->src) : nullptr;
            if (!c || !ro || ro->media.empty()) continue;   // offline: drawn as missing by the UI
            PlanLayer L;
            L.media = resolvePath(ro->media);
            Source *s = source(*mSync, L.media);   // info only — nothing decodes on this thread
            if (!s || !s->ok) continue;
            // Source frame from the clip's SOURCE time at the SOURCE's own rate (a 30p clip in a 24p
            // project steps at 30p); a still is frame 0 forever (R-VOL-6).
            const double srcFps = s->info.fps > 0 ? s->info.fps : P.fps;
            L.frame = s->info.frames <= 1 ? 0 : (long long)std::floor(a.localTime * srcFps + 1e-6);
            for (const auto &fx : P.effects)
            {
                if (fx.type == "freeze" && fx.clip == c->id)
                    L.frame = freezeRemap(L.frame, (long long)std::floor((c->in + fx.at * c->speed) * srcFps + 1e-6));
                else if (fx.type != "freeze" && fx.node == ro->id && L.fxType.empty())
                {
                    L.fxType = fx.type;
                    L.fxRadius = fx.radius;
                    L.fxStrength = fx.strength;
                    L.fxKey = "|" + fx.type + ":" + std::to_string(fx.radius) + ":" + canonicalNumber(fx.strength);
                }
            }
            std::string e;
            if (!gradeFor(tl, ro->id, L.params, e)) L.params = EditParams{};   // an unbound node renders ungraded
            L.identity = render::GradeEngine::isIdentity(L.params);
            // D-7: ancestor groups whose weight is below 1 fade their own contribution.
            std::set<NodeId> partial;
            double gw = 1.0;
            if (haveTree && idx.count(ro->id))
            {
                int guard = (int)tree.size();
                for (int p = tree[(size_t)idx[ro->id]].parent; p >= 0 && guard-- > 0; p = tree[(size_t)p].parent)
                {
                    const auto g = roOfIndex.find(p);
                    if (g == roOfIndex.end()) continue;
                    const RackObj *gro = P.rackObj(g->second);
                    if (gro && gro->weight < 1.0 && !tree[(size_t)p].bypass)
                    {
                        partial.insert(gro->id);
                        gw *= std::clamp(gro->weight, 0.0, 1.0);
                    }
                }
            }
            if (!partial.empty() && gradeForBypassing(tl, ro->id, partial, L.paramsGroupsOff, e))
            {
                L.groupMix = true;
                L.groupWeight = gw;
            }
            L.weight = std::clamp(ro->weight, 0.0, 1.0);
            // A partial mix needs the frames it mixes at one size — grade at source size and let
            // the composite scale.
            L.edge = (L.weight < 1.0 && L.weight > 0.0) || L.groupMix ? 0 : proxyEdge;

            const Track *tr = tracks[c->track];
            // Clip offsets are stored in FRAME units, so a proxy and a full render place a clip
            // identically; the composite works in output pixels.
            L.layer.geom.x = c->geom.x * plan.width;
            L.layer.geom.y = c->geom.y * plan.height;
            L.layer.geom.scale = c->geom.scale;
            L.layer.geom.rotation = c->geom.rotation;
            L.layer.geom.anchorX = c->geom.anchorX;
            L.layer.geom.anchorY = c->geom.anchorY;
            L.layer.geom.cropX = c->geom.cropX;
            L.layer.geom.cropY = c->geom.cropY;
            L.layer.geom.cropW = c->geom.cropW;
            L.layer.geom.cropH = c->geom.cropH;
            L.layer.fit = fitOf(c->fit);
            L.layer.opacity = std::clamp(c->opacity * (tr ? tr->opacity : 1.0) * a.weight, 0.0, 1.0);
            L.layer.blend = blendOf(c->blend);
            L.layer.dissolveWithPrevious = a.dissolveWithPrevious;
            plan.layers.push_back(std::move(L));
        }
        if (anyClip) *anyClip = !plan.layers.empty();
        plan.key = std::to_string(plan.width) + "x" + std::to_string(plan.height);
        for (const auto &l : plan.layers) planKeyAppend(plan.key, l);
        return true;
    }

    bool InterstellarService::planReferenceFrame(int proxyEdge, FramePlan &plan)
    {
        // Nothing cut at t: the Grade target's reference frame, graded — so grading works before a
        // single clip exists, and choosing a reference frame is visible (DR-UI-9).
        plan = FramePlan{};
        if (!mModel.hasGradeTarget || mModel.selectedRack < 0) return false;
        const NodeId roId = mModel.rack[(size_t)mModel.selectedRack].rackObj;
        const RackObj *ro = mProject->rackObj(roId);
        if (!ro || ro->media.empty()) return false;
        PlanLayer L;
        L.media = resolvePath(ro->media);
        Source *s = source(*mSync, L.media);
        if (!s || !s->ok) return false;
        L.frame = s->info.frames <= 1 ? 0 : (long long)std::llround(ro->frame * (s->info.fps > 0 ? s->info.fps : 24.0));
        std::string e;
        if (!gradeFor(currentTimeline(), roId, L.params, e)) return false;
        L.identity = render::GradeEngine::isIdentity(L.params);
        L.edge = proxyEdge;
        outputSize(mProject->width, mProject->height, proxyEdge, plan.width, plan.height);
        plan.layers.push_back(L);
        plan.key = "ref|" + std::to_string(plan.width) + "x" + std::to_string(plan.height);
        planKeyAppend(plan.key, plan.layers.back());
        return true;
    }

    bool InterstellarService::executePlan(RenderCtx &ctx, const FramePlan &plan, Raster &out)
    {
        // Graded frames must outlive the compose call: one slot per layer.
        std::vector<Raster> graded(plan.layers.size());
        std::vector<render::Layer> layers;
        for (size_t i = 0; i < plan.layers.size(); ++i)
        {
            const PlanLayer &l = plan.layers[i];
            const bool ungradedOnly = l.weight <= 0.0 || (l.identity && !l.groupMix);
            render::FrameCache::Key key;
            key.source = l.media + l.fxKey;
            key.sourceFrame = l.frame;
            key.paramHash = ungradedOnly ? render::FrameCache::kUngraded : render::hashParams(l.params);
            key.level = l.edge;
            if (l.weight > 0.0 && l.weight < 1.0) key.source += "|w" + canonicalNumber(l.weight);
            if (l.groupMix && !ungradedOnly)
                key.source += "|g" + canonicalNumber(l.groupWeight) + ":" + std::to_string(render::hashParams(l.paramsGroupsOff));
            if (!mCache->get(key, graded[i]))
            {
                Raster ungraded;
                if (!decodeLayer(ctx, l, ungraded)) continue;
                if (key.paramHash == render::FrameCache::kUngraded) graded[i] = std::move(ungraded);
                else
                {
                    if (!ctx.grade->render(ungraded, l.params, !l.identity, l.edge, graded[i])) continue;
                    if (l.groupMix)
                    {
                        // The groups' own contribution, faded: off ↔ on by the product of their weights.
                        Raster off;
                        if (ctx.grade->render(ungraded, l.paramsGroupsOff, !render::GradeEngine::isIdentity(l.paramsGroupsOff), l.edge, off))
                        {
                            mixWeight(off, graded[i], l.groupWeight);
                        }
                    }
                    mixWeight(ungraded, graded[i], l.weight);
                }
                mCache->put(key, graded[i]);
            }
            render::Layer L = l.layer;
            L.src = &graded[i];
            layers.push_back(L);
        }
        render::compose(layers, plan.width, plan.height, out);
        return true;
    }

    bool InterstellarService::renderTimelineFrame(const NodeId &tl, double t, int proxyEdge, Raster &out, bool *anyClip)
    {
        FramePlan plan;
        if (!planFrame(tl, t, proxyEdge, plan, anyClip)) return false;
        return executePlan(*mSync, plan, out);
    }

    bool InterstellarService::renderFrame(double t, int proxyEdge, Raster &out)
    {
        if (!mOpen) return false;
        // Preview quality caps the monitor's render edge (R-SET-3) — a cap, never an upscale. A
        // render and an export-still go through renderTimelineFrame at full size, untouched.
        if (mSettings.previewEdge > 0) proxyEdge = proxyEdge > 0 ? std::min(proxyEdge, mSettings.previewEdge) : mSettings.previewEdge;
        FramePlan plan;
        bool any = false;
        if (!planFrame(currentTimeline(), t, proxyEdge, plan, &any)) return false;
        if (!any)
        {
            FramePlan ref;
            if (planReferenceFrame(proxyEdge, ref)) plan = std::move(ref);
        }
        if (!mHost.asyncPreview) return executePlan(*mSync, plan, out);

        // The monitor on a worker (D-5): hand over the plan, answer at once with the newest
        // finished frame. When the requested frame lands, pump() raises frameSeq and the view
        // asks again — and gets it.
        PreviewWorker &w = *mPreview;
        std::lock_guard<std::mutex> l(w.mu);
        if (w.doneKey == plan.key && !w.done.empty()) { out = w.done; return true; }
        if (w.busyKey != plan.key && (!w.pending || w.pending->key != plan.key))
        {
            w.pending.reset(new FramePlan(std::move(plan)));
            w.cv.notify_one();
        }
        if (w.done.empty()) return false;   // nothing finished yet: the view shows its loading state
        out = w.done;
        return true;
    }

    void InterstellarService::previewLoop()
    {
        PreviewWorker &w = *mPreview;
        for (;;)
        {
            std::unique_ptr<FramePlan> plan;
            {
                std::unique_lock<std::mutex> l(w.mu);
                w.cv.wait(l, [&] { return w.stop || w.pending; });
                if (w.stop) return;
                plan = std::move(w.pending);
                w.busyKey = plan->key;
                if (w.resetSources)
                {
                    w.ctx.sources.clear();   // the worker's own decoders, cleared on the worker's thread
                    w.resetSources = false;
                }
            }
            Raster frame;
            const bool ok = executePlan(w.ctx, *plan, frame);
            {
                std::lock_guard<std::mutex> l(w.mu);
                w.busyKey.clear();
                if (ok)
                {
                    w.done = std::move(frame);
                    w.doneKey = plan->key;
                }
            }
            w.doneSeq.fetch_add(1);
        }
    }

    // ── delivery ────────────────────────────────────────────────────────────────────────────────

    bool InterstellarService::renderCommand(const Command &c)
    {
        if (c.kind == CK::RenderCancel)
        {
            for (auto &j : mJobs)
                if (j->model.id == c.arg(0))
                {
                    if (j->model.state != "queued" && j->model.state != "running") return fail("render cancel: " + c.arg(0) + " already " + j->model.state);
                    if (j->writer && j->begun) j->writer->end();
                    j->model.state = "cancelled";
                    emit(Event(EK::RenderFailed).with("job", j->model.id).with("timeline", j->model.timelineName).with("why", "cancelled"));
                    return true;
                }
            return fail("render cancel: no job " + c.arg(0));
        }
        // R-RENDER-1: a render NAMES its timeline. There is no implicit current one.
        if (!c.has("timeline")) return fail("render: --timeline is required — a render never implies the open tab (R-RENDER-1)");
        const NodeId tl = timelineRef(c.flag("timeline"));
        if (tl.empty()) return fail("render: no timeline named " + c.flag("timeline"));
        const std::string out = c.flag("out");
        if (out.empty()) return fail("render: --out <path> is required");
        std::string format = c.flag("format");
        const std::string ext = fs::path(out).extension().string();
        if (format.empty()) format = ext == ".mov" ? "prores" : ext == ".mp4" || ext == ".mkv" ? "h264" : "png-seq";
        if (format != "h264" && format != "prores" && format != "png-seq")
            return fail("render: --format is h264, prores or png-seq, got " + format);
        for (const auto &u : mProject->unrenderable())
            if (u.find(tl) != std::string::npos)
                return fail("render: " + u + " — refused rather than rendered without it");

        const double fps = mProject->fps;
        const double dur = timelineDuration(tl);
        double a = 0, b = dur;
        if (c.has("range"))
        {
            const std::string r = c.flag("range");
            const auto colon = r.find(':');
            if (colon == std::string::npos) return fail("render: --range a:b, in seconds");
            a = std::atof(r.substr(0, colon).c_str());
            b = std::atof(r.substr(colon + 1).c_str());
            if (!(b > a) || a < 0) return fail("render: --range needs 0 <= a < b");
        }
        auto job = std::make_unique<Job>();
        job->first = (long long)std::llround(a * fps);
        job->count = std::max<long long>(0, (long long)std::llround(b * fps) - job->first);
        if (job->count <= 0) return fail("render: timeline " + c.flag("timeline") + " is empty — nothing to render");
        job->png = format == "png-seq";
        if (!job->png)
        {
            if (!mHost.frameWriter) return fail("render: no encoder installed (the host must provide one)");
            job->writer = mHost.frameWriter();
        }
        else if (!mHost.writeImage)
            return fail("render: no PNG writer installed (the host must provide one)");
        job->model.id = "r" + std::to_string(mNextJob++);
        job->model.timeline = tl;
        job->model.timelineName = mProject->timeline(tl)->name;
        job->model.outPath = fs::absolute(out).string();
        job->model.format = format;
        job->model.total = (int)job->count;
        job->model.state = "queued";
        emit(Event(EK::RenderQueued).with("job", job->model.id).with("timeline", job->model.timelineName).with("out", job->model.outPath));
        mJobs.push_back(std::move(job));
        return true;
    }

    void InterstellarService::pumpJobs()
    {
        // One frame per pump for the first live job: a render yields between frames, so a window
        // stays responsive and a cancel lands on the next frame (R-RENDER-4).
        for (auto &jp : mJobs)
        {
            Job &j = *jp;
            if (j.model.state != "queued" && j.model.state != "running") continue;
            auto failJob = [&](const std::string &why) {
                j.model.state = "failed";
                j.model.error = why;
                if (j.writer && j.begun) j.writer->end();
                emit(Event(EK::RenderFailed).with("job", j.model.id).with("timeline", j.model.timelineName).with("why", why));
            };
            if (!mProject->timeline(j.model.timeline)) { failJob("its timeline was deleted"); return; }
            if (!j.begun)
            {
                j.model.state = "running";
                if (j.png)
                {
                    std::error_code ec;
                    fs::create_directories(j.model.outPath, ec);
                    if (ec) { failJob("cannot create " + j.model.outPath); return; }
                }
                else if (!j.writer || !j.writer->begin(j.model.outPath, mProject->width, mProject->height, mProject->fps, j.count))
                {
                    failJob("the encoder refused " + j.model.outPath);
                    return;
                }
                j.begun = true;
            }
            Raster frame;
            const double t = (double)(j.first + j.next) / mProject->fps;
            if (!renderTimelineFrame(j.model.timeline, t, 0, frame)) { failJob("frame " + std::to_string(j.next) + " failed"); return; }
            if (j.png)
            {
                char name[32];
                std::snprintf(name, sizeof name, "/frame_%06lld.png", j.first + j.next);
                std::string err;
                if (!mHost.writeImage(j.model.outPath + name, frame, err)) { failJob(err); return; }
            }
            else if (!j.writer->write(frame))
            {
                failJob("the encoder refused frame " + std::to_string(j.next));
                return;
            }
            ++j.next;
            j.model.done = (int)j.next;
            if (j.next % 24 == 0 || j.next == j.count)
                emit(Event(EK::RenderProgress).with("job", j.model.id).with("timeline", j.model.timelineName)
                         .with("done", (long long)j.next).with("total", (long long)j.count));
            if (j.next >= j.count)
            {
                if (j.writer && !j.writer->end()) { failJob("the encoder failed to finish"); return; }
                j.model.state = "done";
                mSync->grade->releaseScratch();
                emit(Event(EK::RenderFinished).with("job", j.model.id).with("timeline", j.model.timelineName)
                         .with("frames", (long long)j.count).with("out", j.model.outPath));
            }
            refreshModel();
            return;
        }
    }

    bool InterstellarService::exportStill(const Command &c)
    {
        if (!c.has("timeline")) return fail("export-still: --timeline is required (R-RENDER-1)");
        const NodeId tl = timelineRef(c.flag("timeline"));
        if (tl.empty()) return fail("export-still: no timeline named " + c.flag("timeline"));
        const std::string out = c.flag("out");
        if (out.empty()) return fail("export-still: --out <p.png> is required");
        if (!mHost.writeImage) return fail("export-still: no PNG writer installed");
        double t = mModel.playhead;
        if (c.has("at")) t = std::atof(c.flag("at").c_str());
        t = snapToFrame(std::max(0.0, t), mProject->fps);
        Raster frame;
        bool any = false;
        if (!renderTimelineFrame(tl, t, 0, frame, &any)) return false;
        std::string err;
        if (!mHost.writeImage(out, frame, err)) return fail("export-still: " + err);
        mOutput = out + "\n";
        emit(Event(EK::RenderFinished).with("job", "still").with("timeline", mProject->timeline(tl)->name).with("frames", 1).with("out", out));
        return true;
    }
}
}
