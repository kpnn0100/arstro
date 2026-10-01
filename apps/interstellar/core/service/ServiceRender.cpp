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

    InterstellarService::Source *InterstellarService::source(const std::string &media)
    {
        auto it = mSources.find(media);
        if (it != mSources.end()) return it->second.get();
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
        mSources[media] = std::move(s);
        return out;
    }

    bool InterstellarService::sourceFrame(const std::string &media, long long frame, const std::vector<int> &fxIdx,
                                          Raster &out)
    {
        Source *s = source(media);
        if (!s || !s->ok) return false;
        const long long last = std::max<long long>(0, s->volume->extent().frames - 1);
        frame = std::clamp<long long>(frame, 0, last);

        // v1 renders the FIRST temporal effect on a source; lint reports any after it.
        const Fx *fx = nullptr;
        for (int i : fxIdx)
            if (!fx) fx = &mProject->effects[(size_t)i];
        if (fx && fx->radius > 0 && s->volume->extent().frames > 1)
        {
            FrameRGBA f;
            bool ok = false;
            if (fx->type == "denoise")
            {
                TemporalDenoise op;
                op.setRadius(fx->radius);
                op.setStrength((float)fx->strength);
                ok = renderTemporal(*s->volume, op, frame, f);
            }
            else
            {
                FrameBlend op;
                op.setRadius(fx->radius);
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

    bool InterstellarService::renderTimelineFrame(const NodeId &tl, double t, int proxyEdge, Raster &out, bool *anyClip)
    {
        if (anyClip) *anyClip = false;
        if (!mOpen) return false;
        const Project &P = *mProject;
        ResolvedTimeline R;
        std::string err;
        if (!resolved(tl, R, err)) return fail("render: " + err);

        int ow = 0, oh = 0;
        outputSize(P.width, P.height, proxyEdge, ow, oh);

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

        // Graded frames must outlive the compose call: one slot per active clip.
        std::vector<Raster> graded(active.size());
        std::vector<render::Layer> layers;
        std::map<NodeId, EditParams> grades;
        for (size_t i = 0; i < active.size(); ++i)
        {
            const render::Active &a = active[i];
            const Clip *c = clips[a.id];
            const RackObj *ro = c ? P.rackObj(c->src) : nullptr;
            if (!c || !ro || ro->media.empty()) continue;   // offline: drawn as missing by the UI
            const std::string media = resolvePath(ro->media);
            Source *s = source(media);
            if (!s || !s->ok) continue;

            // Source frame from the clip's SOURCE time at the SOURCE's own rate (a 30p clip in a 24p
            // project steps at 30p); a still is frame 0 forever (R-VOL-6).
            const double srcFps = s->info.fps > 0 ? s->info.fps : P.fps;
            long long frame = s->info.frames <= 1 ? 0 : (long long)std::floor(a.localTime * srcFps + 1e-6);
            std::vector<int> fxIdx;
            std::string fxKey;
            for (size_t k = 0; k < P.effects.size(); ++k)
            {
                const Fx &fx = P.effects[k];
                if (fx.type == "freeze" && fx.clip == c->id)
                    frame = freezeRemap(frame, (long long)std::floor((c->in + fx.at * c->speed) * srcFps + 1e-6));
                else if (fx.type != "freeze" && fx.node == ro->id)
                {
                    fxIdx.push_back((int)k);
                    if (fxKey.empty())
                        fxKey = "|" + fx.type + ":" + std::to_string(fx.radius) + ":" + canonicalNumber(fx.strength);
                }
            }

            if (!grades.count(ro->id))
            {
                EditParams p;
                std::string e;
                if (!gradeFor(tl, ro->id, p, e)) p = EditParams{};   // an unbound node renders ungraded
                grades[ro->id] = p;
            }
            const EditParams &p = grades[ro->id];
            const bool identity = render::GradeEngine::isIdentity(p);
            const double weight = std::clamp(ro->weight, 0.0, 1.0);
            // A partial weight mixes ungraded and graded pixels, which needs both at one size —
            // so it grades at source size and lets the composite scale.
            const int edge = weight < 1.0 && weight > 0.0 ? 0 : proxyEdge;
            render::FrameCache::Key key;
            key.source = media + fxKey;
            key.sourceFrame = frame;
            key.paramHash = (identity || weight <= 0.0) ? render::FrameCache::kUngraded : render::hashParams(p);
            key.level = edge;
            if (weight > 0.0 && weight < 1.0) key.source += "|w" + canonicalNumber(weight);
            if (!mCache->get(key, graded[i]))
            {
                Raster ungraded;
                if (!sourceFrame(media, frame, fxIdx, ungraded)) continue;
                if (key.paramHash == render::FrameCache::kUngraded) graded[i] = std::move(ungraded);
                else
                {
                    if (!mGrade->render(ungraded, p, true, edge, graded[i])) continue;
                    mixWeight(ungraded, graded[i], weight);
                }
                mCache->put(key, graded[i]);
            }

            const Track *tr = tracks[c->track];
            render::Layer L;
            L.src = &graded[i];
            // Clip offsets are stored in FRAME units, so a proxy and a full render place a clip
            // identically; the composite works in output pixels.
            L.geom.x = c->geom.x * ow;
            L.geom.y = c->geom.y * oh;
            L.geom.scale = c->geom.scale;
            L.geom.rotation = c->geom.rotation;
            L.geom.anchorX = c->geom.anchorX;
            L.geom.anchorY = c->geom.anchorY;
            L.geom.cropX = c->geom.cropX;
            L.geom.cropY = c->geom.cropY;
            L.geom.cropW = c->geom.cropW;
            L.geom.cropH = c->geom.cropH;
            L.fit = fitOf(c->fit);
            L.opacity = std::clamp(c->opacity * (tr ? tr->opacity : 1.0) * a.weight, 0.0, 1.0);
            L.blend = blendOf(c->blend);
            L.dissolveWithPrevious = a.dissolveWithPrevious;
            layers.push_back(L);
        }
        if (anyClip) *anyClip = !layers.empty();
        render::compose(layers, ow, oh, out);
        return true;
    }

    bool InterstellarService::renderFrame(double t, int proxyEdge, Raster &out)
    {
        if (!mOpen) return false;
        bool any = false;
        if (!renderTimelineFrame(currentTimeline(), t, proxyEdge, out, &any)) return false;
        if (any) return true;
        // Nothing is cut at t: show the Grade target's reference frame, graded, so grading works
        // before a single clip exists.
        if (!mModel.hasGradeTarget || mModel.selectedRack < 0) return true;
        const NodeId roId = mModel.rack[(size_t)mModel.selectedRack].rackObj;
        const RackObj *ro = mProject->rackObj(roId);
        if (!ro || ro->media.empty()) return true;
        const std::string media = resolvePath(ro->media);
        Source *s = source(media);
        if (!s || !s->ok) return true;
        const long long frame = s->info.frames <= 1 ? 0 : (long long)std::llround(ro->frame * (s->info.fps > 0 ? s->info.fps : 24.0));
        Raster raw;
        if (!sourceFrame(media, frame, {}, raw)) return true;
        EditParams p;
        std::string e;
        if (!gradeFor(currentTimeline(), roId, p, e)) return true;
        Raster g;
        if (!mGrade->render(raw, p, !render::GradeEngine::isIdentity(p), proxyEdge, g)) return true;
        int ow = 0, oh = 0;
        outputSize(mProject->width, mProject->height, proxyEdge, ow, oh);
        render::Layer L;
        L.src = &g;
        render::compose({L}, ow, oh, out);
        return true;
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
                mGrade->releaseScratch();
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
