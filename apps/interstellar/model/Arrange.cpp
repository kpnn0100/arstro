/*
 *  interstellar_model — Arrange: the cut operations, as compositions of the derived-aware
 *  primitives in Versions.
 *
 *  Each operation (1) resolves the timeline the user is looking at, (2) computes the new values
 *  from the RESOLVED clip — so an inherited clip's overrides are respected — (3) refuses anything
 *  that would leave no frames, and only then (4) writes through `setFields` / `dropNode` / a
 *  local add. Computed times are snapped to the file's millisecond grid BEFORE they are checked,
 *  so the value validated is the value written: a split whose halves both pass at 13.3333 s must
 *  not land as 13.333 s on one side and something else on the other.
 */
#include "Arrange.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar
{
namespace arrange
{
    namespace
    {
        constexpr double kEps = 1e-9;
        constexpr double kAdjacent = 0.0005;     // half a millisecond: two times one grid step apart are not equal

        double snap(double v)
        {
            double d = 0.0;
            parseNumber(canonicalTime(v), d);
            return d;
        }
        std::string t3(double v) { return canonicalTime(v); }

        bool timelineOf(const Project &P, const NodeId &ref, NodeId &out, std::string &err)
        {
            out = P.idForRef(ref);
            if (P.timeline(out)) return true;
            err = "no such timeline: " + ref;
            return false;
        }

        const Clip *rClip(const ResolvedTimeline &R, const NodeId &id)
        {
            for (const auto &c : R.clips) if (c.id == id) return &c;
            return nullptr;
        }
        const Track *rTrack(const ResolvedTimeline &R, const NodeId &id)
        {
            for (const auto &t : R.tracks) if (t.id == id) return &t;
            return nullptr;
        }

        /** Resolve `tl` and find `clip` in it, with a message that says where it went if not. */
        bool clipIn(const Project &P, const NodeId &timeline, const NodeId &clip, NodeId &tl, ResolvedTimeline &R,
                    const Clip *&c, std::string &err)
        {
            if (!timelineOf(P, timeline, tl, err) || !resolve(P, tl, R, err)) return false;
            const NodeId id = P.idForRef(clip);
            c = rClip(R, id);
            if (c) return true;
            if (P.tldrop(tl, id)) err = id + " is dropped from " + tl;
            else if (P.clip(id)) err = id + " is not in timeline " + tl;
            else err = "no such clip: " + clip;
            return false;
        }

        bool forward(const Clip &c, std::string &err)
        {
            if (c.speed > 0.0) return true;
            err = "clip " + c.id + " plays in reverse (speed " + canonicalNumber(c.speed) + "); v1 cuts forward clips only";
            return false;
        }

        bool finite(std::initializer_list<double> vs, std::string &err)
        {
            for (double v : vs)
                if (!std::isfinite(v)) { err = "times are finite numbers"; return false; }
            return true;
        }

        /** `base_1`, `base_2`, … — a clip named after its source reads better than `clp_7`. */
        std::string numbered(const Project &P, const std::string &base)
        {
            std::string why;
            const std::string b = Project::nameIsLegal(base, why) && base.size() <= 24 ? base : P.freshName(base.substr(0, 20));
            for (int i = 1;; ++i)
            {
                const std::string c = b + "_" + std::to_string(i);
                if (!P.nameIsTaken(c)) return c;
            }
        }

        /** A node `tl` adds on `track` carries `timeline=` exactly when the track is someone else's. */
        NodeId declaredIn(const Project &P, const NodeId &tl, const NodeId &track)
        {
            const Track *t = P.track(track);
            return t && t->timeline == tl ? NodeId() : tl;
        }
    }

    bool addTrack(Project &P, const NodeId &timeline, const std::string &kind, const std::string &name, NodeId &outId,
                  std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        if (!timelineOf(P, timeline, tl, err) || !resolve(P, tl, R, err)) return false;
        if (kind != "video" && kind != "audio") { err = "a track is video or audio, not '" + kind + "'"; return false; }
        std::string nm = name;
        if (nm.empty())
            for (int i = 0; nm.empty() || P.nameIsTaken(nm); ++i) nm = (kind == "video" ? "v" : "a") + std::to_string(i);
        else
        {
            if (!Project::nameIsLegal(nm, err)) return false;
            if (P.nameIsTaken(nm)) { err = "that bind name is already used: " + nm; return false; }
        }
        Track t;
        t.id = P.freshId("trk_");
        t.name = nm;
        t.timeline = tl;
        t.kind = kind;
        for (const auto &x : R.tracks) t.order = std::max(t.order, x.order + 1);
        P.tracks.push_back(t);
        outId = t.id;
        return true;
    }

    bool addClip(Project &P, const NodeId &timeline, const NodeId &track, const NodeId &src, double in, double out,
                 double at, const std::string &name, NodeId &outId, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        if (!timelineOf(P, timeline, tl, err) || !resolve(P, tl, R, err)) return false;
        const NodeId trk = P.idForRef(track);
        const Track *t = rTrack(R, trk);
        if (!t || t->audio())
        {
            std::string list;
            for (const auto &x : R.tracks)
                if (!x.audio()) list += (list.empty() ? "" : ", ") + x.id;
            err = track + " is not a video track of " + tl + " (video tracks: " + (list.empty() ? "none" : list) + ")";
            return false;
        }
        const NodeId ro = P.idForRef(src);
        const RackObj *r = P.rackObj(ro);
        if (!r)
        {
            std::string names;
            for (const auto &n : P.bindNames()) names += (names.empty() ? "" : ", ") + n;
            err = src + " names no #rackobj; the rack's bind names are: " + (names.empty() ? "(none)" : names);
            return false;
        }
        if (!finite({in, out, at}, err)) return false;
        in = snap(in); out = snap(out); at = snap(at);
        if (!(in < out)) { err = "in " + t3(in) + " >= out " + t3(out) + " — a clip with no frames is not a clip"; return false; }
        if (in < 0.0 || at < 0.0) { err = "in and at are never negative"; return false; }
        std::string nm = name;
        if (nm.empty()) nm = numbered(P, r->name.empty() ? "clip" : r->name);
        else
        {
            if (!Project::nameIsLegal(nm, err)) return false;
            if (P.nameIsTaken(nm)) { err = "that bind name is already used: " + nm; return false; }
        }
        Clip c;
        c.id = P.freshId("clp_");
        c.name = nm;
        c.track = trk;
        c.timeline = declaredIn(P, tl, trk);
        for (const auto &x : R.clips)
            if (x.track == trk) c.order = std::max(c.order, x.order + 1);
        c.src = ro;
        c.at = at; c.in = in; c.out = out;
        P.clips.push_back(c);
        outId = c.id;
        return true;
    }

    bool trim(Project &P, const NodeId &timeline, const NodeId &clip, Edge edge, double t, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        const Clip *c = nullptr;
        if (!clipIn(P, timeline, clip, tl, R, c, err) || !forward(*c, err) || !finite({t}, err)) return false;
        t = snap(t);
        if (edge == Edge::Head)
        {
            if (t < 0.0) { err = "the head cannot move before the timeline starts"; return false; }
            const double in = snap(c->in + (t - c->at) * c->speed);
            if (in >= c->out)
            {
                err = "trimming the head of " + c->id + " to " + t3(t) + " leaves no frames — refused, not clamped";
                return false;
            }
            if (in < 0.0)
            {
                err = "trimming the head of " + c->id + " to " + t3(t) + " would read before the source's first frame";
                return false;
            }
            return setFields(P, tl, c->id, {{"in", t3(in)}, {"at", t3(t)}}, err);
        }
        const double out = snap(c->in + (t - c->at) * c->speed);
        if (out <= c->in)
        {
            err = "trimming the tail of " + c->id + " to " + t3(t) + " leaves no frames — refused, not clamped";
            return false;
        }
        return setFields(P, tl, c->id, {{"out", t3(out)}}, err);
    }

    bool split(Project &P, const NodeId &timeline, const NodeId &clip, double t, NodeId &outRight, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        const Clip *cp = nullptr;
        if (!clipIn(P, timeline, clip, tl, R, cp, err) || !forward(*cp, err) || !finite({t}, err)) return false;
        const Clip c = *cp;
        t = snap(t);
        if (!(t > c.at && t < c.end()))
        {
            err = "split at " + t3(t) + " is not inside " + c.id + " (" + t3(c.at) + " .. " + t3(c.end()) + ")";
            return false;
        }
        const double cut = snap(c.in + (t - c.at) * c.speed);
        if (!(cut > c.in && cut < c.out))
        {
            err = "split at " + t3(t) + " leaves a half of " + c.id + " with no frames — refused, not clamped";
            return false;
        }
        const double leftDur = (cut - c.in) / c.speed, rightDur = (c.out - cut) / c.speed;
        for (const auto &x : R.transitions)
        {
            if (x.clipB == c.id && x.dur > leftDur + kEps)
            {
                err = "transition " + x.id + " into " + c.id + " (" + t3(x.dur) + " s) is longer than the left half";
                return false;
            }
            if (x.clipA == c.id && x.dur > rightDur + kEps)
            {
                err = "transition " + x.id + " out of " + c.id + " (" + t3(x.dur) + " s) is longer than the right half";
                return false;
            }
        }

        const Project backup = P;
        // The right half starts from the RESOLVED clip, overrides and all — it is what the user
        // saw — but it is a new node of this timeline's, never a copy of the base's.
        Clip r = c;
        r.id = P.freshId("clp_");
        r.name = c.name.empty() ? std::string() : numbered(P, c.name);
        r.timeline = declaredIn(P, tl, r.track);
        r.at = t;
        r.in = cut;
        r.from.clear();
        r.notes = Notes();
        P.clips.push_back(r);
        for (const auto &x : R.transitions)
            if (x.clipA == c.id && !setFields(P, tl, x.id, {{"between", r.id + "," + x.clipB}}, err))
            {
                P = backup;
                return false;
            }
        if (!setFields(P, tl, c.id, {{"out", t3(cut)}}, err))
        {
            P = backup;
            return false;
        }
        outRight = r.id;
        return true;
    }

    bool move(Project &P, const NodeId &timeline, const NodeId &clip, double at, const NodeId &track, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        const Clip *c = nullptr;
        if (!clipIn(P, timeline, clip, tl, R, c, err) || !finite({at}, err)) return false;
        at = snap(at);
        if (at < 0.0) { err = "a clip cannot start before the timeline does"; return false; }
        Fields kv = {{"at", t3(at)}};
        if (!track.empty()) kv.emplace_back("track", track);
        return setFields(P, tl, c->id, kv, err);
    }

    bool roll(Project &P, const NodeId &timeline, const NodeId &left, const NodeId &right, double t, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        const Clip *ap = nullptr;
        if (!clipIn(P, timeline, left, tl, R, ap, err) || !finite({t}, err)) return false;
        const Clip *bp = rClip(R, P.idForRef(right));
        if (!bp) { err = right + " is not a clip of " + tl; return false; }
        const Clip a = *ap, b = *bp;
        if (!forward(a, err) || !forward(b, err)) return false;
        if (a.track != b.track) { err = "roll needs two clips on one track; " + a.id + " and " + b.id + " are not"; return false; }
        if (std::fabs(a.end() - b.at) > kAdjacent)
        {
            err = a.id + " ends at " + t3(a.end()) + " and " + b.id + " starts at " + t3(b.at) + " — roll needs adjacent clips";
            return false;
        }
        t = snap(t);
        const double aOut = snap(a.in + (t - a.at) * a.speed);
        const double bIn = snap(b.in + (t - b.at) * b.speed);
        if (aOut <= a.in) { err = "rolling to " + t3(t) + " leaves " + a.id + " no frames — refused, not clamped"; return false; }
        if (bIn >= b.out) { err = "rolling to " + t3(t) + " leaves " + b.id + " no frames — refused, not clamped"; return false; }
        if (bIn < 0.0) { err = "rolling to " + t3(t) + " reads " + b.id + " before its source's first frame"; return false; }
        const Project backup = P;
        if (!setFields(P, tl, a.id, {{"out", t3(aOut)}}, err) ||
            !setFields(P, tl, b.id, {{"in", t3(bIn)}, {"at", t3(t)}}, err))
        {
            P = backup;
            return false;
        }
        return true;
    }

    bool slip(Project &P, const NodeId &timeline, const NodeId &clip, double delta, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        const Clip *c = nullptr;
        if (!clipIn(P, timeline, clip, tl, R, c, err) || !finite({delta}, err)) return false;
        const double in = snap(c->in + delta), out = snap(c->out + delta);
        if (in < 0.0)
        {
            err = "slipping " + c->id + " by " + canonicalNumber(delta) + " s reads before the source's first frame";
            return false;
        }
        return setFields(P, tl, c->id, {{"in", t3(in)}, {"out", t3(out)}}, err);
    }

    bool remove(Project &P, const NodeId &timeline, const NodeId &clip, bool ripple, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        const Clip *cp = nullptr;
        if (!clipIn(P, timeline, clip, tl, R, cp, err)) return false;
        const Clip c = *cp;
        const Project backup = P;
        if (!dropNode(P, tl, c.id, err)) return false;
        if (!ripple) return true;
        ResolvedTimeline after;
        if (!resolve(P, tl, after, err)) { P = backup; return false; }
        const double len = c.duration();
        for (const auto &x : after.clips)
            if (x.track == c.track && x.at >= c.end() - kAdjacent &&
                !setFields(P, tl, x.id, {{"at", t3(snap(x.at - len))}}, err))
            {
                P = backup;
                return false;
            }
        return true;
    }

    bool insertEdit(Project &P, const NodeId &timeline, const NodeId &track, const NodeId &src, double in, double out,
                    double at, NodeId &outId, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        if (!timelineOf(P, timeline, tl, err) || !resolve(P, tl, R, err)) return false;
        const NodeId trk = P.idForRef(track);
        const Track *t = rTrack(R, trk);
        if (!t || t->audio()) { err = track + " is not a video track of " + tl; return false; }
        if (!(out > in)) { err = "an insert needs out after in"; return false; }
        at = snap(std::max(0.0, at));
        const double len = snap(out - in);
        const Project backup = P;
        // a clip of the target track across the record in is split there: the insert goes between
        for (const auto &c : R.clips)
            if (c.track == trk && c.at < at - kEps && c.end() > at + kEps)
            {
                NodeId right;
                if (!split(P, tl, c.id, at, right, err)) { P = backup; return false; }
                break;
            }
        ResolvedTimeline after;
        if (!resolve(P, tl, after, err)) { P = backup; return false; }
        // everything at or after the record in moves right — latest first, so nothing passes through another
        std::vector<const Clip *> later;
        for (const auto &c : after.clips) if (c.at >= at - kEps) later.push_back(&c);
        std::sort(later.begin(), later.end(), [](const Clip *a, const Clip *b) { return a->at > b->at; });
        for (const Clip *c : later)
            if (!setFields(P, tl, c->id, {{"at", t3(snap(c->at + len))}}, err)) { P = backup; return false; }
        for (const auto &a : after.audioClips)
            if (a.at >= at - kEps && !setFields(P, tl, a.id, {{"at", t3(snap(a.at + len))}}, err)) { P = backup; return false; }
        if (!addClip(P, tl, trk, src, in, out, at, std::string(), outId, err)) { P = backup; return false; }
        return true;
    }

    bool overwriteEdit(Project &P, const NodeId &timeline, const NodeId &track, const NodeId &src, double in, double out,
                       double at, NodeId &outId, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        if (!timelineOf(P, timeline, tl, err) || !resolve(P, tl, R, err)) return false;
        const NodeId trk = P.idForRef(track);
        const Track *t = rTrack(R, trk);
        if (!t || t->audio()) { err = track + " is not a video track of " + tl; return false; }
        if (!(out > in)) { err = "an overwrite needs out after in"; return false; }
        at = snap(std::max(0.0, at));
        const double a = at, b = snap(at + out - in);
        const Project backup = P;
        // split what crosses either end, then drop everything inside [a, b)
        for (double cut : {a, b})
        {
            ResolvedTimeline now;
            if (!resolve(P, tl, now, err)) { P = backup; return false; }
            for (const auto &c : now.clips)
                if (c.track == trk && c.at < cut - kEps && c.end() > cut + kEps)
                {
                    NodeId right;
                    if (!split(P, tl, c.id, cut, right, err)) { P = backup; return false; }
                    break;
                }
        }
        ResolvedTimeline cut;
        if (!resolve(P, tl, cut, err)) { P = backup; return false; }
        for (const auto &c : cut.clips)
            if (c.track == trk && c.at >= a - kEps && c.end() <= b + kEps && !dropNode(P, tl, c.id, err)) { P = backup; return false; }
        if (!addClip(P, tl, trk, src, in, out, at, std::string(), outId, err)) { P = backup; return false; }
        return true;
    }

    bool addTransition(Project &P, const NodeId &timeline, const NodeId &a, const NodeId &b, const std::string &kind,
                       double dur, NodeId &outId, std::string &err)
    {
        NodeId tl;
        ResolvedTimeline R;
        const Clip *A = nullptr;
        if (!clipIn(P, timeline, a, tl, R, A, err)) return false;
        const Clip *B = rClip(R, P.idForRef(b));
        if (!B) { err = b + " is not a clip of " + tl; return false; }
        if (A->track != B->track) { err = "a transition joins two clips of one track"; return false; }
        if (!(A->at < B->at)) { err = A->id + " must come before " + B->id; return false; }
        if (kind != "dissolve" && kind != "dip") { err = "a transition is dissolve or dip, not '" + kind + "'"; return false; }
        if (!std::isfinite(dur) || !(snap(dur) > 0.0)) { err = "a transition's duration is positive"; return false; }
        dur = snap(dur);
        if (dur > A->duration() + kEps || dur > B->duration() + kEps)
        {
            const Clip *s = dur > A->duration() + kEps ? A : B;
            err = "a " + t3(dur) + " s transition is longer than " + s->id + " (" + t3(s->duration()) +
                  " s) — it would consume the clip";
            return false;
        }
        for (const auto &x : R.transitions)
            if (x.clipA == A->id && x.clipB == B->id) { err = "there is already a transition between them: " + x.id; return false; }
        Transition x;
        x.id = P.freshId("tr_");
        x.track = A->track;
        x.timeline = declaredIn(P, tl, A->track);
        x.clipA = A->id;
        x.clipB = B->id;
        x.kind = kind;
        x.dur = dur;
        P.transitions.push_back(x);
        outId = x.id;
        return true;
    }
}
}
}
