#include "Timeline.h"
#include <algorithm>
#include <cmath>

namespace arstro
{
namespace interstellar
{
    Track *Timeline::addTrack(bool audio, const std::string &name, int order, std::string &err)
    {
        Track t;
        t.id = mP.freshId("trk_");
        t.name = name.empty() ? mP.freshName(audio ? "a0" : "v0") : name;
        std::string why;
        if (!Project::nameIsLegal(t.name, why)) { err = why; return nullptr; }
        if (mP.nameIsTaken(t.name)) { err = "bind name already used: " + t.name; return nullptr; }
        t.audio = audio;
        t.order = order >= 0 ? order : (int)mP.tracks.size();
        mP.tracks.push_back(t);
        return &mP.tracks.back();
    }

    Clip *Timeline::addClip(const std::string &trackRef, const std::string &src, double in,
                            double out, double at, const std::string &name, std::string &err)
    {
        const Track *tr = mP.track(trackRef);
        if (!tr) { err = "no such track: " + trackRef; return nullptr; }
        if (in >= out) { err = "in >= out — a clip with no frames is not a clip"; return nullptr; }
        Clip c;
        c.id = mP.freshId("clp_");
        c.name = name.empty() ? mP.freshName("clp") : name;
        std::string why;
        if (!Project::nameIsLegal(c.name, why)) { err = why; return nullptr; }
        if (mP.nameIsTaken(c.name)) { err = "bind name already used: " + c.name; return nullptr; }
        c.track = tr->id;
        c.src = src;
        c.in = in; c.out = out; c.at = at;
        c.order = (int)mP.clips.size();
        mP.clips.push_back(c);
        return &mP.clips.back();
    }

    bool Timeline::trim(const std::string &clipRef, bool head, double t, std::string &err)
    {
        Clip *c = mP.clip(clipRef);
        if (!c) { err = "no such clip: " + clipRef; return false; }
        if (head)
        {
            if (t >= c->out) { err = "trim would leave no frames"; return false; }
            // Trimming the head moves the source in-point AND the timeline position together,
            // so the frames that remain stay where they were on the timeline.
            const double delta = t - c->in;
            c->in = t;
            c->at += delta / (c->speed != 0 ? c->speed : 1.0);
        }
        else
        {
            if (t <= c->in) { err = "trim would leave no frames"; return false; }
            c->out = t;
        }
        return true;
    }

    bool Timeline::split(const std::string &clipRef, double at, std::string &err)
    {
        Clip *c = mP.clip(clipRef);
        if (!c) { err = "no such clip: " + clipRef; return false; }
        if (at <= c->at || at >= c->end()) { err = "the split point is outside the clip"; return false; }
        const double localIn = (at - c->at) * c->speed + c->in;
        Clip right = *c;
        right.id = mP.freshId("clp_");
        right.name = mP.freshName(c->name);
        right.in = localIn;
        right.at = at;
        right.order = c->order + 1;
        c->out = localIn;
        for (auto &o : mP.clips)
            if (o.track == right.track && o.order > c->order && o.id != c->id) ++o.order;
        mP.clips.push_back(right);
        return true;
    }

    bool Timeline::move(const std::string &clipRef, double at, const std::string &trackRef,
                        std::string &err)
    {
        Clip *c = mP.clip(clipRef);
        if (!c) { err = "no such clip: " + clipRef; return false; }
        if (!trackRef.empty())
        {
            const Track *tr = mP.track(trackRef);
            if (!tr) { err = "no such track: " + trackRef; return false; }
            c->track = tr->id;
        }
        c->at = at < 0 ? 0 : at;
        return true;
    }

    bool Timeline::roll(const std::string &a, const std::string &b, double dt, std::string &err)
    {
        Clip *ca = mP.clip(a);
        Clip *cb = mP.clip(b);
        if (!ca || !cb) { err = "roll needs two clips"; return false; }
        if (ca->track != cb->track) { err = "roll needs two clips on one track"; return false; }
        if (ca->at > cb->at) std::swap(ca, cb);
        const double newOut = ca->out + dt * ca->speed;
        const double newIn = cb->in + dt * cb->speed;
        if (newOut <= ca->in) { err = "roll would empty the outgoing clip"; return false; }
        if (newIn >= cb->out) { err = "roll would empty the incoming clip"; return false; }
        ca->out = newOut;
        cb->in = newIn;
        cb->at += dt;
        return true;
    }

    bool Timeline::slip(const std::string &clipRef, double dt, std::string &err)
    {
        Clip *c = mP.clip(clipRef);
        if (!c) { err = "no such clip: " + clipRef; return false; }
        // The window slides; the length and the timeline position do not.
        const double len = c->out - c->in;
        double in = c->in + dt;
        if (in < 0) in = 0;
        c->in = in;
        c->out = in + len;
        return true;
    }

    bool Timeline::remove(const std::string &clipRef, bool ripple, std::string &err)
    {
        Clip *c = mP.clip(clipRef);
        if (!c) { err = "no such clip: " + clipRef; return false; }
        const std::string track = c->track;
        const double at = c->at, dur = c->duration();
        const NodeId id = c->id;
        mP.clips.erase(std::remove_if(mP.clips.begin(), mP.clips.end(),
                                      [&](const Clip &x) { return x.id == id; }),
                       mP.clips.end());
        // A transition that named it cannot survive it.
        mP.transitions.erase(std::remove_if(mP.transitions.begin(), mP.transitions.end(),
                                            [&](const Transition &t) {
                                                return t.clipA == id || t.clipB == id;
                                            }),
                             mP.transitions.end());
        if (ripple)
            for (auto &x : mP.clips)
                if (x.track == track && x.at >= at) x.at -= dur;
        return true;
    }

    Transition *Timeline::addTransition(const std::string &a, const std::string &b,
                                        const std::string &kind, double dur, std::string &err)
    {
        const Clip *ca = mP.clip(a);
        const Clip *cb = mP.clip(b);
        if (!ca || !cb) { err = "a transition needs two clips"; return nullptr; }
        if (ca->track != cb->track) { err = "a transition needs two clips on one track"; return nullptr; }
        if (kind != "dissolve" && kind != "dip") { err = "transition kind: dissolve | dip"; return nullptr; }
        if (dur > ca->duration() || dur > cb->duration())
        { err = "the transition is longer than one of its clips"; return nullptr; }
        Transition t;
        t.id = mP.freshId("tr_");
        t.name = mP.freshName("tr");
        t.track = ca->track;
        t.clipA = ca->id;
        t.clipB = cb->id;
        t.kind = kind;
        t.dur = dur;
        // A dissolve is a LINEAR alpha ramp: an eased one reads as a luminance bump in the
        // middle, because two frames at 50% do not sum to one at 100%.
        t.easing = Ease::Linear;
        mP.transitions.push_back(t);
        return &mP.transitions.back();
    }

    std::vector<double> Timeline::cutPoints() const
    {
        std::vector<double> v{0.0};
        for (const auto &c : mP.clips) { v.push_back(c.at); v.push_back(c.end()); }
        for (const auto &m : mP.markers) v.push_back(m.at);
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(),
                            [](double a, double b) { return std::fabs(a - b) < 1e-6; }),
                v.end());
        return v;
    }
    double Timeline::nextCut(double t) const
    {
        for (double c : cutPoints())
            if (c > t + 1e-6) return c;
        return mP.duration();
    }
    double Timeline::prevCut(double t) const
    {
        double best = 0;
        for (double c : cutPoints())
            if (c < t - 1e-6) best = c;
        return best;
    }
}
}
