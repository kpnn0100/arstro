#include "CppEmitter.h"
#include "../BaseCatalog.h"
#include "../Theme.h"
#include "../gene/Gene.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace genesis
{
    namespace
    {
        const char *kVersion = "0.1";

        std::string upperFirst(const std::string &s)
        {
            std::string o = s;
            if (!o.empty() && o[0] >= 'a' && o[0] <= 'z')
                o[0] = (char)(o[0] - 'a' + 'A');
            return o;
        }

        /** A C++-safe identifier fragment from an authored id. */
        std::string ident(const std::string &s)
        {
            std::string o;
            for (char c : s)
                o += (isalnum((unsigned char)c) || c == '_') ? c : '_';
            if (o.empty() || isdigit((unsigned char)o[0]))
                o = "_" + o;
            return o;
        }

        std::string memberOf(const std::string &shapeId) { return "m" + upperFirst(ident(shapeId)); }
        std::string localOf(const std::string &shapeId, const std::string &field)
        {
            return ident(shapeId) + "_" + ident(field);
        }
        std::string stylePropOf(const std::string &shapeId, const std::string &field)
        {
            return "m" + upperFirst(ident(shapeId)) + upperFirst(ident(field));
        }
        std::string paramMember(const std::string &name) { return "m" + upperFirst(ident(name)); }
        std::string ownFlag(const std::string &shapeId, const std::string &field)
        {
            return "mOwn" + upperFirst(ident(shapeId)) + upperFirst(ident(field));
        }

        std::string numLit(double d)
        {
            if (d == (long long)d && std::fabs(d) < 1e15)
                return std::to_string((long long)d) + ".0";
            char buf[40];
            std::snprintf(buf, sizeof buf, "%.17g", d);
            return buf;
        }

        std::string colorLit(double r, double g, double b, double a)
        {
            return "artboard::Color{" + numLit(r) + ", " + numLit(g) + ", " + numLit(b) + ", " + numLit(a) + "}";
        }

        std::string segmentClassFor(ShapeKind k)
        {
            switch (k)
            {
            case ShapeKind::Rect: return "artboard::RectangleSegment";
            case ShapeKind::Circle: return "artboard::CircleSegment";
            case ShapeKind::Path: return "artboard::PathSegment";
            case ShapeKind::Label: return "artboard::LabelSegment";
            }
            return "artboard::RectangleSegment";
        }

        /** Style fields: the ones with no Segment Property behind them. */
        std::vector<const FieldDef *> styleFieldsFor(ShapeKind k)
        {
            std::vector<const FieldDef *> out;
            for (const auto *f : fieldsFor(k))
                if (f->type == FieldType::Number && !*f->segmentProperty)
                    out.push_back(f);
            return out;
        }

        std::string cppEscape(const std::string &s)
        {
            std::string o;
            for (char c : s)
            {
                if (c == '"' || c == '\\') { o += '\\'; o += c; }
                else if (c == '\n') o += "\\n";
                else o += c;
            }
            return o;
        }

        // ── the emitter ──────────────────────────────────────────────────────────
        class Emitter
        {
        public:
            explicit Emitter(const Document &d) : doc(d), base(findBase(d.base)) {}

            EmittedCode run()
            {
                EmittedCode out;
                out.headerName = doc.name + ".h";
                out.sourceName = doc.name + ".cpp";
                for (const auto &d : doc.validate())
                    if (d.isError())
                    {
                        out.error = d.where + ": " + d.message;
                        return out;
                    }
                if (!base)
                {
                    out.error = "unknown base class '" + doc.base + "'";
                    return out;
                }
                analyse();
                if (!error.empty())
                {
                    out.error = error;
                    return out;
                }
                out.header = emitHeader();
                out.source = emitSource();
                out.error = error;
                return out;
            }

        private:
            const Document &doc;
            const BaseDef *base;
            std::string error;

            std::vector<std::pair<std::string, std::string>> order;  // (shape, field) evaluation order
            bool layoutEveryFrame = false;                            // some binding reads base.*
            std::map<std::string, bool> handlesSignal;

            void setError(const std::string &m)
            {
                if (error.empty()) error = m;
            }

            // ---- Gene name resolution ----------------------------------------------
            /** Reads during layout(): shape fields are the locals emitted just above. */
            gene::CppNames layoutNames(const std::string &owner) const
            {
                gene::CppNames n;
                n.ident = [this](const std::string &name) -> std::string {
                    if (const Param *p = doc.findParam(name))
                        return paramMember(p->name);
                    return std::string();
                };
                n.member = [this, owner](const std::string &obj, const std::string &f) -> std::string {
                    if (obj == "theme")
                    {
                        double r, g, b, a;
                        if (themeColor(f, r, g, b, a)) return colorLit(r, g, b, a);
                        return std::string();
                    }
                    if (obj == "base")
                    {
                        for (const auto &rd : base->reads)
                            if (rd.name == f) return rd.cpp;
                        return std::string();
                    }
                    if (obj == "parent")
                    {
                        const std::string p = doc.parentOf(owner);
                        // A top-level object's parent IS the component, which publishes w/h.
                        if (p.empty())
                            return f == "w" ? "w" : (f == "h" ? "h" : std::string());
                        return findField(f) ? localOf(p, f) : std::string();
                    }
                    const std::string shape = obj == "self" ? owner : obj;
                    if (doc.findShape(shape) && findField(f)) return localOf(shape, f);
                    return std::string();
                };
                return n;
            }

            /** Reads at signal time: shape fields read their LIVE animated value. */
            gene::CppNames liveNames(const std::string &owner) const
            {
                gene::CppNames n = layoutNames(owner);
                n.member = [this, owner](const std::string &obj, const std::string &f) -> std::string {
                    if (obj == "theme")
                    {
                        double r, g, b, a;
                        if (themeColor(f, r, g, b, a)) return colorLit(r, g, b, a);
                        return std::string();
                    }
                    if (obj == "base")
                    {
                        for (const auto &rd : base->reads)
                            if (rd.name == f) return rd.cpp;
                        return std::string();
                    }
                    std::string shape = obj == "self" ? owner : obj;
                    if (obj == "parent")
                    {
                        const std::string p = doc.parentOf(owner);
                        if (p.empty())
                            return f == "w" ? "w" : (f == "h" ? "h" : std::string());
                        shape = p;
                    }
                    const Shape *s = doc.findShape(shape);
                    const FieldDef *fd = findField(f);
                    if (!s || !fd) return std::string();
                    if (*fd->segmentProperty)
                        return memberOf(shape) + "->" + fd->segmentProperty + ".value()";
                    if (fd->type == FieldType::Number)
                        return stylePropOf(shape, f) + ".value()";
                    return std::string();
                };
                return n;
            }

            /** liveNames plus `current` — the target field's value at the moment the reaction
             *  fires, which is what makes `to = current + 10` mean "ten more than now" — and
             *  `original`, the target field's own binding compiled inline (G-22). The binding is
             *  compiled with plain `liveNames`, the same scope the interpreter evaluates it in,
             *  so a field's expression can never reach `current`/`original` and the two sides
             *  cannot drift. */
            gene::CppNames trackNames(const std::string &owner, const std::string &propExpr,
                                      const std::string &originalExpr, const std::string &where) const
            {
                gene::CppNames n = liveNames(owner);
                auto base = n.ident;
                const Emitter *self = this;
                n.ident = [base, propExpr, originalExpr, where, owner,
                           self](const std::string &name) -> std::string {
                    if (name == "current") return propExpr + ".value()";
                    if (name == "original")
                        return "(" +
                               const_cast<Emitter *>(self)->expr(originalExpr, self->liveNames(owner),
                                                                 where + " original") +
                               ")";
                    return base ? base(name) : std::string();
                };
                return n;
            }

            std::string expr(const std::string &src, const gene::CppNames &names, const std::string &where)
            {
                gene::NodePtr n = gene::parse(src, nullptr);
                if (!n)
                {
                    setError(where + ": cannot parse '" + src + "'");
                    return "0.0";
                }
                n = gene::fold(n);
                std::string err;
                std::string out = gene::emitCpp(n, names, &err);
                if (!err.empty())
                {
                    setError(where + ": " + err);
                    return "0.0";
                }
                return out;
            }

            // ---- analysis -----------------------------------------------------------
            void analyse()
            {
                // Topologically order every (shape, field) so a local is declared before it
                // is read. Cycles are already rejected by Document::validate().
                std::map<std::string, std::vector<std::string>> deps;
                std::vector<std::string> keys;
                std::map<std::string, std::pair<std::string, std::string>> split;
                for (const auto &s : doc.shapes)
                    for (const auto *fd : fieldsFor(s.kind))
                    {
                        const std::string key = s.id + "\x1f" + fd->name;
                        keys.push_back(key);
                        split[key] = {s.id, fd->name};
                        gene::NodePtr n = gene::parse(s.effectiveField(fd->name), nullptr);
                        if (!n) continue;
                        if (readsBase(n)) layoutEveryFrame = true;
                        std::vector<std::pair<std::string, std::string>> ms;
                        gene::collectMembers(n, ms);
                        for (const auto &m : ms)
                        {
                            if (m.first == "base" || m.first == "theme") continue;
                            std::string obj = m.first == "self" ? s.id : m.first;
                            if (m.first == "parent")
                            {
                                obj = doc.parentOf(s.id);
                                if (obj.empty()) continue;   // the component: w/h, not a field
                            }
                            deps[key].push_back(obj + "\x1f" + m.second);
                        }
                    }
                // A binding that transitively reads an ANIMATED field — or base.* — must be
                // re-evaluated every frame, because what it reads changes every frame (G-6a).
                // One that reads neither is still only re-evaluated on resize. The dependency
                // graph already exists for ordering, so this is reachability over it.
                {
                    std::set<std::string> live;
                    for (const auto &sh : doc.shapes)
                        for (const auto &a : doc.animatedFields(sh.id))
                            live.insert(sh.id + "\x1f" + a);
                    bool changed = true;
                    while (changed)
                    {
                        changed = false;
                        for (const auto &kv : deps)
                            for (const auto &d : kv.second)
                                if (live.count(d) && !live.count(kv.first))
                                {
                                    live.insert(kv.first);
                                    layoutEveryFrame = true;   // something READS a live value
                                    changed = true;
                                }
                    }
                }

                std::set<std::string> done;
                std::function<void(const std::string &)> visit = [&](const std::string &k) {
                    if (!done.insert(k).second) return;
                    auto it = deps.find(k);
                    if (it != deps.end())
                        for (const auto &d : it->second)
                            if (split.count(d))
                                visit(d);
                    order.push_back(split[k]);
                };
                for (const auto &k : keys)
                    visit(k);

                // Path coordinates may also read base.*, which forces per-frame layout.
                for (const auto &s : doc.shapes)
                    for (const auto &c : s.path)
                        for (const auto &a : c.args)
                        {
                            gene::NodePtr n = gene::parse(a, nullptr);
                            if (n && readsBase(n)) layoutEveryFrame = true;
                        }

                for (const auto &pair : doc.allReactions())
                    handlesSignal[pair.second->signal] = true;
            }

            static bool readsBase(const gene::NodePtr &n)
            {
                std::vector<std::pair<std::string, std::string>> ms;
                gene::collectMembers(n, ms);
                for (const auto &m : ms)
                    if (m.first == "base") return true;
                return false;
            }

            /** Every (object, reaction) pair handling `signal` — a signal is a component-level
             *  event, so they all run. */
            std::vector<std::pair<const Shape *, const Reaction *>> reactionsFor(
                const std::string &signal) const
            {
                std::vector<std::pair<const Shape *, const Reaction *>> out;
                for (const auto &pair : doc.allReactions())
                    if (pair.second->signal == signal) out.push_back(pair);
                return out;
            }

            /** The hook a signal overrides, and whether two signals share it. */
            const SignalDef *signalDef(const std::string &name) const
            {
                for (const auto &s : base->signals)
                    if (s.name == name) return &s;
                return nullptr;
            }

            bool anyStyleProps() const
            {
                for (const auto &s : doc.shapes)
                    if (!styleFieldsFor(s.kind).empty()) return true;
                return false;
            }

            /** Several objects may react to one signal, so every generated name carries the
             *  owning object as well. */
            static std::string tag(const std::string &shapeId, const std::string &signal)
            {
                return upperFirst(ident(shapeId)) + upperFirst(ident(signal));
            }
            std::string reactionFn(const std::string &shapeId, const std::string &signal, int step) const
            {
                return "play" + tag(shapeId, signal) + "Step" + std::to_string(step);
            }
            std::string tokenMember(const std::string &sh, const std::string &sg) const { return "mTok" + tag(sh, sg); }
            std::string pendingMember(const std::string &sh, const std::string &sg) const { return "mPending" + tag(sh, sg); }
            std::string runningMember(const std::string &sh, const std::string &sg) const { return "mRunning" + tag(sh, sg); }
            std::string queuedMember(const std::string &sh, const std::string &sg) const { return "mQueued" + tag(sh, sg); }

            // ---- header -------------------------------------------------------------
            std::string emitHeader() const
            {
                std::ostringstream o;
                o << "/*\n"
                  << " *  " << doc.name << " — generated by Genesis " << kVersion << " from "
                  << doc.name << ".genesis.\n"
                  << " *\n"
                  << " *  DO NOT EDIT: regenerate with `genesis-cc " << doc.name << ".genesis -o .`\n"
                  << " *  " << base->summary << "\n"
                  << " *\n"
                  << " *  Depends on artboard only — Genesis ships no runtime.\n"
                  << " */\n"
                  << "#pragma once\n"
                  << "#include <artboard/artboard.h>\n"
                  << "#include <memory>\n";
                if (hasLabel()) o << "#include <string>\n";
                o << "\n";
                o << "namespace " << doc.nameSpace << "\n{\n";
                o << "    class " << doc.name << " : public " << base->cppClass << "\n    {\n";
                o << "    public:\n";
                o << "        " << doc.name << "();\n";
                if (!doc.params.empty())
                {
                    o << "\n        // ---- authored params ----\n";
                    for (const auto &p : doc.params)
                    {
                        const std::string N = upperFirst(ident(p.name));
                        if (p.type == ParamType::Color)
                        {
                            o << "        void set" << N << "(const artboard::Color &v);\n";
                            o << "        const artboard::Color &" << ident(p.name) << "() const { return " << paramMember(p.name) << "; }\n";
                        }
                        else if (p.type == ParamType::Text)
                        {
                            o << "        void set" << N << "(std::string v);\n";
                            o << "        const std::string &" << ident(p.name) << "() const { return " << paramMember(p.name) << "; }\n";
                        }
                        else
                        {
                            o << "        void set" << N << "(double v);";
                            if (p.hasRange)
                                o << "   // " << numLit(p.minimum) << " .. " << numLit(p.maximum);
                            o << "\n";
                            o << "        double " << ident(p.name) << "() const { return " << paramMember(p.name) << "; }\n";
                        }
                    }
                }
                o << "\n        void advance(double nowMs) override;\n";
                o << "\n    protected:\n";
                for (const auto &hook : overriddenHooks())
                    o << "        " << hook.decl << "\n";
                o << "\n    private:\n";
                o << "        void buildTree();\n";
                o << "        void layout(double transitionMs);\n";
                if (anyStyleProps() || anyColorMember())
                    o << "        void applyStyles();\n";
                o << "        /** Assign a bound value unless motion owns the field (see the .cpp). */\n";
                o << "        void bindProp(artboard::Property &p, double v, double ms, bool owned);\n";
                for (const auto &pair : doc.allReactions())
                    for (size_t i = 0; i < doc.expandSteps(*pair.second, pair.first->id).size(); ++i)
                        o << "        void " << reactionFn(pair.first->id, pair.second->signal, (int)i)
                          << "();\n";

                o << "\n";
                for (const auto &p : doc.params)
                {
                    if (p.type == ParamType::Color)
                        o << "        artboard::Color " << paramMember(p.name) << ";\n";
                    else if (p.type == ParamType::Text)
                        o << "        std::string " << paramMember(p.name) << ";\n";
                    else
                        o << "        double " << paramMember(p.name) << " = 0.0;\n";
                }
                o << "        double mNowMs = 0.0;\n";
                o << "        double mLastW = -1.0, mLastH = -1.0;\n";
                o << "        bool mAttached = false;\n";
                for (const auto &s : doc.shapes)
                    o << "        std::shared_ptr<" << segmentClassFor(s.kind) << "> " << memberOf(s.id) << ";\n";
                for (const auto &s : doc.shapes)
                {
                    for (const auto *fd : styleFieldsFor(s.kind))
                        o << "        artboard::Property " << stylePropOf(s.id, fd->name) << "{0.0};\n";
                    for (const auto *fd : fieldsFor(s.kind))
                        if (fd->type == FieldType::Color && !s.effectiveField(fd->name).empty())
                            o << "        artboard::Color " << stylePropOf(s.id, fd->name) << ";\n";
                }
                for (const auto &s : doc.shapes)
                    for (const auto &a : doc.animatedFields(s.id))
                        o << "        bool " << ownFlag(s.id, a) << " = false;\n";
                for (const auto &pair : doc.allReactions())
                {
                    const std::string sh = pair.first->id, sg = pair.second->signal;
                    o << "        int " << tokenMember(sh, sg) << " = 0;\n";
                    o << "        int " << pendingMember(sh, sg) << " = 0;\n";
                    o << "        bool " << runningMember(sh, sg) << " = false;\n";
                    if (pair.second->cancel == Cancel::Queue)
                        o << "        bool " << queuedMember(sh, sg) << " = false;\n";
                }
                if (!doc.allReactions().empty())
                    o << "        int mSeq = 0;   // monotonic token: a restarted reaction ignores stale callbacks\n";
                o << "    };\n}\n";
                return o.str();
            }

            bool hasLabel() const
            {
                for (const auto &s : doc.shapes)
                    if (s.kind == ShapeKind::Label) return true;
                for (const auto &p : doc.params)
                    if (p.type == ParamType::Text) return true;
                return false;
            }
            bool anyColorMember() const
            {
                for (const auto &s : doc.shapes)
                    for (const auto *fd : fieldsFor(s.kind))
                        if (fd->type == FieldType::Color && !s.effectiveField(fd->name).empty())
                            return true;
                return false;
            }

            struct Hook
            {
                std::string decl;
                std::string name;       // C++ hook
                std::string argType;
                std::string argName;
                std::vector<std::string> signals;   // authored signals routed through it
            };

            /** Hooks the generated class overrides, one entry per distinct C++ hook. */
            std::vector<Hook> overriddenHooks() const
            {
                std::vector<Hook> hooks;
                for (const auto &sd : base->signals)
                {
                    if (sd.hook.empty()) continue;               // attach/resize are synthesised
                    if (!handlesSignal.count(sd.name)) continue;
                    auto found = std::find_if(hooks.begin(), hooks.end(),
                                              [&](const Hook &h) { return h.name == sd.hook; });
                    if (found != hooks.end())
                    {
                        found->signals.push_back(sd.name);
                        continue;
                    }
                    Hook h;
                    h.name = sd.hook;
                    h.argType = sd.argType;
                    h.argName = sd.argName;
                    h.signals = {sd.name};
                    h.decl = "void " + sd.hook + "(" +
                             (sd.argType.empty() ? std::string() : sd.argType + " " + sd.argName) +
                             ") override;";
                    hooks.push_back(h);
                }
                return hooks;
            }

            // ---- source -------------------------------------------------------------
            std::string emitSource() const
            {
                std::ostringstream o;
                o << "// " << doc.name << ".cpp — generated by Genesis " << kVersion << ". DO NOT EDIT.\n";
                o << "#include \"" << doc.name << ".h\"\n";
                o << "#include <algorithm>\n#include <cmath>\n\n";
                o << "namespace " << doc.nameSpace << "\n{\n";
                o << "    namespace\n    {\n";
                o << "        // Motion constant for a re-layout after the component is resized: bound\n";
                o << "        // geometry eases into its new place rather than snapping (design rule R1).\n";
                o << "        constexpr double kResizeMs = artboard::motion::kDurationShort3;\n";
                o << "\n        // Gene built-ins, defined so the generated expressions below read like the\n";
                o << "        // authored ones and match the editor's preview exactly.\n";
                o << "        inline double genesisClamp(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }\n";
                o << "        inline double genesisSign(double v) { return v > 0.0 ? 1.0 : (v < 0.0 ? -1.0 : 0.0); }\n";
                o << "        inline double genesisSqrt(double v) { return v <= 0.0 ? 0.0 : std::sqrt(v); }\n";
                o << "        inline double genesisLog(double v) { return v <= 0.0 ? 0.0 : std::log(v); }\n";
                o << "        inline double genesisNorm(double v, double lo, double hi)\n";
                o << "        {\n            const double span = hi - lo;\n            return span == 0.0 ? 0.0 : genesisClamp((v - lo) / span, 0.0, 1.0);\n        }\n";
                o << "        inline double genesisDiv(double a, double b) { return b == 0.0 ? 0.0 : std::trunc(a / b); }\n";
                o << "        inline double genesisAsin(double v) { return std::asin(genesisClamp(v, -1.0, 1.0)); }\n";
                o << "        inline double genesisAcos(double v) { return std::acos(genesisClamp(v, -1.0, 1.0)); }\n";
                o << "        inline double genesisSnap(double v, double step)\n";
                o << "        {\n            return step == 0.0 ? v : std::round(v / step) * step;\n        }\n";
                o << "        inline double genesisWrap(double v, double lo, double hi)\n";
                o << "        {\n";
                o << "            const double span = hi - lo;\n";
                o << "            if (span <= 0.0) return lo;\n";
                o << "            double t = std::fmod(v - lo, span);\n";
                o << "            if (t < 0.0) t += span;   // whole periods, so a negative angle wraps up\n";
                o << "            return lo + t;\n";
                o << "        }\n";
                o << "        inline double genesisRemap(double v, double inLo, double inHi, double outLo, double outHi)\n";
                o << "        {\n";
                o << "            const double span = inHi - inLo;\n";
                o << "            return span == 0.0 ? outLo : outLo + (v - inLo) / span * (outHi - outLo);\n";
                o << "        }\n";
                o << "        inline double genesisSmoothstep(double e0, double e1, double v)\n";
                o << "        {\n";
                o << "            const double span = e1 - e0;\n";
                o << "            double t = span == 0.0 ? (v < e0 ? 0.0 : 1.0) : (v - e0) / span;\n";
                o << "            t = genesisClamp(t, 0.0, 1.0);\n";
                o << "            return t * t * (3.0 - 2.0 * t);\n";
                o << "        }\n";
                o << "        inline artboard::Color genesisFade(const artboard::Color &c, double t)\n";
                o << "        {\n            return artboard::Color{c.r, c.g, c.b, c.a * t};\n        }\n";
                o << "        inline artboard::Color genesisMix(const artboard::Color &a, const artboard::Color &b, double t)\n";
                o << "        {\n            return artboard::Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,\n";
                o << "                                  a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};\n        }\n";
                o << "        // Sector angles are authored in DEGREES, from one angle to another; Artboard's\n";
                o << "        // Arc takes radians and a signed sweep. An end at or before the start wraps\n";
                o << "        // forward a turn, so 330 -> 30 is a 60-degree wedge rather than nothing.\n";
                o << "        inline artboard::Arc genesisArc(double startDeg, double endDeg, double innerRatio)\n";
                o << "        {\n";
                o << "            double sweep = endDeg - startDeg;\n";
                o << "            if (sweep <= 0.0) sweep += 360.0;\n";
                o << "            if (sweep > 360.0) sweep = 360.0;\n";
                o << "            constexpr double kDeg = 3.14159265358979324 / 180.0;\n";
                o << "            artboard::Arc a;\n";
                o << "            a.start = startDeg * kDeg;\n";
                o << "            a.sweep = sweep * kDeg;\n";
                o << "            a.innerRatio = innerRatio;\n";
                o << "            return a;\n";
                o << "        }\n";
                o << "    }\n\n";

                emitCtor(o);
                emitBuildTree(o);
                emitBindProp(o);
                emitLayout(o);
                if (anyStyleProps() || anyColorMember())
                    emitApplyStyles(o);
                emitParamSetters(o);
                emitReactions(o);
                emitHooks(o);
                emitAdvance(o);
                o << "}\n";
                return o.str();
            }

            void emitCtor(std::ostringstream &o) const
            {
                o << "    " << doc.name << "::" << doc.name << "()\n    {\n";
                for (const auto &p : doc.params)
                {
                    if (p.type == ParamType::Text)
                        o << "        " << paramMember(p.name) << " = \"" << cppEscape(p.defaultExpr) << "\";\n";
                    else
                    {
                        gene::NodePtr n = gene::fold(gene::parse(p.defaultExpr, nullptr));
                        if (!n)
                        {
                            o << "        // param '" << p.name << "': unparsable default\n";
                            continue;
                        }
                        // A param default is a root: it cannot read a shape or another
                        // param, but it MUST be able to name a theme role, which is how a
                        // starter follows the palette in force instead of baking a literal.
                        gene::CppNames names;
                        names.member = [](const std::string &obj, const std::string &f) -> std::string {
                            if (obj != "theme") return std::string();
                            double r, g, b, a;
                            if (!themeColor(f, r, g, b, a)) return std::string();
                            return colorLit(r, g, b, a);
                        };
                        std::string err;
                        const std::string v = gene::emitCpp(n, names, &err);
                        o << "        " << paramMember(p.name) << " = " << (err.empty() ? v : std::string("0.0")) << ";\n";
                    }
                }
                o << "        // The authored shapes are this component's whole appearance: the base\n";
                o << "        // supplies behaviour only (FR-41).\n";
                o << "        drawsBuiltInVisuals = false;\n";
                o << "        width.set(" << numLit(doc.designW) << ");\n";
                o << "        height.set(" << numLit(doc.designH) << ");\n";
                o << "        buildTree();\n";
                o << "        layout(0.0);   // the first evaluation snaps; later ones ease\n";
                if (anyStyleProps() || anyColorMember())
                    o << "        applyStyles();\n";
                o << "    }\n\n";
            }

            void emitBuildTree(std::ostringstream &o) const
            {
                o << "    void " << doc.name << "::buildTree()\n    {\n";
                o << "        // Authored shapes are decoration: input belongs to the base control, or a\n";
                o << "        // press would land on the topmost drawn shape and its signals would never fire.\n";
                for (const auto &s : doc.shapes)
                {
                    o << "        " << memberOf(s.id) << " = std::make_shared<" << segmentClassFor(s.kind) << ">();\n";
                    o << "        " << memberOf(s.id) << "->inputTransparent = true;\n";
                    if (s.kind == ShapeKind::Label)
                    {
                        const bool isParam = s.text.size() > 2 && s.text.front() == '{' && s.text.back() == '}';
                        if (isParam)
                        {
                            const std::string pname = s.text.substr(1, s.text.size() - 2);
                            o << "        " << memberOf(s.id) << "->text = " << paramMember(pname) << ";\n";
                        }
                        else
                            o << "        " << memberOf(s.id) << "->text = \"" << cppEscape(s.text) << "\";\n";
                    }
                }
                for (const auto &s : doc.shapes)
                {
                    if (s.parent.empty())
                        o << "        addChild(" << memberOf(s.id) << ");\n";
                    else
                        o << "        " << memberOf(s.parent) << "->addChild(" << memberOf(s.id) << ");\n";
                }
                o << "    }\n\n";
            }

            void emitBindProp(std::ostringstream &o) const
            {
                o << "    // An edge (resize, a param change) always RETARGETS: easing toward the new value\n";
                o << "    // from wherever the previous ease had got to. A per-frame refresh (ms == 0, used\n";
                o << "    // when a binding tracks live base state) must not cut that ease short, so it yields\n";
                o << "    // while one is in flight. `owned` outranks both: once a reaction has driven a field,\n";
                o << "    // motion owns it, and layout stops asserting the bound value so a resize cannot\n";
                o << "    // stomp the animation's result back to its rest value.\n";
                o << "    void " << doc.name << "::bindProp(artboard::Property &p, double v, double ms, bool owned)\n";
                o << "    {\n";
                o << "        if (owned)\n            return;\n";
                o << "        if (ms > 0.0)\n";
                o << "        {\n            p.animateTo(v, ms, artboard::Easing::EaseOutCubic, mNowMs);\n            return;\n        }\n";
                o << "        if (p.isAnimating())\n            return;\n";
                o << "        p.set(v);\n";
                o << "    }\n\n";
            }

            void emitLayout(std::ostringstream &o) const
            {
                o << "    // Bindings. Every authored expression, evaluated in dependency order.\n";
                o << "    void " << doc.name << "::layout(double transitionMs)\n    {\n";
                o << "        const double w = width.value(), h = height.value();\n";
                o << "        (void)w; (void)h;\n\n";
                Emitter &self = const_cast<Emitter &>(*this);
                for (const auto &node : order)
                {
                    const Shape *s = doc.findShape(node.first);
                    const FieldDef *fd = findField(node.second);
                    if (!s || !fd) continue;
                    const std::string src = s->effectiveField(fd->name);
                    const std::string where = node.first + "." + node.second;
                    if (fd->type == FieldType::Color)
                    {
                        if (src.empty()) continue;
                        o << "        const artboard::Color " << localOf(s->id, fd->name) << " = "
                          << self.expr(src, layoutNames(s->id), where) << ";\n";
                    }
                    else
                    {
                        const std::string expr = self.expr(src, layoutNames(s->id), where);
                        o << "        const double " << localOf(s->id, fd->name) << " = ";
                        if (doc.isAnimated(s->id, fd->name))
                        {
                            // Once motion owns a field its VALUE is the animated one, so a
                            // binding reading it follows the animation instead of freezing at
                            // the pre-animation value (G-6a).
                            const std::string live = *fd->segmentProperty
                                                         ? memberOf(s->id) + "->" + fd->segmentProperty +
                                                               ".value()"
                                                         : stylePropOf(s->id, fd->name) + ".value()";
                            o << ownFlag(s->id, fd->name) << " ? " << live << " : (" << expr << ")";
                        }
                        else
                            o << expr;
                        o << ";   // " << src << "\n";
                    }
                }
                o << "\n";
                for (const auto &s : doc.shapes)
                {
                    o << "        // " << s.id << "\n";
                    for (const auto *fd : fieldsFor(s.kind))
                    {
                        const std::string local = localOf(s.id, fd->name);
                        if (fd->type == FieldType::Color)
                        {
                            if (s.effectiveField(fd->name).empty()) continue;
                            o << "        " << stylePropOf(s.id, fd->name) << " = " << local << ";\n";
                            continue;
                        }
                        const bool animated = doc.isAnimated(s.id, fd->name);
                        const std::string owned = animated ? ownFlag(s.id, fd->name) : "false";
                        if (*fd->segmentProperty)
                            o << "        bindProp(" << memberOf(s.id) << "->" << fd->segmentProperty
                              << ", " << local << ", transitionMs, " << owned << ");\n";
                        else
                            o << "        bindProp(" << stylePropOf(s.id, fd->name) << ", " << local
                              << ", transitionMs, " << owned << ");\n";
                    }
                    if (s.kind == ShapeKind::Path && !s.path.empty())
                    {
                        o << "        " << memberOf(s.id) << "->path.clear()";
                        for (const auto &c : s.path)
                        {
                            switch (c.op)
                            {
                            case 'M': o << "\n            .moveTo("; break;
                            case 'L': o << "\n            .lineTo("; break;
                            case 'Q': o << "\n            .quadTo("; break;
                            case 'C': o << "\n            .cubicTo("; break;
                            case 'Z': o << "\n            .close("; break;
                            default: break;
                            }
                            for (size_t k = 0; k < c.args.size(); ++k)
                                o << (k ? ", " : "") << self.expr(c.args[k], layoutNames(s.id), s.id + " path");
                            o << ")";
                        }
                        o << ";\n";
                    }
                }
                o << "    }\n\n";
            }

            void emitApplyStyles(std::ostringstream &o) const
            {
                o << "    // Push the (possibly animating) style values into each node's style.\n";
                o << "    void " << doc.name << "::applyStyles()\n    {\n";
                for (const auto &s : doc.shapes)
                {
                    const bool hasFill = !s.effectiveField("fill").empty();
                    const bool hasStroke = !s.effectiveField("stroke").empty();
                    o << "        {   // " << s.id << "\n";
                    if (s.kind == ShapeKind::Label)
                    {
                        if (hasFill)
                            o << "            " << memberOf(s.id) << "->style.color = " << stylePropOf(s.id, "fill") << ";\n";
                        o << "            " << memberOf(s.id) << "->style.sizePx = " << stylePropOf(s.id, "fontSize") << ".value();\n";
                        o << "            " << memberOf(s.id) << "->style.letterSpacingPx = " << stylePropOf(s.id, "letterSpacing") << ".value();\n";
                        o << "        }\n";
                        continue;
                    }
                    o << "            artboard::Paint p;\n";
                    if (hasFill)
                        o << "            p.hasFill = true;\n            p.fill = " << stylePropOf(s.id, "fill") << ";\n";
                    if (hasStroke)
                    {
                        o << "            p.hasStroke = true;\n            p.stroke = " << stylePropOf(s.id, "stroke") << ";\n";
                        o << "            p.strokeWidth = " << stylePropOf(s.id, "strokeWidth") << ".value();\n";
                    }
                    if (s.kind == ShapeKind::Path)
                        o << "            " << memberOf(s.id) << "->path.paint = p;\n";
                    else
                    {
                        o << "            " << memberOf(s.id) << "->style.paint = p;\n";
                        if (s.kind == ShapeKind::Rect)
                            o << "            " << memberOf(s.id) << "->style.cornerRadius = " << stylePropOf(s.id, "cornerRadius") << ".value();\n";
                    }
                    if (s.kind == ShapeKind::Circle)
                        o << "            " << memberOf(s.id) << "->arc = genesisArc("
                          << stylePropOf(s.id, "arcStart") << ".value(), "
                          << stylePropOf(s.id, "arcEnd") << ".value(), "
                          << stylePropOf(s.id, "arcInner") << ".value());\n";
                    o << "            " << memberOf(s.id) << "->trim = {"
                      << stylePropOf(s.id, "trimStart") << ".value(), "
                      << stylePropOf(s.id, "trimEnd") << ".value(), "
                      << stylePropOf(s.id, "trimOffset") << ".value()};\n";
                    o << "        }\n";
                }
                o << "    }\n\n";
            }

            void emitParamSetters(std::ostringstream &o) const
            {
                for (const auto &p : doc.params)
                {
                    const std::string N = upperFirst(ident(p.name));
                    if (p.type == ParamType::Color)
                        o << "    void " << doc.name << "::set" << N << "(const artboard::Color &v)\n    {\n        "
                          << paramMember(p.name) << " = v;\n        layout(0.0);\n"
                          << (anyStyleProps() || anyColorMember() ? "        applyStyles();\n" : "")
                          << "    }\n\n";
                    else if (p.type == ParamType::Text)
                        o << "    void " << doc.name << "::set" << N << "(std::string v)\n    {\n        "
                          << paramMember(p.name) << " = std::move(v);\n        layout(0.0);\n    }\n\n";
                    else
                        o << "    void " << doc.name << "::set" << N << "(double v)\n    {\n        "
                          << paramMember(p.name) << " = v;\n        layout(kResizeMs);\n"
                          << (anyStyleProps() || anyColorMember() ? "        applyStyles();\n" : "")
                          << "    }\n\n";
                }
            }

            void emitReactions(std::ostringstream &o) const
            {
                Emitter &self = const_cast<Emitter &>(*this);
                for (const auto &pair : doc.allReactions())
                {
                    const Shape &host = *pair.first;
                    const Reaction &r = *pair.second;
                    // The SAME expansion the interpreter uses (G-22), so the compiled class and
                    // the preview run identical track lists — which is what the Verifier diffs.
                    const std::vector<Step> steps = doc.expandSteps(r, host.id);
                    for (size_t si = 0; si < steps.size(); ++si)
                    {
                        const Step &step = steps[si];
                        int finite = 0;
                        for (const auto &t : step.tracks)
                            if (t.repeat >= 0) ++finite;

                        o << "    void " << doc.name << "::" << reactionFn(host.id, r.signal, (int)si)
                          << "()\n    {\n";
                        o << "        const double w = width.value(), h = height.value();\n";
                        o << "        (void)w; (void)h;\n";
                        if (finite > 0)
                        {
                            o << "        const int token = " << tokenMember(host.id, r.signal) << ";\n";
                            o << "        " << pendingMember(host.id, r.signal) << " = " << finite << ";\n";
                        }
                        for (const auto &t : step.tracks)
                        {
                            std::string owner, field;
                            Document::splitTarget(t.target, host.id, owner, field);
                            const Shape *s = doc.findShape(owner);
                            const FieldDef *fd = findField(field);
                            if (!s || !fd) continue;
                            const std::string prop = *fd->segmentProperty
                                                         ? memberOf(owner) + "->" + fd->segmentProperty
                                                         : stylePropOf(owner, field);
                            const std::string where = host.id + " " + r.signal + " " + t.target;
                            const gene::CppNames names =
                                trackNames(owner, prop, s->effectiveField(field), where);
                            const std::string from = t.from.empty() ? prop + ".value()"
                                                                    : self.expr(t.from, names, where + " from");
                            const std::string to = self.expr(t.to, names, where + " to");
                            const std::string dur = self.expr(t.durationMs, names, where + " duration");
                            const std::string delay = self.expr(t.delayMs, names, where + " delay");
                            o << "        " << ownFlag(owner, field) << " = true;   // motion now owns "
                              << owner << "." << field << "\n";
                            o << "        " << prop << ".animate(\n";
                            o << "            artboard::Tween(" << from << ", " << to << ", " << dur
                              << ", " << delay << ",\n                            artboard::Easing::"
                              << t.easing << ", " << t.repeat << ", " << (t.yoyo ? "true" : "false")
                              << "),\n";
                            o << "            mNowMs";
                            if (t.repeat >= 0)
                            {
                                o << ",\n            [this, token] {\n";
                                o << "                if (token != " << tokenMember(host.id, r.signal) << ")\n";
                                o << "                    return;   // a newer run of this reaction superseded us\n";
                                // `to = original` hands the field back to its BINDING when the
                                // track completes, so layout drives it again (G-22). The runtime
                                // clears the same flag at the same moment.
                                if (releasesToBinding(t))
                                    o << "                " << ownFlag(owner, field)
                                      << " = false;   // back to its binding: layout owns "
                                      << owner << "." << field << " again\n";
                                o << "                if (--" << pendingMember(host.id, r.signal)
                                  << " > 0)\n                    return;\n";
                                if (si + 1 < steps.size())
                                    o << "                " << reactionFn(host.id, r.signal, (int)si + 1)
                                      << "();\n";
                                else
                                {
                                    o << "                " << runningMember(host.id, r.signal) << " = false;\n";
                                    if (r.cancel == Cancel::Queue)
                                    {
                                        o << "                if (" << queuedMember(host.id, r.signal)
                                          << ")\n                {\n";
                                        o << "                    " << queuedMember(host.id, r.signal)
                                          << " = false;\n";
                                        o << "                    " << tokenMember(host.id, r.signal)
                                          << " = ++mSeq;\n";
                                        o << "                    " << runningMember(host.id, r.signal)
                                          << " = true;\n";
                                        o << "                    " << reactionFn(host.id, r.signal, 0)
                                          << "();\n                }\n";
                                    }
                                }
                                o << "            }";
                            }
                            o << ");\n";
                        }
                        if (finite == 0)
                        {
                            o << "        // every track in this step repeats forever: the chain ends here.\n";
                            o << "        " << runningMember(host.id, r.signal) << " = false;\n";
                        }
                        o << "    }\n\n";
                    }
                }
            }

            /** The body that starts a reaction, honouring its cancellation policy. */
            /** Start every object that reacts to `signal` — a signal is a component-level
             *  event, so one hook may set several objects moving. */
            std::string startReactions(const std::string &signal, const std::string &indent) const
            {
                std::ostringstream o;
                for (const auto &pair : reactionsFor(signal))
                {
                    const std::string sh = pair.first->id;
                    const Reaction &r = *pair.second;
                    o << indent << "{   // " << sh << "\n";
                    const std::string in = indent + "    ";
                    if (r.cancel == Cancel::IgnoreIfRunning)
                    {
                        o << in << "if (!" << runningMember(sh, signal) << ")\n";
                        o << in << "{   // cancel policy: ignore while running\n";
                        o << in << "    " << tokenMember(sh, signal) << " = ++mSeq;\n";
                        o << in << "    " << runningMember(sh, signal) << " = true;\n";
                        o << in << "    " << reactionFn(sh, signal, 0) << "();\n";
                        o << in << "}\n";
                    }
                    else if (r.cancel == Cancel::Queue)
                    {
                        o << in << "if (" << runningMember(sh, signal) << ")\n";
                        o << in << "    " << queuedMember(sh, signal)
                          << " = true;   // cancel policy: run again after\n";
                        o << in << "else\n";
                        o << in << "{\n";
                        o << in << "    " << tokenMember(sh, signal) << " = ++mSeq;\n";
                        o << in << "    " << runningMember(sh, signal) << " = true;\n";
                        o << in << "    " << reactionFn(sh, signal, 0) << "();\n";
                        o << in << "}\n";
                    }
                    else
                    {
                        o << in << tokenMember(sh, signal) << " = ++mSeq;   // supersede any in-flight run\n";
                        o << in << runningMember(sh, signal) << " = true;\n";
                        o << in << reactionFn(sh, signal, 0) << "();\n";
                    }
                    o << indent << "}\n";
                }
                return o.str();
            }

            void emitHooks(std::ostringstream &o) const
            {
                for (const auto &h : overriddenHooks())
                {
                    o << "    void " << doc.name << "::" << h.name << "("
                      << (h.argType.empty() ? std::string() : h.argType + " " + h.argName) << ")\n    {\n";
                    if (!h.argType.empty())
                        o << "        (void)" << h.argName << ";\n";
                    // Two authored signals can share one C++ hook (hoverEnter/hoverExit).
                    if (h.name == "onHoverChanged" || h.name == "onFocusChanged")
                    {
                        const std::string onSig = h.name == "onHoverChanged" ? "hoverEnter" : "focusGained";
                        const std::string offSig = h.name == "onHoverChanged" ? "hoverExit" : "focusLost";
                        const bool anyOn = !reactionsFor(onSig).empty();
                        const bool anyOff = !reactionsFor(offSig).empty();
                        const std::string arg = h.name == "onHoverChanged" ? "hovered" : "focused";
                        o << "        " << base->cppClass << "::" << h.name << "(" << arg << ");\n";
                        if (anyOn)
                        {
                            o << "        if (" << arg << ")\n        {\n";
                            o << startReactions(onSig, "            ");
                            o << "        }\n";
                        }
                        if (anyOff)
                        {
                            o << "        " << (anyOn ? "else\n        " : "if (!" + arg + ")\n        ");
                            o << "{\n" << startReactions(offSig, "            ") << "        }\n";
                        }
                        o << "    }\n\n";
                        continue;
                    }
                    o << "        " << base->cppClass << "::" << h.name << "("
                      << (h.argName.empty() ? std::string() : h.argName) << ");\n";
                    o << startReactions(h.signals.front(), "        ");
                    o << "    }\n\n";
                }
            }

            void emitAdvance(std::ostringstream &o) const
            {
                o << "    void " << doc.name << "::advance(double nowMs)\n    {\n";
                o << "        mNowMs = nowMs;\n";
                o << "        // Tick the base (and every child Property) FIRST, so the bindings below\n";
                o << "        // read THIS frame's values rather than the previous frame's — a binding\n";
                o << "        // that reads an animating field has to follow it frame by frame.\n";
                o << "        " << base->cppClass << "::advance(nowMs);\n";
                o << "        // The FIRST sizing snaps: a component being placed into a layout has not\n";
                o << "        // \"changed size\", and easing in from the design size would read as a spurious\n";
                o << "        // entrance animation. Every later change eases.\n";
                o << "        const bool firstSizing = mLastW < 0.0;\n";
                o << "        const bool resized = width.value() != mLastW || height.value() != mLastH;\n";
                o << "        if (resized)\n        {\n";
                o << "            mLastW = width.value();\n            mLastH = height.value();\n";
                o << "        }\n";
                o << "        const double sizeMs = firstSizing ? 0.0 : kResizeMs;\n";
                if (layoutEveryFrame)
                {
                    o << "        // A binding reads base.*, so the layout tracks live state every frame.\n";
                    o << "        layout(resized ? sizeMs : 0.0);\n";
                }
                else
                    o << "        if (resized)\n            layout(sizeMs);\n";
                for (const auto &s : doc.shapes)
                    for (const auto *fd : styleFieldsFor(s.kind))
                        o << "        " << stylePropOf(s.id, fd->name) << ".update(nowMs);\n";
                if (anyStyleProps() || anyColorMember())
                    o << "        applyStyles();\n";
                const bool anyAttach = !reactionsFor("attach").empty();
                const bool anyResize = !reactionsFor("resize").empty();
                if (anyAttach)
                {
                    o << "        if (!mAttached)\n        {\n";
                    o << "            mAttached = true;\n";
                    o << startReactions("attach", "            ");
                    o << "        }\n";
                }
                else if (anyResize)
                    o << "        mAttached = true;\n";
                if (anyResize)
                {
                    o << "        if (resized && mAttached)\n        {\n";
                    o << startReactions("resize", "            ");
                    o << "        }\n";
                }
                o << "    }\n\n";
            }
        };
    }

    EmittedCode emitCpp(const Document &doc)
    {
        Emitter e(doc);
        return e.run();
    }

    bool writeEmitted(const EmittedCode &code, const std::string &dir, std::string *error)
    {
        const std::string prefix = dir.empty() || dir.back() == '/' ? dir : dir + "/";
        std::ofstream h(prefix + code.headerName, std::ios::binary);
        if (!h)
        {
            if (error) *error = "cannot write " + prefix + code.headerName;
            return false;
        }
        h << code.header;
        std::ofstream c(prefix + code.sourceName, std::ios::binary);
        if (!c)
        {
            if (error) *error = "cannot write " + prefix + code.sourceName;
            return false;
        }
        c << code.source;
        if (error) error->clear();
        return true;
    }
}
