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
#include "Prescale.h"
#include "Project.h"
#include "Versions.h"
#include "volume/TemporalOps.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>

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

        std::string workingOf(const Project &P)
        {
            return render::colour::known(render::colour::workings(), P.colorspace) ? P.colorspace : std::string("rec709");
        }

        /** A transform once per (from, to): plans are made every frame and a transform is a few
         *  matrices. Null for identity, so the render path skips it without a call. */
        std::shared_ptr<const render::colour::Transform> cachedTransform(const std::string &key, const std::function<render::colour::Transform()> &make)
        {
            static std::mutex mu;
            static std::map<std::string, std::shared_ptr<const render::colour::Transform>> cache;
            std::lock_guard<std::mutex> l(mu);
            auto it = cache.find(key);
            if (it != cache.end()) return it->second;
            auto t = std::make_shared<render::colour::Transform>(make());
            std::shared_ptr<const render::colour::Transform> out = t->identity() ? nullptr : t;
            cache[key] = out;
            return out;
        }

        std::shared_ptr<const render::colour::Transform> inputTransform(const RackObj &ro, const std::string &working)
        {
            const std::string in = ro.input.empty() ? std::string("rec709") : ro.input;
            return cachedTransform("in|" + in + "|" + working, [&] { return render::colour::Transform::input(in, working); });
        }

        std::shared_ptr<const render::colour::Transform> outputTransform(const std::string &working, const std::string &output, double peak)
        {
            return cachedTransform("out|" + working + "|" + output + "|" + canonicalNumber(peak),
                                   [&] { return render::colour::Transform::output(working, output, peak); });
        }

        /** The grade weight: a continuous bypass, ungraded → graded per pixel (R-RACK-4). */
        void mixWeight(const Raster &ungraded, Raster &graded, double w)
        {
            if (w >= 1.0 || ungraded.width != graded.width || ungraded.height != graded.height) return;
            if (graded.deep() || ungraded.deep())
            {
                // R-COLOR-1: the deep frame mixes at 16 bits, the weight unquantised
                if (!graded.deep() || !ungraded.deep()) return;
                const double k = std::clamp(w, 0.0, 1.0);
                uint16_t *g = graded.rgba16.data();
                const uint16_t *u = ungraded.rgba16.data();
                const size_t n = std::min(graded.rgba16.size(), ungraded.rgba16.size());
                for (size_t i = 0; i < n; ++i) g[i] = (uint16_t)std::lround(u[i] + (g[i] - (double)u[i]) * k);
                return;
            }
            const int k = (int)std::lround(std::clamp(w, 0.0, 1.0) * 256.0);
            uint8_t *g = graded.rgba.data();
            const uint8_t *u = ungraded.rgba.data();
            const size_t n = graded.rgba.size();
            for (size_t i = 0; i < n; ++i) g[i] = (uint8_t)((u[i] * (256 - k) + g[i] * k + 128) >> 8);
        }
    }

    std::shared_ptr<const render::Lut> loadLut(const std::string &path, std::string &err, std::string *stamp)
    {
        static std::mutex mu;
        struct Entry { std::string version; std::shared_ptr<const render::Lut> lut; std::string err; };
        static std::map<std::string, Entry> cache;
        std::error_code ec;
        const auto size = fs::file_size(path, ec);
        if (ec) { err = "cannot read " + path; return nullptr; }
        const auto mtime = fs::last_write_time(path, ec).time_since_epoch().count();
        const std::string version = path + "@" + std::to_string((long long)size) + ":" + std::to_string((long long)mtime);
        if (stamp) *stamp = version;
        std::lock_guard<std::mutex> l(mu);
        auto it = cache.find(path);
        if (it == cache.end() || it->second.version != version)
        {
            // read once per version: an edited .cube on disk is read again, a plan never re-parses
            auto lut = std::make_shared<render::Lut>();
            Entry e;
            e.version = version;
            if (render::readCube(path, *lut, e.err)) e.lut = lut;
            it = cache.insert_or_assign(path, std::move(e)).first;
        }
        if (!it->second.lut) err = it->second.err;
        return it->second.lut;
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

    bool InterstellarService::decodeLayer(RenderCtx &ctx, const PlanLayer &l, Raster &out, bool deep)
    {
        Source *s = source(ctx, l.media);
        if (!s || !s->ok) return false;
        const long long last = std::max<long long>(0, s->volume->extent().frames - 1);
        const long long frame = std::clamp<long long>(l.frame, 0, last);
        // R-COLOR-1: a deep render decodes past the 8-bit volume, straight from the decoder at 16
        // bits. A temporal effect needs the volume's window, so that layer stays 8-bit and is
        // widened by the composite (stated in DR-COLOR-1).
        if (deep && (l.fxType.empty() || l.fxRadius <= 0 || s->volume->extent().frames <= 1))
            return s->src->frameAtDeep(frame, out) && !out.empty();
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
                                                EditParams &out, std::string &err, double srcT)
    {
        ColourTree tree;
        std::map<NodeId, int> idx;
        std::string source;
        if (!colourTreeFor(tl, tree, idx, source, err)) return false;
        const auto it = idx.find(rackObj);
        if (it == idx.end()) { err = "rack node " + rackObj + " is not in the " + source; return false; }
        if (srcT >= 0) applyColourCurves(tl, tree, idx, srcT);
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
            k += l.media + l.fxKey + l.effectsKey + buf;
            if (l.input) k += "|" + l.input->key();
            if (l.lut) k += "|lut:" + l.lutKey;
            if (l.nested) k += "{" + l.nested->key + "}";   // law 7: the nested picture is part of this one
        }
    }

    bool InterstellarService::planFrame(const NodeId &tl, double t, int proxyEdge, FramePlan &plan, bool *anyClip)
    {
        std::vector<NodeId> stack;
        return planFrameIn(tl, t, proxyEdge, plan, anyClip, stack);
    }

    bool InterstellarService::planFrameIn(const NodeId &tl, double t, int proxyEdge, FramePlan &plan, bool *anyClip, std::vector<NodeId> &stack)
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

        // the clip's own placement — geometry, fit, opacity, blend — over whatever picture it shows
        auto place = [&](PlanLayer &L, const Clip *c, const render::Active &a, double srcT) {
            const auto tr = tracks.find(c->track);
            const double trackOpacity = tr != tracks.end() ? tr->second->opacity : 1.0;
            auto cv = [&](const char *k, double v) { return curveAt(c->id, k, srcT, v); };
            // Clip offsets are stored in FRAME units, so a proxy and a full render place a clip
            // identically; the composite works in output pixels.
            L.layer.geom.x = cv("geom.x", c->geom.x) * plan.width;
            L.layer.geom.y = cv("geom.y", c->geom.y) * plan.height;
            L.layer.geom.scale = cv("geom.scale", c->geom.scale);
            L.layer.geom.rotation = cv("geom.rotation", c->geom.rotation);
            L.layer.geom.anchorX = cv("geom.anchor.x", c->geom.anchorX);
            L.layer.geom.anchorY = cv("geom.anchor.y", c->geom.anchorY);
            L.layer.geom.cropX = cv("geom.crop.x", c->geom.cropX);
            L.layer.geom.cropY = cv("geom.crop.y", c->geom.cropY);
            L.layer.geom.cropW = cv("geom.crop.w", c->geom.cropW);
            L.layer.geom.cropH = cv("geom.crop.h", c->geom.cropH);
            L.layer.fit = fitOf(c->fit);
            L.layer.opacity = std::clamp(cv("opacity", c->opacity) * trackOpacity * a.weight, 0.0, 1.0);
            L.layer.blend = blendOf(c->blend);
            L.layer.dissolveWithPrevious = a.dissolveWithPrevious;
        };

        for (const auto &a : active)
        {
            const Clip *c = clips[a.id];
            if (!c) continue;
            double localTime = a.localTime;
            // R-EDT-3: a ramped clip's source time is the integral of its speed, not in + offset × speed
            if (const anim::Ramp *rp = rampFor(*c); rp && t >= c->at - 1e-9 && t < c->end() + 1e-9) localTime = rp->sourceAt(t - c->at);
            if (P.timeline(c->src))
            {
                // R-EDT-4: a nested timeline — its frame at the clip's local time, at this frame's size.
                // A timeline already being planned around this one is a loop a hand-edited .isp made
                // (lint names it): it places nothing rather than recursing forever.
                if (std::find(stack.begin(), stack.end(), c->src) != stack.end() || c->src == tl) continue;
                long long f = (long long)std::floor(localTime * P.fps + 1e-6);
                for (const auto &fx : P.effects)
                    if (fx.type == "freeze" && fx.clip == c->id)
                        f = freezeRemap(f, (long long)std::floor((c->in + fx.at * c->speed) * P.fps + 1e-6));
                auto inner = std::make_shared<FramePlan>();
                bool innerAny = false;
                stack.push_back(tl);
                const bool ok = planFrameIn(c->src, (double)f / P.fps, proxyEdge, *inner, &innerAny, stack);
                stack.pop_back();
                if (!ok || !innerAny) continue;   // nothing cut there: the nested clip is transparent
                inner->output.reset();
                PlanLayer L;
                L.media = "timeline:" + c->src;
                L.frame = f;
                L.edge = proxyEdge;
                L.srcWidth = inner->width;
                L.nested = inner;
                place(L, c, a, std::max(0.0, localTime));
                plan.layers.push_back(std::move(L));
                continue;
            }
            const RackObj *ro = P.rackObj(c->src);
            if (!ro || ro->media.empty()) continue;   // offline: drawn as missing by the UI
            PlanLayer L;
            L.media = resolvePath(ro->media);
            Source *s = source(*mSync, L.media);   // info only — nothing decodes on this thread
            if (!s || !s->ok) continue;
            // Source frame from the clip's SOURCE time at the SOURCE's own rate (a 30p clip in a 24p
            // project steps at 30p); a still is frame 0 forever (R-VOL-6).
            const double srcFps = s->info.fps > 0 ? s->info.fps : P.fps;
            L.frame = s->info.frames <= 1 ? 0 : (long long)std::floor(localTime * srcFps + 1e-6);
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
            // R-ANIM: the rack's and the effects' curves at this SOURCE time; the clip's on its own footage clock
            const double srcT = std::max(0.0, localTime);
            if (!gradeFor(tl, ro->id, L.params, e, srcT)) L.params = EditParams{};   // an unbound node renders ungraded
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
            if (!partial.empty() && gradeForBypassing(tl, ro->id, partial, L.paramsGroupsOff, e, srcT))
            {
                L.groupMix = true;
                L.groupWeight = gw;
            }
            L.weight = std::clamp(ro->weight, 0.0, 1.0);
            // A partial mix needs the frames it mixes at one size — grade at source size and let
            // the composite scale.
            L.edge = (L.weight < 1.0 && L.weight > 0.0) || L.groupMix ? 0 : proxyEdge;
            effectChain(ro->id, L.effects, L.effectsKey, srcT);
            L.srcWidth = s->info.width;
            L.input = inputTransform(*ro, workingOf(P));
            if (!ro->lut.empty())
            {
                std::string why;
                L.lut = loadLut(resolvePath(ro->lut), why, &L.lutKey);   // unreadable: lint says so; the frame goes without
            }

            place(L, c, a, srcT);
            plan.layers.push_back(std::move(L));
        }
        if (anyClip) *anyClip = !plan.layers.empty();
        plan.key = std::to_string(plan.width) + "x" + std::to_string(plan.height);
        for (const auto &l : plan.layers) planKeyAppend(plan.key, l);
        // R-COLOR-4: the monitor's view is the Rec.709 output of the working space (nothing, in rec709)
        plan.output = outputTransform(workingOf(P), "rec709", 1000.0);
        if (plan.output) plan.key += "|" + plan.output->key();
        return true;
    }

    bool InterstellarService::planSourceFrame(const NodeId &roId, double t, int proxyEdge, FramePlan &plan)
    {
        // One rack source, graded as the open version resolves it, full frame: what Grade's monitor
        // shows (R-UI-3) and what the reference-frame slider previews (R-RACK-3). `t < 0` = the
        // source's chosen reference frame.
        plan = FramePlan{};
        const RackObj *ro = mProject->rackObj(roId);
        if (!ro || ro->media.empty()) return false;
        PlanLayer L;
        L.media = resolvePath(ro->media);
        Source *s = source(*mSync, L.media);
        if (!s || !s->ok) return false;
        const double fps = s->info.fps > 0 ? s->info.fps : 24.0;
        const double at = t < 0 ? ro->frame : t;
        L.frame = s->info.frames <= 1 ? 0 : std::clamp<long long>((long long)std::llround(at * fps), 0, s->info.frames - 1);
        std::string e;
        if (!gradeFor(currentTimeline(), roId, L.params, e, at)) return false;
        L.identity = render::GradeEngine::isIdentity(L.params);
        L.weight = std::clamp(ro->weight, 0.0, 1.0);
        L.edge = L.weight < 1.0 && L.weight > 0.0 ? 0 : proxyEdge;
        effectChain(roId, L.effects, L.effectsKey, at);
        L.srcWidth = s->info.width;
        L.input = inputTransform(*ro, workingOf(*mProject));
        if (!ro->lut.empty())
        {
            std::string why;
            L.lut = loadLut(resolvePath(ro->lut), why, &L.lutKey);
        }
        // The source's own shape, fitted to the long edge — not the project's: a portrait phone clip
        // is graded as itself.
        outputSize(s->info.width, s->info.height, proxyEdge, plan.width, plan.height);
        plan.layers.push_back(L);
        plan.key = "src|" + std::to_string(plan.width) + "x" + std::to_string(plan.height);
        planKeyAppend(plan.key, plan.layers.back());
        plan.output = outputTransform(workingOf(*mProject), "rec709", 1000.0);
        if (plan.output) plan.key += "|" + plan.output->key();
        return true;
    }

    bool InterstellarService::planReferenceFrame(int proxyEdge, FramePlan &plan)
    {
        // Nothing cut at t: the Grade target's reference frame, graded (DR-UI-9).
        plan = FramePlan{};
        if (!mModel.hasGradeTarget || mModel.selectedRack < 0) return false;
        return planSourceFrame(mModel.rack[(size_t)mModel.selectedRack].rackObj, -1.0, proxyEdge, plan);
    }

    bool InterstellarService::executePlan(RenderCtx &ctx, const FramePlan &plan, Raster &out, bool remember, bool deep)
    {
        // R-COLOR-1: a deep frame touches no cache (they hold what the monitor shows, at 8 bits)
        if (deep) remember = false;
        // R-GPU-1: this thread's engine follows the setting (each thread owns its engine and its
        // GL context; the GPU path declines to the CPU for any stage it has not ported)
        ctx.grade->setPreferGpu(mGpuWanted.load(std::memory_order_relaxed));
        // Graded frames must outlive the compose call: one slot per layer.
        std::vector<Raster> graded(plan.layers.size());
        std::vector<render::Layer> layers;
        for (size_t i = 0; i < plan.layers.size(); ++i)
        {
            const PlanLayer &l = plan.layers[i];
            if (l.nested)
            {
                // R-EDT-4: the nested timeline composited as a picture, then placed like any layer
                if (!executePlan(ctx, *l.nested, graded[i], remember, deep)) continue;
                render::Layer L = l.layer;
                L.src = &graded[i];
                layers.push_back(L);
                continue;
            }
            const bool ungradedOnly = l.weight <= 0.0 || (l.identity && !l.groupMix);
            render::FrameCache::Key key;
            key.source = l.media + l.fxKey + l.effectsKey + (l.input ? "|" + l.input->key() : std::string()) +
                         (l.lut ? "|lut:" + l.lutKey : std::string());
            key.sourceFrame = l.frame;
            key.paramHash = ungradedOnly ? render::FrameCache::kUngraded : render::hashParams(l.params);
            key.level = l.edge;
            // law 7: the cache keys on everything that changes the pixels — and the GPU's grade may
            // differ from the CPU's by a code value (R-GPU-1), so which one made a frame is part of it
            if (mGpuWanted.load(std::memory_order_relaxed) && !ungradedOnly) key.source += "|gpu";
            if (l.weight > 0.0 && l.weight < 1.0) key.source += "|w" + canonicalNumber(l.weight);
            if (l.groupMix && !ungradedOnly)
                key.source += "|g" + canonicalNumber(l.groupWeight) + ":" + std::to_string(render::hashParams(l.paramsGroupsOff));
            if (!remember || !mCache->get(key, graded[i]))
            {
                Raster ungraded;
                if (!decodeLayer(ctx, l, ungraded, deep)) continue;
                // a PREVIEW of a large source: shrink it (linear-light box) before the grade turns every
                // pixel into float — the 4K-at-640 cost (R-PLAY-2); a full-size render never takes this,
                // and nor does a deep one (the grade's own downscale sizes it, in float)
                if (l.edge > 0 && !ungraded.deep())
                {
                    Raster small;
                    if (render::prescale(ungraded, l.edge, small)) ungraded = std::move(small);
                }
                // R-COLOR-2: the source's space → the working space; Cosmo grades the result, and the
                // grade weight's "ungraded" is this picture, never the raw log
                if (l.input) l.input->apply(ungraded);
                if (l.lut) l.lut->apply(ungraded);   // R-COLOR-5: the source's input LUT, after its transform
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
                // the plugins after Cosmo (R-FX-5), sized in source pixels: scale to this frame
                if (!l.effects.empty() && !graded[i].empty())
                {
                    const double scale = l.srcWidth > 0 ? (double)graded[i].width / l.srcWidth : 1.0;
                    for (const auto &e : l.effects) render::applyEffect(e, scale, graded[i]);
                }
                if (remember) mCache->put(key, graded[i]);
            }
            render::Layer L = l.layer;
            L.src = &graded[i];
            layers.push_back(L);
        }
        render::compose(layers, plan.width, plan.height, out);
        if (plan.output) plan.output->apply(out);   // R-COLOR-4: the view, or a render's --output
        return true;
    }

    bool InterstellarService::renderTimelineFrame(const NodeId &tl, double t, int proxyEdge, Raster &out, bool *anyClip, bool deep)
    {
        FramePlan plan;
        if (!planFrame(tl, t, proxyEdge, plan, anyClip)) return false;
        return executePlan(*mSync, plan, out, true, deep);
    }

    int InterstellarService::playEdge(int requested) const
    {
        // Preview quality caps the monitor's render edge (R-SET-3) — a cap, never an upscale — and
        // while playing, the edge the read-ahead keeps up at caps it again (R-PLAY-2).
        int e = requested;
        if (mSettings.previewEdge > 0) e = e > 0 ? std::min(e, mSettings.previewEdge) : mSettings.previewEdge;
        if (mPlaying && mPlayEdge > 0) e = e > 0 ? std::min(e, mPlayEdge) : mPlayEdge;
        return e;
    }

    void InterstellarService::scheduleAhead()
    {
        if (!mAhead) return;
        const NodeId tl = currentTimeline();
        const double fps = mProject->fps > 0 ? mProject->fps : 24.0;
        const double dur = timelineDuration(tl);
        const int edge = playEdge(mLastMonitorEdge > 0 ? mLastMonitorEdge : 1600);
        const double now = mModel.playhead;
        const int workers = (int)mAhead->threads.size();
        const int horizon = workers * 2 + 2;
        // Aim where the playhead WILL be when a frame is done (its work time, smoothed), and when the
        // pool finishes fewer frames than the timeline shows, grade every n-th one so the ones it
        // finishes are on time — a lower picture rate, never a picture that lags.
        double workMs = 0;
        {
            std::lock_guard<std::mutex> l(mAhead->mu);
            workMs = mAhead->workMs;
        }
        const int lead = 1 + (int)std::ceil(workMs / 1000.0 * fps);
        int step = mPlayRate > 0 && mPlayRate < fps * 0.95 ? std::max(1, (int)std::ceil(fps / std::max(1.0, mPlayRate))) : 1;
        // R-EDT-2: the shuttle — frames ahead in the playback's direction, every |rate|-th at 2× and 4×
        const int dir = mShuttle < 0 ? -1 : 1;
        const int stride = std::max(1, (int)std::lround(std::fabs(mShuttle)));
        step *= stride;
        // plan on THIS thread (it reads the project and the rack), hand the work to the pool
        std::vector<AheadPool::Item> fresh;
        {
            std::lock_guard<std::mutex> l(mAhead->mu);
            // behind the playhead in the playback's direction: no longer wanted
            auto behind = [&](double t) { return dir > 0 ? t < now - 1e-6 : t > now + 1e-6; };
            for (auto q = mAhead->queue.begin(); q != mAhead->queue.end();) q = behind(q->t) ? mAhead->queue.erase(q) : std::next(q);
            for (auto it = mAhead->done.begin(); it != mAhead->done.end();)
            {
                if (dir > 0 ? it->second.first >= now - 2.0 / fps : it->second.first <= now + 2.0 / fps) { ++it; continue; }
                mAhead->doneFromCache.erase(it->first);
                it = mAhead->done.erase(it);
            }
        }
        // plan each frame once per (edge, project state): planning reads the rack and is not free
        if (edge != mAheadPlannedEdge || mModel.revision != mAheadPlannedRevision)
        {
            mAheadPlanned.clear();
            mAheadPlannedEdge = edge;
            mAheadPlannedRevision = mModel.revision;
        }
        const long long nowFrame = (long long)std::llround(now * fps);
        for (auto it = mAheadPlanned.begin(); it != mAheadPlanned.end();) it = (dir > 0 ? *it < nowFrame : *it > nowFrame) ? mAheadPlanned.erase(it) : std::next(it);
        for (int i = 0; i < horizon; ++i)
        {
            const long long f = nowFrame + dir * (lead * stride + (long long)i * step);
            if (f < 0) break;
            const double t = snapToFrame(f / fps, fps);
            if (dur > 0 && t >= dur) break;
            if (!mAheadPlanned.insert(f).second) continue;
            AheadPool::Item it;
            bool any = false;
            if (!planFrame(tl, t, edge, it.plan, &any) || !any) continue;
            it.key = it.plan.key;
            it.t = t;
            // R-PLAY-1: this frame in the preview cache, unchanged since it was cached → decode, don't grade
            FramePlan atCache;
            if (mPCache && planFrame(tl, t, cacheEdge(), atCache, nullptr))
                cacheLookup(f, atCache, it.cacheFile, it.cacheIndex, it.cacheW, it.cacheH);
            fresh.push_back(std::move(it));
        }
        {
            std::lock_guard<std::mutex> l(mAhead->mu);
            for (auto &it : fresh)
            {
                if (mAhead->done.count(it.key) || mAhead->busy.count(it.key)) continue;
                bool queued = false;
                for (const auto &q : mAhead->queue) queued = queued || q.key == it.key;
                if (!queued) mAhead->queue.push_back(std::move(it));
            }
        }
        mAhead->cv.notify_all();
        // keep up: if the pool finished fewer frames than the timeline asks for, grade smaller
        if (mNowMs - mRateFromMs >= 500.0)
        {
            const long long fin = mAhead->finished.load();
            mPlayRate = (fin - mRateFromDone) * 1000.0 / (mNowMs - mRateFromMs);
            mRateFromMs = mNowMs;
            mRateFromDone = fin;
            const int cap = mSettings.previewEdge > 0 ? mSettings.previewEdge : 1600;
            if (mPlayRate < fps * 0.9 && mPlayEdge > 640)
                mPlayEdge = mPlayEdge > 1280 ? 1280 : mPlayEdge > 960 ? 960 : 640;
            else if (mPlayRate > fps * 1.8 && mPlayEdge < std::min(cap, 1280))
                mPlayEdge = std::min(std::min(cap, 1280), mPlayEdge < 960 ? 960 : 1280);   // headroom: sharper again
        }
    }

    void InterstellarService::aheadLoop(size_t worker)
    {
        AheadPool &p = *mAhead;
        for (;;)
        {
            AheadPool::Item it;
            {
                std::unique_lock<std::mutex> l(p.mu);
                // with the GPU on, two workers feed it: more GL contexts add memory, not speed
                p.cv.wait(l, [&] { return p.stop || (!p.queue.empty() && (worker < 2 || !mGpuWanted.load())); });
                if (p.stop) return;
                it = std::move(p.queue.front());
                p.queue.pop_front();
                p.busy.insert(it.key);
                if (p.resetSources[worker])
                {
                    p.ctxs[worker]->sources.clear();   // this worker's own decoders, on its own thread
                    p.cacheSrcs[worker].clear();
                    p.resetSources[worker] = 0;
                }
            }
            Raster frame;
            const auto t0 = std::chrono::steady_clock::now();
            bool cached = false;
            if (!it.cacheFile.empty())
            {
                // R-PLAY-1: decode the graded frame from its segment (this worker's own decoder for it)
                auto &srcs = p.cacheSrcs[worker];
                auto s = srcs.find(it.cacheFile);
                if (s == srcs.end())
                {
                    if (srcs.size() >= 8) srcs.clear();
                    std::unique_ptr<IFrameSource> src = mHost.frameSource ? mHost.frameSource() : nullptr;
                    IFrameSource::Info info;
                    if (src && !src->open(it.cacheFile, info)) src.reset();
                    s = srcs.emplace(it.cacheFile, std::move(src)).first;
                }
                Raster dec;
                if (s->second && s->second->frameAt(it.cacheIndex, dec) && dec.width >= it.cacheW && dec.height >= it.cacheH)
                {
                    if (dec.width == it.cacheW && dec.height == it.cacheH) frame = std::move(dec);
                    else
                    {
                        frame.allocate(it.cacheW, it.cacheH);   // the encoder's even padding, cropped off
                        for (int y = 0; y < it.cacheH; ++y)
                            std::copy_n(&dec.rgba[(size_t)y * dec.width * 4], (size_t)it.cacheW * 4, &frame.rgba[(size_t)y * it.cacheW * 4]);
                    }
                    cached = true;
                }
            }
            const bool ok = cached || executePlan(*p.ctxs[worker], it.plan, frame);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            {
                std::lock_guard<std::mutex> l(p.mu);
                p.workMs = p.workMs <= 0 ? ms : p.workMs * 0.8 + ms * 0.2;
                p.busy.erase(it.key);
                if (ok) p.done[it.key] = {it.t, std::move(frame)};
                if (ok && cached) p.doneFromCache.insert(it.key);
                else p.doneFromCache.erase(it.key);
            }
            p.finished.fetch_add(1);
        }
    }

    bool InterstellarService::renderFrame(double t, int proxyEdge, Raster &out)
    {
        if (!mOpen) return false;
        if (proxyEdge > 0) mLastMonitorEdge = proxyEdge;
        proxyEdge = playEdge(proxyEdge);
        FramePlan plan;
        bool any = false;
        if (!planFrame(currentTimeline(), t, proxyEdge, plan, &any)) return false;
        if (!any)
        {
            FramePlan ref;
            if (planReferenceFrame(proxyEdge, ref)) plan = std::move(ref);
        }
        return present(std::move(plan), out);
    }

    bool InterstellarService::renderSourceFrame(const std::string &bind, double t, int proxyEdge, Raster &out)
    {
        if (!mOpen) return false;
        if (mSettings.previewEdge > 0) proxyEdge = proxyEdge > 0 ? std::min(proxyEdge, mSettings.previewEdge) : mSettings.previewEdge;
        FramePlan plan;
        if (!planSourceFrame(mProject->idForRef(bind), t, proxyEdge, plan)) return false;
        return present(std::move(plan), out);
    }

    bool InterstellarService::captureFrame(const std::string &bind, Raster &out)
    {
        // What the monitor shows, at FULL resolution, synchronously (R-UI-11): a source's reference
        // frame when one is named (Grade), else the current timeline at the playhead — or the Grade
        // target's reference frame when nothing is cut there.
        if (!mOpen) return false;
        FramePlan plan;
        if (!bind.empty())
        {
            if (!planSourceFrame(mProject->idForRef(bind), -1.0, 0, plan)) return fail("capture: " + bind + " has no frame to capture");
        }
        else
        {
            bool any = false;
            if (!planFrame(currentTimeline(), mModel.playhead, 0, plan, &any)) return false;
            if (!any && !planReferenceFrame(0, plan)) return fail("capture: nothing at the playhead and no Grade target");
        }
        return executePlan(*mSync, plan, out);
    }

    bool InterstellarService::present(FramePlan &&plan, Raster &out)
    {
        if (!mHost.asyncPreview) return executePlan(*mSync, plan, out);

        // Playing: the frame due now from the read-ahead ring (R-PLAY-2) — or, when it is not
        // ready, the newest ring frame not after it: dropping a frame, never stalling the picture.
        if (mPlaying && mAhead && plan.key.compare(0, 4, "src|") != 0)   // a timeline frame, not Grade's source
        {
            std::lock_guard<std::mutex> l(mAhead->mu);
            const double fps = mProject->fps > 0 ? mProject->fps : 24.0;
            const auto hit = mAhead->done.find(plan.key);
            if (hit != mAhead->done.end())
            {
                out = hit->second.second;
                ++mAheadHits;
                ++mAheadShown;
                mLastFromCache = mAhead->doneFromCache.count(plan.key) > 0;
                mCacheShown += mLastFromCache;
                return true;
            }
            ++mAheadMisses;
            const std::pair<double, Raster> *best = nullptr;
            const std::string *bestKey = nullptr;
            // the newest finished frame not past the playhead — in the playback's direction (R-EDT-2)
            const bool back = mShuttle < 0;
            for (const auto &kv : mAhead->done)
                if (back ? (kv.second.first >= mModel.playhead - 1e-6 && (!best || kv.second.first < best->first))
                         : (kv.second.first <= mModel.playhead + 1e-6 && (!best || kv.second.first > best->first)))
                {
                    best = &kv.second;
                    bestKey = &kv.first;
                }
            if (best)
            {
                out = best->second;
                mLastFromCache = mAhead->doneFromCache.count(*bestKey) > 0;
                mCacheShown += mLastFromCache;
                ++mAheadShown;
                mAheadLag += std::fabs(mModel.playhead - best->first) * fps;
                return true;
            }
        }

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
        std::string ext = fs::path(out).extension().string();
        for (char &ch : ext) ch = (char)std::tolower((unsigned char)ch);
        if (format.empty()) format = ext == ".mov" ? "prores" : ext == ".mp4" || ext == ".mkv" ? "h264" : "png-seq";
        if (format != "h264" && format != "h265" && format != "prores" && format != "dnxhr" && format != "png-seq")
            return fail("render: --format is h264, h265, prores, dnxhr or png-seq, got " + format);

        // ── the output spec (R-RENDER-6): every flag checked against the codec BEFORE queueing ──
        const bool lossy = format == "h264" || format == "h265";
        const bool inter = format == "prores" || format == "dnxhr";
        EncodeSpec spec;
        spec.codec = format;
        if (inter && ext != ".mov") return fail("render: " + format + " is written into .mov, not " + (ext.empty() ? "a folder" : ext));
        if (lossy && ext != ".mp4" && ext != ".mkv" && ext != ".mov")
            return fail("render: " + format + " is written into .mp4, .mkv or .mov, not " + (ext.empty() ? "a folder" : ext));
        if (c.has("profile"))
        {
            const std::string pf = c.flag("profile");
            static const std::set<std::string> proresP{"proxy", "lt", "standard", "hq", "4444"}, dnxP{"lb", "sq", "hq", "hqx", "444"};
            if (format == "prores" ? !proresP.count(pf) : format == "dnxhr" ? !dnxP.count(pf) : true)
                return fail(inter ? "render: --profile for " + format + " is " + (format == "prores" ? "proxy, lt, standard, hq or 4444" : "lb, sq, hq, hqx or 444") + ", got " + pf
                                  : "render: --profile applies to ProRes and DNxHR — " + format + " is chosen by --quality");
            spec.profile = pf;
        }
        else if (inter) spec.profile = format == "prores" ? "standard" : "hq";
        if (c.has("quality"))
        {
            if (!lossy) return fail("render: --quality applies to H.264 and H.265 — " + (inter ? format + "'s quality is its --profile" : std::string("a PNG sequence is lossless")));
            const std::string qs = c.flag("quality");   // flag() returns by value: keep it alive for `end`
            char *end = nullptr;
            const long q = std::strtol(qs.c_str(), &end, 10);
            if (!end || *end || q < 0 || q > 51) return fail("render: --quality is 0 (best) … 51, got " + c.flag("quality"));
            spec.quality = (int)q;
        }
        if (c.has("speed"))
        {
            static const std::set<std::string> speeds{"ultrafast", "superfast", "veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"};
            if (!lossy) return fail("render: --speed applies to H.264 and H.265");
            if (!speeds.count(c.flag("speed"))) return fail("render: --speed is ultrafast … veryslow, got " + c.flag("speed"));
            spec.speed = c.flag("speed");
        }
        // the encoder: the GPU's video unit when the setting (or the flag) asks and the codec has one
        spec.hardware = lossy && mSettings.hardwareVideo;
        if (c.has("encoder"))
        {
            const std::string e = c.flag("encoder");
            if (e != "software" && e != "hardware") return fail("render: --encoder is software or hardware, got " + e);
            if (e == "hardware" && !lossy) return fail("render: " + format + " has no hardware encoder — H.264 and H.265 do");
            spec.hardware = e == "hardware";
        }
        spec.bitDepth = format == "prores" ? 10 : format == "dnxhr" ? (spec.profile == "hqx" || spec.profile == "444" ? 10 : 8) : 8;
        if (c.has("bits"))
        {
            const std::string b = c.flag("bits");
            if (b != "8" && b != "10") return fail("render: --bits is 8 or 10, got " + b);
            const int bits = b == "10" ? 10 : 8;
            if (format == "h265") spec.bitDepth = bits;
            else if (bits != spec.bitDepth)
                return fail("render: " + format + (format == "dnxhr" ? " " + spec.profile : std::string()) + " is " + std::to_string(spec.bitDepth) +
                            "-bit" + (format == "h264" ? " here — H.265 offers 10-bit" : format == "dnxhr" ? " — the profile decides (hqx and 444 are 10-bit)" : ""));
        }

        // R-COLOR-4: the output colour transform; HDR needs 10 bits, and is signalled by the software encoder
        std::string output = "rec709";
        double peak = 1000.0;
        if (c.has("output"))
        {
            output = c.flag("output");
            if (!render::colour::known(render::colour::outputs(), output))
                return fail("render: --output is rec709, rec709-2.4, srgb, p3d65, pq or hlg, got " + output);
        }
        if (c.has("peak"))
        {
            if (output != "pq") return fail("render: --peak is PQ's mastering peak — this output is " + output);
            const std::string ps = c.flag("peak");
            char *end = nullptr;
            peak = std::strtod(ps.c_str(), &end);
            if (!end || *end || !(peak >= 400.0) || peak > 10000.0) return fail("render: --peak is 400 … 10000 cd/m², got " + ps);
        }
        if (render::colour::isHdr(output))
        {
            if (format == "png-seq" || format == "h264")
                return fail("render: HDR needs 10 bits — H.265 --bits 10, ProRes or DNxHR HQX/444; " +
                            std::string(format == "h264" ? "H.264 here is 8-bit" : "a PNG sequence here is 8-bit"));
            if (spec.bitDepth < 10)
            {
                if (format == "h265" && !c.has("bits")) spec.bitDepth = 10;   // HDR H.265 is 10-bit
                else return fail("render: HDR needs 10 bits — " + format + (format == "dnxhr" ? " " + spec.profile : std::string()) + " is 8-bit");
            }
            if (spec.hardware)
            {
                if (c.has("encoder")) return fail("render: HDR is encoded in software here — its mastering metadata is the software encoder's");
                spec.hardware = false;
            }
        }
        spec.output = output;
        spec.peak = peak;
        // R-AUD-9: every video render carries the master when the timeline has sound to carry
        {
            render::AudioPlan ap;
            if (format != "png-seq" && mHost.audioSource && planAudio(tl, ap) && !ap.empty()) spec.audioRate = ap.rate;
        }

        // size: never above the project, and its aspect (a reframe is not in v1)
        const int PW = mProject->width, PH = mProject->height;
        int W = PW, H = PH, proxyEdge = 0;
        if (c.has("res"))
        {
            int rw = 0, rh = 0;
            if (std::sscanf(c.flag("res").c_str(), "%dx%d", &rw, &rh) != 2 || rw <= 0 || rh <= 0) return fail("render: --res is WxH, got " + c.flag("res"));
            if (rw > PW || rh > PH)
                return fail("render: --res " + c.flag("res") + " is larger than the project (" + std::to_string(PW) + "x" + std::to_string(PH) + ") — a render never upscales");
            int ow = 0, oh = 0;
            outputSize(PW, PH, std::max(rw, rh), ow, oh);
            if (std::abs(ow - rw) > 1 || std::abs(oh - rh) > 1)
                return fail("render: --res " + c.flag("res") + " changes the project's aspect (" + std::to_string(ow) + "x" + std::to_string(oh) + " keeps it) — a reframe is not in v1");
            W = ow;
            H = oh;
            proxyEdge = std::max(rw, rh) >= std::max(PW, PH) ? 0 : std::max(rw, rh);
        }
        if (format != "png-seq" && ((W % 2) || (H % 2)))
            return fail("render: " + format + " needs even dimensions, " + std::to_string(W) + "x" + std::to_string(H) + " is not");
        double fps = mProject->fps;
        if (c.has("fps"))
        {
            // a rate, or an exact fraction — 24000/1001 is 23.976 exactly, which "23.976" is not
            const std::string f = c.flag("fps");
            const auto slash = f.find('/');
            char *end = nullptr, *end2 = nullptr;
            fps = std::strtod(f.substr(0, slash).c_str(), &end);
            bool ok = end && !*end;
            if (slash != std::string::npos)
            {
                const double den = std::strtod(f.substr(slash + 1).c_str(), &end2);
                ok = ok && end2 && !*end2 && den > 0.0;
                if (ok) fps /= den;
            }
            if (!ok || !(fps > 0.0) || fps > 240.0) return fail("render: --fps is a rate in (0, 240] or num/den, got " + f);
        }
        for (const auto &u : mProject->unrenderable())
            if (u.find(tl) != std::string::npos)
                return fail("render: " + u + " — refused rather than rendered without it");

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
        job->spec = spec;
        job->width = W;
        job->height = H;
        job->proxyEdge = proxyEdge;
        job->fps = fps;
        job->output = outputTransform(workingOf(*mProject), output, peak);
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
        job->model.width = W;
        job->model.height = H;
        job->model.fps = fps;
        {
            // the spec in words, for the queue row — what was asked, all of it
            const std::string name = format == "h264" ? "H.264" : format == "h265" ? "H.265" : format == "prores" ? "ProRes" : format == "dnxhr" ? "DNxHR" : "PNG sequence";
            std::string w = name;
            if (inter) { std::string pf = spec.profile; for (char &ch : pf) ch = (char)std::toupper((unsigned char)ch); w += " " + (pf == "STANDARD" ? std::string("422") : pf); }
            if (format == "h265" || inter) w += " " + std::to_string(spec.bitDepth) + "-bit";
            if (lossy) w += " \xC2\xB7 q" + std::to_string(spec.quality) + " \xC2\xB7 " + (spec.hardware ? std::string("hardware") : spec.speed);
            if (spec.audioRate > 0)
                w += std::string(" \xC2\xB7 ") + (inter ? "PCM 24-bit " : "AAC ") + std::to_string(spec.audioRate / 1000) + " kHz";
            if (output != "rec709")
                w += std::string(" \xC2\xB7 ") + render::colour::label(render::colour::outputs(), output) +
                     (output == "pq" ? " " + std::to_string((long long)std::llround(peak)) + " cd/m\xC2\xB2" : std::string());
            char rate[32];
            std::snprintf(rate, sizeof rate, "%.3f", fps);
            std::string r = rate;
            while (!r.empty() && r.back() == '0') r.pop_back();
            if (!r.empty() && r.back() == '.') r.pop_back();
            w += " \xC2\xB7 " + std::to_string(W) + "\xC3\x97" + std::to_string(H) + " \xC2\xB7 " + r + " fps";
            job->model.spec = w;
        }
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
                else if (!j.writer || !j.writer->begin(j.model.outPath, j.width, j.height, j.fps, j.count, j.spec))
                {
                    failJob("the encoder refused " + j.model.outPath);
                    return;
                }
                // what the writer did differently (a hardware encode that fell back), said in the row
                if (j.writer && !j.writer->note().empty())
                {
                    j.model.spec += " \xC2\xB7 " + j.writer->note();
                    emit(Event(EK::Info).with("text", "render " + j.model.id + ": " + j.writer->note()));
                }
                j.begun = true;
            }
            Raster frame;
            // the timeline is SAMPLED at the output rate (R-RENDER-6) — still a pure function of t
            const double t = (double)(j.first + j.next) / j.fps;
            // R-COLOR-1: a codec that keeps more than 8 bits gets a picture that has them — decoded,
            // graded, composited and handed to the encoder at 16 bits per component
            const bool deep = !j.png && j.spec.bitDepth > 8;
            FramePlan plan;
            if (!planFrame(j.model.timeline, t, j.proxyEdge, plan, nullptr)) { failJob("frame " + std::to_string(j.next) + " failed"); return; }
            plan.output = j.output;   // R-COLOR-4: the render's --output, not the monitor's view
            if (!executePlan(*mSync, plan, frame, true, deep)) { failJob("frame " + std::to_string(j.next) + " failed"); return; }
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
            if (!j.png && j.spec.audioRate > 0)
            {
                // exactly the samples [k·rate/fps, (k+1)·rate/fps) of output frame k — sample-accurate,
                // and the rounding never drifts because both ends come from the frame number
                render::AudioPlan ap;
                planAudio(j.model.timeline, ap);
                ap.rate = j.spec.audioRate;
                const long long k = j.first + j.next;
                const long long s0 = std::llround((double)k * ap.rate / j.fps), s1 = std::llround((double)(k + 1) * ap.rate / j.fps);
                std::vector<float> pcm((size_t)std::max<long long>(0, s1 - s0) * 2);
                render::mixAudio(ap, [&](const std::string &m) { return audioSourceFor(*mSync, m, ap.rate); }, s0, (int)(s1 - s0), pcm.data());
                if (!j.writer->writeAudio(pcm.data(), (int)(s1 - s0))) { failJob("the encoder refused the sound of frame " + std::to_string(j.next)); return; }
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

    bool InterstellarService::lutExport(const Command &c)
    {
        // R-COLOR-6: the colour of one source — everything per-pixel between its file and the working
        // space (or an output) — evaluated on a lattice at 16 bits and written as a 3D .cube
        const RackObj *ro = mProject->rackObj(mProject->idForRef(c.arg(0)));
        if (!ro) return fail("lut export: no rack node named " + c.arg(0));
        if (ro->kind == "group") return fail("lut export: " + ro->name + " is a group — a LUT is baked from a source");
        const std::string out = c.flag("out");
        if (out.empty()) return fail("lut export: --out <file.cube> is required");
        int n = 33;
        if (c.has("size"))
        {
            const std::string ns = c.flag("size");
            char *end = nullptr;
            const long v = std::strtol(ns.c_str(), &end, 10);
            if (!end || *end || v < 2 || v > 129) return fail("lut export: --size is 2 … 129, got " + ns);
            n = (int)v;
        }
        const std::string output = c.has("output") ? c.flag("output") : std::string();
        if (!output.empty() && !render::colour::known(render::colour::outputs(), output))
            return fail("lut export: --output is rec709, rec709-2.4, srgb, p3d65, pq or hlg, got " + output);

        EditParams p;
        std::string err;
        if (!gradeFor(currentTimeline(), ro->id, p, err, ro->frame)) return fail("lut export: " + err);
        // not colour, or not per-pixel: a LUT cannot hold them, so they are left out (and said so)
        p.texture = p.clarity = p.dehaze = 0;
        p.grainAmount = 0;
        p.sharpenAmount = 0;
        p.nrLuminance = p.nrColor = 0;
        p.lensDistortion = p.lensCA = p.lensVignette = 0;
        p.cropX = p.cropY = 0;
        p.cropW = p.cropH = 1;
        p.rotation = 0;
        p.quarterTurns = 0;
        p.masks.clear();
        p.mixerSpread = 0;   // the mixer's neighbourhood reach: per pixel, its strict answer

        // the lattice: red fastest, then green, then blue — the .cube's own order, one row per blue
        Raster lattice;
        lattice.allocate16(n * n, n, 65535);
        for (int b = 0; b < n; ++b)
            for (int g = 0; g < n; ++g)
                for (int r = 0; r < n; ++r)
                {
                    uint16_t *px = &lattice.rgba16[(((size_t)b * n + g) * n + r) * 4];
                    px[0] = (uint16_t)std::lround(65535.0 * r / (n - 1));
                    px[1] = (uint16_t)std::lround(65535.0 * g / (n - 1));
                    px[2] = (uint16_t)std::lround(65535.0 * b / (n - 1));
                }
        const std::string working = workingOf(*mProject);
        if (auto t = inputTransform(*ro, working)) t->apply(lattice);
        std::string lutNote;
        if (!ro->lut.empty())
        {
            std::string why;
            if (auto l = loadLut(resolvePath(ro->lut), why)) l->apply(lattice);
            else return fail("lut export: the input LUT does not read — " + why);
            lutNote = ", its input LUT " + fs::path(ro->lut).filename().string();
        }
        Raster graded;
        if (!mSync->grade->render(lattice, p, !render::GradeEngine::isIdentity(p), 0, graded) || !graded.deep() ||
            graded.width != n * n || graded.height != n)
            return fail("lut export: the grade did not render the lattice");
        if (ro->weight < 1.0)
        {
            const double k = std::clamp(ro->weight, 0.0, 1.0);
            for (size_t i = 0; i < graded.rgba16.size(); ++i)
                graded.rgba16[i] = (uint16_t)std::lround(lattice.rgba16[i] + (graded.rgba16[i] - (double)lattice.rgba16[i]) * k);
        }
        std::vector<render::EffectRun> fx;
        std::string fxKey;
        effectChain(ro->id, fx, fxKey, ro->frame);
        int luts = 0;
        for (const auto &e : fx)
            if (e.type == "lut.cube" && e.lut) { render::applyEffect(e, 1.0, graded); ++luts; }   // colour; the blurs are not
        if (!output.empty()) render::colour::Transform::output(working, output, 1000.0).apply(graded);

        render::Lut lut;
        lut.title = ro->name + " (Interstellar)";
        lut.size3d = n;
        lut.table.resize((size_t)n * n * n * 3);
        for (size_t i = 0, cnt = (size_t)n * n * n; i < cnt; ++i)
            for (int k = 0; k < 3; ++k) lut.table[i * 3 + k] = graded.rgba16[i * 4 + k] / 65535.0f;
        const std::string inLabel = render::colour::label(render::colour::inputs(), ro->input.empty() ? "rec709" : ro->input);
        std::vector<std::string> comments = {
            "Interstellar: the colour of " + ro->name + ", baked from its grade on timeline " + mProject->timeline(currentTimeline())->name,
            "In: " + std::string(inLabel) + " code values as decoded" + lutNote,
            "Out: " + std::string(render::colour::label(render::colour::workings(), working)) +
                (output.empty() ? std::string() : std::string(" through the ") + render::colour::label(render::colour::outputs(), output) + " output"),
            "Included: the input transform, Cosmo's per-pixel stages, the grade weight" + std::string(luts ? ", its LUT effects" : ""),
            "Left out (not per-pixel colour): texture, clarity, dehaze, sharpening, noise reduction, grain, lens, crop and rotation, masks;",
            "  the colour mixer's neighbourhood spread is taken as 0, its strict per-pixel answer",
        };
        if (!render::writeCube(out, lut, comments, err)) return fail("lut export: " + err);
        mOutput = out + "\n";
        emit(Event(EK::Info).with("text", "lut export: " + ro->name + " → " + out));
        return true;
    }
}
}
