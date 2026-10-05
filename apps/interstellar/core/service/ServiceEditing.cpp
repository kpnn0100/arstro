/*
 *  interstellar_core — editing like an editor (R-EDT-1, R-EDT-2): the J/K/L shuttle, the timeline's
 *  and the source viewer's In/Out, the target track, and three-point Insert and Overwrite.
 *
 *  Three points decide the fourth: with the source's In and Out the length is known and the timeline
 *  In (else the playhead) places it — or a timeline Out alone backtimes it; with one source mark and
 *  both timeline marks the other source mark follows from the timeline's length. Missing source marks
 *  default to the source's ends. After an edit the playhead lands at the clip's end and the timeline
 *  marks clear, as editors expect; the source marks stay for the next take.
 */
#include "ServiceInternal.h"
#include "Arrange.h"
#include "Versions.h"
#include "Timecode.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        bool parseDouble(const std::string &s, double &out)
        {
            char *end = nullptr;
            out = std::strtod(s.c_str(), &end);
            return end && end != s.c_str() && !*end && std::isfinite(out);
        }
    }

    const anim::Ramp *InterstellarService::rampFor(const Clip &c) const
    {
        const Anim *a = mProject->animOf(c.id, "speed");
        if (!a) return nullptr;
        const auto keys = mProject->keysOf(a->id);
        if (keys.empty()) return nullptr;
        // built once per (keys, in, out): planning asks every frame
        std::string sig = std::to_string(c.in) + ":" + std::to_string(c.out);
        for (const auto &k : keys)
            sig += "|" + std::to_string(k.t) + "," + std::to_string(k.v) + "," + anim::sideName(k.in) + anim::sideName(k.out) + "," +
                   std::to_string(k.speedIn) + "," + std::to_string(k.speedOut) + "," + std::to_string(k.inflIn) + "," + std::to_string(k.inflOut);
        auto &slot = mRamps[c.id];
        if (slot.first != sig) slot = {sig, anim::Ramp::build(keys, c.in, c.out)};
        return &slot.second;
    }

    void InterstellarService::retimeRamps()
    {
        // a ramped clip's stored speed is the AVERAGE that makes its length the curve's — so everything
        // that measures a clip (spans, the end of the timeline, a render's frame count) is right
        Project &P = *mProject;
        ResolvedTimeline R;
        std::string err;
        const NodeId tl = currentTimeline();
        if (tl.empty() || !resolve(P, tl, R, err)) return;
        for (const auto &c : R.clips)
        {
            const anim::Ramp *r = rampFor(c);
            if (!r || !(r->duration() > 0.0)) continue;
            const double avg = (c.out - c.in) / r->duration();
            char b[32];
            std::snprintf(b, sizeof b, "%.7g", avg);
            if (std::fabs(std::atof(b) - c.speed) > 1e-6) setField(P, tl, c.id, "speed", b, err);
        }
    }

    NodeId InterstellarService::multicamAt(const ResolvedTimeline &R, double t, bool anyNested) const
    {
        std::map<NodeId, int> order;
        for (const auto &x : R.tracks)
            if (!x.audio()) order[x.id] = x.order;
        const double half = 0.5 / (mProject->fps > 0 ? mProject->fps : 24.0);
        NodeId best;
        int bestOrder = -1;
        bool bestAngle = false;
        for (const auto &c : R.clips)
        {
            const auto o = order.find(c.track);
            const auto pv = R.provenance.find(c.id);
            if (o == order.end() || !mProject->timeline(c.src) || t < c.at - 1e-9 || t > c.end() - half) continue;
            if (pv != R.provenance.end() && pv->second == Provenance::Dangling) continue;
            const bool a = c.angle > 0;
            if (!a && !anyNested) continue;
            if ((a && !bestAngle) || (a == bestAngle && o->second > bestOrder)) { best = c.id; bestOrder = o->second; bestAngle = a; }
        }
        return best;
    }

    std::vector<NodeId> InterstellarService::anglesOf(const NodeId &tl) const
    {
        std::vector<NodeId> out;
        ResolvedTimeline R;
        std::string err;
        if (!resolved(tl, R, err)) return out;
        for (const auto &t : R.tracks)   // by order already
            if (!t.audio()) out.push_back(t.id);
        return out;
    }

    bool InterstellarService::editingCommand(const Command &c)
    {
        Project &P = *mProject;
        const double fps = P.fps > 0 ? P.fps : 24.0;
        std::string err;
        auto parseT = [&](const std::string &flag, double &out) {
            if (!c.has(flag)) return true;
            const std::string v = c.flag(flag);
            if (!parseDouble(v, out) || out < 0) return fail(std::string(specFor(c.kind)->verb) + ": --" + flag + " is seconds, got " + v);
            return true;
        };

        if (c.kind == CK::Shuttle)
        {
            const std::string a = c.arg(0);
            if (a != "forward" && a != "back" && a != "stop") return fail("shuttle: forward, back or stop, got " + a);
            if (a == "stop")
            {
                Command p;
                p.kind = CK::Pause;
                return playheadCommand(p);
            }
            const double was = mPlaying ? mShuttle : 0.0;
            double rate;
            if (a == "forward") rate = was <= 0.0 ? 1.0 : std::min(4.0, was * 2.0);
            else rate = was >= 0.0 ? -1.0 : std::max(-4.0, was * 2.0);
            if (!mPlaying)
            {
                Command p;
                p.kind = CK::Play;
                if (!playheadCommand(p)) return false;
            }
            // re-anchor the clock at the new rate; the sound plays at 1× forward only
            mShuttle = rate;
            mPlayFromT = mModel.playhead;
            mPlayFromMs = mNowMs;
            if (rate == 1.0) { if (!mSoundClock && !mPreroll) startSound(); }
            else stopSound();
            emit(Event(EK::PlaybackChanged).with("playing", true).with("rate", rate));
            return true;
        }

        if (c.kind == CK::Mark)
        {
            const std::string a = c.arg(0);
            const bool src = c.has("source");
            if (src && mSourceView.empty()) return fail("mark --source: no source in the viewer — `source view <node>` first");
            if (a == "clear")
            {
                (src ? mSourceIn : mMarkIn) = -1.0;
                (src ? mSourceOut : mMarkOut) = -1.0;
                return true;
            }
            if (a != "in" && a != "out") return fail("mark: in, out or clear, got " + a);
            double t = src ? mSourcePlayhead : mModel.playhead;
            if (!parseT("at", t)) return false;
            t = snapToFrame(t, fps);
            double &in = src ? mSourceIn : mMarkIn, &out = src ? mSourceOut : mMarkOut;
            if (a == "in") { in = t; if (out >= 0 && out <= in) out = -1.0; }
            else { out = t; if (in >= 0 && in >= out) in = -1.0; }
            return true;
        }

        if (c.kind == CK::SourceView)
        {
            const std::string a = c.arg(0);
            if (a == "none") { mSourceView.clear(); return true; }
            const RackObj *ro = P.rackObj(P.idForRef(a));
            if (!ro || ro->kind == "group" || ro->media.empty()) return fail("source view: no source named " + a);
            if (ro->name != mSourceView) { mSourceIn = mSourceOut = -1.0; mSourcePlayhead = 0.0; }
            mSourceView = ro->name;
            source(*mSync, resolvePath(ro->media));   // opened now: its length and rate are the model's
            return true;
        }

        if (c.kind == CK::SourcePlayhead)
        {
            if (mSourceView.empty()) return fail("source playhead: no source in the viewer");
            double t = 0;
            if (!parseDouble(c.arg(0), t) || t < 0) return fail("source playhead: seconds, got " + c.arg(0));
            const RackObj *ro = P.rackObj(P.idForRef(mSourceView));
            const Source *s = ro ? source(*mSync, resolvePath(ro->media)) : nullptr;
            const double sfps = s && s->ok && s->info.fps > 0 ? s->info.fps : fps;
            const double dur = s && s->ok && s->info.frames > 1 ? s->info.frames / sfps : 0.0;
            mSourcePlayhead = snapToFrame(dur > 0 ? std::min(t, dur - 1.0 / sfps) : t, sfps);
            bumpFrame();
            return true;
        }

        if (c.kind == CK::MulticamNew)
        {
            // R-EDT-5: one video track per source, lined up; the sound of one of them
            const std::string name = c.arg(0);
            auto split = [](const std::string &v) {
                std::vector<std::string> out;
                size_t a = 0;
                while (a <= v.size())
                {
                    const size_t b = std::min(v.find(',', a), v.size());
                    if (b > a) out.push_back(v.substr(a, b - a));
                    a = b + 1;
                }
                return out;
            };
            const std::vector<std::string> refs = split(c.flag("sources"));
            if (refs.size() < 2) return fail("multicam new: --sources names two or more rack sources, comma-separated");
            const std::string sync = c.has("sync") ? c.flag("sync") : c.has("in") ? "in" : "timecode";
            if (sync != "timecode" && sync != "in") return fail("multicam new: --sync is timecode or in, got " + sync);
            if (sync == "timecode" && c.has("in")) return fail("multicam new: --in lines the sources up by in-points — it goes with --sync in");
            std::map<NodeId, double> ins;
            for (const auto &kv : split(c.flag("in")))
            {
                const auto eq = kv.find('=');
                double v = 0;
                const RackObj *ro = eq == std::string::npos ? nullptr : P.rackObj(P.idForRef(kv.substr(0, eq)));
                if (!ro || !parseDouble(kv.substr(eq + 1), v) || v < 0) return fail("multicam new: --in is <source>=<seconds>,…, got " + kv);
                ins[ro->id] = v;
            }
            struct Cam { const RackObj *ro; double len, start; };
            std::vector<Cam> cams;
            for (const auto &r : refs)
            {
                const RackObj *ro = P.rackObj(P.idForRef(r));
                if (!ro || ro->kind == "group" || ro->media.empty()) return fail("multicam new: no rack source named " + r);
                const Source *s = source(*mSync, resolvePath(ro->media));
                if (!s || !s->ok) return fail("multicam new: " + ro->name + " cannot be opened");
                const double sfps = s->info.fps > 0 ? s->info.fps : fps;
                Cam cam{ro, s->info.frames > 1 ? s->info.frames / sfps : 5.0, 0.0};
                if (sync == "timecode")
                {
                    long long f = 0;
                    if (s->info.timecode.empty() || !xch::framesFromTc(s->info.timecode, xch::TcRate::of(sfps), f))
                        return fail("multicam new: " + ro->name + " carries no timecode — line the sources up by their in-points: --sync in --in " +
                                    ro->name + "=<seconds>,…");
                    cam.start = f / sfps;
                }
                else cam.start = -ins[ro->id];   // the in-points meet: source i starts in_i before them
                cams.push_back(cam);
            }
            const std::string audioRef = c.flag("audio", refs.front());
            const Cam *heard = nullptr;
            for (const auto &cam : cams)
                if (audioRef != "none" && cam.ro->id == P.idForRef(audioRef)) heard = &cam;
            if (audioRef != "none" && !heard) return fail("multicam new: --audio names one of the --sources (or none), got " + audioRef);
            NodeId track;
            if (c.has("track"))
            {
                const NodeId outer = currentTimeline();
                ResolvedTimeline R;
                if (!resolved(outer, R, err)) return fail(err);
                track = P.idForRef(c.flag("track"));
                bool video = false;
                for (const auto &t : R.tracks) video = video || (t.id == track && !t.audio());
                if (!video) return fail("multicam new: --track " + c.flag("track") + " is not a video track of this timeline");
            }
            double at = mModel.playhead;
            if (!parseT("at", at)) return false;
            double t0 = cams.front().start;
            for (const auto &cam : cams) t0 = std::min(t0, cam.start);
            NodeId id;
            if (!newTimeline(P, name, "", id, err)) return fail("multicam new: " + err);
            for (const auto &cam : cams)
            {
                NodeId trk, cl;
                if (!arrange::addTrack(P, id, "video", "", trk, err) ||
                    !arrange::addClip(P, id, trk, cam.ro->id, 0.0, snapToFrame(cam.len, fps), snapToFrame(cam.start - t0, fps), "", cl, err))
                    return fail("multicam new: " + err);
            }
            if (heard)
            {
                NodeId atrk;
                if (!arrange::addTrack(P, id, "audio", "", atrk, err)) return fail("multicam new: " + err);
                AClip a;
                a.id = P.freshId("aclp_");
                a.name = P.freshName(heard->ro->name + "_sound");
                a.track = atrk;
                a.timeline = id;
                a.src = heard->ro->media;
                a.at = snapToFrame(heard->start - t0, fps);
                a.out = snapToFrame(heard->len, fps);
                P.audioClips.push_back(a);
            }
            mOutput = id + "\n";
            if (!track.empty())
            {
                NodeId placed;
                const NodeId outer = currentTimeline();
                if (!arrange::addClip(P, outer, track, id, 0.0, snapToFrame(timelineDuration(id), fps), snapToFrame(at, fps), "", placed, err) ||
                    !setField(P, outer, placed, "angle", "1", err))
                    return fail("multicam new: " + err);
                mOutput += placed + "\n";
            }
            markDirty();
            bumpFrame();
            emit(Event(EK::TimelineChanged).with("timeline", id).with("what", "multicam " + name));
            return true;
        }

        const NodeId tl = currentTimeline();
        ResolvedTimeline R;
        if (!resolved(tl, R, err)) return fail(err);

        if (c.kind == CK::MulticamAngle)
        {
            // R-EDT-5: from this frame on, angle n — a cut, unless it is the clip's first frame
            const std::string a = c.arg(0);
            char *end = nullptr;
            const long n = std::strtol(a.c_str(), &end, 10);
            if (!end || *end || n < 1 || n > 99) return fail("multicam angle: an angle number, 1 or more, got " + a);
            double t = mModel.playhead;
            if (!parseT("at", t)) return false;
            t = snapToFrame(t, fps);
            const NodeId id = c.has("clip") ? P.idForRef(c.flag("clip")) : multicamAt(R, t, true);
            const Clip *cl = nullptr;
            for (const auto &x : R.clips)
                if (x.id == id) cl = &x;
            if (!cl || !P.timeline(cl->src))
                return fail(c.has("clip") ? "multicam angle: " + c.flag("clip") + " places no timeline" : "multicam angle: no multicam clip under the playhead");
            if (t < cl->at - 1e-9 || t > cl->end() - 0.5 / fps) return fail("multicam angle: " + cl->name + " is not under " + canonicalTime(t));
            const auto angles = anglesOf(cl->src);
            if ((size_t)n > angles.size())
                return fail("multicam angle: " + P.timeline(cl->src)->name + " has " + std::to_string(angles.size()) + " angle(s)");
            NodeId target = cl->id;
            const int was = cl->angle;
            if (t > cl->at + 0.5 / fps && was != n)
            {
                NodeId right;
                if (!arrange::split(P, tl, cl->id, t, right, err)) return fail("multicam angle: " + err);
                copyAnims(cl->id, right, 0.0);
                target = right;
            }
            if (was != n && !setField(P, tl, target, "angle", std::to_string(n), err)) return fail("multicam angle: " + err);
            markDirty();
            bumpFrame();
            mOutput = target + "\n";
            emit(Event(EK::ArrangeChanged).with("timeline", tl).with("what", "angle " + std::to_string(n)).with("node", target));
            return true;
        }

        if (c.kind == CK::EditTarget)
        {
            const NodeId id = P.idForRef(c.arg(0));
            for (const auto &t : R.tracks)
                if (t.id == id && !t.audio()) { mTargetTrack = id; return true; }
            return fail("edit target: " + c.arg(0) + " is not a video track of this timeline");
        }

        // ── three-point insert / overwrite ──
        const std::string verb = c.kind == CK::EditInsert ? "edit insert" : "edit overwrite";
        const std::string srcRef = c.has("src") ? c.flag("src") : mSourceView;
        if (srcRef.empty()) return fail(verb + ": no source — `source view <node>` or --src");
        const RackObj *ro = P.rackObj(P.idForRef(srcRef));
        if (!ro || ro->kind == "group" || ro->media.empty()) return fail(verb + ": no source named " + srcRef);
        const Source *s = source(*mSync, resolvePath(ro->media));
        const double sfps = s && s->ok && s->info.fps > 0 ? s->info.fps : fps;
        const double srcEnd = s && s->ok && s->info.frames > 1 ? s->info.frames / sfps : 5.0;   // a still: 5 s, as a drop gives it
        const bool viewed = !c.has("src") || P.idForRef(c.flag("src")) == P.idForRef(mSourceView);
        double sIn = viewed ? mSourceIn : -1.0, sOut = viewed ? mSourceOut : -1.0, rIn = mMarkIn, rOut = mMarkOut;
        if (c.has("in") && !parseT("in", sIn)) return false;
        if (c.has("out") && !parseT("out", sOut)) return false;
        if (c.has("at")) { if (!parseT("at", rIn)) return false; rOut = -1.0; }
        const bool haveRange = rIn >= 0 && rOut > rIn;
        if (sIn >= 0 && sOut >= 0) { if (rIn < 0 && rOut >= 0) rIn = rOut - (sOut - sIn); }       // backtimed from the timeline Out
        else if (sIn >= 0) sOut = haveRange ? sIn + (rOut - rIn) : srcEnd;
        else if (sOut >= 0) sIn = haveRange ? std::max(0.0, sOut - (rOut - rIn)) : 0.0;
        else { sIn = 0.0; sOut = haveRange ? std::min(srcEnd, rOut - rIn) : srcEnd; }
        if (rIn < 0) rIn = mModel.playhead;
        sIn = std::max(0.0, sIn);
        sOut = std::min(srcEnd, sOut);
        if (!(sOut > sIn)) return fail(verb + ": the source In must come before its Out");
        if (rIn < 0) return fail(verb + ": the edit would start before the timeline does");
        NodeId track = c.has("track") ? P.idForRef(c.flag("track")) : mTargetTrack;
        bool isVideo = false;
        for (const auto &t : R.tracks) isVideo = isVideo || (t.id == track && !t.audio());
        if (!isVideo)
        {
            // the target: the lowest video track there is
            track.clear();
            int best = 1 << 30;
            for (const auto &t : R.tracks)
                if (!t.audio() && t.order < best) { best = t.order; track = t.id; }
            if (track.empty()) return fail(verb + ": the timeline has no video track — `track add --kind video`");
        }
        NodeId id;
        const bool ok = c.kind == CK::EditInsert ? arrange::insertEdit(P, tl, track, ro->id, sIn, sOut, rIn, id, err)
                                                 : arrange::overwriteEdit(P, tl, track, ro->id, sIn, sOut, rIn, id, err);
        if (!ok) return fail(verb + ": " + err);
        // the playhead lands at the clip's end; the timeline marks are spent
        mModel.playhead = snapToFrame(rIn + (sOut - sIn), fps);
        mMarkIn = mMarkOut = -1.0;
        markDirty();
        bumpFrame();
        mOutput = id + "\n";
        emit(Event(EK::ArrangeChanged).with("timeline", tl).with("what", c.kind == CK::EditInsert ? "insert" : "overwrite").with("node", id));
        return true;
    }
}
}
