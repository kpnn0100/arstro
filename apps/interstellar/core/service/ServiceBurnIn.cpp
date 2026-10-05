/*
 *  interstellar_core — burn-ins (R-DLV-2).
 *
 *  `render --burnin "tc@bl,clip@tl,source@tr,srctc@br,text=Draft@tc"` puts, on every frame of that
 *  render: the record timecode (01:00:00:00 at the timeline's start, the broadcast habit the
 *  interchange formats share), the top clip's source timecode, its name, its source's name, and free
 *  text — each at one of six places, sized to the frame (3.2 % of its height) on a translucent plate.
 *  Computed here per frame from the timeline at t; drawn by the host (`Host::drawText`) after the
 *  grade and the output transform, so a burn-in is never graded — and in an HDR render its white is
 *  graphics white (BT.2408), not the peak. Renders only — the monitor, a still
 *  and the preview cache never carry one. A render's burned-in captions (R-DLV-1) are drawn here too,
 *  bottom centre, a line to a plate.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include "Timecode.h"
#include "Versions.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace arstro
{
namespace interstellar
{
    bool InterstellarService::burnIn(const Job &j, double t, Raster &frame)
    {
        const Project &P = *mProject;
        const double fps = j.fps > 0 ? j.fps : (P.fps > 0 ? P.fps : 24.0);
        const xch::TcRate rate = xch::TcRate::of(fps);
        // the top clip at t, for its name, its source and its source timecode
        const Clip *top = nullptr;
        ResolvedTimeline R;
        std::string err;
        if (resolved(j.model.timeline, R, err))
        {
            std::map<NodeId, int> order;
            for (const auto &tr : R.tracks) if (!tr.audio() && !tr.mute) order[tr.id] = tr.order;
            int best = -1;
            for (const auto &c : R.clips)
                if (t >= c.at - 1e-9 && t < c.end() - 1e-9 && order.count(c.track) && order[c.track] > best) { best = order[c.track]; top = &c; }
        }
        std::vector<OverlayText> items;
        const double W = frame.width, H = frame.height;
        const double px = std::max(10.0, H * 0.032), margin = std::max(6.0, H * 0.025);
        // drawn after the output transform, so its white is the delivery's graphics white (BT.2408)
        const double white = j.spec.output == "pq" ? 0.5807 : j.spec.output == "hlg" ? 0.75 : 1.0;
        for (const auto &b : j.burns)
        {
            OverlayText o;
            o.px = px;
            o.white = white;
            if (b.what == "tc")
            {
                long long start = 0;
                xch::framesFromTc("01:00:00:00", rate, start);   // right for drop-frame and for 23.976 alike
                o.text = xch::tcFromFrames((long long)std::llround(t * fps) + start, rate);
            }
            else if (b.what == "text") { o.text = b.text; o.mono = false; }
            else if (!top) o.text = "\xE2\x80\x94";   // nothing cut here: a dash, not a blank
            else if (b.what == "clip") { o.text = top->name; o.mono = false; }
            else if (b.what == "source")
            {
                const RackObj *ro = P.rackObj(top->src);
                const Timeline *nt = ro ? nullptr : P.timeline(top->src);
                o.text = ro ? ro->name : nt ? nt->name : top->src;
                o.mono = false;
            }
            else   // srctc: the source's own timecode at the frame it shows
            {
                const RackObj *ro = P.rackObj(top->src);
                const double srcT = top->in + (t - top->at) * top->speed;
                double start = 0;
                xch::TcRate sr = rate;
                if (ro)
                    if (const Source *s = source(*mSync, resolvePath(ro->media)); s && s->ok)
                    {
                        sr = xch::TcRate::of(s->info.fps > 0 ? s->info.fps : fps);
                        long long f0 = 0;
                        if (!s->info.timecode.empty() && xch::framesFromTc(s->info.timecode, sr, f0)) start = f0 / sr.fps;
                    }
                o.text = xch::tcFromFrames((long long)std::llround((start + srcT) * sr.fps), sr);
            }
            const char h = b.at[1], v = b.at[0];
            o.align = h == 'l' ? 0 : h == 'c' ? 1 : 2;
            o.x = h == 'l' ? margin : h == 'c' ? W * 0.5 : W - margin;
            o.valign = v == 't' ? 0 : 1;
            o.y = v == 't' ? margin : H - margin;
            items.push_back(std::move(o));
        }
        // R-DLV-1: the caption on screen at this frame — bottom centre, inside title safe, a plate to a
        // line, the last line lowest (the cues never overlap: captionCues flattened them)
        const double rs = (double)(std::llround(t * fps) - j.first) / fps;
        const double cpx = std::max(12.0, H * 0.045);
        for (const auto &q : j.burnCues)
        {
            if (rs < q.start - 1e-6 || rs >= q.end - 1e-6) continue;
            std::vector<std::string> lines;
            std::string line;
            for (const char ch : q.text + "\n")
                if (ch == '\n') { if (!line.empty()) lines.push_back(line); line.clear(); }
                else line += ch;
            double y = H * 0.92;
            for (auto it = lines.rbegin(); it != lines.rend(); ++it)
            {
                OverlayText o;
                o.text = *it;
                o.x = W * 0.5;
                o.y = y;
                o.align = 1;
                o.valign = 1;
                o.px = cpx;
                o.mono = false;
                o.white = white;
                items.push_back(std::move(o));
                y -= std::ceil(cpx * 1.5) + 2.0;
            }
        }
        return mHost.drawText && mHost.drawText(frame, items);
    }
}
}
