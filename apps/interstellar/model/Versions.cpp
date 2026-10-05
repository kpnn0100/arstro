/*
 *  interstellar_model — Versions: resolution, derived-aware edits, colour deltas, freeze/thaw,
 *  rebase and diff.
 *
 *  How the pieces lean on each other: `resolve` is the single definition of what a version
 *  contains, and every edit begins by resolving — an edit is validated against the timeline the
 *  user is LOOKING at, never against raw document rows, because a row may be overridden, dropped
 *  or a local addition depending on where you stand. Ownership (which timeline declares a node)
 *  then decides the one question that matters: edit in place, or record a delta.
 *
 *  Order inside one resolution step: explicit drops, then overrides, then the CONSEQUENCES of the
 *  drops (inherited clips on a dropped track, transitions touching a dropped clip), then local
 *  additions, then anchor and conflict checks. Overrides precede consequences on purpose: a user
 *  who moved an inherited clip off a track and then dropped the track expects the clip to stay.
 */
#include "Versions.h"
#include "Schema.h"
#include <algorithm>
#include <cmath>
#include <set>
#include <type_traits>

namespace arstro
{
namespace interstellar
{
    std::string deltaId(const std::string &kind, const NodeId &timeline, const NodeId &node)
    {
        return kind + ":" + timeline + ":" + node;
    }

    namespace
    {
        constexpr double kEps = 1e-9;

        template <class R, class F> bool visitResolved(R &r, const NodeId &id, F &&f)
        {
            for (auto &n : r.tracks) if (n.id == id) { f(n); return true; }
            for (auto &n : r.clips) if (n.id == id) { f(n); return true; }
            for (auto &n : r.transitions) if (n.id == id) { f(n); return true; }
            for (auto &n : r.markers) if (n.id == id) { f(n); return true; }
            for (auto &n : r.audioTracks) if (n.id == id) { f(n); return true; }
            for (auto &n : r.audioClips) if (n.id == id) { f(n); return true; }
            return false;
        }

        template <class F> bool visitDoc(Project &p, const NodeId &id, F &&f)
        {
            for (auto &n : p.tracks) if (n.id == id) { f(n); return true; }
            for (auto &n : p.clips) if (n.id == id) { f(n); return true; }
            for (auto &n : p.transitions) if (n.id == id) { f(n); return true; }
            for (auto &n : p.markers) if (n.id == id) { f(n); return true; }
            for (auto &n : p.audioTracks) if (n.id == id) { f(n); return true; }
            for (auto &n : p.audioClips) if (n.id == id) { f(n); return true; }
            return false;
        }

        template <class V> void eraseIf(V &v, const std::set<NodeId> &ids)
        {
            v.erase(std::remove_if(v.begin(), v.end(), [&](const auto &n) { return ids.count(n.id) > 0; }), v.end());
        }

        void put(Fields &kv, const std::string &k, const std::string &v)
        {
            for (auto &p : kv)
                if (p.first == k) { p.second = v; return; }
            kv.emplace_back(k, v);
        }
        void unput(Fields &kv, const std::string &k)
        {
            kv.erase(std::remove_if(kv.begin(), kv.end(), [&](const auto &p) { return p.first == k; }), kv.end());
        }

        bool timelineId(const Project &p, const NodeId &ref, NodeId &out, std::string &err)
        {
            out = p.idForRef(ref);
            if (p.timeline(out)) return true;
            std::string names;
            for (const auto &t : p.timelines) names += (names.empty() ? "" : ", ") + (t.name.empty() ? t.id : t.name);
            err = "no such timeline: " + ref + " (timelines: " + (names.empty() ? "none" : names) + ")";
            return false;
        }

        std::string label(const Project &p, const NodeId &id)
        {
            std::string name;
            if (const auto *n = p.track(id)) name = n->name;
            else if (const auto *n = p.clip(id)) name = n->name;
            else if (const auto *n = p.transition(id)) name = n->name;
            else if (const auto *n = p.marker(id)) name = n->name;
            else if (const auto *n = p.audioTrack(id)) name = n->name;
            else if (const auto *n = p.audioClip(id)) name = n->name;
            else if (const auto *n = p.rackObj(id)) name = n->name;
            else if (const auto *n = p.timeline(id)) name = n->name;
            return name.empty() || name == id ? id : id + " (" + name + ")";
        }

        /** The timeline that owns the TRACK a node sits on; "" when the track is gone. */
        NodeId trackOwner(const Project &p, const NodeId &track)
        {
            if (const Track *t = p.track(track)) return t->timeline;
            if (const ATrack *a = p.audioTrack(track)) return a->timeline;
            if (const RawNode *r = p.rawNode(track))
                for (const auto &kv : r->fields)
                    if (kv.first == "timeline") return kv.second;
            return {};
        }

        /** Keep a clip/transition/audio clip declared in `tl` after its track changed: explicit
         *  `timeline=` exactly when the track belongs to someone else (or is gone). */
        template <class T> void rehome(const Project &p, const NodeId &tl, T &n)
        {
            if constexpr (std::is_same<T, Clip>::value || std::is_same<T, Transition>::value ||
                          std::is_same<T, AClip>::value)
                n.timeline = trackOwner(p, n.track) == tl ? NodeId() : tl;
            (void)p; (void)tl; (void)n;
        }

        std::string whyMissing(const Project &p, const Timeline &t, const NodeId &node)
        {
            if (t.base.empty()) return t.id + " is a root, so a delta on " + node + " has no base to apply to";
            if (t.frozen()) return "the cut of " + t.id + " is frozen, so a delta on " + node + " has nothing to apply to";
            if (p.kindOf(node) == NodeKind::None) return "the base no longer has " + node;
            if (p.ownerOf(node) == t.id) return node + " is " + t.id + "'s own node — edit it directly";
            return node + " is not in the resolution of " + t.base;
        }

        bool resolveImpl(const Project &P, const NodeId &tlId, ResolvedTimeline &out, std::string &err,
                         std::vector<NodeId> &stack)
        {
            const Timeline *T = P.timeline(tlId);
            if (!T) { err = "no such timeline: " + tlId; return false; }
            if (std::find(stack.begin(), stack.end(), tlId) != stack.end())
            {
                err = "the base chain is a cycle at " + tlId;
                return false;
            }
            out = ResolvedTimeline();
            out.timeline = tlId;
            std::set<NodeId> inherited;
            if (!T->base.empty() && !T->frozen())
            {
                stack.push_back(tlId);
                ResolvedTimeline B;
                const bool ok = resolveImpl(P, T->base, B, err, stack);
                stack.pop_back();
                if (!ok) return false;
                out.tracks = std::move(B.tracks);
                out.clips = std::move(B.clips);
                out.transitions = std::move(B.transitions);
                out.markers = std::move(B.markers);
                out.audioTracks = std::move(B.audioTracks);
                out.audioClips = std::move(B.audioClips);
                // Re-derived at this level rather than copied: a node broken in the base may be
                // fixed by an override here, and a healthy one broken by one.
                for (const auto &kv : B.provenance)
                {
                    out.provenance[kv.first] = Provenance::Inherited;
                    inherited.insert(kv.first);
                }
            }
            auto eraseAll = [&](const std::set<NodeId> &ids) {
                eraseIf(out.tracks, ids); eraseIf(out.clips, ids); eraseIf(out.transitions, ids);
                eraseIf(out.markers, ids); eraseIf(out.audioTracks, ids); eraseIf(out.audioClips, ids);
                for (const auto &i : ids) out.provenance.erase(i);
            };

            // ── minus every #tldrop ──
            std::set<NodeId> dropped;
            for (const auto &d : P.drops)
            {
                if (d.timeline != tlId) continue;
                if (inherited.count(d.node)) dropped.insert(d.node);
                else out.dangling.push_back({deltaId("tldrop", tlId, d.node), d.node, "tldrop", whyMissing(P, *T, d.node)});
            }
            eraseAll(dropped);

            // ── with every #tlset applied ──
            for (const auto &s : P.sets)
            {
                if (s.timeline != tlId) continue;
                if (!inherited.count(s.node))
                {
                    out.dangling.push_back({deltaId("tlset", tlId, s.node), s.node, "tlset", whyMissing(P, *T, s.node)});
                    continue;
                }
                if (dropped.count(s.node))
                {
                    out.dangling.push_back({deltaId("tlset", tlId, s.node), s.node, "tlset",
                                            tlId + " also drops " + s.node + ", so the override applies to nothing"});
                    continue;
                }
                visitResolved(out, s.node, [&](auto &n) { schema::applyLenient(n, s.fields, nullptr); });
                out.provenance[s.node] = Provenance::Overridden;
            }

            // ── consequences of the drops, judged on the overridden values ──
            std::set<NodeId> gone = dropped, consequence;
            for (bool grew = true; grew;)
            {
                grew = false;
                auto sweep = [&](const NodeId &id, std::initializer_list<NodeId> anchors) {
                    if (gone.count(id)) return;
                    for (const auto &a : anchors)
                        if (gone.count(a)) { gone.insert(id); consequence.insert(id); grew = true; return; }
                };
                for (const auto &c : out.clips) sweep(c.id, {c.track});
                for (const auto &a : out.audioClips) sweep(a.id, {a.track});
                for (const auto &t : out.transitions) sweep(t.id, {t.track, t.clipA, t.clipB});
            }
            eraseAll(consequence);

            // ── plus every node declared here ──
            for (const auto &n : P.tracks)
                if (n.timeline == tlId) { out.tracks.push_back(n); out.provenance[n.id] = Provenance::Local; }
            for (const auto &n : P.markers)
                if (n.timeline == tlId) { out.markers.push_back(n); out.provenance[n.id] = Provenance::Local; }
            for (const auto &n : P.audioTracks)
                if (n.timeline == tlId) { out.audioTracks.push_back(n); out.provenance[n.id] = Provenance::Local; }
            for (const auto &n : P.clips)
                if (P.clipTimeline(n) == tlId) { out.clips.push_back(n); out.provenance[n.id] = Provenance::Local; }
            for (const auto &n : P.transitions)
                if (P.transitionTimeline(n) == tlId) { out.transitions.push_back(n); out.provenance[n.id] = Provenance::Local; }
            for (const auto &n : P.audioClips)
                if (P.audioClipTimeline(n) == tlId) { out.audioClips.push_back(n); out.provenance[n.id] = Provenance::Local; }

            // ── anchors and conflicts ──
            const std::map<NodeId, Provenance> origin = out.provenance;
            std::set<NodeId> videoTracks, audioLanes, clipIds;
            for (const auto &t : out.tracks) (t.audio() ? audioLanes : videoTracks).insert(t.id);
            for (const auto &t : out.audioTracks) audioLanes.insert(t.id);
            for (const auto &r : P.raw)
                if (r.type == "atrack" && !r.id.empty()) audioLanes.insert(r.id);  // refused at render, not lost
            for (const auto &c : out.clips) clipIds.insert(c.id);

            std::set<NodeId> listed;
            auto flag = [&](const NodeId &id, const NodeId &target, const char *kind, const std::string &why,
                            const NodeId &culprit = NodeId()) {
                out.provenance[id] = Provenance::Dangling;
                if (listed.count(id)) return;
                const Provenance was = origin.at(id);
                if (was == Provenance::Local && culprit.empty())
                    out.dangling.push_back({id, target, kind, why});
                else if (was == Provenance::Overridden || !culprit.empty())
                    out.dangling.push_back({culprit.empty() ? deltaId("tlset", tlId, id) : culprit, target, "conflict", why});
                else
                    return;                     // inherited broken: its owner lists it
                listed.insert(id);
            };
            for (const auto &c : out.clips)
                if (!videoTracks.count(c.track)) flag(c.id, c.track, "clip", "its track " + c.track + " is not in " + tlId);
            for (const auto &a : out.audioClips)
                if (!audioLanes.count(a.track)) flag(a.id, a.track, "aclip", "its track " + a.track + " is not in " + tlId);
            for (const auto &t : out.transitions)
            {
                if (!videoTracks.count(t.track)) flag(t.id, t.track, "transition", "its track " + t.track + " is not in " + tlId);
                else if (!clipIds.count(t.clipA)) flag(t.id, t.clipA, "transition", "its outgoing clip " + t.clipA + " is not in " + tlId);
                else if (!clipIds.count(t.clipB)) flag(t.id, t.clipB, "transition", "its incoming clip " + t.clipB + " is not in " + tlId);
            }
            for (const auto &c : out.clips)
                if (!(c.in < c.out) || c.speed == 0.0)
                    flag(c.id, c.id, "clip", "no frames once applied: in " + canonicalTime(c.in) + ", out " +
                                                 canonicalTime(c.out) + ", speed " + canonicalNumber(c.speed));
            for (const auto &a : out.audioClips)
                if (!(a.in < a.out))
                    flag(a.id, a.id, "aclip", "no frames once applied: in " + canonicalTime(a.in) + ", out " + canonicalTime(a.out));
            for (const auto &t : out.transitions)
            {
                if (out.provenance[t.id] == Provenance::Dangling) continue;
                const Clip *a = nullptr, *b = nullptr;
                for (const auto &c : out.clips)
                {
                    if (c.id == t.clipA) a = &c;
                    if (c.id == t.clipB) b = &c;
                }
                if (!a || !b) continue;
                // Beside a clip already broken, the transition cannot render either — but its
                // cause is that clip's, already listed once; a second line would be noise.
                if (out.provenance[a->id] == Provenance::Dangling || out.provenance[b->id] == Provenance::Dangling)
                {
                    out.provenance[t.id] = Provenance::Dangling;
                    continue;
                }
                const Clip *shortN = t.dur > a->duration() + kEps ? a : t.dur > b->duration() + kEps ? b : nullptr;
                if (!shortN) continue;
                // Listed when this timeline touched any of the three; otherwise the base owns it.
                NodeId culprit;
                const Provenance pt = origin.at(t.id), pa = origin.at(a->id), pb = origin.at(b->id);
                if (pt == Provenance::Local) culprit = t.id;
                else if (pt == Provenance::Overridden) culprit = deltaId("tlset", tlId, t.id);
                else if (pa != Provenance::Inherited) culprit = pa == Provenance::Local ? a->id : deltaId("tlset", tlId, a->id);
                else if (pb != Provenance::Inherited) culprit = pb == Provenance::Local ? b->id : deltaId("tlset", tlId, b->id);
                const std::string why = "transition " + t.id + " (" + canonicalTime(t.dur) + " s) is longer than " +
                                        shortN->id + " (" + canonicalTime(shortN->duration()) + " s)";
                if (culprit.empty()) out.provenance[t.id] = Provenance::Dangling;
                else flag(t.id, shortN->id, "conflict", why, culprit);
            }

            // ── colour deltas on a rack node that is gone ──
            for (const auto &g : P.grades)
                if (g.timeline == tlId && !P.rackObj(g.node))
                    out.dangling.push_back({deltaId("tlgrade", tlId, g.node), g.node, "tlgrade", "the rack no longer has " + g.node});

            std::sort(out.tracks.begin(), out.tracks.end(), [](const Track &a, const Track &b) {
                return a.order != b.order ? a.order < b.order : a.id < b.id;
            });
            std::sort(out.clips.begin(), out.clips.end(), [](const Clip &a, const Clip &b) {
                if (a.at != b.at) return a.at < b.at;
                return a.order != b.order ? a.order < b.order : a.id < b.id;
            });
            std::sort(out.transitions.begin(), out.transitions.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
            std::sort(out.markers.begin(), out.markers.end(), [](const Marker &a, const Marker &b) {
                return a.at != b.at ? a.at < b.at : a.id < b.id;
            });
            std::sort(out.audioTracks.begin(), out.audioTracks.end(), [](const ATrack &a, const ATrack &b) {
                return a.order != b.order ? a.order < b.order : a.id < b.id;
            });
            std::sort(out.audioClips.begin(), out.audioClips.end(), [](const AClip &a, const AClip &b) {
                return a.at != b.at ? a.at < b.at : a.id < b.id;
            });
            return true;
        }
    }

    bool resolve(const Project &P, const NodeId &timeline, ResolvedTimeline &out, std::string &err)
    {
        NodeId tl;
        if (!timelineId(P, timeline, tl, err)) return false;
        std::vector<NodeId> stack;
        return resolveImpl(P, tl, out, err, stack);
    }

    namespace
    {
        // the timelines `tl`'s clips place, directly, as `tl` resolves (a version may re-point one)
        std::vector<NodeId> placedTimelines(const Project &P, const NodeId &tl)
        {
            std::vector<NodeId> out;
            ResolvedTimeline R;
            std::string err;
            if (!resolve(P, tl, R, err)) return out;
            for (const auto &c : R.clips)
                if (P.timeline(c.src)) out.push_back(c.src);
            return out;
        }

        std::string timelineName(const Project &P, const NodeId &id)
        {
            const Timeline *t = P.timeline(id);
            return t && !t->name.empty() ? t->name : id;
        }
    }

    bool nestingRefused(const Project &P, const NodeId &into, const NodeId &src, std::string &err)
    {
        // where the new clip would live: `into`, and every version of it (they inherit the clip)
        std::set<NodeId> holders;
        for (const auto &t : P.timelines)
            for (const auto &c : P.chain(t.id))
                if (c == into) holders.insert(t.id);
        // what `src` shows, at every depth, itself included — reaching a holder closes a loop
        std::set<NodeId> seen;
        std::vector<std::pair<NodeId, std::string>> todo{{src, timelineName(P, src)}};
        while (!todo.empty())
        {
            const auto [x, path] = todo.back();
            todo.pop_back();
            if (!seen.insert(x).second) continue;
            if (holders.count(x))
            {
                err = "placing " + timelineName(P, src) + " in " + timelineName(P, into) + " would put a timeline inside itself (" +
                      timelineName(P, into) + " → " + path + (x == into ? "" : ", a version of " + timelineName(P, into)) + ")";
                return true;
            }
            for (const auto &y : placedTimelines(P, x)) todo.push_back({y, path + " → " + timelineName(P, y)});
        }
        return false;
    }

    std::vector<NodeId> nestingCycles(const Project &P)
    {
        std::vector<NodeId> out;
        for (const auto &t : P.timelines)
        {
            std::set<NodeId> seen;
            std::vector<NodeId> todo = placedTimelines(P, t.id);
            bool loop = false;
            while (!todo.empty() && !loop)
            {
                const NodeId x = todo.back();
                todo.pop_back();
                if (x == t.id) loop = true;
                else if (seen.insert(x).second)
                    for (const auto &y : placedTimelines(P, x)) todo.push_back(y);
            }
            if (loop) out.push_back(t.id);
        }
        return out;
    }

    std::vector<std::pair<std::string, double>> gradeDeltas(const Project &P, const NodeId &timeline, const NodeId &rackObj)
    {
        std::vector<std::pair<std::string, double>> out;
        const NodeId ro = P.idForRef(rackObj);
        const NodeId start = P.idForRef(timeline);
        for (const NodeId &t : P.chain(start))
        {
            if (const TlGrade *g = P.tlgrade(t, ro))
                for (const auto &d : g->deltas)
                {
                    bool have = false;
                    for (const auto &o : out) have = have || o.first == d.first;
                    if (!have) out.push_back(d);       // nearest timeline already spoke for this key
                }
            const Timeline *x = P.timeline(t);
            if (x && x->pinned()) break;
        }
        return out;
    }

    bool newTimeline(Project &P, const std::string &name, const NodeId &base, NodeId &outId, std::string &err)
    {
        if (!Project::nameIsLegal(name, err)) return false;
        if (P.nameIsTaken(name)) { err = "that bind name is already used: " + name; return false; }
        NodeId b;
        if (!base.empty() && !timelineId(P, base, b, err)) return false;
        Timeline t;
        t.id = P.freshId("tl_");
        t.name = name;
        t.base = b;
        for (const auto &x : P.timelines) t.order = std::max(t.order, x.order + 1);
        P.timelines.push_back(t);
        outId = t.id;
        return true;
    }

    // ── derived-aware editing ────────────────────────────────────────────────────────────────
    namespace
    {
        const Track *rTrack(const ResolvedTimeline &R, const NodeId &id)
        {
            for (const auto &t : R.tracks) if (t.id == id) return &t;
            return nullptr;
        }
        const Clip *rClip(const ResolvedTimeline &R, const NodeId &id)
        {
            for (const auto &c : R.clips) if (c.id == id) return &c;
            return nullptr;
        }

        std::string trackList(const ResolvedTimeline &R, bool audio)
        {
            std::string s;
            for (const auto &t : R.tracks)
                if (t.audio() == audio) s += (s.empty() ? "" : ", ") + t.id;
            if (audio)
                for (const auto &t : R.audioTracks) s += (s.empty() ? "" : ", ") + t.id;
            return s.empty() ? "none" : s;
        }

        /** A reference an edit wants to write must resolve INSIDE this timeline: a clip moved to
         *  a track the user cannot see in this version is a clip they have lost. */
        bool checkRef(const Project &P, const ResolvedTimeline &R, const std::string &type, const std::string &key,
                      const NodeId &id, std::string &err)
        {
            if (type == "clip" && key == "track")
            {
                const Track *t = rTrack(R, id);
                if (t && !t->audio()) return true;
                err = "track " + id + " is not a video track of " + R.timeline + " (video tracks: " + trackList(R, false) + ")";
                return false;
            }
            if (type == "clip" && key == "src")
            {
                if (P.rackObj(id)) return true;
                if (P.timeline(id)) return !nestingRefused(P, R.timeline, id, err);   // R-EDT-4
                std::string names;
                for (const auto &n : P.bindNames()) names += (names.empty() ? "" : ", ") + n;
                err = "src=" + id + " names no #rackobj or #timeline; the rack's bind names are: " + (names.empty() ? "(none)" : names);
                return false;
            }
            if (type == "aclip" && key == "track")
            {
                const Track *t = rTrack(R, id);
                if (t && t->audio()) return true;
                for (const auto &a : R.audioTracks) if (a.id == id) return true;
                err = "track " + id + " is not an audio track of " + R.timeline + " (audio tracks: " + trackList(R, true) + ")";
                return false;
            }
            if (type == "atrack" && key == "out")
            {
                const NodeKind k = P.kindOf(id);
                if (k == NodeKind::Track || k == NodeKind::ATrack || k == NodeKind::Raw) return true;
                err = "out=" + id + " names no track or bus";
                return false;
            }
            return true;
        }

        bool invariants(const Project &, const ResolvedTimeline &R, const Clip &c, std::string &err)
        {
            if (c.speed == 0.0) { err = "clip " + c.id + ": speed 0 leaves no frames — refused, not clamped"; return false; }
            if (!(c.in < c.out))
            {
                err = "clip " + c.id + ": in " + canonicalTime(c.in) + " >= out " + canonicalTime(c.out) +
                      " leaves no frames — refused, not clamped";
                return false;
            }
            if (c.in < 0.0) { err = "clip " + c.id + ": in " + canonicalTime(c.in) + " is before the source's first frame"; return false; }
            if (c.at < 0.0) { err = "clip " + c.id + ": at " + canonicalTime(c.at) + " is before the timeline starts"; return false; }
            for (const auto &t : R.transitions)
                if ((t.clipA == c.id || t.clipB == c.id) && t.dur > c.duration() + kEps)
                {
                    err = "clip " + c.id + " would be " + canonicalTime(c.duration()) + " s, shorter than transition " +
                          t.id + " (" + canonicalTime(t.dur) + " s) — it would consume the clip";
                    return false;
                }
            return true;
        }
        bool invariants(const Project &, const ResolvedTimeline &R, const Transition &t, std::string &err)
        {
            if (!(t.dur > 0.0)) { err = "transition " + t.id + ": dur must be positive"; return false; }
            const Clip *a = rClip(R, t.clipA), *b = rClip(R, t.clipB);
            if (!a || !b) { err = "transition " + t.id + ": between= names clips of " + R.timeline; return false; }
            if (a->track != t.track || b->track != t.track)
            { err = "transition " + t.id + ": both clips must sit on its track " + t.track; return false; }
            if (t.dur > a->duration() + kEps || t.dur > b->duration() + kEps)
            {
                err = "transition " + t.id + " (" + canonicalTime(t.dur) + " s) would be longer than " +
                      (t.dur > a->duration() + kEps ? a->id : b->id) + " — it would consume the clip";
                return false;
            }
            return true;
        }
        bool invariants(const Project &, const ResolvedTimeline &, const AClip &a, std::string &err)
        {
            if (!(a.in < a.out)) { err = "audio clip " + a.id + ": in >= out leaves no samples — refused"; return false; }
            if (a.in < 0.0 || a.at < 0.0) { err = "audio clip " + a.id + ": in and at are never negative"; return false; }
            return true;
        }
        bool invariants(const Project &, const ResolvedTimeline &, const Marker &m, std::string &err)
        {
            if (m.at < 0.0) { err = "marker " + m.id + ": at is never negative"; return false; }
            return true;
        }
        bool invariants(const Project &, const ResolvedTimeline &, const Track &, std::string &) { return true; }
        bool invariants(const Project &, const ResolvedTimeline &, const ATrack &, std::string &) { return true; }

        template <class T>
        bool planNode(const Project &P, const ResolvedTimeline &R, const T &cur, const Fields &kv, Fields &canon,
                      std::string &err)
        {
            T next = cur;
            for (const auto &p : kv)
            {
                if (std::is_same<T, Clip>::value && Project::fieldIsColour(p.first))
                {
                    err = "a clip carries no colour: '" + p.first + "' belongs to the rack node it names (R-TL-2); "
                          "grade the rack node, or override it for this version with a #tlgrade";
                    return false;
                }
                if (p.first == "name" || p.first == "id")
                {
                    err = "'" + p.first + "' is project-wide — use rename, so every reference follows";
                    return false;
                }
                const schema::Field<T> *f = schema::find<T>(p.first);
                if (!f)
                {
                    err = "'" + p.first + "' is not a field of #" + schema::typeName<T>() + " (editable: " +
                          schema::keyList<T>() + ")";
                    return false;
                }
                if (!f->editable)
                {
                    err = "'" + p.first + "' of #" + schema::typeName<T>() + " cannot be edited — identity and shape are "
                          "the same in every version (editable: " + schema::keyList<T>() + ")";
                    return false;
                }
                std::string v = p.second;
                if (f->kind == schema::Kind::Ref)
                {
                    const NodeId id = P.idForRef(v);
                    v = id.empty() ? v : id;
                    if (!checkRef(P, R, schema::typeName<T>(), p.first, v, err)) return false;
                }
                else if (f->kind == schema::Kind::Pair)
                {
                    const auto comma = v.find(',');
                    if (comma == std::string::npos) { err = "between= is outgoing,incoming"; return false; }
                    NodeId a = P.idForRef(v.substr(0, comma)), b = P.idForRef(v.substr(comma + 1));
                    if (!rClip(R, a) || !rClip(R, b)) { err = "between=" + v + " names clips not in " + R.timeline; return false; }
                    v = a + "," + b;
                }
                if (!schema::readField(*f, next, v, true, nullptr, err)) return false;
                put(canon, p.first, schema::fieldText(*f, next));
            }
            return invariants(P, R, next, err);
        }

        /** Apply a validated edit. The fork in this function is the whole of R-G-3. */
        bool applyCanon(Project &P, const NodeId &tl, const NodeId &node, const Fields &canon, std::string &err)
        {
            if (P.ownerOf(node) == tl)
            {
                visitDoc(P, node, [&](auto &n) {
                    using T = std::decay_t<decltype(n)>;
                    std::string e;
                    for (const auto &p : canon) schema::readField(*schema::find<T>(p.first), n, p.second, true, nullptr, e);
                    rehome(P, tl, n);
                });
                return true;
            }
            // Inherited: record a delta, never a copy. The base's value decides whether the
            // override is needed at all — equal to the base is no divergence, so no delta.
            const Timeline *T = P.timeline(tl);
            ResolvedTimeline B;
            if (!T || T->base.empty() || !resolve(P, T->base, B, err)) return false;
            Fields baseText;
            const bool inBase = visitResolved(B, node, [&](const auto &bn) {
                using N = std::decay_t<decltype(bn)>;
                for (const auto &p : canon) baseText.emplace_back(p.first, schema::fieldText(*schema::find<N>(p.first), bn));
            });
            if (!inBase) { err = node + " is not in the base of " + tl; return false; }
            TlSet *s = P.tlset(tl, node);
            if (!s)
            {
                TlSet fresh;
                fresh.timeline = tl;
                fresh.node = node;
                P.sets.push_back(fresh);
                s = &P.sets.back();
            }
            for (size_t i = 0; i < canon.size(); ++i)
            {
                if (canon[i].second == baseText[i].second) unput(s->fields, canon[i].first);
                else put(s->fields, canon[i].first, canon[i].second);
            }
            if (s->fields.empty())
                P.sets.erase(std::remove_if(P.sets.begin(), P.sets.end(),
                                            [&](const TlSet &x) { return x.timeline == tl && x.node == node; }),
                             P.sets.end());
            return true;
        }

        std::string notIn(const Project &P, const NodeId &tl, const NodeId &ref)
        {
            const NodeId id = P.idForRef(ref);
            if (id.empty()) return "no such node: " + ref;
            if (P.tldrop(tl, id)) return id + " is dropped from " + tl;
            const NodeKind k = P.kindOf(id);
            if (k != NodeKind::Track && k != NodeKind::Clip && k != NodeKind::Transition && k != NodeKind::Marker &&
                k != NodeKind::ATrack && k != NodeKind::AClip)
                return id + " is " + std::string(nodeKindName(k)) + ", not an arrangement node";
            return id + " is not in timeline " + tl + " (it belongs to " + P.ownerOf(id) + ")";
        }

        /** This timeline's own nodes that hang on `seed` (and on each other), transitively. */
        std::set<NodeId> dependents(const Project &P, const NodeId &tl, const NodeId &seed)
        {
            std::set<NodeId> gone = {seed};
            for (bool grew = true; grew;)
            {
                grew = false;
                for (const auto &c : P.clips)
                    if (!gone.count(c.id) && gone.count(c.track) && P.clipTimeline(c) == tl) { gone.insert(c.id); grew = true; }
                for (const auto &a : P.audioClips)
                    if (!gone.count(a.id) && gone.count(a.track) && P.audioClipTimeline(a) == tl) { gone.insert(a.id); grew = true; }
                for (const auto &t : P.transitions)
                    if (!gone.count(t.id) && (gone.count(t.track) || gone.count(t.clipA) || gone.count(t.clipB)) &&
                        P.transitionTimeline(t) == tl)
                    { gone.insert(t.id); grew = true; }
            }
            return gone;
        }

        void eraseIds(Project &P, const std::set<NodeId> &ids)
        {
            eraseIf(P.tracks, ids); eraseIf(P.clips, ids); eraseIf(P.transitions, ids);
            eraseIf(P.markers, ids); eraseIf(P.audioTracks, ids); eraseIf(P.audioClips, ids);
            P.effects.erase(std::remove_if(P.effects.begin(), P.effects.end(),
                                           [&](const Fx &x) { return !x.clip.empty() && ids.count(x.clip); }),
                            P.effects.end());
        }
    }

    bool setFields(Project &P, const NodeId &timeline, const NodeId &nodeRef, const Fields &kv, std::string &err)
    {
        NodeId tl;
        if (!timelineId(P, timeline, tl, err)) return false;
        if (kv.empty()) { err = "nothing to set"; return false; }
        ResolvedTimeline R;
        if (!resolve(P, tl, R, err)) return false;
        const NodeId node = P.idForRef(nodeRef);
        Fields canon;
        bool ok = false;
        const bool found = visitResolved(R, node, [&](const auto &cur) { ok = planNode(P, R, cur, kv, canon, err); });
        if (!found) { err = notIn(P, tl, nodeRef); return false; }
        if (!ok) return false;
        return applyCanon(P, tl, node, canon, err);
    }

    bool setField(Project &P, const NodeId &timeline, const NodeId &node, const std::string &key,
                  const std::string &value, std::string &err)
    {
        return setFields(P, timeline, node, {{key, value}}, err);
    }

    bool dropNode(Project &P, const NodeId &timeline, const NodeId &nodeRef, std::string &err)
    {
        NodeId tl;
        if (!timelineId(P, timeline, tl, err)) return false;
        ResolvedTimeline R;
        if (!resolve(P, tl, R, err)) return false;
        const NodeId node = P.idForRef(nodeRef);
        if (!R.provenance.count(node)) { err = notIn(P, tl, nodeRef); return false; }
        std::set<NodeId> gone = dependents(P, tl, node);
        if (P.ownerOf(node) != tl)
        {
            gone.erase(node);                   // the base's node itself is untouched
            if (!P.tldrop(tl, node))
            {
                TlDrop d;
                d.timeline = tl;
                d.node = node;
                P.drops.push_back(d);
            }
            P.sets.erase(std::remove_if(P.sets.begin(), P.sets.end(),
                                        [&](const TlSet &s) { return s.timeline == tl && s.node == node; }),
                         P.sets.end());
        }
        eraseIds(P, gone);
        return true;
    }

    // ── colour ───────────────────────────────────────────────────────────────────────────────
    namespace
    {
        bool rackObjId(const Project &P, const NodeId &ref, NodeId &out, std::string &err)
        {
            out = P.idForRef(ref);
            if (P.rackObj(out)) return true;
            std::string names;
            for (const auto &n : P.bindNames()) names += (names.empty() ? "" : ", ") + n;
            err = ref + " is not a rack node; the rack's bind names are: " + (names.empty() ? "(none)" : names);
            return false;
        }
        bool refusePinned(const Timeline &t, std::string &err)
        {
            if (!t.pinned()) return false;
            err = "timeline " + (t.name.empty() ? t.id : t.name) + " is pinned at " + t.pinCommit() +
                  " — its colour is read-only; a pin that yields is not a pin (unpin it first)";
            return true;
        }
    }

    bool setGrade(Project &P, const NodeId &timeline, const NodeId &rackObj, const std::string &key, double delta,
                  std::string &err)
    {
        NodeId tl, ro;
        if (!timelineId(P, timeline, tl, err)) return false;
        if (refusePinned(*P.timeline(tl), err)) return false;
        if (!rackObjId(P, rackObj, ro, err)) return false;
        bool legal = !key.empty() && (std::isalpha((unsigned char)key[0]) || key[0] == '_');
        for (char c : key) legal = legal && (std::isalnum((unsigned char)c) || c == '_' || c == '.');
        if (!legal) { err = "'" + key + "' is not a parameter key"; return false; }
        if (!std::isfinite(delta)) { err = "a colour delta is a finite number"; return false; }
        TlGrade *g = P.tlgrade(tl, ro);
        if (!g)
        {
            TlGrade fresh;
            fresh.timeline = tl;
            fresh.node = ro;
            P.grades.push_back(fresh);
            g = &P.grades.back();
        }
        for (auto &d : g->deltas)
            if (d.first == key) { d.second = delta; return true; }
        g->deltas.emplace_back(key, delta);
        return true;
    }

    bool clearGrade(Project &P, const NodeId &timeline, const NodeId &rackObj, const std::string &key, std::string &err)
    {
        NodeId tl, ro;
        if (!timelineId(P, timeline, tl, err)) return false;
        if (refusePinned(*P.timeline(tl), err)) return false;
        if (!rackObjId(P, rackObj, ro, err)) return false;
        TlGrade *g = P.tlgrade(tl, ro);
        if (!key.empty())
        {
            bool had = false;
            if (g)
                for (const auto &d : g->deltas) had = had || d.first == key;
            if (!had)
            {
                std::string keys;
                if (g)
                    for (const auto &d : g->deltas) keys += (keys.empty() ? "" : ", ") + d.first;
                err = tl + " has no '" + key + "' override on " + ro + " (overrides: " + (keys.empty() ? "none" : keys) + ")";
                return false;
            }
            g->deltas.erase(std::remove_if(g->deltas.begin(), g->deltas.end(), [&](const auto &d) { return d.first == key; }),
                            g->deltas.end());
        }
        if (!g) return true;
        if (key.empty() || g->deltas.empty())
            P.grades.erase(std::remove_if(P.grades.begin(), P.grades.end(),
                                          [&](const TlGrade &x) { return x.timeline == tl && x.node == ro; }),
                           P.grades.end());
        return true;
    }

    bool pinColour(Project &P, const NodeId &timeline, const std::string &commit, std::string &err)
    {
        NodeId tl;
        if (!timelineId(P, timeline, tl, err)) return false;
        Timeline &t = *P.timeline(tl);
        if (t.base.empty()) { err = tl + " is a root — there is no base colour to pin"; return false; }
        bool legal = !commit.empty();
        for (char c : commit) legal = legal && (std::isalnum((unsigned char)c) || c == '.' || c == '_' || c == '-');
        if (!legal) { err = "'" + commit + "' is not a commit"; return false; }
        t.colour = "pin@" + commit;
        return true;
    }

    bool unpinColour(Project &P, const NodeId &timeline, std::string &err)
    {
        NodeId tl;
        if (!timelineId(P, timeline, tl, err)) return false;
        P.timeline(tl)->colour = "follow";
        return true;
    }

    // ── freeze / thaw ────────────────────────────────────────────────────────────────────────
    namespace
    {
        /** Does `d` receive `tl`'s arrangement live — is `tl` on its chain with no frozen cut on
         *  the way? Those are the versions whose references must follow a freeze or a thaw. */
        bool inheritsCutFrom(const Project &P, const NodeId &d, const NodeId &tl)
        {
            NodeId cur = d;
            std::set<NodeId> seen;
            while (cur != tl)
            {
                const Timeline *t = P.timeline(cur);
                if (!t || t->base.empty() || t->frozen() || !seen.insert(cur).second) return false;
                cur = t->base;
            }
            return d != tl;
        }

        template <class T> void remapOverrides(TlSet &s, const std::map<NodeId, NodeId> &m)
        {
            for (auto &kv : s.fields)
            {
                const schema::Field<T> *f = schema::find<T>(kv.first);
                if (!f) continue;
                if (f->kind == schema::Kind::Ref)
                {
                    auto it = m.find(kv.second);
                    if (it != m.end()) kv.second = it->second;
                }
                else if (f->kind == schema::Kind::Pair)
                {
                    const auto comma = kv.second.find(',');
                    if (comma == std::string::npos) continue;
                    NodeId a = kv.second.substr(0, comma), b = kv.second.substr(comma + 1);
                    if (m.count(a)) a = m.at(a);
                    if (m.count(b)) b = m.at(b);
                    kv.second = a + "," + b;
                }
            }
        }

        /** Re-point everything `tl` declares — its deltas and its own nodes — through `m`. */
        void remapIn(Project &P, const NodeId &tl, const std::map<NodeId, NodeId> &m)
        {
            auto mp = [&](NodeId &r) {
                auto it = m.find(r);
                if (it != m.end()) r = it->second;
            };
            for (auto &d : P.drops)
                if (d.timeline == tl) mp(d.node);
            for (auto &s : P.sets)
            {
                if (s.timeline != tl) continue;
                mp(s.node);
                switch (P.kindOf(s.node))
                {
                    case NodeKind::Clip: remapOverrides<Clip>(s, m); break;
                    case NodeKind::Transition: remapOverrides<Transition>(s, m); break;
                    case NodeKind::ATrack: remapOverrides<ATrack>(s, m); break;
                    case NodeKind::AClip: remapOverrides<AClip>(s, m); break;
                    default: break;
                }
            }
            // Ownership is read BEFORE anything moves: re-pointing a clip's track changes which
            // timeline it would appear to belong to.
            std::vector<Clip *> cs;
            std::vector<Transition *> ts;
            std::vector<AClip *> as;
            for (auto &c : P.clips) if (P.clipTimeline(c) == tl) cs.push_back(&c);
            for (auto &t : P.transitions) if (P.transitionTimeline(t) == tl) ts.push_back(&t);
            for (auto &a : P.audioClips) if (P.audioClipTimeline(a) == tl) as.push_back(&a);
            for (Clip *c : cs) { c->timeline = tl; mp(c->track); }
            for (Transition *t : ts) { t->timeline = tl; mp(t->track); mp(t->clipA); mp(t->clipB); }
            for (AClip *a : as) { a->timeline = tl; mp(a->track); }
            for (auto &a : P.audioTracks)
                if (a.timeline == tl) mp(a.out);
            for (Clip *c : cs) rehome(P, tl, *c);
            for (Transition *t : ts) rehome(P, tl, *t);
            for (AClip *a : as) rehome(P, tl, *a);
        }

        template <class T>
        Fields thawDiff(const T &copy, const T &origin, const std::map<NodeId, NodeId> &inv)
        {
            auto mapped = [&](const std::string &s) {
                auto it = inv.find(s);
                return it == inv.end() ? s : it->second;
            };
            Fields d;
            for (const auto &f : schema::fields<T>())
            {
                if (!f.editable) continue;
                std::string a = schema::fieldText(f, copy);
                if (f.kind == schema::Kind::Ref) a = mapped(a);
                else if (f.kind == schema::Kind::Pair)
                {
                    const auto comma = a.find(',');
                    if (comma != std::string::npos) a = mapped(a.substr(0, comma)) + "," + mapped(a.substr(comma + 1));
                }
                if (a != schema::fieldText(f, origin)) d.emplace_back(f.key, a);
            }
            for (const auto &u : copy.unknown)
            {
                bool same = false;
                for (const auto &o : origin.unknown) same = same || (o.first == u.first && o.second == u.second);
                if (!same) d.push_back(u);
            }
            return d;
        }
    }

    bool freezeCut(Project &P, const NodeId &timeline, std::string &err)
    {
        NodeId tl;
        if (!timelineId(P, timeline, tl, err)) return false;
        const Timeline T = *P.timeline(tl);
        if (T.base.empty()) { err = tl + " is a root — it inherits no cut to freeze"; return false; }
        if (T.frozen()) { err = tl + " is already frozen"; return false; }
        ResolvedTimeline R;
        if (!resolve(P, tl, R, err)) return false;
        if (!R.dangling.empty())
        {
            err = tl + " has " + std::to_string(R.dangling.size()) + " dangling item(s) (first: " + R.dangling[0].why +
                  ") — rebase before freezing, or the freeze would bake the breakage in";
            return false;
        }
        for (const auto &kv : R.provenance)
            if (kv.second == Provenance::Dangling)
            {
                err = tl + " shows " + kv.first + " broken (inherited from its base) — fix it there before freezing";
                return false;
            }

        const std::string suffix = T.name.empty() ? tl : T.name;
        auto copyName = [&](const std::string &n) { return n.empty() ? std::string() : P.freshName(n + "_" + suffix); };
        std::map<NodeId, NodeId> m;
        auto inherited = [&](const NodeId &id) { return R.provenance.at(id) != Provenance::Local; };
        auto stamp = [&](auto c, const char *prefix, auto &into) {
            c.from = c.id;
            c.id = P.freshId(prefix);
            c.name = copyName(c.name);
            c.notes = Notes();
            m[c.from] = c.id;
            into.push_back(c);
        };
        for (auto t : R.tracks) if (inherited(t.id)) { t.timeline = tl; stamp(t, "trk_", P.tracks); }
        for (auto t : R.audioTracks) if (inherited(t.id)) { t.timeline = tl; stamp(t, "atr_", P.audioTracks); }
        for (auto t : R.markers) if (inherited(t.id)) { t.timeline = tl; stamp(t, "mk_", P.markers); }
        for (auto c : R.clips) if (inherited(c.id)) { c.timeline = tl; stamp(c, "clp_", P.clips); }
        for (auto t : R.transitions) if (inherited(t.id)) { t.timeline = tl; stamp(t, "tr_", P.transitions); }
        for (auto a : R.audioClips) if (inherited(a.id)) { a.timeline = tl; stamp(a, "ac_", P.audioClips); }

        remapIn(P, tl, m);
        // Folded into the copies: an override is now just the copy's value, a drop is a copy
        // that was never made.
        P.sets.erase(std::remove_if(P.sets.begin(), P.sets.end(), [&](const TlSet &s) { return s.timeline == tl; }), P.sets.end());
        P.drops.erase(std::remove_if(P.drops.begin(), P.drops.end(), [&](const TlDrop &d) { return d.timeline == tl; }), P.drops.end());
        for (const auto &d : P.timelines)
            if (inheritsCutFrom(P, d.id, tl)) remapIn(P, d.id, m);
        P.timeline(tl)->cut = "frozen";
        return true;
    }

    bool thawCut(Project &P, const NodeId &timeline, std::string &err)
    {
        NodeId tl;
        if (!timelineId(P, timeline, tl, err)) return false;
        const Timeline T = *P.timeline(tl);
        if (!T.frozen()) { err = tl + " is not frozen"; return false; }
        if (T.base.empty()) { err = tl + " is a root — there is no base to thaw against"; return false; }
        ResolvedTimeline B;
        if (!resolve(P, T.base, B, err)) return false;

        // copy -> origin, for every local copy whose origin the base still has (same type).
        std::map<NodeId, NodeId> inv;
        auto link = [&](const auto &locals, const auto &baseNodes, auto owns) {
            for (const auto &n : locals)
            {
                if (n.from.empty() || !owns(n)) continue;
                for (const auto &o : baseNodes)
                    if (o.id == n.from) { inv[n.id] = o.id; break; }
            }
        };
        link(P.tracks, B.tracks, [&](const Track &n) { return n.timeline == tl; });
        link(P.audioTracks, B.audioTracks, [&](const ATrack &n) { return n.timeline == tl; });
        link(P.markers, B.markers, [&](const Marker &n) { return n.timeline == tl; });
        link(P.clips, B.clips, [&](const Clip &n) { return P.clipTimeline(n) == tl; });
        link(P.transitions, B.transitions, [&](const Transition &n) { return P.transitionTimeline(n) == tl; });
        link(P.audioClips, B.audioClips, [&](const AClip &n) { return P.audioClipTimeline(n) == tl; });

        for (const auto &x : P.effects)
            if (!x.clip.empty() && inv.count(x.clip))
            {
                err = "fx " + x.id + " is attached to " + x.clip + ", a frozen copy that thawing turns back into a delta — "
                      "an effect is not versioned, so move or remove it first";
                return false;
            }

        std::vector<TlSet> newSets;
        auto diffAll = [&](const auto &locals, const auto &baseNodes) {
            for (const auto &n : locals)
            {
                auto it = inv.find(n.id);
                if (it == inv.end()) continue;
                for (const auto &o : baseNodes)
                    if (o.id == it->second)
                    {
                        Fields d = thawDiff(n, o, inv);
                        if (!d.empty())
                        {
                            TlSet s;
                            s.timeline = tl;
                            s.node = o.id;
                            s.fields = d;
                            newSets.push_back(s);
                        }
                        break;
                    }
            }
        };
        diffAll(P.tracks, B.tracks);
        diffAll(P.audioTracks, B.audioTracks);
        diffAll(P.markers, B.markers);
        diffAll(P.clips, B.clips);
        diffAll(P.transitions, B.transitions);
        diffAll(P.audioClips, B.audioClips);

        // An origin with no copy was dropped — unless its anchor was dropped too, in which case
        // the anchor's drop already says so and a second line would say nothing new.
        std::set<NodeId> covered;
        for (const auto &kv : inv) covered.insert(kv.second);
        std::set<NodeId> missing;
        for (const auto &kv : B.provenance)
            if (!covered.count(kv.first)) missing.insert(kv.first);
        std::vector<TlDrop> newDrops;
        auto drop = [&](const NodeId &id) {
            TlDrop d;
            d.timeline = tl;
            d.node = id;
            newDrops.push_back(d);
        };
        for (const auto &n : B.tracks) if (missing.count(n.id)) drop(n.id);
        for (const auto &n : B.audioTracks) if (missing.count(n.id)) drop(n.id);
        for (const auto &n : B.markers) if (missing.count(n.id)) drop(n.id);
        for (const auto &n : B.clips) if (missing.count(n.id) && !missing.count(n.track)) drop(n.id);
        for (const auto &n : B.audioClips) if (missing.count(n.id) && !missing.count(n.track)) drop(n.id);
        for (const auto &n : B.transitions)
            if (missing.count(n.id) && !missing.count(n.track) && !missing.count(n.clipA) && !missing.count(n.clipB)) drop(n.id);

        // Ownership is judged through tracks, so every re-pointing happens while the copies — the
        // tracks this timeline's own clips sit on — still exist; only then are the copies erased.
        // Erasing first would orphan a local clip from its timeline before it could be re-homed.
        auto unlink = [&](auto &v, auto owns) {
            for (auto &n : v)
                if (!n.from.empty() && !inv.count(n.id) && owns(n)) n.from.clear();   // origin gone: just local now
        };
        unlink(P.tracks, [&](const Track &n) { return n.timeline == tl; });
        unlink(P.audioTracks, [&](const ATrack &n) { return n.timeline == tl; });
        unlink(P.markers, [&](const Marker &n) { return n.timeline == tl; });
        unlink(P.clips, [&](const Clip &n) { return P.clipTimeline(n) == tl; });
        unlink(P.transitions, [&](const Transition &n) { return P.transitionTimeline(n) == tl; });
        unlink(P.audioClips, [&](const AClip &n) { return P.audioClipTimeline(n) == tl; });
        remapIn(P, tl, inv);
        for (const auto &d : P.timelines)
            if (inheritsCutFrom(P, d.id, tl)) remapIn(P, d.id, inv);

        std::set<NodeId> copies;
        for (const auto &kv : inv) copies.insert(kv.first);
        eraseIds(P, copies);
        for (auto &s : newSets) P.sets.push_back(s);
        for (auto &d : newDrops) P.drops.push_back(d);
        P.timeline(tl)->cut = "follow";
        return true;
    }

    // ── rebase / diff ────────────────────────────────────────────────────────────────────────

    RebaseReport rebase(Project &P, const NodeId &timeline, bool prune, bool dryRun)
    {
        RebaseReport rep;
        NodeId tl;
        std::string err;
        if (!timelineId(P, timeline, tl, err)) { rep.message = err; return rep; }
        ResolvedTimeline R;
        if (!resolve(P, tl, R, err)) { rep.message = err; return rep; }
        rep.dangling = R.dangling;
        const Timeline T = *P.timeline(tl);

        std::vector<const Dangling *> prunable;
        int kept = 0;
        for (const auto &d : R.dangling)
        {
            if (d.kind == "tlset" || d.kind == "tldrop" || d.kind == "tlgrade" || d.kind == "transition") prunable.push_back(&d);
            else ++kept;
        }
        if (prune)
        {
            rep.pruned = (int)prunable.size();
            if (!dryRun)
                for (const Dangling *d : prunable)
                {
                    const NodeId t = d->target;
                    if (d->kind == "tlset")
                        P.sets.erase(std::remove_if(P.sets.begin(), P.sets.end(),
                                                    [&](const TlSet &s) { return s.timeline == tl && s.node == t; }), P.sets.end());
                    else if (d->kind == "tldrop")
                        P.drops.erase(std::remove_if(P.drops.begin(), P.drops.end(),
                                                     [&](const TlDrop &s) { return s.timeline == tl && s.node == t; }), P.drops.end());
                    else if (d->kind == "tlgrade")
                        P.grades.erase(std::remove_if(P.grades.begin(), P.grades.end(),
                                                      [&](const TlGrade &s) { return s.timeline == tl && s.node == t; }), P.grades.end());
                    else if (P.ownerOf(d->delta) == tl)
                        eraseIds(P, {d->delta});
                }
        }

        std::string m = label(P, tl) + ": ";
        if (R.dangling.empty()) m += "nothing dangles — already current (resolution is live)";
        else
        {
            m += std::to_string(R.dangling.size()) + " dangling";
            for (const auto &d : R.dangling) m += "\n  " + d.kind + " " + d.target + ": " + d.why;
            if (prune) m += std::string("\n") + (dryRun ? "would prune " : "pruned ") + std::to_string(rep.pruned);
            else if (!prunable.empty()) m += "\n" + std::to_string(prunable.size()) + " prunable — rebase with prune to remove them";
            if (kept) m += "\n" + std::to_string(kept) + " left for you: a clip of this version's own, or a conflict, is never pruned";
        }
        if (T.pinned()) m += "\ncolour pinned at " + T.pinCommit() + " — advance the pin to take the base's newer colour";
        if (T.frozen()) m += "\ncut frozen — thaw to follow the base's arrangement again";
        rep.message = m;
        return rep;
    }

    namespace
    {
        template <class T> std::string fieldsAgainst(const T *base, const Fields &keys)
        {
            std::string s;
            for (const auto &kv : keys)
            {
                s += (s.empty() ? "" : ", ") + kv.first + " " + kv.second;
                if (base)
                    if (const auto *f = schema::find<T>(kv.first)) s += " (base " + schema::fieldText(*f, *base) + ")";
            }
            return s;
        }

        void describeVersion(const Project &P, const NodeId &tl, std::vector<std::string> &lines)
        {
            const Timeline &T = *P.timeline(tl);
            ResolvedTimeline B, R;
            std::string err;
            if (!T.base.empty() && !T.frozen()) resolve(P, T.base, B, err);
            resolve(P, tl, R, err);
            for (const auto &d : P.drops)
                if (d.timeline == tl) lines.push_back("  drop      " + label(P, d.node));
            for (const auto &s : P.sets)
            {
                if (s.timeline != tl) continue;
                std::string detail;
                const bool found = visitResolved(B, s.node, [&](const auto &bn) {
                    using N = std::decay_t<decltype(bn)>;
                    detail = fieldsAgainst<N>(&bn, s.fields);
                });
                if (!found)
                    for (const auto &kv : s.fields) detail += (detail.empty() ? "" : ", ") + kv.first + " " + kv.second;
                lines.push_back("  override  " + label(P, s.node) + ": " + detail);
            }
            auto added = [&](const char *kind, const auto &v) {
                for (const auto &n : v)
                    if (R.provenance.count(n.id) && P.ownerOf(n.id) == tl)
                        lines.push_back(std::string("  add       ") + kind + " " + label(P, n.id));
            };
            added("track", R.tracks);
            added("clip", R.clips);
            added("transition", R.transitions);
            added("marker", R.markers);
            added("atrack", R.audioTracks);
            added("aclip", R.audioClips);
            for (const auto &g : P.grades)
            {
                if (g.timeline != tl) continue;
                std::string d;
                for (const auto &kv : g.deltas)
                    d += (d.empty() ? "" : ", ") + kv.first + " " + (kv.second >= 0 ? "+" : "") + canonicalNumber(kv.second);
                lines.push_back("  grade     " + label(P, g.node) + ": " + d);
            }
            for (const auto &d : R.dangling) lines.push_back("  dangling  " + d.kind + " " + d.target + ": " + d.why);
        }
    }

    std::string diff(const Project &P, const NodeId &timeline)
    {
        NodeId tl;
        std::string err;
        if (!timelineId(P, timeline, tl, err)) return err;
        const Timeline &T = *P.timeline(tl);
        std::string head = label(P, tl);
        std::vector<std::string> lines;
        if (T.base.empty())
        {
            ResolvedTimeline R;
            resolve(P, tl, R, err);
            head += " — a root: " + std::to_string(R.tracks.size()) + " tracks, " + std::to_string(R.clips.size()) +
                    " clips, " + std::to_string(R.transitions.size()) + " transitions, " + std::to_string(R.markers.size()) +
                    " markers, " + std::to_string(R.audioTracks.size() + R.audioClips.size()) + " audio — all its own";
            for (const auto &g : P.grades)
                if (g.timeline == tl)
                {
                    std::string d;
                    for (const auto &kv : g.deltas)
                        d += (d.empty() ? "" : ", ") + kv.first + " " + (kv.second >= 0 ? "+" : "") + canonicalNumber(kv.second);
                    lines.push_back("  grade     " + label(P, g.node) + ": " + d);
                }
        }
        else
        {
            head += " — a version of " + label(P, T.base) + "; colour " +
                    (T.pinned() ? "pinned at " + T.pinCommit() : std::string("follows")) + "; cut " +
                    (T.frozen() ? "frozen" : "follows");
            if (T.frozen())
            {
                // What a thaw would record, computed on a scratch copy — diff never edits.
                Project q = P;
                if (thawCut(q, tl, err))
                {
                    lines.push_back("  (frozen: against the base's present cut, a thaw would record)");
                    describeVersion(q, tl, lines);
                    if (lines.size() == 1) lines.push_back("  (no changes — identical to its base)");
                }
                else
                    lines.push_back("  (frozen; cannot compare with the base: " + err + ")");
            }
            else
            {
                describeVersion(P, tl, lines);
                if (lines.empty()) lines.push_back("  (no changes — identical to its base)");
            }
        }
        std::string out = head + "\n";
        for (const auto &l : lines) out += l + "\n";
        return out;
    }
}
}
