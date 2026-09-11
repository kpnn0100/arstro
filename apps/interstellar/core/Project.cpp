#include "Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    namespace
    {
        std::string trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
            return s.substr(a, b - a);
        }

        /** Strip a trailing `; comment`, honouring quotes so a `;` inside a string survives. */
        std::string stripComment(const std::string &s)
        {
            bool q = false;
            for (size_t i = 0; i < s.size(); ++i)
            {
                if (s[i] == '"') q = !q;
                else if (s[i] == ';' && !q) return s.substr(0, i);
            }
            return s;
        }

        std::string unquote(const std::string &v)
        {
            if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
            {
                std::string out;
                for (size_t i = 1; i + 1 < v.size(); ++i)
                {
                    if (v[i] == '\\' && i + 2 < v.size())
                    {
                        ++i;
                        out += v[i] == 'n' ? '\n' : v[i];
                    }
                    else out += v[i];
                }
                return out;
            }
            return v;
        }

        /** Split `key = value` / `key=value`. */
        bool splitKV(const std::string &line, std::string &k, std::string &v)
        {
            const auto eq = line.find('=');
            if (eq == std::string::npos) return false;
            k = trim(line.substr(0, eq));
            v = trim(line.substr(eq + 1));
            return !k.empty();
        }

        /** Tokenize a node header's `key=value` pairs, honouring quotes. */
        std::vector<std::pair<std::string, std::string>> headerFields(const std::string &s)
        {
            std::vector<std::pair<std::string, std::string>> out;
            std::string cur;
            bool q = false;
            auto flush = [&]() {
                const std::string t = trim(cur);
                cur.clear();
                if (t.empty()) return;
                std::string k, v;
                if (splitKV(t, k, v)) out.emplace_back(k, unquote(v));
            };
            for (char c : s)
            {
                if (c == '"') { q = !q; cur += c; continue; }
                if (!q && (c == ' ' || c == '\t')) { flush(); continue; }
                cur += c;
            }
            flush();
            return out;
        }

        /** A number, repaired to `fallback` when the text is not finite (cosmo's D-36). */
        double num(const std::string &v, double fallback, int *repaired)
        {
            const double d = std::atof(v.c_str());
            if (!std::isfinite(d))
            {
                if (repaired) ++*repaired;
                return fallback;
            }
            return d;
        }

        bool boolOf(const std::string &v) { return v == "1" || v == "true" || v == "yes"; }
        const char *boolText(bool b) { return b ? "true" : "false"; }

        /** Cubic bezier-ish smoothing for the eased families. One implementation, shared. */
        double smoothIn(double t) { return t * t * t; }
        double smoothOut(double t) { const double u = 1.0 - t; return 1.0 - u * u * u; }
    }

    // ── enums ────────────────────────────────────────────────────────────────────────────────
    const char *blendName(Blend b)
    {
        switch (b)
        {
            case Blend::Normal: return "normal";
            case Blend::Multiply: return "multiply";
            case Blend::Screen: return "screen";
            case Blend::Overlay: return "overlay";
            case Blend::Add: return "add";
            case Blend::Subtract: return "subtract";
            case Blend::Difference: return "difference";
        }
        return "normal";
    }
    const char *fitName(Fit f)
    {
        switch (f)
        {
            case Fit::Contain: return "contain";
            case Fit::Cover: return "cover";
            case Fit::Stretch: return "stretch";
            case Fit::None: return "none";
        }
        return "contain";
    }
    const char *interpName(Interp i)
    {
        switch (i)
        {
            case Interp::Linear: return "linear";
            case Interp::Bezier: return "bezier";
            case Interp::Hold: return "hold";
            case Interp::Step: return "step";
        }
        return "bezier";
    }
    const char *autoModeName(AutoMode m)
    {
        switch (m)
        {
            case AutoMode::Absolute: return "absolute";
            case AutoMode::Add: return "add";
            case AutoMode::Multiply: return "multiply";
        }
        return "absolute";
    }
    const char *easeName(Ease e)
    {
        switch (e)
        {
            case Ease::Linear: return "linear";
            case Ease::EaseIn: return "easeIn";
            case Ease::EaseOut: return "easeOut";
            case Ease::EaseInOut: return "easeInOut";
        }
        return "linear";
    }
    bool parseBlend(const std::string &s, Blend &o)
    {
        for (int i = 0; i <= (int)Blend::Difference; ++i)
            if (s == blendName((Blend)i)) { o = (Blend)i; return true; }
        return false;
    }
    bool parseFit(const std::string &s, Fit &o)
    {
        for (int i = 0; i <= (int)Fit::None; ++i)
            if (s == fitName((Fit)i)) { o = (Fit)i; return true; }
        return false;
    }
    bool parseInterp(const std::string &s, Interp &o)
    {
        for (int i = 0; i <= (int)Interp::Step; ++i)
            if (s == interpName((Interp)i)) { o = (Interp)i; return true; }
        return false;
    }
    bool parseAutoMode(const std::string &s, AutoMode &o)
    {
        for (int i = 0; i <= (int)AutoMode::Multiply; ++i)
            if (s == autoModeName((AutoMode)i)) { o = (AutoMode)i; return true; }
        return false;
    }
    bool parseEase(const std::string &s, Ease &o)
    {
        for (int i = 0; i <= (int)Ease::EaseInOut; ++i)
            if (s == easeName((Ease)i)) { o = (Ease)i; return true; }
        return false;
    }
    double applyEase(Ease e, double t)
    {
        t = t < 0 ? 0 : (t > 1 ? 1 : t);
        switch (e)
        {
            case Ease::Linear: return t;
            case Ease::EaseIn: return smoothIn(t);
            case Ease::EaseOut: return smoothOut(t);
            case Ease::EaseInOut: return t < 0.5 ? 4 * t * t * t : 1 - std::pow(-2 * t + 2, 3) / 2;
        }
        return t;
    }

    // ── canonical text ───────────────────────────────────────────────────────────────────────
    std::string canonicalNumber(double v)
    {
        if (!std::isfinite(v)) v = 0.0;
        // An integral value keeps one ".0" so a float field stays legible as a float; otherwise
        // the shortest representation that round-trips exactly. Gene's emitter solves the same
        // problem the same way, and for the same reason: the file is read by people and must
        // still be exact.
        if (v == std::floor(v) && std::fabs(v) < 1e15)
        {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%.1f", v);
            return buf;
        }
        for (int prec = 1; prec <= 17; ++prec)
        {
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.*g", prec, v);
            if (std::atof(buf) == v) return buf;
        }
        return "0.0";
    }

    std::string canonicalTime(double v)
    {
        if (!std::isfinite(v)) v = 0.0;
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.3f", v);
        return buf;
    }

    std::string quoteIfNeeded(const std::string &v)
    {
        bool need = v.empty();
        for (char c : v)
            if (c == ' ' || c == '\t' || c == '"' || c == ';' || c == '=' || c == '\n') need = true;
        if (!need) return v;
        std::string out = "\"";
        for (char c : v)
        {
            if (c == '"' || c == '\\') out += '\\';
            if (c == '\n') { out += "\\n"; continue; }
            out += c;
        }
        return out + "\"";
    }

    // ── AutoClip sampling ────────────────────────────────────────────────────────────────────
    double AutoClip::value(double localT) const
    {
        if (points.empty()) return 0.0;
        if (localT <= points.front().t) return points.front().value;
        if (localT >= points.back().t) return points.back().value;
        for (size_t i = 0; i + 1 < points.size(); ++i)
        {
            const AutoPoint &a = points[i], &b = points[i + 1];
            if (localT < a.t || localT > b.t) continue;
            const double span = b.t - a.t;
            if (span <= 0.0) return b.value;
            const double u = (localT - a.t) / span;
            switch (interp)
            {
                case Interp::Hold: return a.value;
                case Interp::Step: return u < 0.5 ? a.value : b.value;
                case Interp::Linear: return a.value + (b.value - a.value) * u;
                case Interp::Bezier: break;
            }
            // The easing named on a point is the easing OUT of it, into the next.
            return a.value + (b.value - a.value) * applyEase(a.ease, u);
        }
        return points.back().value;
    }

    // ── validation ───────────────────────────────────────────────────────────────────────────
    bool Project::fieldIsColour(const std::string &key)
    {
        // R-CUT-2: a colour field on a clip is a VALIDATION ERROR, not an ignored key. The list
        // is deliberately generous — every spelling a user might reach for — because the failure
        // it prevents is silent loss of their edit.
        static const char *kColour[] = {
            "grade", "curve", "mixer", "lut", "look", "exposure", "contrast", "temp", "tint",
            "saturation", "vibrance", "highlights", "shadows", "whites", "blacks", "clarity",
            "texture", "dehaze", "editparams", "params", "basic", "detail", "wb"};
        std::string lower;
        for (char c : key) lower += (char)std::tolower((unsigned char)c);
        for (const char *c : kColour)
            if (lower == c || lower.rfind(std::string(c) + ".", 0) == 0) return true;
        return false;
    }

    bool Project::nameIsLegal(const std::string &name, std::string &why)
    {
        if (name.empty()) { why = "a bind name may not be empty"; return false; }
        if (name.size() > 32) { why = "a bind name is at most 32 characters"; return false; }
        if (!(std::isalpha((unsigned char)name[0]) || name[0] == '_'))
        { why = "a bind name starts with a letter or underscore: " + name; return false; }
        for (char c : name)
            if (!(std::isalnum((unsigned char)c) || c == '_'))
            { why = "a bind name allows only letters, digits and underscore: " + name; return false; }
        // Reserved, because an expression could not tell them apart from a name (R-PARAM-2).
        static const char *kReserved[] = {"project", "t", "frame", "fps", "dur", "rack", "self",
                                          "min", "max", "clamp", "lerp", "mix", "remap", "abs",
                                          "sin", "cos", "tan", "pow", "sqrt", "log", "exp",
                                          "floor", "ceil", "round", "sign", "step", "smoothstep"};
        for (const char *r : kReserved)
            if (name == r) { why = "reserved word: " + name; return false; }
        return true;
    }

    // ── lookup ───────────────────────────────────────────────────────────────────────────────
    template <class V> static auto findById(V &v, const NodeId &id) -> decltype(&v[0])
    {
        for (auto &e : v)
            if (e.id == id || e.name == id) return &e;
        return nullptr;
    }
    Track *Project::track(const NodeId &id) { return findById(tracks, id); }
    const Track *Project::track(const NodeId &id) const { return findById(tracks, id); }
    Clip *Project::clip(const NodeId &id) { return findById(clips, id); }
    const Clip *Project::clip(const NodeId &id) const { return findById(clips, id); }
    AutoClip *Project::autoClip(const NodeId &id) { return findById(autoClips, id); }
    const AutoClip *Project::autoClip(const NodeId &id) const { return findById(autoClips, id); }
    AutoLink *Project::autoLink(const NodeId &id) { return findById(autoLinks, id); }
    const AutoLink *Project::autoLink(const NodeId &id) const { return findById(autoLinks, id); }

    RackObj *Project::rackObj(const NodeId &id) { return findById(rackObjs, id); }
    const RackObj *Project::rackObj(const NodeId &id) const { return findById(rackObjs, id); }

    const RackObj *Project::rackObjForNode(const std::string &cosmoNode) const
    {
        for (const auto &r : rackObjs)
            if (r.node == cosmoNode) return &r;
        return nullptr;
    }

    RackObj &Project::ensureRackObj(const std::string &cosmoNode, const std::string &hint)
    {
        for (auto &r : rackObjs)
            if (r.node == cosmoNode) return r;
        RackObj r;
        r.node = cosmoNode;
        r.id = freshId("ro_");
        // Derive a LEGAL bind name from Cosmo's own name, which may be a filename or contain
        // spaces. Stable once assigned, because an expression spells it (R-PARAM-2).
        std::string base;
        for (char c : hint)
        {
            if (std::isalnum((unsigned char)c)) base += (char)std::tolower((unsigned char)c);
            else if (!base.empty() && base.back() != '_') base += '_';
        }
        while (!base.empty() && base.back() == '_') base.pop_back();
        if (base.empty() || !(std::isalpha((unsigned char)base[0]) || base[0] == '_')) base = "s_" + base;
        if (base.size() > 32) base.resize(32);
        std::string why;
        if (!nameIsLegal(base, why)) base = "s_" + std::to_string(rackObjs.size() + 1);
        r.name = freshName(base);
        rackObjs.push_back(r);
        return rackObjs.back();
    }

    const Embed *Project::rackEmbed() const
    {
        for (const auto &e : embeds)
            if (e.role == "rack") return &e;
        return nullptr;
    }
    Embed *Project::rackEmbed()
    {
        for (auto &e : embeds)
            if (e.role == "rack") return &e;
        return nullptr;
    }

    bool Project::idForName(const std::string &n, NodeId &out) const
    {
        auto scan = [&](const auto &v) {
            for (const auto &e : v)
                if (e.name == n) { out = e.id; return true; }
            return false;
        };
        return scan(tracks) || scan(clips) || scan(autoClips) || scan(autoLinks) ||
               scan(markers) || scan(embeds) || scan(transitions) || scan(rackObjs);
    }

    bool Project::nameIsTaken(const std::string &n) const
    {
        NodeId ignored;
        return idForName(n, ignored);
    }

    NodeId Project::freshId(const std::string &prefix)
    {
        for (;;)
        {
            const NodeId candidate = prefix + std::to_string(mNextId++);
            bool taken = false;
            auto scan = [&](const auto &v) {
                for (const auto &e : v)
                    if (e.id == candidate) taken = true;
            };
            scan(tracks); scan(clips); scan(transitions); scan(autoClips);
            scan(autoLinks); scan(markers); scan(embeds); scan(rackObjs);
            for (const auto &b : bindings)
                if (b.id == candidate) taken = true;
            if (!taken) return candidate;
        }
    }

    std::string Project::freshName(const std::string &base) const
    {
        if (!nameIsTaken(base)) return base;
        for (int i = 2;; ++i)
        {
            const std::string c = base + std::to_string(i);
            if (!nameIsTaken(c)) return c;
        }
    }

    bool Project::rename(const NodeId &id, const std::string &newName, std::string &err)
    {
        if (!nameIsLegal(newName, err)) return false;
        if (nameIsTaken(newName)) { err = "that bind name is already used: " + newName; return false; }

        std::string oldName;
        auto retarget = [&](auto &v) {
            for (auto &e : v)
                if (e.id == id) { oldName = e.name; e.name = newName; }
        };
        retarget(tracks); retarget(clips); retarget(transitions); retarget(autoClips);
        retarget(autoLinks); retarget(markers); retarget(embeds); retarget(rackObjs);
        if (oldName.empty()) { err = "no such node: " + id; return false; }
        if (oldName == newName) return true;

        // Rewrite every reference, atomically: an expression or an automation target left
        // holding the old name is worse than a rename that refused (R-PARAM-2).
        auto rewriteAddress = [&](std::string &addr) {
            const auto dot = addr.find('.');
            const std::string obj = dot == std::string::npos ? addr : addr.substr(0, dot);
            if (obj != oldName) return;
            addr = newName + (dot == std::string::npos ? std::string() : addr.substr(dot));
        };
        for (auto &l : autoLinks) rewriteAddress(l.target);
        for (auto &b : bindings)
        {
            rewriteAddress(b.target);
            // The expression is text, so the rewrite is textual — but only on whole identifiers,
            // or renaming `gr1` would corrupt `gr10` and every function name that contained it.
            std::string out;
            for (size_t i = 0; i < b.expr.size();)
            {
                if (std::isalpha((unsigned char)b.expr[i]) || b.expr[i] == '_')
                {
                    size_t j = i;
                    while (j < b.expr.size() && (std::isalnum((unsigned char)b.expr[j]) || b.expr[j] == '_')) ++j;
                    const std::string word = b.expr.substr(i, j - i);
                    out += (word == oldName) ? newName : word;
                    i = j;
                }
                else out += b.expr[i++];
            }
            b.expr = out;
        }
        return true;
    }

    double Project::duration() const
    {
        double d = 0;
        for (const auto &c : clips) d = std::max(d, c.end());
        return d;
    }
    long long Project::frameAt(double seconds) const
    {
        // Frames are the authority (R-CUT-5): round rather than truncate, so a time written as
        // 4.000 at 24 fps cannot land on frame 95 through a 1e-15 error.
        return (long long)std::llround(seconds * fps);
    }
    double Project::secondsAt(long long frame) const { return fps != 0.0 ? (double)frame / fps : 0.0; }

    // ── parse ────────────────────────────────────────────────────────────────────────────────
    bool Project::parse(const std::string &text, std::string &err, int *repaired)
    {
        *this = Project();
        err.clear();
        if (repaired) *repaired = 0;

        std::istringstream in(text);
        std::string raw;
        int lineNo = 0;
        enum class Ctx { Header, Track, Clip, Transition, AutoClip, AutoLink, Bind, Marker, Embed, RackObj, Settings };
        Ctx ctx = Ctx::Header;
        auto fail = [&](const std::string &m) { err = "line " + std::to_string(lineNo) + ": " + m; return false; };

        while (std::getline(in, raw))
        {
            ++lineNo;
            const std::string line = trim(stripComment(raw));
            if (line.empty()) continue;

            if (line[0] == '#')
            {
                const auto sp = line.find_first_of(" \t");
                const std::string type = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
                const auto fields = headerFields(sp == std::string::npos ? std::string() : line.substr(sp));
                auto f = [&](const char *k, const std::string &d = std::string()) {
                    for (const auto &kv : fields)
                        if (kv.first == k) return kv.second;
                    return d;
                };
                auto known = [&](std::initializer_list<const char *> keys, UnknownFields &un) {
                    for (const auto &kv : fields)
                    {
                        bool hit = false;
                        for (const char *k : keys)
                            if (kv.first == k) hit = true;
                        if (!hit) un.push_back(kv);
                    }
                };

                if (type == "track")
                {
                    Track t;
                    t.id = f("id"); t.name = f("name", t.id);
                    t.audio = f("kind", "video") == "audio";
                    t.order = std::atoi(f("order", "0").c_str());
                    t.mute = boolOf(f("mute", "false"));
                    t.lock = boolOf(f("lock", "false"));
                    t.opacity = num(f("opacity", "1.0"), 1.0, repaired);
                    t.gain = num(f("gain", "0.0"), 0.0, repaired);
                    parseBlend(f("blend", "normal"), t.blend);
                    known({"id", "name", "kind", "order", "mute", "lock", "opacity", "gain", "blend"}, t.unknown);
                    if (t.id.empty()) return fail("#track needs an id");
                    tracks.push_back(t); ctx = Ctx::Track;
                }
                else if (type == "clip")
                {
                    Clip c;
                    c.id = f("id"); c.name = f("name", c.id); c.track = f("track");
                    c.order = std::atoi(f("order", "0").c_str());
                    c.src = f("src");
                    c.at = num(f("at", "0"), 0, repaired);
                    c.in = num(f("in", "0"), 0, repaired);
                    c.out = num(f("out", "0"), 0, repaired);
                    c.speed = num(f("speed", "1.0"), 1.0, repaired);
                    parseFit(f("fit", "contain"), c.fit);
                    c.opacity = num(f("opacity", "1.0"), 1.0, repaired);
                    parseBlend(f("blend", "normal"), c.blend);
                    for (const auto &kv : fields)
                    {
                        if (fieldIsColour(kv.first))
                            return fail("a clip carries no colour: '" + kv.first +
                                        "' belongs to the rack node it references (R-CUT-2)");
                        if (kv.first == "geom.x") c.geom.x = num(kv.second, 0, repaired);
                        else if (kv.first == "geom.y") c.geom.y = num(kv.second, 0, repaired);
                        else if (kv.first == "geom.scale") c.geom.scale = num(kv.second, 1, repaired);
                        else if (kv.first == "geom.rotation") c.geom.rotation = num(kv.second, 0, repaired);
                        else if (kv.first == "geom.anchor.x") c.geom.anchorX = num(kv.second, 0.5, repaired);
                        else if (kv.first == "geom.anchor.y") c.geom.anchorY = num(kv.second, 0.5, repaired);
                        else if (kv.first == "geom.crop.x") c.geom.cropX = num(kv.second, 0, repaired);
                        else if (kv.first == "geom.crop.y") c.geom.cropY = num(kv.second, 0, repaired);
                        else if (kv.first == "geom.crop.w") c.geom.cropW = num(kv.second, 1, repaired);
                        else if (kv.first == "geom.crop.h") c.geom.cropH = num(kv.second, 1, repaired);
                    }
                    known({"id", "name", "track", "order", "src", "at", "in", "out", "speed", "fit",
                           "opacity", "blend", "geom.x", "geom.y", "geom.scale", "geom.rotation",
                           "geom.anchor.x", "geom.anchor.y", "geom.crop.x", "geom.crop.y",
                           "geom.crop.w", "geom.crop.h"}, c.unknown);
                    if (c.id.empty()) return fail("#clip needs an id");
                    if (c.in >= c.out) return fail("#clip " + c.id + ": in >= out — a clip with no frames is not a clip");
                    if (c.speed == 0.0) return fail("#clip " + c.id + ": speed=0");
                    clips.push_back(c); ctx = Ctx::Clip;
                }
                else if (type == "transition")
                {
                    Transition t;
                    t.id = f("id"); t.name = f("name", t.id); t.track = f("track");
                    const std::string between = f("between");
                    const auto comma = between.find(',');
                    if (comma != std::string::npos)
                    { t.clipA = between.substr(0, comma); t.clipB = between.substr(comma + 1); }
                    t.kind = f("kind", "dissolve");
                    t.dur = num(f("dur", "0.5"), 0.5, repaired);
                    parseEase(f("easing", "linear"), t.easing);
                    t.color = f("color", "#000000");
                    known({"id", "name", "track", "between", "kind", "dur", "easing", "color"}, t.unknown);
                    if (t.id.empty()) return fail("#transition needs an id");
                    transitions.push_back(t); ctx = Ctx::Transition;
                }
                else if (type == "autoclip")
                {
                    AutoClip a;
                    a.id = f("id"); a.name = f("name", a.id);
                    a.dur = num(f("dur", "1.0"), 1.0, repaired);
                    parseInterp(f("interp", "bezier"), a.interp);
                    known({"id", "name", "dur", "interp"}, a.unknown);
                    if (a.id.empty()) return fail("#autoclip needs an id");
                    autoClips.push_back(a); ctx = Ctx::AutoClip;
                }
                else if (type == "autolink")
                {
                    AutoLink l;
                    l.id = f("id"); l.name = f("name", l.id); l.clip = f("clip");
                    l.target = f("target");
                    l.at = num(f("at", "0"), 0, repaired);
                    l.dur = num(f("dur", "0"), 0, repaired);
                    l.haveFromTo = !f("from").empty() || !f("to").empty() || f("scale").empty();
                    l.from = num(f("from", "0"), 0, repaired);
                    l.to = num(f("to", "1"), 1, repaired);
                    l.scale = num(f("scale", "1"), 1, repaired);
                    l.offset = num(f("offset", "0"), 0, repaired);
                    parseAutoMode(f("mode", "absolute"), l.mode);
                    l.scope = f("scope");
                    l.fadeIn = std::atoi(f("fadeIn", "0").c_str());
                    l.fadeOut = std::atoi(f("fadeOut", "0").c_str());
                    known({"id", "name", "clip", "target", "at", "dur", "from", "to", "scale",
                           "offset", "mode", "scope", "fadeIn", "fadeOut"}, l.unknown);
                    if (l.id.empty()) return fail("#autolink needs an id");
                    if (l.target.empty()) return fail("#autolink " + l.id + " needs a target address");
                    autoLinks.push_back(l); ctx = Ctx::AutoLink;
                }
                else if (type == "bind")
                {
                    Binding b;
                    b.id = f("id"); b.target = f("target"); b.expr = f("expr");
                    known({"id", "target", "expr"}, b.unknown);
                    if (b.target.empty()) return fail("#bind needs a target address");
                    if (b.id.empty()) b.id = "bn_" + std::to_string(bindings.size() + 1);
                    bindings.push_back(b); ctx = Ctx::Bind;
                }
                else if (type == "marker")
                {
                    Marker m;
                    m.id = f("id"); m.name = f("name", m.id);
                    m.at = num(f("at", "0"), 0, repaired);
                    m.color = f("color", "#4F7EF7"); m.note = f("note");
                    known({"id", "name", "at", "color", "note"}, m.unknown);
                    if (m.id.empty()) return fail("#marker needs an id");
                    markers.push_back(m); ctx = Ctx::Marker;
                }
                else if (type == "embed")
                {
                    Embed e;
                    e.id = f("id"); e.name = f("name", e.id);
                    e.role = f("as"); e.target = f("target"); e.path = f("path");
                    e.branch = f("branch", "main"); e.writeBranch = f("writeBranch");
                    e.track = f("track"); e.offset = num(f("offset", "0"), 0, repaired);
                    known({"id", "name", "as", "target", "path", "branch", "writeBranch", "track", "offset"}, e.unknown);
                    if (e.id.empty()) return fail("#embed needs an id");
                    if (e.role != "rack" && e.role != "audio")
                        return fail("#embed " + e.id + ": as= must be rack or audio");
                    if (e.role == "rack" && rackEmbed())
                        return fail("a project has exactly one as=rack embed — two colour authorities (R-COSMO-1)");
                    if (e.pinned() && !e.writeBranch.empty())
                        return fail("#embed " + e.id + ": a pinned embed is read-only, so writeBranch is illegal (R-VCS-6)");
                    embeds.push_back(e); ctx = Ctx::Embed;
                }
                else if (type == "rackobj")
                {
                    RackObj r;
                    r.id = f("id"); r.name = f("name", r.id); r.node = f("node");
                    r.opacity = num(f("opacity", "1.0"), 1.0, repaired);
                    known({"id", "name", "node", "opacity"}, r.unknown);
                    if (r.id.empty() || r.node.empty()) return fail("#rackobj needs an id and a node");
                    rackObjs.push_back(r); ctx = Ctx::RackObj;
                }
                else if (type == "settings") { ctx = Ctx::Settings; }
                else return fail("unknown node type: #" + type);
                continue;
            }

            // ── an indented field line, or a header assignment ──
            std::string k, v;
            if (!splitKV(line, k, v)) return fail("expected key = value: " + line);
            v = unquote(v);

            switch (ctx)
            {
                case Ctx::Header:
                    if (k == "arstro-project") { /* version; accepted */ }
                    else if (k == "app")
                    { if (v != "interstellar") return fail("app = " + v + " — this is not an interstellar project"); }
                    else if (k == "id") id = v;
                    else if (k == "name") name = v;
                    else if (k == "fps") fps = num(v, 24.0, repaired);
                    else if (k == "width") width = std::atoi(v.c_str());
                    else if (k == "height") height = std::atoi(v.c_str());
                    else if (k == "par") par = num(v, 1.0, repaired);
                    else if (k == "colorspace") colorspace = v;
                    else if (k == "duration") { /* derived; read and ignored */ }
                    else headerUnknown.emplace_back(k, v);
                    break;
                case Ctx::AutoClip:
                {
                    // A breakpoint line is `<localTime> = <value> [ease=<e>]`.
                    AutoClip &a = autoClips.back();
                    AutoPoint p;
                    p.t = num(k, 0, repaired);
                    const auto sp = v.find_first_of(" \t");
                    p.value = num(sp == std::string::npos ? v : v.substr(0, sp), 0, repaired);
                    if (sp != std::string::npos)
                        for (const auto &kv : headerFields(v.substr(sp)))
                            if (kv.first == "ease") parseEase(kv.second, p.ease);
                    a.points.push_back(p);
                    break;
                }
                case Ctx::Settings:
                    if (k == "proxyEdge") settings.proxyEdge = std::atoi(v.c_str());
                    else if (k == "cpuPercent") settings.cpuPercent = std::atoi(v.c_str());
                    else if (k == "cacheBytes") settings.cacheBytes = std::atoll(v.c_str());
                    else if (k == "lintOnRender") settings.lintOnRender = boolOf(v);
                    else settings.unknown.emplace_back(k, v);
                    break;
                default:
                    // Field lines on the other node types are the header's own keys, indented.
                    // Re-dispatch through the same header parser so one spelling serves both.
                    return fail("unexpected indented field '" + k + "' — put node fields on the #node line");
            }
        }

        for (auto &a : autoClips)
            std::sort(a.points.begin(), a.points.end(),
                      [](const AutoPoint &x, const AutoPoint &y) { return x.t < y.t; });

        // Ids must be unique across the whole document: they are the merge anchors (R-FMT-5).
        std::set<std::string> seen;
        auto uniq = [&](const auto &v) {
            for (const auto &e : v)
                if (!seen.insert(e.id).second) { err = "duplicate id: " + e.id; return false; }
            return true;
        };
        if (!uniq(tracks) || !uniq(clips) || !uniq(transitions) || !uniq(autoClips) ||
            !uniq(autoLinks) || !uniq(markers) || !uniq(embeds) || !uniq(rackObjs))
            return false;
        return true;
    }

    // ── serialize ────────────────────────────────────────────────────────────────────────────
    std::string Project::serialize() const
    {
        std::ostringstream o;
        auto un = [&](const UnknownFields &f) {
            for (const auto &kv : f) o << ' ' << kv.first << '=' << quoteIfNeeded(kv.second);
        };

        o << "arstro-project = 1\n";
        o << "app            = interstellar\n";
        o << "id             = " << quoteIfNeeded(id) << '\n';
        o << "name           = " << quoteIfNeeded(name) << '\n';
        o << "fps            = " << canonicalNumber(fps) << '\n';
        o << "width          = " << width << '\n';
        o << "height         = " << height << '\n';
        o << "par            = " << canonicalNumber(par) << '\n';
        o << "colorspace     = " << colorspace << '\n';
        for (const auto &kv : headerUnknown) o << kv.first << " = " << quoteIfNeeded(kv.second) << '\n';

        // Node order: by type in the order of project-format.md §2, then by id, so "no change"
        // is literally no diff. `order=` fields carry the user-visible ordering, which is why
        // file order never has to (R-FMT-4).
        auto sorted = [](auto v) {
            std::sort(v.begin(), v.end(), [](const auto &a, const auto &b) { return a.id < b.id; });
            return v;
        };

        if (!embeds.empty()) o << '\n';
        for (const auto &e : sorted(embeds))
        {
            o << "#embed id=" << e.id << " name=" << e.name << " as=" << e.role
              << " target=" << quoteIfNeeded(e.target) << " path=" << quoteIfNeeded(e.path)
              << " branch=" << e.branch;
            if (!e.writeBranch.empty()) o << " writeBranch=" << e.writeBranch;
            if (!e.track.empty()) o << " track=" << e.track;
            if (e.offset != 0.0) o << " offset=" << canonicalTime(e.offset);
            un(e.unknown);
            o << '\n';
        }

        if (!rackObjs.empty()) o << '\n';
        {
            auto rs = rackObjs;
            std::sort(rs.begin(), rs.end(), [](const RackObj &a, const RackObj &b) { return a.id < b.id; });
            for (const auto &r : rs)
            {
                o << "#rackobj id=" << r.id << " name=" << r.name << " node=" << r.node
                  << " opacity=" << canonicalNumber(r.opacity);
                un(r.unknown);
                o << '\n';
            }
        }

        if (!tracks.empty()) o << '\n';
        for (const auto &t : sorted(tracks))
        {
            o << "#track id=" << t.id << " name=" << t.name << " kind=" << (t.audio ? "audio" : "video")
              << " order=" << t.order << " mute=" << boolText(t.mute) << " lock=" << boolText(t.lock)
              << " opacity=" << canonicalNumber(t.opacity) << " blend=" << blendName(t.blend);
            if (t.audio) o << " gain=" << canonicalNumber(t.gain);
            un(t.unknown);
            o << '\n';
        }

        if (!clips.empty()) o << '\n';
        for (const auto &c : sorted(clips))
        {
            o << "#clip id=" << c.id << " name=" << c.name << " track=" << c.track
              << " order=" << c.order << " src=" << quoteIfNeeded(c.src)
              << " at=" << canonicalTime(c.at) << " in=" << canonicalTime(c.in)
              << " out=" << canonicalTime(c.out) << " speed=" << canonicalNumber(c.speed)
              << " fit=" << fitName(c.fit) << " opacity=" << canonicalNumber(c.opacity)
              << " blend=" << blendName(c.blend)
              << " geom.x=" << canonicalNumber(c.geom.x) << " geom.y=" << canonicalNumber(c.geom.y)
              << " geom.scale=" << canonicalNumber(c.geom.scale)
              << " geom.rotation=" << canonicalNumber(c.geom.rotation)
              << " geom.anchor.x=" << canonicalNumber(c.geom.anchorX)
              << " geom.anchor.y=" << canonicalNumber(c.geom.anchorY)
              << " geom.crop.x=" << canonicalNumber(c.geom.cropX)
              << " geom.crop.y=" << canonicalNumber(c.geom.cropY)
              << " geom.crop.w=" << canonicalNumber(c.geom.cropW)
              << " geom.crop.h=" << canonicalNumber(c.geom.cropH);
            un(c.unknown);
            o << '\n';
        }

        if (!transitions.empty()) o << '\n';
        for (const auto &t : sorted(transitions))
        {
            o << "#transition id=" << t.id << " name=" << t.name << " track=" << t.track
              << " between=" << t.clipA << ',' << t.clipB << " kind=" << t.kind
              << " dur=" << canonicalTime(t.dur) << " easing=" << easeName(t.easing);
            if (t.kind == "dip") o << " color=" << t.color;
            un(t.unknown);
            o << '\n';
        }

        if (!autoClips.empty()) o << '\n';
        for (const auto &a : sorted(autoClips))
        {
            o << "#autoclip id=" << a.id << " name=" << a.name << " dur=" << canonicalTime(a.dur)
              << " interp=" << interpName(a.interp);
            un(a.unknown);
            o << '\n';
            for (const auto &p : a.points)
                o << "  " << canonicalTime(p.t) << " = " << canonicalNumber(p.value)
                  << " ease=" << easeName(p.ease) << '\n';
        }

        if (!autoLinks.empty()) o << '\n';
        for (const auto &l : sorted(autoLinks))
        {
            o << "#autolink id=" << l.id << " name=" << l.name << " clip=" << l.clip
              << " target=" << l.target << " at=" << canonicalTime(l.at)
              << " dur=" << canonicalTime(l.dur);
            if (l.haveFromTo)
                o << " from=" << canonicalNumber(l.from) << " to=" << canonicalNumber(l.to);
            else
                o << " scale=" << canonicalNumber(l.scale) << " offset=" << canonicalNumber(l.offset);
            o << " mode=" << autoModeName(l.mode);
            if (!l.scope.empty()) o << " scope=" << l.scope;
            o << " fadeIn=" << l.fadeIn << " fadeOut=" << l.fadeOut;
            un(l.unknown);
            o << '\n';
        }

        if (!bindings.empty()) o << '\n';
        {
            auto bs = bindings;
            std::sort(bs.begin(), bs.end(), [](const Binding &a, const Binding &b) { return a.id < b.id; });
            for (const auto &b : bs)
            {
                o << "#bind id=" << b.id << " target=" << b.target << " expr=" << quoteIfNeeded(b.expr);
                un(b.unknown);
                o << '\n';
            }
        }

        if (!markers.empty()) o << '\n';
        for (const auto &m : sorted(markers))
        {
            o << "#marker id=" << m.id << " name=" << m.name << " at=" << canonicalTime(m.at)
              << " color=" << m.color;
            if (!m.note.empty()) o << " note=" << quoteIfNeeded(m.note);
            un(m.unknown);
            o << '\n';
        }

        o << "\n#settings\n"
          << "  proxyEdge = " << settings.proxyEdge << '\n'
          << "  cpuPercent = " << settings.cpuPercent << '\n'
          << "  cacheBytes = " << settings.cacheBytes << '\n'
          << "  lintOnRender = " << boolText(settings.lintOnRender) << '\n';
        for (const auto &kv : settings.unknown) o << "  " << kv.first << " = " << quoteIfNeeded(kv.second) << '\n';
        return o.str();
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
        std::ofstream f(path, std::ios::binary);
        if (!f) { err = "cannot write " + path; return false; }
        f << serialize();
        if (!f) { err = "write failed: " + path; return false; }
        return true;
    }

    bool Project::roundTripsExactly(const std::string &text, std::string &err)
    {
        Project a;
        if (!a.parse(text, err)) return false;
        const std::string once = a.serialize();
        Project b;
        if (!b.parse(once, err)) return false;
        const std::string twice = b.serialize();
        if (once != twice) { err = "serialize is not a fixed point"; return false; }
        return true;
    }
}
}
