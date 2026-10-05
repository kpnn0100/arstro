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

        const NodeId tl = currentTimeline();
        ResolvedTimeline R;
        if (!resolved(tl, R, err)) return fail(err);

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
