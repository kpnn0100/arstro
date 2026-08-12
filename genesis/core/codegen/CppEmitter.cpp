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
                    const std::string shape = obj == "self" ? owner : obj;
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
                            const std::string obj = m.first == "self" ? s.id : m.first;
                            deps[key].push_back(obj + "\x1f" + m.second);
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

                for (const auto &r : doc.reactions)
                    handlesSignal[r.signal] = true;
            }

            static bool readsBase(const gene::NodePtr &n)
            {
                std::vector<std::pair<std::string, std::string>> ms;
                gene::collectMembers(n, ms);
                for (const auto &m : ms)
                    if (m.first == "base") return true;
                return false;
            }

            const Reaction *reactionFor(const std::string &signal) const
            {
                for (const auto &r : doc.reactions)
                    if (r.signal == signal) return &r;
                return nullptr;
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

            std::string reactionFn(const std::string &signal, int step) const
            {
                return "play" + upperFirst(ident(signal)) + "Step" + std::to_string(step);
            }
            std::string tokenMember(const std::string &signal) const { return "mTok" + upperFirst(ident(signal)); }
            std::string pendingMember(const std::string &signal) const { return "mPending" + upperFirst(ident(signal)); }
            std::string runningMember(const std::string &signal) const { return "mRunning" + upperFirst(ident(signal)); }
            std::string queuedMember(const std::string &signal) const { return "mQueued" + upperFirst(ident(signal)); }

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
                for (const auto &r : doc.reactions)
                    for (size_t i = 0; i < r.steps.size(); ++i)
                        o << "        void " << reactionFn(r.signal, (int)i) << "();\n";

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
                    for (const auto &a : s.animated)
                        o << "        bool " << ownFlag(s.id, a) << " = false;\n";
                for (const auto &r : doc.reactions)
                {
                    o << "        int " << tokenMember(r.signal) << " = 0;\n";
                    o << "        int " << pendingMember(r.signal) << " = 0;\n";
                    o << "        bool " << runningMember(r.signal) << " = false;\n";
                    if (r.cancel == Cancel::Queue)
                        o << "        bool " << queuedMember(r.signal) << " = false;\n";
                }
                if (!doc.reactions.empty())
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
                o << "        inline artboard::Color genesisFade(const artboard::Color &c, double t)\n";
                o << "        {\n            return artboard::Color{c.r, c.g, c.b, c.a * t};\n        }\n";
                o << "        inline artboard::Color genesisMix(const artboard::Color &a, const artboard::Color &b, double t)\n";
                o << "        {\n            return artboard::Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,\n";
                o << "                                  a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};\n        }\n";
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
                        gene::CppNames names;
                        std::string err;
                        const std::string v = gene::emitCpp(n, names, &err);
                        o << "        " << paramMember(p.name) << " = " << (err.empty() ? v : std::string("0.0")) << ";\n";
                    }
                }
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
                for (const auto &s : doc.shapes)
                {
                    o << "        " << memberOf(s.id) << " = std::make_shared<" << segmentClassFor(s.kind) << ">();\n";
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
                        o << "        const double " << localOf(s->id, fd->name) << " = "
                          << self.expr(src, layoutNames(s->id), where) << ";   // " << src << "\n";
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
                        const bool animated = s.isAnimated(fd->name);
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
                for (const auto &r : doc.reactions)
                {
                    for (size_t si = 0; si < r.steps.size(); ++si)
                    {
                        const Step &step = r.steps[si];
                        int finite = 0;
                        for (const auto &t : step.tracks)
                            if (t.repeat >= 0) ++finite;

                        o << "    void " << doc.name << "::" << reactionFn(r.signal, (int)si) << "()\n    {\n";
                        o << "        const double w = width.value(), h = height.value();\n";
                        o << "        (void)w; (void)h;\n";
                        if (finite > 0)
                        {
                            o << "        const int token = " << tokenMember(r.signal) << ";\n";
                            o << "        " << pendingMember(r.signal) << " = " << finite << ";\n";
                        }
                        for (const auto &t : step.tracks)
                        {
                            const size_t dot = t.target.find('.');
                            const std::string owner = t.target.substr(0, dot);
                            const std::string field = t.target.substr(dot + 1);
                            const Shape *s = doc.findShape(owner);
                            const FieldDef *fd = findField(field);
                            if (!s || !fd) continue;
                            const std::string prop = *fd->segmentProperty
                                                         ? memberOf(owner) + "->" + fd->segmentProperty
                                                         : stylePropOf(owner, field);
                            const gene::CppNames names = liveNames(owner);
                            const std::string where = "reaction " + r.signal + " " + t.target;
                            const std::string from = t.from.empty() ? prop + ".value()"
                                                                    : self.expr(t.from, names, where + " from");
                            const std::string to = self.expr(t.to, names, where + " to");
                            const std::string dur = self.expr(t.durationMs, names, where + " duration");
                            const std::string delay = self.expr(t.delayMs, names, where + " delay");
                            o << "        " << ownFlag(owner, field) << " = true;   // motion now owns "
                              << t.target << "\n";
                            o << "        " << prop << ".animate(\n";
                            o << "            artboard::Tween(" << from << ", " << to << ", " << dur << ", " << delay
                              << ",\n                            artboard::Easing::" << t.easing << ", " << t.repeat
                              << ", " << (t.yoyo ? "true" : "false") << "),\n";
                            o << "            mNowMs";
                            if (t.repeat >= 0)
                            {
                                o << ",\n            [this, token] {\n";
                                o << "                if (token != " << tokenMember(r.signal) << ")\n";
                                o << "                    return;   // a newer run of this reaction superseded us\n";
                                o << "                if (--" << pendingMember(r.signal) << " > 0)\n                    return;\n";
                                if (si + 1 < r.steps.size())
                                    o << "                " << reactionFn(r.signal, (int)si + 1) << "();\n";
                                else
                                {
                                    o << "                " << runningMember(r.signal) << " = false;\n";
                                    if (r.cancel == Cancel::Queue)
                                    {
                                        o << "                if (" << queuedMember(r.signal) << ")\n                {\n";
                                        o << "                    " << queuedMember(r.signal) << " = false;\n";
                                        o << "                    " << tokenMember(r.signal) << " = ++mSeq;\n";
                                        o << "                    " << runningMember(r.signal) << " = true;\n";
                                        o << "                    " << reactionFn(r.signal, 0) << "();\n                }\n";
                                    }
                                }
                                o << "            }";
                            }
                            o << ");\n";
                        }
                        if (finite == 0)
                        {
                            o << "        // every track in this step repeats forever: the chain ends here.\n";
                            o << "        " << runningMember(r.signal) << " = false;\n";
                        }
                        o << "    }\n\n";
                    }
                }
            }

            /** The body that starts a reaction, honouring its cancellation policy. */
            std::string startReaction(const Reaction &r, const std::string &indent) const
            {
                std::ostringstream o;
                if (r.cancel == Cancel::IgnoreIfRunning)
                {
                    o << indent << "if (" << runningMember(r.signal) << ")\n";
                    o << indent << "    return;   // cancel policy: ignore while running\n";
                }
                else if (r.cancel == Cancel::Queue)
                {
                    o << indent << "if (" << runningMember(r.signal) << ")\n";
                    o << indent << "{\n";
                    o << indent << "    " << queuedMember(r.signal) << " = true;   // cancel policy: run again after\n";
                    o << indent << "    return;\n";
                    o << indent << "}\n";
                }
                o << indent << tokenMember(r.signal) << " = ++mSeq;   // supersede any in-flight run\n";
                o << indent << runningMember(r.signal) << " = true;\n";
                o << indent << reactionFn(r.signal, 0) << "();\n";
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
                        const Reaction *on = reactionFor(onSig);
                        const Reaction *off = reactionFor(offSig);
                        const std::string arg = h.name == "onHoverChanged" ? "hovered" : "focused";
                        o << "        " << base->cppClass << "::" << h.name << "(" << arg << ");\n";
                        if (on)
                        {
                            o << "        if (" << arg << ")\n        {\n";
                            o << startReaction(*on, "            ");
                            o << "        }\n";
                        }
                        if (off)
                        {
                            o << "        " << (on ? "else " : "if (!" + arg + ")\n        ") << (on ? "\n        " : "");
                            o << "{\n" << startReaction(*off, "            ") << "        }\n";
                        }
                        o << "    }\n\n";
                        continue;
                    }
                    o << "        " << base->cppClass << "::" << h.name << "("
                      << (h.argName.empty() ? std::string() : h.argName) << ");\n";
                    const Reaction *r = reactionFor(h.signals.front());
                    if (r)
                        o << startReaction(*r, "        ");
                    o << "    }\n\n";
                }
            }

            void emitAdvance(std::ostringstream &o) const
            {
                o << "    void " << doc.name << "::advance(double nowMs)\n    {\n";
                o << "        mNowMs = nowMs;\n";
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
                const Reaction *attach = reactionFor("attach");
                const Reaction *resize = reactionFor("resize");
                if (attach)
                {
                    o << "        if (!mAttached)\n        {\n";
                    o << "            mAttached = true;\n";
                    o << startReaction(*attach, "            ");
                    o << "        }\n";
                }
                else if (resize)
                    o << "        mAttached = true;\n";
                if (resize)
                {
                    o << "        if (resized && mAttached)\n        {\n";
                    o << startReaction(*resize, "            ");
                    o << "        }\n";
                }
                o << "        " << base->cppClass << "::advance(nowMs);\n";
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
