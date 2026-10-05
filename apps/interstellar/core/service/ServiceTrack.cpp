/*
 *  interstellar_core — tracking a window (R-CLR-2).
 *
 *  `track window <effect>` makes a window FOLLOW what is under it: from the playhead's source frame,
 *  forward (or `--back`) to `--to` (or the source's end / start), each next frame is searched for the
 *  patch the window covers — the least sum of absolute differences of luma, within a reach around
 *  where it was — and the window's centre is KEYED there (R-ANIM): a track is ordinary keys, edited,
 *  copied and undone like any. The patch is taken again where it was found each frame, so a slowly
 *  changing subject is followed (a fast change of appearance drifts — said in DR-CLR-2).
 *
 *  The search runs on the source's decoded frames at an analysis size (a long edge of 320 px at
 *  most), one frame per pump like a render, so a window stays responsive. The whole track undoes as
 *  one step, from the state when it began.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include "Versions.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        constexpr int kAnalysisEdge = 320;

        // luma at the analysis size: an area average of Rec.709 luma
        void lumaAt(const Raster &in, int aw, int ah, std::vector<float> &out)
        {
            out.assign((size_t)aw * ah, 0.0f);
            const double sx = (double)in.width / aw, sy = (double)in.height / ah;
            for (int y = 0; y < ah; ++y)
                for (int x = 0; x < aw; ++x)
                {
                    const int x0 = (int)(x * sx), x1 = std::max(x0 + 1, (int)((x + 1) * sx)), y0 = (int)(y * sy), y1 = std::max(y0 + 1, (int)((y + 1) * sy));
                    double acc = 0;
                    int n = 0;
                    for (int v = y0; v < std::min(in.height, y1); ++v)
                        for (int u = x0; u < std::min(in.width, x1); ++u)
                        {
                            const uint8_t *p = &in.rgba[((size_t)v * in.width + u) * 4];
                            acc += 0.2126 * p[0] + 0.7152 * p[1] + 0.0722 * p[2];
                            ++n;
                        }
                    out[(size_t)y * aw + x] = (float)(n ? acc / n : 0.0);
                }
        }

        void patchAt(const std::vector<float> &img, int aw, int ah, int cx, int cy, int tw, int th, std::vector<float> &out)
        {
            out.assign((size_t)tw * th, 0.0f);
            for (int y = 0; y < th; ++y)
                for (int x = 0; x < tw; ++x)
                {
                    const int u = std::clamp(cx - tw / 2 + x, 0, aw - 1), v = std::clamp(cy - th / 2 + y, 0, ah - 1);
                    out[(size_t)y * tw + x] = img[(size_t)v * aw + u];
                }
        }
    }

    bool InterstellarService::trackCommand(const Command &c)
    {
        Project &P = *mProject;
        if (c.kind == CK::TrackCancel)
        {
            bool any = false;
            for (auto &j : mTrackJobs)
                if (j->model.state == "queued" || j->model.state == "running")
                {
                    j->model.state = "cancelled";
                    any = true;
                    if (j->before) recordEdit(j->cmd, *j->before);   // what was keyed so far undoes as one step
                }
            if (!any) return fail("track cancel: no track is running");
            refreshModel();
            return true;
        }
        const Effect *e = P.effect(P.idForRef(c.arg(0)));
        if (!e || e->type != "window.shape") return fail("track window: " + c.arg(0) + " is not a window (`effect add <source> --type window.shape`)");
        const RackObj *ro = P.rackObj(e->node);
        if (!ro || ro->kind == "group" || ro->media.empty())
            return fail("track window: " + e->id + " is on a group — a track follows one source's pictures; put the window on the source");
        for (const auto &j : mTrackJobs)
            if (j->model.state == "queued" || j->model.state == "running") return fail("track window: a track is running — `track cancel` first");
        if (!mHost.frameSource) return fail("track window: this build has no decoder");
        auto j = std::make_unique<TrackJob>();
        j->model.id = "t" + std::to_string(++mTrackSeq);
        j->model.effect = e->id;
        j->model.backward = c.has("back");
        j->model.state = "queued";
        j->media = resolvePath(ro->media);
        // from the playhead's frame of this source when a clip of it is under the playhead (Cut), else
        // from its reference frame (Grade's clock)
        j->from = sourceNow(ro->id);
        {
            ResolvedTimeline R;
            std::string err;
            const double t = mModel.playhead;
            if (resolved(currentTimeline(), R, err))
                for (const auto &cl : R.clips)
                    if (cl.src == ro->id && t >= cl.at - 1e-9 && t < cl.end() - 1e-9)
                    {
                        const anim::Ramp *rp = rampFor(cl);
                        j->from = rp ? rp->sourceAt(t - cl.at) : cl.in + (t - cl.at) * cl.speed;
                    }
        }
        j->to = -1.0;
        if (c.has("to"))
        {
            const std::string v = c.flag("to");
            char *end = nullptr;
            j->to = std::strtod(v.c_str(), &end);
            if (!end || *end || j->to < 0) return fail("track window: --to is source seconds, got " + v);
        }
        j->cmd = c;
        j->before = std::make_shared<UndoState>();
        captureState(*j->before);
        mOutput = j->model.id + "\n";
        mTrackJobs.push_back(std::move(j));
        refreshModel();
        return true;
    }

    void InterstellarService::pumpTracks()
    {
        for (auto &jp : mTrackJobs)
        {
            TrackJob &j = *jp;
            if (j.model.state != "queued" && j.model.state != "running") continue;
            struct Refresh { InterstellarService &s; ~Refresh() { s.refreshModel(); } } refresh{*this};
            auto finish = [&](const std::string &state, const std::string &why) {
                j.model.state = state;
                j.model.error = why;
                j.source.reset();
                if (j.before) recordEdit(j.cmd, *j.before);   // the whole track: one undo step
                emit(Event(EK::Info).with("text", "track " + j.model.id + " " + state + (why.empty() ? "" : ": " + why)));
            };
            const Effect *e = mProject->effect(j.model.effect);
            if (!e) { finish("failed", "its window was removed"); return; }
            AnimTarget tx, ty;
            std::string why;
            if (!animTarget(e->id + ".centerX", tx, why) || !animTarget(e->id + ".centerY", ty, why)) { finish("failed", why); return; }
            Raster frame;
            if (!j.begun)
            {
                j.source = mHost.frameSource();
                IFrameSource::Info info;
                if (!j.source || !j.source->open(j.media, info) || !info.valid() || info.frames < 2) { finish("failed", "cannot open " + j.media); return; }
                j.fps = info.fps > 0 ? info.fps : 24.0;
                const double scale = std::min(1.0, (double)kAnalysisEdge / std::max(info.width, info.height));
                j.aw = std::max(8, (int)std::lround(info.width * scale));
                j.ah = std::max(8, (int)std::lround(info.height * scale));
                j.frame = std::clamp<long long>((long long)std::llround(j.from * j.fps), 0, info.frames - 1);
                const long long last = info.frames - 1;
                j.end = j.to >= 0 ? std::clamp<long long>((long long)std::llround(j.to * j.fps), 0, last) : (j.model.backward ? 0 : last);
                if (j.model.backward ? j.end >= j.frame : j.end <= j.frame) { finish("failed", "nothing to track that way from here"); return; }
                j.model.total = std::llabs(j.end - j.frame);
                if (!j.source->frameAt(j.frame, frame)) { finish("failed", "cannot decode the first frame"); return; }
                std::vector<float> luma;
                lumaAt(frame, j.aw, j.ah, luma);
                const double t0 = j.frame / j.fps;
                j.cx = curveAt(e->id, "centerX", t0, staticValue(tx));
                j.cy = curveAt(e->id, "centerY", t0, staticValue(ty));
                // the patch: what the window covers, at least 8 analysis pixels a side
                double w = 0.5, h = 0.5;
                for (const auto &kv : e->unknown)
                {
                    if (kv.first == "width") w = std::atof(kv.second.c_str());
                    if (kv.first == "height") h = std::atof(kv.second.c_str());
                }
                w = curveAt(e->id, "width", t0, w);
                h = curveAt(e->id, "height", t0, h);
                j.tw = std::clamp((int)std::lround(w * j.aw), 8, j.aw);
                j.th = std::clamp((int)std::lround(h * j.ah), 8, j.ah);
                patchAt(luma, j.aw, j.ah, (int)std::lround(j.cx * j.aw - 0.5), (int)std::lround(j.cy * j.ah - 0.5), j.tw, j.th, j.patch);
                // the start is keyed too, so the track holds where it began
                upsertKey(tx, t0, j.cx, nullptr, nullptr);
                upsertKey(ty, t0, j.cy, nullptr, nullptr);
                j.begun = true;
                j.model.state = "running";
                return;
            }
            j.frame += j.model.backward ? -1 : 1;
            if (!j.source->frameAt(j.frame, frame)) { finish("failed", "cannot decode frame " + std::to_string(j.frame)); return; }
            std::vector<float> luma;
            lumaAt(frame, j.aw, j.ah, luma);
            const int px = (int)std::lround(j.cx * j.aw - 0.5), py = (int)std::lround(j.cy * j.ah - 0.5);
            const int reach = std::max(4, std::min(j.aw, j.ah) / 10);
            double best = std::numeric_limits<double>::max();
            int bx = px, by = py;
            for (int dy = -reach; dy <= reach; ++dy)
                for (int dx = -reach; dx <= reach; ++dx)
                {
                    const int cx = px + dx, cy = py + dy;
                    if (cx < 0 || cy < 0 || cx >= j.aw || cy >= j.ah) continue;
                    double sad = 0;
                    bool over = false;   // worse than the best already: abandoned, never a candidate
                    for (int y = 0; y < j.th && !over; ++y)
                    {
                        for (int x = 0; x < j.tw; ++x)
                        {
                            const int u = std::clamp(cx - j.tw / 2 + x, 0, j.aw - 1), v = std::clamp(cy - j.th / 2 + y, 0, j.ah - 1);
                            sad += std::fabs(luma[(size_t)v * j.aw + u] - j.patch[(size_t)y * j.tw + x]);
                        }
                        over = sad > best + 1e-9;
                    }
                    if (over) continue;
                    // the nearest of equals: a flat patch stays put rather than wandering
                    if (sad < best - 1e-9 || (std::fabs(sad - best) < 1e-9 && std::abs(dx) + std::abs(dy) < std::abs(bx - px) + std::abs(by - py)))
                    {
                        best = sad;
                        bx = cx;
                        by = cy;
                    }
                }
            j.cx = std::clamp((bx + 0.5) / j.aw, 0.0, 1.0);
            j.cy = std::clamp((by + 0.5) / j.ah, 0.0, 1.0);
            patchAt(luma, j.aw, j.ah, bx, by, j.tw, j.th, j.patch);
            const double t = j.frame / j.fps;
            upsertKey(tx, t, j.cx, nullptr, nullptr);
            upsertKey(ty, t, j.cy, nullptr, nullptr);
            ++j.model.done;
            markDirty();
            bumpFrame();
            if (j.frame == j.end) finish("done", "");
            return;
        }
    }
}
}
