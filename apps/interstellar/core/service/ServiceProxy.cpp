/*
 *  interstellar_core — proxies, offline/online (R-MEDIA-2).
 *
 *  A proxy is a source's ORIGINAL code values, prescaled to a long edge and encoded cheap to decode
 *  (ProRes Proxy, or H.264), in `<project>.proxies/<bind>_<edge>.mov|mp4`. It carries the original's
 *  frames one for one at the original's rate, so the same source frame index reads it; its input
 *  transform, LUT, grade and plugins are the original's, applied to it as they are to the original —
 *  a proxy is a lighter file, never a second look. Plugins are sized in ORIGINAL pixels, so the
 *  original's width is remembered as `proxyScale`.
 *
 *  Made by a background job, one frame per pump like a render, and only referenced (`#rackobj
 *  proxy=`) once its last frame is written — a half-made proxy is never shown. The project's switch
 *  (`proxy use on`) points the MONITOR at proxies — playback, the preview cache, Grade, capture —
 *  and a render or export-still always decodes the originals (`OriginalsOnly`, in ServiceRender).
 */
#include "ServiceInternal.h"
#include "FrameSelector.h"
#include "Project.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        /** `in` with its long edge at `edge` (never larger than it was): each output pixel the area
         *  average of the source pixels under it — a proxy is a smaller picture, not a sharper one. */
        void shrinkTo(const Raster &in, int edge, Raster &out)
        {
            const int longest = std::max(in.width, in.height);
            if (longest <= edge) { out = in; return; }
            const int ow = std::max(1, (int)std::lround((double)in.width * edge / longest));
            const int oh = std::max(1, (int)std::lround((double)in.height * edge / longest));
            out.allocate(ow, oh);
            const double sx = (double)in.width / ow, sy = (double)in.height / oh;
            for (int y = 0; y < oh; ++y)
            {
                const double y0 = y * sy, y1 = y0 + sy;
                for (int x = 0; x < ow; ++x)
                {
                    const double x0 = x * sx, x1 = x0 + sx;
                    double acc[4] = {0, 0, 0, 0}, area = 0;
                    for (int v = (int)y0; v < std::min(in.height, (int)std::ceil(y1)); ++v)
                    {
                        const double wy = std::min(y1, v + 1.0) - std::max(y0, (double)v);
                        for (int u = (int)x0; u < std::min(in.width, (int)std::ceil(x1)); ++u)
                        {
                            const double w = wy * (std::min(x1, u + 1.0) - std::max(x0, (double)u));
                            const uint8_t *p = &in.rgba[((size_t)v * in.width + u) * 4];
                            for (int c = 0; c < 4; ++c) acc[c] += w * p[c];
                            area += w;
                        }
                    }
                    uint8_t *o = &out.rgba[((size_t)y * ow + x) * 4];
                    for (int c = 0; c < 4; ++c) o[c] = (uint8_t)std::clamp(std::lround(acc[c] / std::max(area, 1e-9)), 0L, 255L);
                }
            }
        }
    }

    std::string InterstellarService::proxyDir() const
    {
        const fs::path p(mModel.projectPath);
        return (p.parent_path() / (p.stem().string() + ".proxies")).string();
    }

    bool InterstellarService::proxyCommand(const Command &c)
    {
        Project &P = *mProject;
        if (c.kind == CK::ProxyUse)
        {
            const std::string a = c.arg(0);
            if (a != "on" && a != "off") return fail("proxy use: on or off, got " + a);
            P.proxies = a == "on";
            markDirty();
            bumpFrame();
            emit(Event(EK::Info).with("text", std::string("the monitor decodes ") + (P.proxies ? "proxies where a source has one" : "the originals")));
            return true;
        }
        if (c.kind == CK::ProxyRemove)
        {
            for (const auto &a : c.args)
            {
                RackObj *ro = P.rackObj(P.idForRef(a));
                if (!ro) return fail("proxy remove: no source named " + a);
                bool making = false;
                for (auto &j : mProxyJobs)
                    if (j->model.rackObj == ro->id && (j->model.state == "queued" || j->model.state == "running"))
                    {
                        if (j->writer && j->begun) j->writer->end();
                        j->model.state = "cancelled";
                        making = true;
                    }
                if (ro->proxy.empty() && !making) return fail("proxy remove: " + ro->name + " has no proxy");
                ro->proxy.clear();
                ro->proxyScale = 1.0;
            }
            markDirty();
            bumpFrame();
            return true;
        }

        // proxy make
        const std::string codec = c.flag("codec", "prores");
        if (codec != "prores" && codec != "h264") return fail("proxy make: --codec is prores (ProRes Proxy) or h264");
        int edge = 960;
        if (c.has("edge"))
        {
            const std::string e = c.flag("edge");
            char *end = nullptr;
            const long v = std::strtol(e.c_str(), &end, 10);
            if (!end || *end || v < 16 || v > 8192) return fail("proxy make: --edge is a long edge in pixels, 16 … 8192, got " + e);
            edge = (int)v;
        }
        if (!mHost.frameWriter || !mHost.frameSource) return fail("proxy make: this build has no encoder");
        if (mModel.projectPath.empty()) return fail("proxy make: save the project first — proxies live beside it");
        std::vector<RackObj *> todo;
        auto making = [&](const NodeId &id) {
            for (const auto &j : mProxyJobs)
                if (j->model.rackObj == id && (j->model.state == "queued" || j->model.state == "running")) return true;
            return false;
        };
        if (c.args.empty())
        {
            for (auto &ro : P.rackObjs)
                if (ro.kind != "group" && !ro.media.empty() && looksLikeVideo(ro.media) && ro.proxy.empty() && !making(ro.id)) todo.push_back(&ro);
        }
        else
            for (const auto &a : c.args)
            {
                RackObj *ro = P.rackObj(P.idForRef(a));
                if (!ro || ro->kind == "group" || ro->media.empty()) return fail("proxy make: no source named " + a);
                if (!looksLikeVideo(ro->media)) return fail("proxy make: " + ro->name + " is a still — a photograph needs no proxy");
                if (making(ro->id)) return fail("proxy make: " + ro->name + "'s proxy is being made");
                todo.push_back(ro);
            }
        if (todo.empty())
        {
            mOutput = "every video source has a proxy\n";
            return true;
        }
        std::error_code ec;
        fs::create_directories(proxyDir(), ec);
        if (ec) return fail("proxy make: cannot create " + proxyDir());
        mOutput.clear();
        for (RackObj *ro : todo)
        {
            auto j = std::make_unique<ProxyJob>();
            j->model.id = "p" + std::to_string(++mProxySeq);
            j->model.rackObj = ro->id;
            j->model.bindName = ro->name;
            j->model.codec = codec;
            j->model.edge = edge;
            j->model.outPath = (fs::path(proxyDir()) / (ro->name + "_" + std::to_string(edge) + (codec == "prores" ? ".mov" : ".mp4"))).string();
            j->model.state = "queued";
            j->spec.codec = codec;
            j->spec.profile = codec == "prores" ? "proxy" : "";
            j->spec.bitDepth = codec == "prores" ? 10 : 8;
            j->spec.quality = 23;
            j->spec.speed = "veryfast";
            j->media = resolvePath(ro->media);
            mOutput += j->model.id + " " + ro->name + " → " + j->model.outPath + "\n";
            emit(Event(EK::Info).with("text", "proxy " + j->model.id + ": " + ro->name + " queued"));
            mProxyJobs.push_back(std::move(j));
        }
        return true;
    }

    void InterstellarService::pumpProxies()
    {
        // one frame per pump for the first live job, as a render does: a window stays responsive;
        // its progress reaches the model every frame, as a render's does
        for (auto &jp : mProxyJobs)
        {
            ProxyJob &j = *jp;
            if (j.model.state != "queued" && j.model.state != "running") continue;
            struct Refresh { InterstellarService &s; ~Refresh() { s.refreshModel(); } } refresh{*this};
            auto failJob = [&](const std::string &why) {
                j.model.state = "failed";
                j.model.error = why;
                if (j.writer && j.begun) j.writer->end();
                emit(Event(EK::Info).with("text", "proxy " + j.model.id + " failed: " + why));
            };
            RackObj *ro = mProject->rackObj(j.model.rackObj);
            if (!ro) { failJob("its source was removed"); return; }
            Raster frame, small;
            if (!j.begun)
            {
                j.source = mHost.frameSource();
                IFrameSource::Info info;
                if (!j.source || !j.source->open(j.media, info) || !info.valid() || info.frames < 1) { failJob("cannot open " + j.media); return; }
                j.model.total = info.frames;
                j.fps = info.fps > 0 ? info.fps : 24.0;
                j.srcWidth = info.width;
                if (!j.source->frameAt(0, frame)) { failJob("cannot decode " + j.media); return; }
                shrinkTo(frame, j.model.edge, small);
                j.width = small.width & ~1;    // the encoders' chroma wants even sizes: an odd row or column is dropped
                j.height = small.height & ~1;
                j.writer = mHost.frameWriter();
                if (!j.writer || !j.writer->begin(j.model.outPath, j.width, j.height, j.fps, j.model.total, j.spec))
                {
                    failJob("the encoder refused " + j.model.outPath);
                    return;
                }
                j.begun = true;
                j.model.state = "running";
            }
            else
            {
                if (!j.source->frameAt(j.model.done, frame)) { failJob("cannot decode frame " + std::to_string(j.model.done)); return; }
                shrinkTo(frame, j.model.edge, small);
            }
            Raster even;
            even.allocate(j.width, j.height);
            if (small.width < j.width || small.height < j.height) { failJob("frame " + std::to_string(j.model.done) + " changed size"); return; }
            for (int y = 0; y < j.height; ++y)
                std::copy_n(&small.rgba[(size_t)y * small.width * 4], (size_t)j.width * 4, &even.rgba[(size_t)y * j.width * 4]);
            if (!j.writer->write(even)) { failJob("the encoder refused frame " + std::to_string(j.model.done)); return; }
            if (++j.model.done < j.model.total) return;
            if (!j.writer->end()) { failJob("the encoder could not finish " + j.model.outPath); return; }
            // the last frame is written: only now is it the source's proxy
            ro->proxy = relativePath(j.model.outPath);
            ro->proxyScale = j.srcWidth > 0 ? (double)j.width / j.srcWidth : 1.0;
            j.model.state = "done";
            j.writer.reset();
            j.source.reset();
            markDirty();
            bumpFrame();
            emit(Event(EK::Info).with("text", "proxy " + j.model.id + ": " + ro->name + " done, " + std::to_string(j.width) + "×" + std::to_string(j.height)));
            return;
        }
    }
}
}
