/*
 *  interstellar_model — Project: parse, validate and canonically serialize the `.isp` document.
 *
 *  The parser works in two passes, and the split is what lets a Solaris node survive:
 *    1. LINES -> BLOCKS. A block is an unindented `#type` line plus everything indented (or a
 *       comment) beneath it. No field is interpreted yet.
 *    2. BLOCKS -> NODES. A block whose type this build implements becomes a typed struct through
 *       the field tables in Schema.h; anything else — and an audio node in a form Interstellar
 *       does not render — becomes a RawNode holding its exact lines.
 *  Deciding "typed or raw" needs the whole block (an `#aclip` is a note clip if it has `#note`
 *  children), which a one-pass line parser cannot know when it meets the header.
 *
 *  After building: ids and names are checked, references spelled as bind names are resolved to
 *  ids, `#tlset` values are canonicalised against their target's field types, and the structural
 *  rules of project-format §7 run. Only then is the document accepted.
 */
#include "Project.h"
#include "Schema.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace arstro
{
namespace interstellar
{
    // ── canonical text ───────────────────────────────────────────────────────────────────────

    std::string canonicalNumber(double v)
    {
        if (!std::isfinite(v)) v = 0.0;
        if (v == 0.0) v = 0.0;                      // -0.0 and 0.0 are one fact; write it once
        char buf[400];
        const double a = std::fabs(v);
        // Fixed notation where a person reads it at a glance; scientific only at the extremes,
        // where fixed would be a wall of zeros. Both are the SHORTEST text that reads back
        // bit-identically — std::to_chars guarantees that, and it ignores the C locale.
        const bool fixed = a == 0.0 || (a >= 1e-6 && a < 1e15);
        auto r = fixed ? std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed)
                       : std::to_chars(buf, buf + sizeof buf, v);
        std::string s(buf, r.ptr);
        if (s.find_first_of(".e") == std::string::npos) s += ".0";
        return s;
    }

    std::string canonicalTime(double v)
    {
        if (!std::isfinite(v)) v = 0.0;
        char buf[400];
        auto r = std::to_chars(buf, buf + sizeof buf, v, std::chars_format::fixed, 3);
        std::string s(buf, r.ptr);
        if (s == "-0.000") s = "0.000";
        return s;
    }

    std::string quoteIfNeeded(const std::string &v)
    {
        bool need = v.empty();
        for (char c : v)
            if (c == ' ' || c == '\t' || c == '"' || c == ';' || c == '=' || c == '\n' || c == '\r') need = true;
        if (!need) return v;
        std::string out = "\"";
        for (char c : v)
        {
            if (c == '"' || c == '\\') { out += '\\'; out += c; }
            else if (c == '\n') out += "\\n";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out + "\"";
    }

    bool parseNumber(const std::string &text, double &out)
    {
        const char *b = text.data(), *e = text.data() + text.size();
        if (b != e && *b == '+') ++b;
        if (b == e) return false;
        double d = 0.0;
        const auto r = std::from_chars(b, e, d);
        if (r.ec != std::errc() || r.ptr != e || !std::isfinite(d)) return false;
        out = d;
        return true;
    }

    const char *nodeKindName(NodeKind k)
    {
        switch (k)
        {
            case NodeKind::None: return "nothing";
            case NodeKind::Rack: return "#rack";
            case NodeKind::RackObj: return "#rackobj";
            case NodeKind::Timeline: return "#timeline";
            case NodeKind::Track: return "#track";
            case NodeKind::Clip: return "#clip";
            case NodeKind::Transition: return "#transition";
            case NodeKind::Marker: return "#marker";
            case NodeKind::ATrack: return "#atrack";
            case NodeKind::AClip: return "#aclip";
            case NodeKind::Fx: return "#fx";
            case NodeKind::Effect: return "#effect";
            case NodeKind::Anim: return "#anim";
            case NodeKind::Raw: return "a preserved node";
        }
        return "?";
    }

    double Clip::duration() const { return speed != 0.0 ? (out - in) / std::fabs(speed) : 0.0; }

    // ── lexing ───────────────────────────────────────────────────────────────────────────────
    namespace
    {
        bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r'; }

        std::string trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && isSpace(s[a])) ++a;
            while (b > a && isSpace(s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        /** Split a line into its content and its `; comment`, honouring quotes AND escapes —
         *  so a `;` or an escaped `\"` inside a string never ends it. */
        struct Lexed
        {
            std::string content, comment;
            size_t commentPos = std::string::npos;
        };
        Lexed lex(const std::string &s)
        {
            bool q = false;
            for (size_t i = 0; i < s.size(); ++i)
            {
                const char c = s[i];
                if (q)
                {
                    if (c == '\\' && i + 1 < s.size()) { ++i; continue; }
                    if (c == '"') q = false;
                    continue;
                }
                if (c == '"') { q = true; continue; }
                if (c == ';')
                {
                    Lexed l;
                    l.content = trim(s.substr(0, i));
                    l.comment = trim(s.substr(i));
                    l.commentPos = i;
                    return l;
                }
            }
            Lexed l;
            l.content = trim(s);
            return l;
        }

        std::string unquote(const std::string &v)
        {
            if (v.empty() || v[0] != '"') return v;
            std::string out;
            for (size_t i = 1; i < v.size(); ++i)
            {
                const char c = v[i];
                if (c == '\\' && i + 1 < v.size())
                {
                    const char d = v[++i];
                    out += d == 'n' ? '\n' : d == 't' ? '\t' : d;
                    continue;
                }
                if (c == '"') break;
                out += c;
            }
            return out;
        }

        struct Token
        {
            std::string key, value, text;
            bool kv = false;
            size_t begin = 0, end = 0;          // in the string that was tokenized
        };
        /** Whitespace-separated tokens of s[0, limit), honouring quotes; `key=value` split at the
         *  first `=` (a key never contains a quote, so the first `=` before any quote). */
        std::vector<Token> tokenize(const std::string &s, size_t limit = std::string::npos)
        {
            std::vector<Token> out;
            const size_t n = std::min(limit, s.size());
            size_t i = 0;
            while (i < n)
            {
                while (i < n && isSpace(s[i])) ++i;
                if (i >= n) break;
                const size_t b = i;
                bool q = false;
                while (i < n)
                {
                    const char c = s[i];
                    if (q)
                    {
                        if (c == '\\' && i + 1 < n) { i += 2; continue; }
                        if (c == '"') q = false;
                        ++i;
                        continue;
                    }
                    if (c == '"') { q = true; ++i; continue; }
                    if (isSpace(c)) break;
                    ++i;
                }
                Token t;
                t.begin = b;
                t.end = i;
                t.text = s.substr(b, i - b);
                const auto eq = t.text.find('=');
                const auto qt = t.text.find('"');
                if (eq != std::string::npos && eq > 0 && (qt == std::string::npos || qt > eq))
                {
                    t.kv = true;
                    t.key = t.text.substr(0, eq);
                    t.value = unquote(t.text.substr(eq + 1));
                }
                out.push_back(t);
            }
            return out;
        }

        /** `#type rest` -> type. */
        std::string headerType(const std::string &content)
        {
            size_t e = 1;
            while (e < content.size() && !isSpace(content[e])) ++e;
            return content.substr(1, e - 1);
        }

        std::string get(const Fields &kv, const char *k)
        {
            for (const auto &p : kv)
                if (p.first == k) return p.second;
            return {};
        }
        bool has(const Fields &kv, const char *k)
        {
            for (const auto &p : kv)
                if (p.first == k) return true;
            return false;
        }
        void put(Fields &kv, const std::string &k, const std::string &v)
        {
            for (auto &p : kv)
                if (p.first == k) { p.second = v; return; }
            kv.emplace_back(k, v);
        }

        /** Reference keys inside PRESERVED nodes — the ones `rename` rewrites in verbatim text. */
        bool rawRefKey(const std::string &k)
        {
            static const char *keys[] = {"track", "node", "from", "to", "out", "src", "clip", "timeline",
                                         "base", "between", "bus", "target"};
            for (const char *r : keys)
                if (k == r) return true;
            return false;
        }

        /** Rebuild a RawNode's read-only view (fields, children, id) from its exact lines. */
        void rawView(RawNode &r)
        {
            r.fields.clear();
            r.children.clear();
            bool lastWasChild = false;
            for (size_t j = 0; j < r.lines.size(); ++j)
            {
                const Lexed lx = lex(r.lines[j]);
                if (lx.content.empty()) continue;
                if (j == 0)
                {
                    for (const auto &t : tokenize(lx.content.substr(1 + headerType(lx.content).size())))
                        if (t.kv) r.fields.emplace_back(t.key, t.value);
                    continue;
                }
                if (lx.content[0] == '#')
                {
                    RawChild c;
                    c.type = headerType(lx.content);
                    c.text = lx.content;
                    for (const auto &t : tokenize(lx.content.substr(1 + c.type.size())))
                        if (t.kv) c.fields.emplace_back(t.key, t.value);
                    r.children.push_back(c);
                    lastWasChild = true;
                    continue;
                }
                const auto toks = tokenize(lx.content);
                bool allKv = !toks.empty() && r.type != "aauto";
                for (const auto &t : toks) allKv = allKv && t.kv;
                if (allKv)
                {
                    Fields &into = lastWasChild && !r.children.empty() ? r.children.back().fields : r.fields;
                    for (const auto &t : toks) into.emplace_back(t.key, t.value);
                    continue;
                }
                RawChild c;
                c.text = lx.content;         // a breakpoint: `0.000 = 0.40`
                r.children.push_back(c);
            }
            r.id = get(r.fields, "id");
            int notes = 0, effects = 0, points = 0;
            for (const auto &c : r.children)
            {
                if (c.type == "note") ++notes;
                else if (c.type == "aeffect") ++effects;
                else if (c.type.empty()) ++points;
            }
            if (r.type == "atrack")
                r.why = "kind=" + get(r.fields, "kind") + "; Interstellar renders kind=audio only";
            else if (r.type == "aclip")
                r.why = "note clip, " + std::to_string(notes) + " #note";
            else if (r.type == "arack")
                r.why = "processor chain, " + std::to_string(effects) + " #aeffect";
            else if (r.type == "aauto")
                r.why = "automation, " + std::to_string(points) + " breakpoints";
            else if (r.type == "asend")
                r.why = "send";
            else
                r.why = "unknown node type";
        }

        bool typedType(const std::string &t)
        {
            static const char *types[] = {"rack", "rackobj", "timeline", "tldrop", "tlset", "tlgrade", "track",
                                          "clip", "transition", "marker", "atrack", "aclip", "fx", "effect", "anim", "key"};
            for (const char *x : types)
                if (t == x) return true;
            return false;
        }

        template <class V> auto findIn(V &v, const NodeId &id) -> decltype(&v[0])
        {
            if (id.empty()) return nullptr;
            for (auto &e : v)
                if (e.id == id) return &e;
            return nullptr;
        }
        template <class V> auto findDelta(V &v, const NodeId &tl, const NodeId &node) -> decltype(&v[0])
        {
            for (auto &e : v)
                if (e.timeline == tl && e.node == node) return &e;
            return nullptr;
        }
    }

    // ── validation helpers ───────────────────────────────────────────────────────────────────

    bool Project::fieldIsColour(const std::string &key)
    {
        // Deliberately generous: every spelling a user might reach for. The failure this prevents
        // is silent loss of their edit, which is worse than a refusal they can read (R-TL-2).
        static const char *colour[] = {"grade", "curve", "mixer", "lut", "look", "exposure", "contrast",
                                       "temp", "tint", "saturation", "vibrance", "highlights", "shadows",
                                       "whites", "blacks", "clarity", "texture", "dehaze", "params",
                                       "editparams", "wb", "colour", "color"};
        std::string lower;
        for (char c : key) lower += (char)std::tolower((unsigned char)c);
        for (const char *c : colour)
            if (lower == c || lower.rfind(std::string(c) + ".", 0) == 0) return true;
        return false;
    }

    bool Project::nameIsLegal(const std::string &name, std::string &why)
    {
        if (name.empty()) { why = "a bind name may not be empty"; return false; }
        if (name.size() > 32) { why = "a bind name is at most 32 characters: " + name; return false; }
        if (!(std::isalpha((unsigned char)name[0]) || name[0] == '_'))
        {
            why = "a bind name starts with a letter or underscore: " + name;
            return false;
        }
        for (char c : name)
            if (!(std::isalnum((unsigned char)c) || c == '_'))
            {
                why = "a bind name allows only letters, digits and underscore: " + name;
                return false;
            }
        // Reserved because an address or an expression could not tell them from a name.
        static const char *reserved[] = {"project", "timeline", "rack", "t", "frame", "fps", "dur", "self",
                                         "true", "false", "min", "max", "clamp", "lerp", "mix", "remap",
                                         "abs", "sin", "cos", "tan", "pow", "sqrt", "log", "exp", "floor",
                                         "ceil", "round", "sign", "step", "smoothstep"};
        for (const char *r : reserved)
            if (name == r)
            {
                why = "reserved word: " + name;
                return false;
            }
        return true;
    }

    // ── lookup ───────────────────────────────────────────────────────────────────────────────

    Timeline *Project::timeline(const NodeId &i) { return findIn(timelines, i); }
    const Timeline *Project::timeline(const NodeId &i) const { return findIn(timelines, i); }
    RackObj *Project::rackObj(const NodeId &i) { return findIn(rackObjs, i); }
    const RackObj *Project::rackObj(const NodeId &i) const { return findIn(rackObjs, i); }
    Track *Project::track(const NodeId &i) { return findIn(tracks, i); }
    const Track *Project::track(const NodeId &i) const { return findIn(tracks, i); }
    Clip *Project::clip(const NodeId &i) { return findIn(clips, i); }
    const Clip *Project::clip(const NodeId &i) const { return findIn(clips, i); }
    Transition *Project::transition(const NodeId &i) { return findIn(transitions, i); }
    const Transition *Project::transition(const NodeId &i) const { return findIn(transitions, i); }
    Marker *Project::marker(const NodeId &i) { return findIn(markers, i); }
    const Marker *Project::marker(const NodeId &i) const { return findIn(markers, i); }
    ATrack *Project::audioTrack(const NodeId &i) { return findIn(audioTracks, i); }
    const ATrack *Project::audioTrack(const NodeId &i) const { return findIn(audioTracks, i); }
    AClip *Project::audioClip(const NodeId &i) { return findIn(audioClips, i); }
    const AClip *Project::audioClip(const NodeId &i) const { return findIn(audioClips, i); }
    Fx *Project::fx(const NodeId &i) { return findIn(effects, i); }
    const Fx *Project::fx(const NodeId &i) const { return findIn(effects, i); }
    Effect *Project::effect(const NodeId &i) { return findIn(imageEffects, i); }
    const Effect *Project::effect(const NodeId &i) const { return findIn(imageEffects, i); }
    Anim *Project::anim(const NodeId &i) { return findIn(anims, i); }
    const Anim *Project::anim(const NodeId &i) const { return findIn(anims, i); }
    const Anim *Project::animOf(const NodeId &node, const std::string &key) const
    {
        for (const auto &a : anims)
            if (a.node == node && a.key == key) return &a;
        return nullptr;
    }
    std::vector<anim::Key> Project::keysOf(const NodeId &animId) const
    {
        std::vector<anim::Key> v;
        for (const auto &k : animKeys)
        {
            if (k.anim != animId) continue;
            anim::Key x;
            x.t = k.t;
            x.v = k.v;
            anim::parseSide(k.in, x.in);
            anim::parseSide(k.out, x.out);
            x.speedIn = k.speedIn;
            x.speedOut = k.speedOut;
            x.inflIn = k.inflIn;
            x.inflOut = k.inflOut;
            v.push_back(x);
        }
        std::sort(v.begin(), v.end(), [](const anim::Key &a, const anim::Key &b) { return a.t < b.t; });
        return v;
    }
    void Project::dropAnim(const NodeId &animId)
    {
        anims.erase(std::remove_if(anims.begin(), anims.end(), [&](const Anim &a) { return a.id == animId; }), anims.end());
        animKeys.erase(std::remove_if(animKeys.begin(), animKeys.end(), [&](const AnimKey &k) { return k.anim == animId; }), animKeys.end());
    }
    void Project::dropAnimsOf(const NodeId &node)
    {
        std::set<NodeId> gone;
        for (const auto &a : anims) if (a.node == node) gone.insert(a.id);
        if (gone.empty()) return;
        anims.erase(std::remove_if(anims.begin(), anims.end(), [&](const Anim &a) { return gone.count(a.id) > 0; }), anims.end());
        animKeys.erase(std::remove_if(animKeys.begin(), animKeys.end(), [&](const AnimKey &k) { return gone.count(k.anim) > 0; }), animKeys.end());
    }
    RawNode *Project::rawNode(const NodeId &i) { return findIn(raw, i); }
    const RawNode *Project::rawNode(const NodeId &i) const { return findIn(raw, i); }
    TlSet *Project::tlset(const NodeId &tl, const NodeId &n) { return findDelta(sets, tl, n); }
    const TlSet *Project::tlset(const NodeId &tl, const NodeId &n) const { return findDelta(sets, tl, n); }
    TlDrop *Project::tldrop(const NodeId &tl, const NodeId &n) { return findDelta(drops, tl, n); }
    const TlDrop *Project::tldrop(const NodeId &tl, const NodeId &n) const { return findDelta(drops, tl, n); }
    TlGrade *Project::tlgrade(const NodeId &tl, const NodeId &n) { return findDelta(grades, tl, n); }
    const TlGrade *Project::tlgrade(const NodeId &tl, const NodeId &n) const { return findDelta(grades, tl, n); }

    NodeKind Project::kindOf(const NodeId &i) const
    {
        if (i.empty()) return NodeKind::None;
        if (hasRack && rack.id == i) return NodeKind::Rack;
        if (rackObj(i)) return NodeKind::RackObj;
        if (timeline(i)) return NodeKind::Timeline;
        if (track(i)) return NodeKind::Track;
        if (clip(i)) return NodeKind::Clip;
        if (transition(i)) return NodeKind::Transition;
        if (marker(i)) return NodeKind::Marker;
        if (audioTrack(i)) return NodeKind::ATrack;
        if (audioClip(i)) return NodeKind::AClip;
        if (fx(i)) return NodeKind::Fx;
        if (effect(i)) return NodeKind::Effect;
        if (anim(i)) return NodeKind::Anim;
        for (const auto &r : raw)
        {
            if (r.id == i) return NodeKind::Raw;
            for (const auto &c : r.children)
                if (get(c.fields, "id") == i) return NodeKind::Raw;
        }
        return NodeKind::None;
    }

    NodeId Project::idForRef(const std::string &s) const
    {
        if (s.empty()) return {};
        if (kindOf(s) != NodeKind::None) return s;
        auto scan = [&](const auto &v) -> NodeId {
            for (const auto &e : v)
                if (e.name == s) return e.id;
            return {};
        };
        NodeId r;
        if (!(r = scan(rackObjs)).empty() || !(r = scan(timelines)).empty() || !(r = scan(tracks)).empty() ||
            !(r = scan(clips)).empty() || !(r = scan(transitions)).empty() || !(r = scan(markers)).empty() ||
            !(r = scan(audioTracks)).empty() || !(r = scan(audioClips)).empty())
            return r;
        for (const auto &n : raw)
            if (!n.id.empty() && get(n.fields, "name") == s) return n.id;
        return {};
    }

    NodeId Project::clipTimeline(const Clip &c) const
    {
        if (!c.timeline.empty()) return c.timeline;
        const Track *t = track(c.track);
        return t ? t->timeline : NodeId();
    }
    NodeId Project::transitionTimeline(const Transition &x) const
    {
        if (!x.timeline.empty()) return x.timeline;
        const Track *t = track(x.track);
        return t ? t->timeline : NodeId();
    }
    NodeId Project::audioClipTimeline(const AClip &a) const
    {
        if (!a.timeline.empty()) return a.timeline;
        if (const Track *t = track(a.track)) return t->timeline;
        if (const ATrack *t = audioTrack(a.track)) return t->timeline;
        if (const RawNode *r = rawNode(a.track)) return get(r->fields, "timeline");
        return {};
    }

    NodeId Project::ownerOf(const NodeId &i) const
    {
        if (const Track *t = track(i)) return t->timeline;
        if (const Clip *c = clip(i)) return clipTimeline(*c);
        if (const Transition *t = transition(i)) return transitionTimeline(*t);
        if (const Marker *m = marker(i)) return m->timeline;
        if (const ATrack *a = audioTrack(i)) return a->timeline;
        if (const AClip *a = audioClip(i)) return audioClipTimeline(*a);
        return {};
    }

    std::vector<NodeId> Project::chain(const NodeId &tl) const
    {
        std::vector<NodeId> out;
        std::set<NodeId> seen;
        NodeId cur = tl;
        while (!cur.empty() && seen.insert(cur).second)
        {
            const Timeline *t = timeline(cur);
            if (!t) break;
            out.push_back(cur);
            cur = t->base;
        }
        return out;
    }

    bool Project::derivesFrom(const NodeId &tl, const NodeId &ancestor) const
    {
        const auto c = chain(tl);
        for (size_t i = 1; i < c.size(); ++i)
            if (c[i] == ancestor) return true;
        return false;
    }

    std::vector<std::string> Project::bindNames() const
    {
        std::vector<std::string> out;
        for (const auto &r : rackObjs)
            if (!r.name.empty()) out.push_back(r.name);
        std::sort(out.begin(), out.end());
        return out;
    }

    // ── identity ─────────────────────────────────────────────────────────────────────────────
    namespace
    {
        /** Every id and every reference in the document, for `freshId`'s floor. */
        void forEachIdOrRef(const Project &p, const std::function<void(const std::string &)> &f)
        {
            if (p.hasRack) f(p.rack.id);
            f(p.current);
            for (const auto &n : p.rackObjs) f(n.id);
            for (const auto &n : p.timelines) { f(n.id); f(n.base); }
            for (const auto &n : p.drops) f(n.node);
            for (const auto &n : p.sets)
            {
                f(n.node);
                for (const auto &kv : n.fields) f(kv.second);
            }
            for (const auto &n : p.grades) f(n.node);
            for (const auto &n : p.tracks) { f(n.id); f(n.from); }
            for (const auto &n : p.clips) { f(n.id); f(n.track); f(n.src); f(n.from); }
            for (const auto &n : p.transitions) { f(n.id); f(n.clipA); f(n.clipB); f(n.from); }
            for (const auto &n : p.markers) { f(n.id); f(n.from); }
            for (const auto &n : p.audioTracks) { f(n.id); f(n.out); f(n.from); }
            for (const auto &n : p.audioClips) { f(n.id); f(n.track); f(n.from); }
            for (const auto &n : p.effects) { f(n.id); f(n.node); f(n.clip); }
            for (const auto &n : p.imageEffects) { f(n.id); f(n.node); }
            for (const auto &n : p.anims) { f(n.id); f(n.node); }
            for (const auto &n : p.raw)
            {
                for (const auto &kv : n.fields) f(kv.second);
                for (const auto &c : n.children)
                    for (const auto &kv : c.fields) f(kv.second);
            }
        }
    }

    NodeId Project::freshId(const std::string &prefix)
    {
        long long n = mNext.count(prefix) ? mNext[prefix] : 1;
        forEachIdOrRef(*this, [&](const std::string &s) {
            if (s.size() <= prefix.size() || s.compare(0, prefix.size(), prefix) != 0) return;
            long long v = 0;
            for (size_t i = prefix.size(); i < s.size(); ++i)
            {
                if (!std::isdigit((unsigned char)s[i]) || v > 100000000000LL) return;
                v = v * 10 + (s[i] - '0');
            }
            n = std::max(n, v + 1);
        });
        while (kindOf(prefix + std::to_string(n)) != NodeKind::None || nameIsTaken(prefix + std::to_string(n))) ++n;
        mNext[prefix] = n + 1;
        return prefix + std::to_string(n);
    }

    bool Project::nameIsTaken(const std::string &nm) const
    {
        if (nm.empty()) return false;
        if (kindOf(nm) != NodeKind::None) return true;
        auto scan = [&](const auto &v) {
            for (const auto &e : v)
                if (e.name == nm) return true;
            return false;
        };
        if (scan(rackObjs) || scan(timelines) || scan(tracks) || scan(clips) || scan(transitions) ||
            scan(markers) || scan(audioTracks) || scan(audioClips))
            return true;
        for (const auto &r : raw)
            if (get(r.fields, "name") == nm) return true;
        return false;
    }

    std::string Project::freshName(const std::string &base) const
    {
        // Derive something LEGAL from whatever we were given — Cosmo names a node "Tokyo Night"
        // or "DSC01.MOV", and neither is addressable (R-RACK-6).
        std::string b;
        for (char c : base) b += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        if (b.empty() || std::isdigit((unsigned char)b[0])) b = "n_" + b;
        if (b.size() > 28) b.resize(28);
        std::string why;
        if (!nameIsLegal(b, why)) b = "n_" + b;
        if (!nameIsTaken(b) && nameIsLegal(b, why)) return b;
        for (int i = 2;; ++i)
        {
            const std::string c = b + std::to_string(i);
            if (!nameIsTaken(c)) return c;
        }
    }

    bool Project::rename(const NodeId &i, const std::string &newName, std::string &err)
    {
        if (!nameIsLegal(newName, err)) return false;
        const NodeKind k = kindOf(i);
        if (k == NodeKind::None) { err = "no such node: " + i; return false; }

        std::string *slot = nullptr;
        if (RackObj *n = rackObj(i)) slot = &n->name;
        else if (Timeline *n = timeline(i)) slot = &n->name;
        else if (Track *n = track(i)) slot = &n->name;
        else if (Clip *n = clip(i)) slot = &n->name;
        else if (Transition *n = transition(i)) slot = &n->name;
        else if (Marker *n = marker(i)) slot = &n->name;
        else if (ATrack *n = audioTrack(i)) slot = &n->name;
        else if (AClip *n = audioClip(i)) slot = &n->name;
        RawNode *rawSelf = slot ? nullptr : rawNode(i);
        if (!slot && !rawSelf) { err = i + " is " + nodeKindName(k) + ", which has no bind name"; return false; }

        const std::string oldName = slot ? *slot : get(rawSelf->fields, "name");
        if (oldName == newName) return true;
        if (nameIsTaken(newName)) { err = "that bind name is already used: " + newName; return false; }

        // Rewrite a `key=value` token inside verbatim text, leaving every other byte alone.
        auto rewriteLine = [&](std::string &line, bool selfHeader) {
            const Lexed lx = lex(line);
            auto toks = tokenize(line, lx.commentPos);
            bool hasName = false;
            for (const auto &t : toks) hasName = hasName || (t.kv && t.key == "name");
            // A preserved node that had no name gets one appended after its last token; inserting
            // past every token leaves the positions the loop below uses untouched.
            if (selfHeader && !hasName && !toks.empty()) line.insert(toks.back().end, " name=" + quoteIfNeeded(newName));
            for (auto it = toks.rbegin(); it != toks.rend(); ++it)
            {
                if (!it->kv) continue;
                std::string repl;
                if (selfHeader && it->key == "name")
                    repl = "name=" + quoteIfNeeded(newName);
                else if (!oldName.empty() && rawRefKey(it->key))
                {
                    if (it->key == "between")
                    {
                        std::string out, part;
                        std::stringstream ss(it->value);
                        bool hit = false;
                        while (std::getline(ss, part, ','))
                        {
                            if (part == oldName) { part = newName; hit = true; }
                            out += (out.empty() ? "" : ",") + part;
                        }
                        if (hit) repl = "between=" + quoteIfNeeded(out);
                    }
                    else if (it->value == oldName)
                        repl = it->key + "=" + quoteIfNeeded(newName);
                }
                if (!repl.empty()) line.replace(it->begin, it->end - it->begin, repl);
            }
        };

        // References are ids once parsed, so a typed node's references never spell a name — but
        // a document built in code might, so they are normalised to the id here too.
        auto fix = [&](std::string &ref) {
            if (!oldName.empty() && ref == oldName) ref = i;
        };
        if (slot) *slot = newName;
        fix(current);
        for (auto &n : timelines) fix(n.base);
        for (auto &n : drops) fix(n.node);
        for (auto &n : sets) fix(n.node);
        for (auto &n : grades) fix(n.node);
        for (auto &n : clips) { fix(n.track); fix(n.src); }
        for (auto &n : transitions) { fix(n.track); fix(n.clipA); fix(n.clipB); }
        for (auto &n : audioTracks) fix(n.out);
        for (auto &n : audioClips) fix(n.track);
        for (auto &n : effects) { fix(n.node); fix(n.clip); }
        for (auto &n : imageEffects) fix(n.node);
        for (auto &n : anims) fix(n.node);
        for (auto &r : raw)
        {
            bool touched = false;
            for (size_t j = 0; j < r.lines.size(); ++j)
            {
                const std::string before = r.lines[j];
                rewriteLine(r.lines[j], &r == rawSelf && j == 0);
                touched = touched || before != r.lines[j];
            }
            if (touched) rawView(r);
        }
        return true;
    }

    // ── parse ────────────────────────────────────────────────────────────────────────────────

    bool Project::parse(const std::string &text, std::string &err, int *repaired)
    {
        *this = Project();
        err.clear();
        int repairs = 0;
        int *rep = &repairs;
        std::string ignored;

        std::vector<std::string> lines;
        {
            size_t b = 0;
            while (b < text.size())
            {
                size_t e = text.find('\n', b);
                if (e == std::string::npos) e = text.size();
                std::string l = text.substr(b, e - b);
                if (!l.empty() && l.back() == '\r') l.pop_back();
                lines.push_back(l);
                b = e + 1;
            }
        }
        auto fail = [&](int no, const std::string &m) {
            err = "line " + std::to_string(no) + ": " + m;
            return false;
        };

        // ── header ──
        size_t i = 0;
        std::string lastKey;
        for (; i < lines.size(); ++i)
        {
            const std::string &raw0 = lines[i];
            const bool indented = !raw0.empty() && (raw0[0] == ' ' || raw0[0] == '\t');
            const Lexed lx = lex(raw0);
            if (!indented && !lx.content.empty() && lx.content[0] == '#') break;
            if (lx.content.empty())
            {
                if (!lx.comment.empty()) (lastKey.empty() ? preamble : headerNotes[lastKey].after).push_back(lx.comment);
                continue;
            }
            const auto eq = lx.content.find('=');
            if (eq == std::string::npos || eq == 0)
                return fail((int)i + 1, "expected `key = value` in the header, got: " + lx.content);
            const std::string k = trim(lx.content.substr(0, eq));
            const std::string v = unquote(trim(lx.content.substr(eq + 1)));
            // An indented `#node` line would otherwise read as a header key called "#rack id".
            bool keyLike = !k.empty();
            for (char c : k) keyLike = keyLike && (std::isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.');
            if (!keyLike)
                return fail((int)i + 1, "'" + k + "' is not a header key" +
                                            (k[0] == '#' ? " — a #node header starts at column 0" : ""));
            if (const auto *f = schema::find<Project>(k)) schema::readField(*f, *this, v, false, rep, ignored);
            else put(headerUnknown, k, v);
            if (!lx.comment.empty()) headerNotes[k].inlineComment = lx.comment;
            lastKey = k;
        }

        // ── lines -> blocks ──
        struct SrcLine
        {
            int no;
            std::string raw, content, comment;
            bool indented;
        };
        struct Block
        {
            std::string type;
            std::vector<SrcLine> lines;
        };
        std::vector<Block> blocks;
        for (; i < lines.size(); ++i)
        {
            SrcLine L;
            L.no = (int)i + 1;
            L.raw = lines[i];
            L.indented = !L.raw.empty() && (L.raw[0] == ' ' || L.raw[0] == '\t');
            const Lexed lx = lex(L.raw);
            L.content = lx.content;
            L.comment = lx.comment;
            if (L.content.empty() && L.comment.empty()) continue;
            if (!L.indented && !L.content.empty() && L.content[0] == '#')
            {
                const std::string type = headerType(L.content);
                if (type.empty()) return fail(L.no, "a node header needs a type: #<type>");
                // A child written flush-left still belongs to its container: re-ordering it among
                // the top-level nodes would detach a note from its clip.
                const bool child = !blocks.empty() && ((type == "note" && blocks.back().type == "aclip") ||
                                                       (type == "aeffect" && blocks.back().type == "arack"));
                if (child) { blocks.back().lines.push_back(L); continue; }
                blocks.push_back({type, {L}});
                continue;
            }
            if (!L.indented && !L.content.empty())
                return fail(L.no, "a field line belongs indented under its #node: " + L.content);
            blocks.back().lines.push_back(L);
        }

        // ── blocks -> nodes ──
        for (const auto &b : blocks)
        {
            const int no = b.lines[0].no;
            Fields kv;
            Notes notes;
            bool hasChild = false;
            int badLine = 0;
            std::string badTok;
            for (size_t j = 0; j < b.lines.size(); ++j)
            {
                const SrcLine &L = b.lines[j];
                if (j > 0 && !L.content.empty() && L.content[0] == '#') { hasChild = true; continue; }
                if (L.content.empty()) { notes.after.push_back(L.comment); continue; }
                if (!L.comment.empty())
                    notes.inlineComment += (notes.inlineComment.empty() ? "" : " ") + L.comment;
                const std::string body = j == 0 ? L.content.substr(1 + b.type.size()) : L.content;
                for (const auto &t : tokenize(body))
                {
                    if (t.kv) kv.emplace_back(t.key, t.value);
                    else if (!badLine) { badLine = L.no; badTok = t.text; }
                }
            }

            bool isRaw = !typedType(b.type);
            if (b.type == "atrack") isRaw = hasChild || (has(kv, "kind") && get(kv, "kind") != "audio");
            if (b.type == "aclip") isRaw = hasChild || !has(kv, "src");
            if (isRaw)
            {
                RawNode r;
                r.type = b.type;
                for (const auto &L : b.lines) r.lines.push_back(L.raw);
                rawView(r);
                raw.push_back(r);
                continue;
            }
            if (hasChild) return fail(no, "#" + b.type + " cannot have child nodes; only Solaris's audio nodes nest");
            if (badLine) return fail(badLine, "expected key=value, got '" + badTok + "'");

            auto build = [&](auto &n) {
                schema::applyLenient(n, kv, rep);
                n.notes = notes;
            };
            if (b.type == "rack")
            {
                if (hasRack)
                    return fail(no, "a project has exactly one #rack; a second would be a second colour authority");
                build(rack);
                hasRack = true;
            }
            else if (b.type == "rackobj") { RackObj n; build(n); rackObjs.push_back(n); }
            else if (b.type == "timeline")
            {
                Timeline n;
                build(n);
                if (n.colour != "follow" && !(n.pinned() && !n.pinCommit().empty()))
                    return fail(no, "#timeline " + n.id + ": colour=" + n.colour + " — it is follow or pin@<commit>");
                if (n.cut != "follow" && n.cut != "frozen")
                    return fail(no, "#timeline " + n.id + ": cut=" + n.cut + " — it is follow or frozen");
                timelines.push_back(n);
            }
            else if (b.type == "track")
            {
                Track n;
                build(n);
                if (n.kind != "video" && n.kind != "audio")
                    return fail(no, "#track " + n.id + ": kind=" + n.kind + " — a track is video or audio");
                tracks.push_back(n);
            }
            else if (b.type == "clip" || b.type == "aclip")
            {
                if (b.type == "clip")
                    for (const auto &p : kv)
                        if (fieldIsColour(p.first))
                            return fail(no, "a clip carries no colour: '" + p.first +
                                                "' belongs to the rack node the clip names (R-TL-2)");
                // A repaired `in`/`out`/`speed` must not turn into a refusal: one stray nan would
                // make the project unopenable after all. So a clip whose frames vanished BECAUSE
                // of a repair is widened to one frame instead, and that counts as a repair too.
                bool frameRepair = false;
                for (const char *k : {"in", "out", "speed"})
                {
                    double d;
                    if (has(kv, k) && !parseNumber(get(kv, k), d)) frameRepair = true;
                }
                auto check = [&](auto &n, double speed) {
                    if (n.in < n.out && speed != 0.0) return true;
                    if (!frameRepair)
                    {
                        err = "line " + std::to_string(no) + ": #" + b.type + " " + n.id +
                              (speed == 0.0 ? ": speed=0" : ": in >= out") + " — a clip with no frames is not a clip";
                        return false;
                    }
                    if (!(n.in < n.out)) { n.out = n.in + 1.0 / (fps > 0 ? fps : 24.0); ++repairs; }
                    return true;
                };
                if (b.type == "clip")
                {
                    Clip n;
                    build(n);
                    if (n.speed == 0.0 && frameRepair) { n.speed = 1.0; ++repairs; }
                    if (!check(n, n.speed)) return false;
                    clips.push_back(n);
                }
                else
                {
                    AClip n;
                    build(n);
                    if (!check(n, 1.0)) return false;
                    audioClips.push_back(n);
                }
            }
            else if (b.type == "transition") { Transition n; build(n); transitions.push_back(n); }
            else if (b.type == "marker") { Marker n; build(n); markers.push_back(n); }
            else if (b.type == "atrack") { ATrack n; build(n); audioTracks.push_back(n); }
            else if (b.type == "effect")
            {
                Effect n;
                build(n);
                if (n.node.empty() || n.type.empty()) return fail(no, "#effect " + n.id + " needs node= (a rack node) and type=");
                imageEffects.push_back(n);
            }
            else if (b.type == "anim")
            {
                Anim n;
                build(n);
                if (n.node.empty() || n.key.empty()) return fail(no, "#anim " + n.id + " needs node= (what it animates) and key=");
                anims.push_back(n);
            }
            else if (b.type == "key")
            {
                AnimKey n;
                build(n);
                if (n.anim.empty() || !has(kv, "t") || !has(kv, "v")) return fail(no, "#key needs anim=, t= and v=");
                animKeys.push_back(n);
            }
            else if (b.type == "fx")
            {
                Fx n;
                build(n);
                if (n.node.empty() == n.clip.empty())
                    return fail(no, "#fx " + n.id + " attaches to exactly one of node= (a rack node) or clip=");
                effects.push_back(n);
            }
            else // the three deltas
            {
                const NodeId tl = get(kv, "timeline"), node = get(kv, "node");
                if (tl.empty() || node.empty()) return fail(no, "#" + b.type + " needs timeline= and node=");
                Fields rest;
                bool seenTl = false, seenNode = false;
                for (const auto &p : kv)
                {
                    if (p.first == "timeline" && !seenTl) { seenTl = true; continue; }
                    if (p.first == "node" && !seenNode) { seenNode = true; continue; }
                    put(rest, p.first, p.second);
                }
                if (b.type == "tldrop")
                {
                    TlDrop d;
                    d.timeline = tl; d.node = node; d.unknown = rest; d.notes = notes;
                    drops.push_back(d);
                }
                else if (b.type == "tlset")
                {
                    for (const auto &p : rest)
                        if (fieldIsColour(p.first))
                            return fail(no, "#tlset cannot carry colour: '" + p.first +
                                                "' — a version's colour override is a #tlgrade on the rack node");
                    TlSet s;
                    s.timeline = tl; s.node = node; s.fields = rest; s.notes = notes;
                    sets.push_back(s);
                }
                else
                {
                    TlGrade g;
                    g.timeline = tl; g.node = node; g.notes = notes;
                    for (const auto &p : rest)
                    {
                        double d = 0.0;
                        if (!parseNumber(p.second, d)) { d = 0.0; ++repairs; }
                        g.deltas.emplace_back(p.first, d);
                    }
                    grades.push_back(g);
                }
            }
        }

        if (!validateIdsAndNames(err)) return false;
        normalizeRefs();
        canonicalizeDeltas(rep);
        if (!validate(err)) return false;
        if (repaired) *repaired = repairs;
        return true;
    }

    bool Project::validateIdsAndNames(std::string &err) const
    {
        std::unordered_map<std::string, std::string> ids;   // id -> what it is, for the message
        auto add = [&](const std::string &id, const std::string &what) {
            if (id.empty()) { err = what + " has no id"; return false; }
            auto r = ids.emplace(id, what);
            if (!r.second) { err = "duplicate id " + id + " (" + r.first->second + " and " + what + ")"; return false; }
            return true;
        };
        if (hasRack && !add(rack.id, "#rack")) return false;
        auto all = [&](const auto &v, const char *what) {
            for (const auto &n : v)
                if (!add(n.id, what)) return false;
            return true;
        };
        if (!all(rackObjs, "#rackobj") || !all(timelines, "#timeline") || !all(tracks, "#track") ||
            !all(clips, "#clip") || !all(transitions, "#transition") || !all(markers, "#marker") ||
            !all(audioTracks, "#atrack") || !all(audioClips, "#aclip") || !all(effects, "#fx") ||
            !all(imageEffects, "#effect") || !all(anims, "#anim"))
            return false;
        for (const auto &r : raw)
        {
            if (!r.id.empty() && !add(r.id, "#" + r.type)) return false;
            for (const auto &c : r.children)
                if (!get(c.fields, "id").empty() && !add(get(c.fields, "id"), "#" + c.type)) return false;
        }

        std::unordered_map<std::string, std::string> names;
        auto named = [&](const auto &v, const char *what) {
            for (const auto &n : v)
            {
                if (n.name.empty()) continue;
                std::string why;
                if (!nameIsLegal(n.name, why)) { err = std::string(what) + " " + n.id + ": " + why; return false; }
                auto r = names.emplace(n.name, n.id);
                if (!r.second)
                {
                    err = "bind name " + n.name + " is used by both " + r.first->second + " and " + n.id;
                    return false;
                }
                auto o = ids.find(n.name);
                if (o != ids.end() && n.name != n.id)
                {
                    err = std::string(what) + " " + n.id + ": bind name " + n.name + " is already the id of " +
                          o->second + " — a reference could not tell them apart";
                    return false;
                }
            }
            return true;
        };
        return named(rackObjs, "#rackobj") && named(timelines, "#timeline") && named(tracks, "#track") &&
               named(clips, "#clip") && named(transitions, "#transition") && named(markers, "#marker") &&
               named(audioTracks, "#atrack") && named(audioClips, "#aclip");
    }

    void Project::normalizeRefs()
    {
        std::unordered_set<std::string> ids;
        std::unordered_map<std::string, std::string> byName;
        if (hasRack) ids.insert(rack.id);
        auto index = [&](const auto &v) {
            for (const auto &n : v)
            {
                ids.insert(n.id);
                if (!n.name.empty()) byName.emplace(n.name, n.id);
            }
        };
        index(rackObjs); index(timelines); index(tracks); index(clips); index(transitions);
        index(markers); index(audioTracks); index(audioClips);
        for (const auto &n : effects) ids.insert(n.id);
        for (const auto &n : imageEffects) ids.insert(n.id);
        for (const auto &n : anims) ids.insert(n.id);
        for (const auto &r : raw)
            if (!r.id.empty()) ids.insert(r.id);
        auto norm = [&](std::string &ref) {
            if (ref.empty() || ids.count(ref)) return;
            auto it = byName.find(ref);
            if (it != byName.end()) ref = it->second;
        };
        norm(current);
        for (auto &n : timelines) norm(n.base);
        for (auto &n : tracks) norm(n.timeline);
        for (auto &n : clips) { norm(n.track); norm(n.timeline); norm(n.src); }
        for (auto &n : transitions) { norm(n.track); norm(n.timeline); norm(n.clipA); norm(n.clipB); }
        for (auto &n : markers) norm(n.timeline);
        for (auto &n : audioTracks) { norm(n.timeline); norm(n.out); }
        for (auto &n : audioClips) { norm(n.track); norm(n.timeline); }
        for (auto &n : effects) { norm(n.node); norm(n.clip); }
        for (auto &n : imageEffects) norm(n.node);
        for (auto &n : anims) norm(n.node);
        for (auto &n : drops) { norm(n.timeline); norm(n.node); }
        for (auto &n : sets) { norm(n.timeline); norm(n.node); }
        for (auto &n : grades) { norm(n.timeline); norm(n.node); }
    }

    namespace
    {
        /** Canonicalise one #tlset's values against the target type's fields: a time reads
         *  `4.200` whatever the file said, a reference is an id. A value that will not parse is
         *  repaired to the field default and counted, exactly as it would be on the node itself. */
        template <class T> void canonOverrides(const Project &p, TlSet &s, int *rep)
        {
            std::string ignored;
            for (auto &kv : s.fields)
            {
                const schema::Field<T> *f = schema::find<T>(kv.first);
                if (!f) continue;                       // a future field: carried verbatim
                std::string v = kv.second;
                if (f->kind == schema::Kind::Ref)
                {
                    const NodeId id = p.idForRef(v);
                    if (!id.empty()) v = id;
                }
                else if (f->kind == schema::Kind::Pair)
                {
                    const auto comma = v.find(',');
                    if (comma != std::string::npos)
                    {
                        NodeId a = p.idForRef(v.substr(0, comma)), b = p.idForRef(v.substr(comma + 1));
                        v = (a.empty() ? v.substr(0, comma) : a) + "," + (b.empty() ? v.substr(comma + 1) : b);
                    }
                }
                T tmp;
                schema::readField(*f, tmp, v, false, rep, ignored);
                kv.second = schema::fieldText(*f, tmp);
            }
        }
    }

    void Project::canonicalizeDeltas(int *rep)
    {
        for (auto &s : sets)
        {
            switch (kindOf(s.node))
            {
                case NodeKind::Track: canonOverrides<Track>(*this, s, rep); break;
                case NodeKind::Clip: canonOverrides<Clip>(*this, s, rep); break;
                case NodeKind::Transition: canonOverrides<Transition>(*this, s, rep); break;
                case NodeKind::Marker: canonOverrides<Marker>(*this, s, rep); break;
                case NodeKind::ATrack: canonOverrides<ATrack>(*this, s, rep); break;
                case NodeKind::AClip: canonOverrides<AClip>(*this, s, rep); break;
                default: break;                         // dangling or refused by validate(): verbatim
            }
        }
    }

    namespace
    {
        bool isArrangement(NodeKind k)
        {
            return k == NodeKind::Track || k == NodeKind::Clip || k == NodeKind::Transition ||
                   k == NodeKind::Marker || k == NodeKind::ATrack || k == NodeKind::AClip;
        }

        template <class T> bool tlsetKeysEditable(const TlSet &s, std::string &err)
        {
            for (const auto &kv : s.fields)
            {
                const schema::Field<T> *f = schema::find<T>(kv.first);
                if (f && !f->editable)
                {
                    err = "#tlset timeline=" + s.timeline + " node=" + s.node + " cannot override '" + kv.first +
                          "' — identity and shape are the same in every version; overridable: " +
                          schema::keyList<T>();
                    return false;
                }
                if (kv.first == "id" || kv.first == "name")
                {
                    err = "#tlset timeline=" + s.timeline + " node=" + s.node + " cannot override '" + kv.first +
                          "' — a bind name means one node in every version";
                    return false;
                }
            }
            return true;
        }
    }

    bool Project::validate(std::string &err) const
    {
        if (!validateIdsAndNames(err)) return false;
        return validateRefs(err);
    }

    bool Project::validateRefs(std::string &err) const
    {
        // ── timelines: bases exist, and no chain is a cycle (named, both ends) ──
        for (const auto &t : timelines)
        {
            if (t.colour != "follow" && !(t.pinned() && !t.pinCommit().empty()))
            { err = "#timeline " + t.id + ": colour=" + t.colour + " — it is follow or pin@<commit>"; return false; }
            if (t.cut != "follow" && t.cut != "frozen")
            { err = "#timeline " + t.id + ": cut=" + t.cut + " — it is follow or frozen"; return false; }
            if (!t.base.empty() && !timeline(t.base))
            { err = "#timeline " + t.id + ": base=" + t.base + " names no #timeline"; return false; }
        }
        for (const auto &t : timelines)
        {
            std::vector<NodeId> path;
            NodeId cur = t.id;
            while (!cur.empty())
            {
                const auto seen = std::find(path.begin(), path.end(), cur);
                if (seen != path.end())
                {
                    std::string cyc;
                    for (auto it = seen; it != path.end(); ++it) cyc += *it + " -> ";
                    err = "the base chain of timelines is a cycle: " + cyc + cur +
                          " — a version cannot be its own ancestor";
                    return false;
                }
                path.push_back(cur);
                const Timeline *x = timeline(cur);
                cur = x ? x->base : NodeId();
            }
        }

        auto needTimeline = [&](const std::string &what, const NodeId &id, const NodeId &tl) {
            if (timeline(tl)) return true;
            err = what + " " + id + ": timeline=" + tl + " names no #timeline";
            return false;
        };
        for (const auto &n : tracks)
        {
            if (!needTimeline("#track", n.id, n.timeline)) return false;
            if (n.kind != "video" && n.kind != "audio")
            { err = "#track " + n.id + ": kind=" + n.kind + " — a track is video or audio"; return false; }
        }
        for (const auto &n : markers)
            if (!needTimeline("#marker", n.id, n.timeline)) return false;
        for (const auto &n : audioTracks)
            if (!needTimeline("#atrack", n.id, n.timeline)) return false;

        // The anchor rule shared by clips, transitions and audio clips. Without an explicit
        // timeline the node belongs to its track, so the track MUST exist — otherwise it would be
        // in no timeline at all and lost without a word. With one, it is a version's own addition
        // on an inherited track; that track may since have been deleted by the base, which is a
        // dangling anchor reported by resolve(), not a reason to make the file unopenable.
        auto anchor = [&](const std::string &what, const NodeId &id, const NodeId &trackId, const NodeId &tl,
                          bool audio) {
            const Track *t = track(trackId);
            const ATrack *a = audio ? audioTrack(trackId) : nullptr;
            const RawNode *r = audio ? rawNode(trackId) : nullptr;
            if (t && !audio && t->audio())
            {
                err = what + " " + id + " sits on audio track " + trackId + " — picture is a #clip, sound is an #aclip";
                return false;
            }
            if (t && audio && !t->audio())
            {
                err = what + " " + id + " sits on video track " + trackId + " — sound needs an audio track";
                return false;
            }
            if (tl.empty())
            {
                if (t || a || r) return true;
                err = what + " " + id + ": track=" + trackId + " names no " + (audio ? "audio track" : "#track");
                return false;
            }
            if (!timeline(tl)) { err = what + " " + id + ": timeline=" + tl + " names no #timeline"; return false; }
            const NodeId owner = t ? t->timeline : a ? a->timeline : NodeId();
            if (!owner.empty() && owner != tl && !derivesFrom(tl, owner))
            {
                err = what + " " + id + " is declared in " + tl + " on track " + trackId + " of " + owner + ", which " +
                      tl + " does not derive from";
                return false;
            }
            return true;
        };

        for (const auto &c : clips)
        {
            if (!(c.in < c.out) || c.speed == 0.0)
            {
                err = "#clip " + c.id + (c.speed == 0.0 ? ": speed=0" : ": in >= out") +
                      " — a clip with no frames is not a clip";
                return false;
            }
            if (!rackObj(c.src))
            {
                std::string names;
                for (const auto &n : bindNames()) names += (names.empty() ? "" : ", ") + n;
                err = "#clip " + c.id + ": src=" + c.src + " names no #rackobj; the rack's bind names are: " +
                      (names.empty() ? "(none — the rack is empty)" : names);
                return false;
            }
            if (!anchor("#clip", c.id, c.track, c.timeline, false)) return false;
        }
        for (const auto &a : audioClips)
        {
            if (!(a.in < a.out))
            { err = "#aclip " + a.id + ": in >= out — a clip with no frames is not a clip"; return false; }
            if (!anchor("#aclip", a.id, a.track, a.timeline, true)) return false;
        }
        for (const auto &t : transitions)
        {
            if (t.clipA.empty() || t.clipB.empty())
            { err = "#transition " + t.id + ": between= names two clips, outgoing,incoming"; return false; }
            if (!anchor("#transition", t.id, t.track, t.timeline, false)) return false;
            // Checked only when the transition and both neighbours are declared by ONE timeline:
            // then no other version's edit can change any of the three, and a violation is the
            // file's own. Across versions it is a conflict that resolve() reports instead —
            // refusing would let a base trim make every derived file unopenable.
            const Clip *a = clip(t.clipA), *b = clip(t.clipB);
            const NodeId tl = transitionTimeline(t);
            if (a && b && clipTimeline(*a) == tl && clipTimeline(*b) == tl &&
                (t.dur > a->duration() || t.dur > b->duration()))
            {
                err = "#transition " + t.id + " (" + canonicalTime(t.dur) + " s) is longer than " +
                      (t.dur > a->duration() ? t.clipA : t.clipB) + " — it would consume the clip";
                return false;
            }
        }
        for (const auto &x : effects)
        {
            if (!x.node.empty() && !rackObj(x.node))
            { err = "#fx " + x.id + ": node=" + x.node + " names no #rackobj"; return false; }
            if (!x.clip.empty() && !clip(x.clip))
            { err = "#fx " + x.id + ": clip=" + x.clip + " names no #clip"; return false; }
            if (x.radius < 0) { err = "#fx " + x.id + ": radius is a frame count, never negative"; return false; }
        }

        for (const auto &x : imageEffects)
        {
            if (!rackObj(x.node)) { err = "#effect " + x.id + ": node=" + x.node + " names no #rackobj"; return false; }
            if (x.mix < 0.0 || x.mix > 1.0) { err = "#effect " + x.id + ": mix is 0..1"; return false; }
        }

        // ── curves (R-ANIM-1): on a node that exists, one curve per parameter, one key per time ──
        {
            std::set<std::string> curves;
            for (const auto &a : anims)
            {
                if (!rackObj(a.node) && !effect(a.node) && !clip(a.node))
                { err = "#anim " + a.id + ": node=" + a.node + " names no #rackobj, #effect or #clip"; return false; }
                if (!curves.insert(a.node + "\x01" + a.key).second)
                { err = "two #anim for node=" + a.node + " key=" + a.key + " — one parameter has one curve"; return false; }
            }
            std::set<std::string> at;
            for (const auto &k : animKeys)
            {
                if (!anim(k.anim)) { err = "#key anim=" + k.anim + " names no #anim"; return false; }
                anim::Side s;
                if (!anim::parseSide(k.in, s) || !anim::parseSide(k.out, s))
                { err = "#key anim=" + k.anim + " t=" + canonicalNumber(k.t) + ": in/out is linear, bezier or hold"; return false; }
                if (k.inflIn <= 0 || k.inflIn > 100 || k.inflOut <= 0 || k.inflOut > 100)
                { err = "#key anim=" + k.anim + " t=" + canonicalNumber(k.t) + ": an influence is a percent, above 0 and at most 100"; return false; }
                if (!at.insert(k.anim + "\x01" + canonicalNumber(k.t)).second)
                { err = "two #key on anim=" + k.anim + " at t=" + canonicalNumber(k.t) + " — a curve has one value at a time"; return false; }
            }
        }

        // ── deltas: the timeline must exist; the TARGET may not (that is "dangling") ──
        std::set<std::string> seen;
        auto delta = [&](const char *type, const NodeId &tl, const NodeId &node, bool rackTarget) {
            if (!timeline(tl)) { err = std::string("#") + type + " node=" + node + ": timeline=" + tl + " names no #timeline"; return false; }
            if (!seen.insert(std::string(type) + "\x01" + tl + "\x01" + node).second)
            {
                err = std::string("two #") + type + " for timeline=" + tl + " node=" + node +
                      " — merge them; one version says one thing about one node";
                return false;
            }
            const NodeKind k = kindOf(node);
            if (k == NodeKind::None) return true;
            if (rackTarget ? k != NodeKind::RackObj : !isArrangement(k))
            {
                err = std::string("#") + type + " timeline=" + tl + " node=" + node + ": " + node + " is " +
                      nodeKindName(k) + (rackTarget ? ", and a colour override names a #rackobj"
                                                    : ", and a version overrides arrangement nodes — colour is #tlgrade");
                return false;
            }
            return true;
        };
        for (const auto &d : drops)
            if (!delta("tldrop", d.timeline, d.node, false)) return false;
        for (const auto &s : sets)
        {
            if (!delta("tlset", s.timeline, s.node, false)) return false;
            for (const auto &kv : s.fields)
                if (fieldIsColour(kv.first))
                {
                    err = "#tlset cannot carry colour: '" + kv.first + "' — a version's colour override is a #tlgrade";
                    return false;
                }
            bool ok = true;
            switch (kindOf(s.node))
            {
                case NodeKind::Track: ok = tlsetKeysEditable<Track>(s, err); break;
                case NodeKind::Clip: ok = tlsetKeysEditable<Clip>(s, err); break;
                case NodeKind::Transition: ok = tlsetKeysEditable<Transition>(s, err); break;
                case NodeKind::Marker: ok = tlsetKeysEditable<Marker>(s, err); break;
                case NodeKind::ATrack: ok = tlsetKeysEditable<ATrack>(s, err); break;
                case NodeKind::AClip: ok = tlsetKeysEditable<AClip>(s, err); break;
                default: break;
            }
            if (!ok) return false;
        }
        for (const auto &g : grades)
            if (!delta("tlgrade", g.timeline, g.node, true)) return false;
        return true;
    }

    // ── serialize ────────────────────────────────────────────────────────────────────────────
    namespace
    {
        void appendNotes(std::string &s, const Notes &n)
        {
            for (const auto &c : n.after) s += c + "\n";
        }

        template <class T> std::string emitNode(const T &n, const Project &p)
        {
            std::vector<std::string> ln;
            for (const auto &f : schema::fields<T>())
            {
                if (f.emit && !f.emit(n, p)) continue;
                if ((int)ln.size() <= f.line) ln.resize(f.line + 1);
                std::string &l = ln[f.line];
                if (!l.empty()) l += ' ';
                l += f.key + "=" + quoteIfNeeded(schema::fieldText(f, n));
            }
            if (ln.empty()) ln.resize(1);
            size_t last = 0;
            for (size_t j = 0; j < ln.size(); ++j)
                if (!ln[j].empty()) last = j;
            for (const auto &u : n.unknown)
            {
                std::string &l = ln[last];
                if (!l.empty()) l += ' ';
                l += u.first + "=" + quoteIfNeeded(u.second);
            }
            std::string s = std::string("#") + schema::typeName<T>();
            if (!ln[0].empty()) s += " " + ln[0];
            if (!n.notes.inlineComment.empty()) s += " " + n.notes.inlineComment;
            s += "\n";
            for (size_t j = 1; j < ln.size(); ++j)
                if (!ln[j].empty()) s += "  " + ln[j] + "\n";
            appendNotes(s, n.notes);
            return s;
        }

        struct Item
        {
            std::string k1, k2, text;
        };
        void group(std::string &o, std::vector<Item> items)
        {
            if (items.empty()) return;
            std::sort(items.begin(), items.end(), [](const Item &a, const Item &b) {
                return a.k1 != b.k1 ? a.k1 < b.k1 : a.k2 < b.k2;
            });
            o += "\n";
            for (const auto &it : items) o += it.text;
        }
        std::string rawText(const RawNode &r)
        {
            std::string s;
            for (const auto &l : r.lines) s += l + "\n";
            return s;
        }
    }

    std::string Project::serialize() const
    {
        std::string o;
        for (const auto &c : preamble) o += c + "\n";
        auto headerLine = [&](const std::string &k, const std::string &v) {
            std::string line = k;
            if (line.size() < 14) line.append(14 - line.size(), ' ');
            line += " = " + quoteIfNeeded(v);
            const auto it = headerNotes.find(k);
            if (it != headerNotes.end() && !it->second.inlineComment.empty()) line += " " + it->second.inlineComment;
            o += line + "\n";
            if (it != headerNotes.end()) appendNotes(o, it->second);
        };
        for (const auto &f : schema::fields<Project>())
            if (!f.emit || f.emit(*this, *this) || headerNotes.count(f.key)) headerLine(f.key, schema::fieldText(f, *this));
        for (const auto &kv : headerUnknown) headerLine(kv.first, kv.second);

        // Node order: by type in project-format §2–§5 order, then by id, so "no change" is
        // literally no diff. `order=` fields carry the user-visible ordering, which is why file
        // order never has to.
        auto typed = [&](const auto &v) {
            std::vector<Item> items;
            for (const auto &n : v) items.push_back({n.id, "", emitNode(n, *this)});
            return items;
        };
        auto rawOf = [&](const std::string &type, std::vector<Item> items) {
            for (const auto &r : raw)
                if (r.type == type) items.push_back({r.id, r.lines.empty() ? "" : r.lines[0], rawText(r)});
            return items;
        };

        if (hasRack) group(o, {{rack.id, "", emitNode(rack, *this)}});
        group(o, typed(rackObjs));
        group(o, typed(timelines));
        {
            std::vector<Item> items;
            for (const auto &d : drops)
            {
                std::string s = "#tldrop timeline=" + quoteIfNeeded(d.timeline) + " node=" + quoteIfNeeded(d.node);
                for (const auto &u : d.unknown) s += " " + u.first + "=" + quoteIfNeeded(u.second);
                if (!d.notes.inlineComment.empty()) s += " " + d.notes.inlineComment;
                s += "\n";
                appendNotes(s, d.notes);
                items.push_back({d.timeline, d.node, s});
            }
            group(o, items);
        }
        {
            std::vector<Item> items;
            for (const auto &d : sets)
            {
                std::string s = "#tlset timeline=" + quoteIfNeeded(d.timeline) + " node=" + quoteIfNeeded(d.node);
                for (const auto &u : d.fields) s += " " + u.first + "=" + quoteIfNeeded(u.second);
                if (!d.notes.inlineComment.empty()) s += " " + d.notes.inlineComment;
                s += "\n";
                appendNotes(s, d.notes);
                items.push_back({d.timeline, d.node, s});
            }
            group(o, items);
        }
        {
            std::vector<Item> items;
            for (const auto &d : grades)
            {
                std::string s = "#tlgrade timeline=" + quoteIfNeeded(d.timeline) + " node=" + quoteIfNeeded(d.node);
                for (const auto &u : d.deltas) s += " " + u.first + "=" + canonicalNumber(u.second);
                if (!d.notes.inlineComment.empty()) s += " " + d.notes.inlineComment;
                s += "\n";
                appendNotes(s, d.notes);
                items.push_back({d.timeline, d.node, s});
            }
            group(o, items);
        }
        group(o, typed(tracks));
        group(o, typed(clips));
        group(o, typed(transitions));
        group(o, typed(markers));
        group(o, rawOf("atrack", typed(audioTracks)));
        group(o, rawOf("aclip", typed(audioClips)));
        group(o, rawOf("arack", {}));
        group(o, rawOf("aauto", {}));
        group(o, rawOf("asend", {}));
        group(o, typed(effects));
        group(o, typed(imageEffects));
        {
            // each curve, then its keyframes in time order — a moved key is a one-line diff
            std::vector<Item> items;
            for (const auto &a : anims) items.push_back({a.id, "", emitNode(a, *this)});
            for (const auto &k : animKeys)
            {
                char sk[48];
                std::snprintf(sk, sizeof sk, "k%024.9f", k.t + 1e6);
                items.push_back({k.anim, sk, emitNode(k, *this)});
            }
            group(o, items);
        }
        {
            std::vector<Item> items;
            for (const auto &r : raw)
            {
                if (r.type == "atrack" || r.type == "aclip" || r.type == "arack" || r.type == "aauto" || r.type == "asend")
                    continue;
                items.push_back({r.type, r.id + "\x01" + (r.lines.empty() ? "" : r.lines[0]), rawText(r)});
            }
            group(o, items);
        }
        return o;
    }

    bool Project::load(const std::string &path, std::string &err, int *repaired)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f) { err = "cannot read " + path; return false; }
        std::ostringstream ss;
        ss << f.rdbuf();
        return parse(ss.str(), err, repaired);
    }

    bool Project::save(const std::string &path, std::string &err) const
    {
        const std::string tmp = path + ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f) { err = "cannot write " + tmp; return false; }
            f << serialize();
            f.flush();
            if (!f) { err = "write failed: " + tmp; return false; }
        }
        if (std::rename(tmp.c_str(), path.c_str()) != 0)
        {
            std::remove(tmp.c_str());
            err = "cannot replace " + path;
            return false;
        }
        return true;
    }

    bool Project::roundTripsExactly(const std::string &text, std::string &err)
    {
        Project p;
        if (!p.parse(text, err)) return false;
        const std::string out = p.serialize();
        if (out == text) return true;
        std::istringstream a(text), b(out);
        std::string la, lb;
        for (int no = 1;; ++no)
        {
            const bool ga = (bool)std::getline(a, la), gb = (bool)std::getline(b, lb);
            if (!ga && !gb) { err = "serialize differs only in line endings or the final newline"; break; }
            if (!ga || !gb || la != lb)
            {
                err = "not a fixed point at line " + std::to_string(no) + ":\n  read:  " + (ga ? la : "<eof>") +
                      "\n  wrote: " + (gb ? lb : "<eof>");
                break;
            }
        }
        return false;
    }

    std::vector<std::string> Project::unrenderable() const
    {
        std::vector<std::string> out;
        for (const auto &r : raw)
        {
            std::string who = r.id;
            if (who.empty() && !r.fields.empty()) who = r.fields.front().first + "=" + r.fields.front().second;
            out.push_back("#" + r.type + (who.empty() ? "" : " " + who) + " (" + r.why + ")");
        }
        std::sort(out.begin(), out.end());
        return out;
    }
}
}
