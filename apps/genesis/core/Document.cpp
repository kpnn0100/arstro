#include "Document.h"
#include "BaseCatalog.h"
#include "gene/Gene.h"
#include <algorithm>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace genesis
{
    const std::vector<FieldDef> &fieldDefs()
    {
        static const std::vector<FieldDef> f = {
            // name          type                 default   kinds    anim  segment property  doc
            {"x",            FieldType::Number,   "0",      kAll,    true,  "x",        "Left edge in the parent's space."},
            {"y",            FieldType::Number,   "0",      kAll,    true,  "y",        "Top edge in the parent's space."},
            {"w",            FieldType::Number,   "w",      kAll,    true,  "width",    "Width."},
            {"h",            FieldType::Number,   "h",      kAll,    true,  "height",   "Height."},
            {"opacity",      FieldType::Number,   "1",      kAll,    true,  "opacity",  "Group alpha; fades this shape and its children as one."},
            {"rotation",     FieldType::Number,   "0",      kAll,    true,  "rotation", "Rotation in radians about the pivot."},
            {"scaleX",       FieldType::Number,   "1",      kAll,    true,  "scaleX",   "Horizontal scale about the pivot."},
            {"scaleY",       FieldType::Number,   "1",      kAll,    true,  "scaleY",   "Vertical scale about the pivot."},
            {"pivotX",       FieldType::Number,   "self.w / 2", kAll, true, "pivotX",   "Pivot X in local space (defaults to the centre)."},
            {"pivotY",       FieldType::Number,   "self.h / 2", kAll, true, "pivotY",   "Pivot Y in local space (defaults to the centre)."},
            {"fill",         FieldType::Color,    "",       kAll,    false, "",         "Fill colour; empty draws no fill."},
            {"stroke",       FieldType::Color,    "",       kAll,    false, "",         "Stroke colour; empty draws no stroke."},
            {"strokeWidth",  FieldType::Number,   "1",      kAll,    true,  "",         "Stroke width in px."},
            {"cornerRadius", FieldType::Number,   "0",      kRect,   true,  "",         "Corner radius, clamped to half the shorter side."},
            {"arcStart",     FieldType::Number,   "0",      kCircle, true,  "",         "Sector START angle in degrees (0 = right, growing clockwise). With arcEnd, cuts the disk into a pie."},
            {"arcEnd",       FieldType::Number,   "360",    kCircle, true,  "",         "Sector END angle in degrees. 30 -> 330 leaves a 60-degree mouth: a pac-man."},
            {"arcInner",     FieldType::Number,   "0",      kCircle, true,  "",         "Hollow centre as a fraction of the radius: 0 is a pie, 0.6 a ring segment, and with a full sweep a donut."},
            {"trimStart",    FieldType::Number,   "0",      kRect | kCircle | kPath, true, "", "Where the drawn OUTLINE starts, as a fraction of its length. Different from arcStart: this thins the stroke, it does not cut the disk."},
            {"trimEnd",      FieldType::Number,   "1",      kRect | kCircle | kPath, true, "", "Where the drawn OUTLINE ends. Animate 0 -> 1 to make the shape draw itself in."},
            {"trimOffset",   FieldType::Number,   "0",      kRect | kCircle | kPath, true, "", "Rotates the trimmed span around the outline; wraps, so an arc can cross the seam."},
            {"fontSize",     FieldType::Number,   "14",     kLabel,  true,  "",         "Text size in px."},
            {"letterSpacing",FieldType::Number,   "0",      kLabel,  true,  "",         "Extra advance between glyphs."},
        };
        return f;
    }

    const FieldDef *findField(const std::string &name)
    {
        for (const auto &f : fieldDefs())
            if (name == f.name)
                return &f;
        return nullptr;
    }

    std::vector<const FieldDef *> fieldsFor(ShapeKind kind)
    {
        const unsigned mask = kind == ShapeKind::Rect ? kRect
                            : kind == ShapeKind::Circle ? kCircle
                            : kind == ShapeKind::Path ? kPath : kLabel;
        std::vector<const FieldDef *> out;
        for (const auto &f : fieldDefs())
            if (f.kinds & mask)
                out.push_back(&f);
        return out;
    }

    std::string shapeKindName(ShapeKind k)
    {
        switch (k)
        {
        case ShapeKind::Rect: return "rect";
        case ShapeKind::Circle: return "circle";
        case ShapeKind::Path: return "path";
        case ShapeKind::Label: return "label";
        }
        return "rect";
    }

    bool parseShapeKind(const std::string &s, ShapeKind &out)
    {
        if (s == "rect") { out = ShapeKind::Rect; return true; }
        if (s == "circle") { out = ShapeKind::Circle; return true; }
        if (s == "path") { out = ShapeKind::Path; return true; }
        if (s == "label") { out = ShapeKind::Label; return true; }
        return false;
    }

    int PathCmd::argCount(char op)
    {
        switch (op)
        {
        case 'M': case 'L': return 2;
        case 'Q': return 4;
        case 'C': return 6;
        case 'Z': return 0;
        }
        return -1;
    }

    std::string cancelName(Cancel c)
    {
        switch (c)
        {
        case Cancel::Restart: return "restart";
        case Cancel::IgnoreIfRunning: return "ignoreIfRunning";
        case Cancel::Queue: return "queue";
        }
        return "restart";
    }

    bool parseCancel(const std::string &s, Cancel &out)
    {
        if (s == "restart") { out = Cancel::Restart; return true; }
        if (s == "ignoreIfRunning") { out = Cancel::IgnoreIfRunning; return true; }
        if (s == "queue") { out = Cancel::Queue; return true; }
        return false;
    }

    // ───────────────────────── Shape ─────────────────────────

    std::string Shape::field(const std::string &name) const
    {
        for (const auto &kv : fields)
            if (kv.first == name)
                return kv.second;
        return std::string();
    }

    std::string Shape::effectiveField(const std::string &name) const
    {
        const std::string v = field(name);
        if (!v.empty())
            return v;
        const FieldDef *d = findField(name);
        return d ? d->defaultExpr : std::string();
    }

    void Shape::setField(const std::string &name, const std::string &expr)
    {
        for (auto &kv : fields)
            if (kv.first == name) { kv.second = expr; return; }
        fields.emplace_back(name, expr);
    }

    bool Shape::isAnimated(const std::string &name) const
    {
        return std::find(animated.begin(), animated.end(), name) != animated.end();
    }

    void Shape::setAnimated(const std::string &name, bool on)
    {
        auto it = std::find(animated.begin(), animated.end(), name);
        if (on && it == animated.end())
            animated.push_back(name);
        else if (!on && it != animated.end())
            animated.erase(it);
    }

    // ───────────────────────── Document lookup ─────────────────────────

    const Shape *Document::findShape(const std::string &id) const
    {
        for (const auto &s : shapes)
            if (s.id == id)
                return &s;
        return nullptr;
    }

    Shape *Document::findShape(const std::string &id)
    {
        for (auto &s : shapes)
            if (s.id == id)
                return &s;
        return nullptr;
    }

    const Param *Document::findParam(const std::string &name) const
    {
        for (const auto &p : params)
            if (p.name == name)
                return &p;
        return nullptr;
    }

    int Document::shapeIndex(const std::string &id) const
    {
        for (size_t i = 0; i < shapes.size(); ++i)
            if (shapes[i].id == id)
                return (int)i;
        return -1;
    }

    std::vector<std::string> Document::childrenOf(const std::string &parent) const
    {
        std::vector<std::string> out;
        for (const auto &s : shapes)
            if (s.parent == parent)
                out.push_back(s.id);
        return out;
    }

    std::string Document::uniqueShapeId(const std::string &stem) const
    {
        if (!findShape(stem))
            return stem;
        for (int n = 2; n < 10000; ++n)
        {
            const std::string candidate = stem + std::to_string(n);
            if (!findShape(candidate))
                return candidate;
        }
        return stem + "_x";
    }

    void Document::splitTarget(const std::string &target, const std::string &owner,
                               std::string &shape, std::string &field)
    {
        const size_t dot = target.find('.');
        if (dot == std::string::npos)
        {
            shape = owner;      // a bare field belongs to the reaction's own object
            field = target;
            return;
        }
        shape = target.substr(0, dot);
        field = target.substr(dot + 1);
    }

    std::vector<std::pair<const Shape *, const Reaction *>> Document::allReactions() const
    {
        std::vector<std::pair<const Shape *, const Reaction *>> out;
        for (const auto &s : shapes)
            for (const auto &r : s.reactions)
                out.emplace_back(&s, &r);
        return out;
    }

    std::string Document::duplicateShape(const std::string &id)
    {
        const Shape *root = findShape(id);
        if (!root)
            return {};

        // The subtree, parents before children (document order already guarantees that).
        std::vector<std::string> subtree{id};
        for (size_t i = 0; i < subtree.size(); ++i)
            for (const auto &s : shapes)
                if (s.parent == subtree[i] &&
                    std::find(subtree.begin(), subtree.end(), s.id) == subtree.end())
                    subtree.push_back(s.id);

        // Name every copy first, so remapping can see the whole mapping.
        std::map<std::string, std::string> renamed;
        for (const auto &old : subtree)
            renamed[old] = uniqueShapeId(old + "_copy");

        std::vector<Shape> copies;
        for (const auto &old : subtree)
        {
            Shape c = *findShape(old);
            c.id = renamed[old];
            auto p = renamed.find(c.parent);
            if (p != renamed.end())
                c.parent = p->second;     // an inner link follows the copy
            // Remap every target INSIDE the subtree; one pointing outside is left alone, so a
            // copy still drives whatever external object the original drove.
            for (auto &r : c.reactions)
                for (auto &step : r.steps)
                    for (auto &t : step.tracks)
                    {
                        std::string shape, field;
                        splitTarget(t.target, old, shape, field);
                        auto m = renamed.find(shape);
                        if (m == renamed.end())
                            continue;
                        t.target = t.target.find('.') == std::string::npos ? field
                                                                          : m->second + "." + field;
                    }
            copies.push_back(std::move(c));
        }
        for (auto &c : copies)
            shapes.push_back(std::move(c));
        return renamed[id];
    }

    std::string Document::addShape(Shape s)
    {
        s.id = uniqueShapeId(s.id.empty() ? shapeKindName(s.kind) : s.id);
        const std::string id = s.id;
        shapes.push_back(std::move(s));
        return id;
    }

    void Document::removeShape(const std::string &id)
    {
        // Collect the id and every descendant, then drop their shapes and any track that
        // targets them — an orphaned track would emit code referring to a deleted member.
        std::vector<std::string> doomed{id};
        for (size_t i = 0; i < doomed.size(); ++i)
            for (const auto &s : shapes)
                if (s.parent == doomed[i] && std::find(doomed.begin(), doomed.end(), s.id) == doomed.end())
                    doomed.push_back(s.id);

        shapes.erase(std::remove_if(shapes.begin(), shapes.end(), [&](const Shape &s) {
                         return std::find(doomed.begin(), doomed.end(), s.id) != doomed.end();
                     }),
                     shapes.end());

        // A track pointing at a shape that no longer exists would emit code referring to a
        // deleted member, so surviving objects drop those tracks (and any step left empty).
        for (auto &owner : shapes)
            for (auto &r : owner.reactions)
            {
                for (auto &step : r.steps)
                    step.tracks.erase(
                        std::remove_if(step.tracks.begin(), step.tracks.end(),
                                       [&](const Track &t) {
                                           std::string shape, field;
                                           splitTarget(t.target, owner.id, shape, field);
                                           return std::find(doomed.begin(), doomed.end(), shape) !=
                                                  doomed.end();
                                       }),
                        step.tracks.end());
                r.steps.erase(std::remove_if(r.steps.begin(), r.steps.end(),
                                             [](const Step &s) { return s.tracks.empty(); }),
                              r.steps.end());
            }
    }

    // ───────────────────────── persistence ─────────────────────────

    namespace
    {
        std::string paramTypeName(ParamType t)
        {
            switch (t)
            {
            case ParamType::Number: return "number";
            case ParamType::Color: return "color";
            case ParamType::Text: return "text";
            }
            return "number";
        }
        bool parseParamType(const std::string &s, ParamType &out)
        {
            if (s == "number") { out = ParamType::Number; return true; }
            if (s == "color") { out = ParamType::Color; return true; }
            if (s == "text") { out = ParamType::Text; return true; }
            return false;
        }
    }

    namespace
    {
        Json reactionsToJson(const std::vector<Reaction> &reactions)
        {
            Json rs = Json::array();
            for (const auto &r : reactions)
            {
                Json j = Json::object();
                j.set("on", Json::string(r.signal));
                j.set("cancel", Json::string(cancelName(r.cancel)));
                Json steps = Json::array();
                for (const auto &st : r.steps)
                {
                    Json tracks = Json::array();
                    for (const auto &t : st.tracks)
                    {
                        Json tj = Json::object();
                        tj.set("target", Json::string(t.target));
                        if (!t.from.empty())
                            tj.set("from", Json::string(t.from));
                        tj.set("to", Json::string(t.to));
                        tj.set("ms", Json::string(t.durationMs));
                        if (t.delayMs != "0" && !t.delayMs.empty())
                            tj.set("delayMs", Json::string(t.delayMs));
                        tj.set("easing", Json::string(t.easing));
                        if (t.repeat != 0)
                            tj.set("repeat", Json::number(t.repeat));
                        if (t.yoyo)
                            tj.set("yoyo", Json::boolean(true));
                        tracks.push(tj);
                    }
                    steps.push(tracks);
                }
                j.set("steps", steps);
                rs.push(j);
            }
            return rs;
        }

        std::vector<Reaction> reactionsFromJson(const Json &rs, std::string *error)
        {
            std::vector<Reaction> out;
            for (int i = 0; i < rs.size(); ++i)
            {
                const Json &rj = rs.at(i);
                Reaction r;
                r.signal = rj["on"].asString("");
                if (rj.has("cancel") && !parseCancel(rj["cancel"].asString("restart"), r.cancel))
                    if (error && error->empty())
                        *error = "reaction \"" + r.signal + "\": unknown cancel policy";
                const Json &steps = rj["steps"];
                for (int k = 0; k < steps.size(); ++k)
                {
                    Step st;
                    const Json &tracks = steps.at(k);
                    for (int m = 0; m < tracks.size(); ++m)
                    {
                        const Json &tj = tracks.at(m);
                        Track t;
                        t.target = tj["target"].asString("");
                        t.from = tj["from"].asString("");
                        t.to = tj["to"].asString("0");
                        // `ms` may be written as a number for convenience; it is stored as an
                        // expression so a `speed` param can re-time the whole component.
                        t.durationMs = tj["ms"].isNumber() ? Json::number(tj["ms"].asNumber()).dump()
                                                           : tj["ms"].asString("200");
                        t.delayMs = tj["delayMs"].isNumber()
                                        ? Json::number(tj["delayMs"].asNumber()).dump()
                                        : tj["delayMs"].asString("0");
                        t.easing = tj["easing"].asString("EaseOutCubic");
                        t.repeat = (int)tj["repeat"].asNumber(0);
                        t.yoyo = tj["yoyo"].asBool(false);
                        st.tracks.push_back(t);
                    }
                    r.steps.push_back(std::move(st));
                }
                out.push_back(std::move(r));
            }
            return out;
        }
    }

    Json Document::toJson() const
    {
        Json root = Json::object();
        root.set("genesis", Json::number(1));

        Json comp = Json::object();
        comp.set("name", Json::string(name));
        comp.set("base", Json::string(base));
        comp.set("namespace", Json::string(nameSpace));
        Json size = Json::array();
        size.push(Json::number(designW));
        size.push(Json::number(designH));
        comp.set("designSize", size);
        root.set("component", comp);

        Json ps = Json::array();
        for (const auto &p : params)
        {
            Json j = Json::object();
            j.set("name", Json::string(p.name));
            j.set("type", Json::string(paramTypeName(p.type)));
            j.set("default", Json::string(p.defaultExpr));
            if (p.hasRange)
            {
                Json range = Json::array();
                range.push(Json::number(p.minimum));
                range.push(Json::number(p.maximum));
                j.set("range", range);
            }
            ps.push(j);
        }
        root.set("params", ps);

        Json ss = Json::array();
        for (const auto &s : shapes)
        {
            Json j = Json::object();
            j.set("id", Json::string(s.id));
            j.set("type", Json::string(shapeKindName(s.kind)));
            if (!s.parent.empty())
                j.set("parent", Json::string(s.parent));
            Json bind = Json::object();
            for (const auto &kv : s.fields)
                bind.set(kv.first, Json::string(kv.second));
            j.set("bind", bind);
            if (!s.animated.empty())
            {
                Json anim = Json::array();
                for (const auto &a : s.animated)
                    anim.push(Json::string(a));
                j.set("animated", anim);
            }
            if (s.kind == ShapeKind::Path)
            {
                Json d = Json::array();
                for (const auto &c : s.path)
                {
                    Json cmd = Json::array();
                    cmd.push(Json::string(std::string(1, c.op)));
                    for (const auto &a : c.args)
                        cmd.push(Json::string(a));
                    d.push(cmd);
                }
                j.set("d", d);
            }
            if (s.kind == ShapeKind::Label)
                j.set("text", Json::string(s.text));
            if (!s.reactions.empty())
                j.set("reactions", reactionsToJson(s.reactions));
            ss.push(j);
        }
        root.set("shapes", ss);

        return root;
    }

    Document Document::fromJson(const Json &j, std::string *error)
    {
        Document d;
        if (error) error->clear();
        auto fail = [&](const std::string &m) {
            if (error && error->empty()) *error = m;
        };
        if (!j.isObject())
        {
            fail("the document root must be a JSON object");
            return d;
        }
        if (j["genesis"].asNumber(0) != 1)
            fail("unsupported or missing \"genesis\" version (expected 1)");

        const Json &comp = j["component"];
        if (!comp.isObject())
        {
            fail("missing \"component\" object");
            return d;
        }
        d.name = comp["name"].asString("MyComponent");
        d.base = comp["base"].asString("VisualLoop");
        d.nameSpace = comp["namespace"].asString("app");
        const Json &size = comp["designSize"];
        if (size.isArray() && size.size() == 2)
        {
            d.designW = size.at(0).asNumber(160);
            d.designH = size.at(1).asNumber(160);
        }

        const Json &ps = j["params"];
        for (int i = 0; i < ps.size(); ++i)
        {
            const Json &pj = ps.at(i);
            Param p;
            p.name = pj["name"].asString("");
            if (!parseParamType(pj["type"].asString("number"), p.type))
                fail("param \"" + p.name + "\": unknown type");
            p.defaultExpr = pj["default"].asString("0");
            const Json &range = pj["range"];
            if (range.isArray() && range.size() == 2)
            {
                p.hasRange = true;
                p.minimum = range.at(0).asNumber(0);
                p.maximum = range.at(1).asNumber(1);
            }
            d.params.push_back(p);
        }

        const Json &ss = j["shapes"];
        for (int i = 0; i < ss.size(); ++i)
        {
            const Json &sj = ss.at(i);
            Shape s;
            s.id = sj["id"].asString("");
            if (!parseShapeKind(sj["type"].asString("rect"), s.kind))
                fail("shape \"" + s.id + "\": unknown type \"" + sj["type"].asString("") + "\"");
            s.parent = sj["parent"].asString("");
            for (const auto &kv : sj["bind"].members())
                s.fields.emplace_back(kv.first, kv.second.asString(""));
            const Json &anim = sj["animated"];
            for (int k = 0; k < anim.size(); ++k)
                s.animated.push_back(anim.at(k).asString(""));
            const Json &dd = sj["d"];
            for (int k = 0; k < dd.size(); ++k)
            {
                const Json &cj = dd.at(k);
                PathCmd c;
                const std::string opStr = cj.at(0).asString("M");
                c.op = opStr.empty() ? 'M' : opStr[0];
                for (int a = 1; a < cj.size(); ++a)
                    c.args.push_back(cj.at(a).asString("0"));
                s.path.push_back(c);
            }
            s.text = sj["text"].asString("");
            s.reactions = reactionsFromJson(sj["reactions"], error);
            d.shapes.push_back(std::move(s));
        }

        // Reactions live inside their shape. A document written before that still has them at
        // the top level with fully-qualified targets, so migrate: each reaction goes to the
        // object its first track drives, which is the object it was always about.
        const Json &legacy = j["reactions"];
        if (legacy.size() > 0)
        {
            for (auto &r : reactionsFromJson(legacy, error))
            {
                std::string owner;
                for (const auto &st : r.steps)
                    for (const auto &t : st.tracks)
                        if (owner.empty())
                        {
                            std::string shape, field;
                            splitTarget(t.target, std::string(), shape, field);
                            owner = shape;
                        }
                Shape *host = owner.empty() ? nullptr : d.findShape(owner);
                if (!host && !d.shapes.empty())
                    host = &d.shapes.front();
                if (host)
                    host->reactions.push_back(std::move(r));
            }
        }

        return d;
    }

    Document Document::load(const std::string &path, std::string *error)
    {
        std::ifstream in(path, std::ios::binary);
        if (!in)
        {
            if (error) *error = "cannot open " + path;
            return Document();
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string parseError;
        Json j = Json::parse(ss.str(), &parseError);
        if (!parseError.empty())
        {
            if (error) *error = path + ": " + parseError;
            return Document();
        }
        return fromJson(j, error);
    }

    bool Document::save(const std::string &path, std::string *error) const
    {
        std::ofstream out(path, std::ios::binary);
        if (!out)
        {
            if (error) *error = "cannot write " + path;
            return false;
        }
        out << toJson().dump() << "\n";
        if (error) error->clear();
        return true;
    }

    // ───────────────────────── validation ─────────────────────────

    namespace
    {
        /** Every name a Gene expression in this document may legally read, and whether it
         *  resolves. Used by validate() to report an unknown name once, precisely. */
        struct NameChecker
        {
            const Document &doc;
            const BaseDef *base;
            std::string ownerShape;   // for `self.`

            bool ident(const std::string &n) const
            {
                const auto &b = gene::builtinIdents();
                if (std::find(b.begin(), b.end(), n) != b.end())
                    return true;
                return doc.findParam(n) != nullptr;
            }
            bool member(const std::string &obj, const std::string &f) const
            {
                if (obj == "self")
                    return !ownerShape.empty() && findField(f) != nullptr;
                if (obj == "base")
                {
                    if (!base) return false;
                    for (const auto &r : base->reads)
                        if (r.name == f) return true;
                    return false;
                }
                if (obj == "theme")
                    return true;   // any Theme role name; resolved by the emitter/runtime
                const Shape *s = doc.findShape(obj);
                return s && findField(f) != nullptr;
            }
        };

        void checkExpr(const std::string &src, const std::string &where, FieldType expect,
                       const NameChecker &nc, std::vector<Diagnostic> &out)
        {
            if (src.empty())
                return;   // an empty field means "use the default"
            std::string err;
            gene::NodePtr n = gene::parse(src, &err);
            if (!n)
            {
                out.push_back({Diagnostic::Severity::Error, where, err});
                return;
            }
            std::vector<std::string> idents;
            gene::collectIdents(n, idents);
            for (const auto &id : idents)
                if (!nc.ident(id))
                    out.push_back({Diagnostic::Severity::Error, where, "unknown name '" + id + "'"});
            std::vector<std::pair<std::string, std::string>> members;
            gene::collectMembers(n, members);
            for (const auto &m : members)
                if (!nc.member(m.first, m.second))
                    out.push_back({Diagnostic::Severity::Error, where,
                                   "unknown field '" + m.first + "." + m.second + "'"});
            if (gene::containsRaw(n))
                out.push_back({Diagnostic::Severity::Warning, where,
                               "raw{ } is emitted verbatim but cannot be previewed"});
            (void)expect;
        }
    }

    std::vector<Diagnostic> Document::validate() const
    {
        std::vector<Diagnostic> out;
        const BaseDef *b = findBase(base);
        if (!b)
            out.push_back({Diagnostic::Severity::Error, "component",
                           "unknown base class '" + base + "'"});
        if (name.empty())
            out.push_back({Diagnostic::Severity::Error, "component", "the component needs a name"});
        if (designW <= 0 || designH <= 0)
            out.push_back({Diagnostic::Severity::Error, "component", "the design size must be positive"});

        std::set<std::string> ids;
        NameChecker rootNc{*this, b, ""};
        for (const auto &p : params)
        {
            if (p.name.empty())
                out.push_back({Diagnostic::Severity::Error, "params", "a param has no name"});
            const auto &bi = gene::builtinIdents();
            if (std::find(bi.begin(), bi.end(), p.name) != bi.end())
                out.push_back({Diagnostic::Severity::Error, "param " + p.name,
                               "'" + p.name + "' is a built-in name"});
            // A default is a root: it may name a theme role, but not a shape or another param.
            if (p.type != ParamType::Text)
            {
                std::string err;
                gene::NodePtr n = gene::parse(p.defaultExpr, &err);
                if (!n)
                    out.push_back({Diagnostic::Severity::Error, "param " + p.name, err});
                else
                {
                    std::vector<std::string> idents;
                    gene::collectIdents(n, idents);
                    for (const auto &id : idents)
                    {
                        const auto &bb = gene::builtinIdents();
                        if (std::find(bb.begin(), bb.end(), id) == bb.end())
                            out.push_back({Diagnostic::Severity::Error, "param " + p.name,
                                           "a default cannot read '" + id + "'"});
                    }
                    std::vector<std::pair<std::string, std::string>> ms;
                    gene::collectMembers(n, ms);
                    for (const auto &m : ms)
                        if (m.first != "theme")
                            out.push_back({Diagnostic::Severity::Error, "param " + p.name,
                                           "a default may only read theme.*, not '" + m.first + "'"});
                }
            }
        }
        (void)rootNc;

        NameChecker nc{*this, b, ""};
        for (const auto &s : shapes)
        {
            if (s.id.empty())
                out.push_back({Diagnostic::Severity::Error, "shapes", "a shape has no id"});
            if (!ids.insert(s.id).second)
                out.push_back({Diagnostic::Severity::Error, s.id, "duplicate shape id"});
            if (!s.parent.empty() && !findShape(s.parent))
                out.push_back({Diagnostic::Severity::Error, s.id, "unknown parent '" + s.parent + "'"});

            NameChecker shapeNc{*this, b, s.id};
            for (const auto *fd : fieldsFor(s.kind))
            {
                const std::string expr = s.effectiveField(fd->name);
                checkExpr(expr, s.id + "." + fd->name, fd->type, shapeNc, out);
            }
            for (const auto &kv : s.fields)
            {
                const FieldDef *fd = findField(kv.first);
                if (!fd)
                    out.push_back({Diagnostic::Severity::Error, s.id, "unknown field '" + kv.first + "'"});
            }
            for (const auto &a : s.animated)
            {
                const FieldDef *fd = findField(a);
                if (!fd)
                    out.push_back({Diagnostic::Severity::Error, s.id, "cannot animate unknown field '" + a + "'"});
                else if (!fd->animatable)
                    out.push_back({Diagnostic::Severity::Error, s.id, "field '" + a + "' is not animatable"});
            }
            if (s.kind == ShapeKind::Path)
            {
                if (s.path.empty())
                    out.push_back({Diagnostic::Severity::Warning, s.id, "path has no commands: it draws nothing"});
                for (size_t k = 0; k < s.path.size(); ++k)
                {
                    const PathCmd &c = s.path[k];
                    const int want = PathCmd::argCount(c.op);
                    const std::string where = s.id + " path[" + std::to_string(k) + "]";
                    if (want < 0)
                    {
                        out.push_back({Diagnostic::Severity::Error, where,
                                       std::string("unknown path command '") + c.op + "'"});
                        continue;
                    }
                    if ((int)c.args.size() != want)
                        out.push_back({Diagnostic::Severity::Error, where,
                                       std::string(1, c.op) + " takes " + std::to_string(want) + " coordinates"});
                    for (const auto &a : c.args)
                        checkExpr(a, where, FieldType::Number, shapeNc, out);
                }
            }
        }

        // Binding cycles: a field may read other fields, but the graph must be a DAG or the
        // layout pass could never settle. Reported with the shape and field that closes it.
        {
            std::vector<std::pair<std::string, std::string>> nodes;   // shape.field
            std::map<std::string, std::vector<std::string>> edges;
            for (const auto &s : shapes)
                for (const auto *fd : fieldsFor(s.kind))
                {
                    const std::string key = s.id + "." + fd->name;
                    nodes.emplace_back(s.id, fd->name);
                    gene::NodePtr n = gene::parse(s.effectiveField(fd->name), nullptr);
                    if (!n) continue;
                    std::vector<std::pair<std::string, std::string>> ms;
                    gene::collectMembers(n, ms);
                    for (const auto &m : ms)
                    {
                        const std::string obj = m.first == "self" ? s.id : m.first;
                        if (obj == "base" || obj == "theme") continue;
                        edges[key].push_back(obj + "." + m.second);
                    }
                }
            std::map<std::string, int> state;   // 0 unvisited, 1 on stack, 2 done
            std::function<bool(const std::string &)> visit = [&](const std::string &key) -> bool {
                int &st = state[key];
                if (st == 1) return true;
                if (st == 2) return false;
                st = 1;
                for (const auto &next : edges[key])
                    if (visit(next))
                    {
                        st = 2;
                        return true;
                    }
                st = 2;
                return false;
            };
            std::set<std::string> reported;
            for (const auto &n : nodes)
            {
                const std::string key = n.first + "." + n.second;
                std::map<std::string, int> fresh;
                state = fresh;
                if (visit(key) && reported.insert(n.first).second)
                    out.push_back({Diagnostic::Severity::Error, key,
                                   "this binding depends on itself (cycle)"});
            }
        }

        std::set<std::string> handled;
        for (const auto &owner : shapes)
            for (const auto &r : owner.reactions)
            {
                const std::string where =
                    owner.id + " / " + (r.signal.empty() ? "<no signal>" : r.signal);
                handled.insert(r.signal);
                if (b)
                {
                    bool known = false;
                    for (const auto &sd : b->signals)
                        if (sd.name == r.signal) known = true;
                    if (!known)
                        out.push_back({Diagnostic::Severity::Error, where,
                                       "'" + base + "' has no signal '" + r.signal + "'"});
                }
                if (r.steps.empty())
                    out.push_back({Diagnostic::Severity::Warning, where,
                                   "reaction has no steps: it does nothing"});
                for (size_t si = 0; si < r.steps.size(); ++si)
                {
                    const std::string sw = where + " step " + std::to_string(si + 1);
                    if (r.steps[si].tracks.empty())
                        out.push_back({Diagnostic::Severity::Warning, sw, "step has no tracks"});
                    for (const auto &t : r.steps[si].tracks)
                    {
                        // A bare field targets the owning object; a qualified one may reach
                        // a sibling.
                        std::string shapeId, field;
                        splitTarget(t.target, owner.id, shapeId, field);
                        if (field.empty())
                        {
                            out.push_back({Diagnostic::Severity::Error, sw,
                                           "track has no target field"});
                            continue;
                        }
                        const Shape *s = findShape(shapeId);
                        if (!s)
                        {
                            out.push_back({Diagnostic::Severity::Error, sw,
                                           "unknown shape '" + shapeId + "'"});
                            continue;
                        }
                        const FieldDef *fd = findField(field);
                        if (!fd || !fd->animatable)
                        {
                            out.push_back({Diagnostic::Severity::Error, sw,
                                           "'" + t.target + "' is not an animatable field"});
                            continue;
                        }
                        if (fd->type != FieldType::Number)
                            out.push_back({Diagnostic::Severity::Error, sw,
                                           "'" + t.target + "' is a colour; only numbers animate"});
                        if (!s->isAnimated(field))
                            out.push_back({Diagnostic::Severity::Error, sw,
                                           "'" + t.target + "' must be marked animated on " + s->id});
                        if (!isEasingName(t.easing))
                            out.push_back({Diagnostic::Severity::Error, sw,
                                           "unknown easing '" + t.easing + "'"});
                        NameChecker tnc{*this, b, shapeId};
                        checkExpr(t.to, sw + " to", FieldType::Number, tnc, out);
                        if (!t.from.empty())
                            checkExpr(t.from, sw + " from", FieldType::Number, tnc, out);
                        checkExpr(t.durationMs, sw + " duration", FieldType::Number, tnc, out);
                        checkExpr(t.delayMs, sw + " delay", FieldType::Number, tnc, out);
                        if (t.repeat < -1)
                            out.push_back({Diagnostic::Severity::Error, sw, "repeat must be >= -1"});
                    }
                }
            }

        if (shapes.empty())
            out.push_back({Diagnostic::Severity::Warning, "shapes", "the component draws nothing"});
        if (b)
            for (const auto &sd : b->signals)
                if (sd.expected && !handled.count(sd.name))
                    out.push_back({Diagnostic::Severity::Warning, "reactions",
                                   "nothing reacts to '" + sd.name + "' — " + sd.doc});
        return out;
    }

    bool Document::isExportable() const
    {
        for (const auto &d : validate())
            if (d.isError())
                return false;
        return true;
    }

    namespace
    {
        Param numberParam(const std::string &name, const std::string &def, double lo, double hi)
        {
            Param p;
            p.name = name;
            p.type = ParamType::Number;
            p.defaultExpr = def;
            p.hasRange = true;
            p.minimum = lo;
            p.maximum = hi;
            return p;
        }
        Param colorParam(const std::string &name, const std::string &def)
        {
            Param p;
            p.name = name;
            p.type = ParamType::Color;
            p.defaultExpr = def;
            return p;
        }
        Track track(const std::string &target, const std::string &from, const std::string &to,
                    const std::string &ms, const std::string &easing, int repeat = 0, bool yoyo = false)
        {
            Track t;
            t.target = target;
            t.from = from;
            t.to = to;
            t.durationMs = ms;
            t.easing = easing;
            t.repeat = repeat;
            t.yoyo = yoyo;
            return t;
        }
        Reaction reaction(const std::string &signal, std::vector<Step> steps,
                          Cancel cancel = Cancel::Restart)
        {
            Reaction r;
            r.signal = signal;
            r.cancel = cancel;
            r.steps = std::move(steps);
            return r;
        }
    }

    /*  A new project must be a working, idiomatic EXAMPLE of its base — the fastest way to
     *  learn the tool is to open something that already moves and take it apart. Each starter
     *  therefore ships the design size that suits its base, shapes bound responsively, and
     *  reactions on the signals that base expects.
     */
    Document Document::starter(const std::string &baseName, const std::string &componentName)
    {
        Document d;
        d.base = baseName;
        d.name = componentName.empty() ? "MyComponent" : componentName;
        d.params.push_back(colorParam("accent", "theme.accent"));

        if (baseName == "ProgressIndicator")
        {
            d.designW = 260.0;
            d.designH = 10.0;
            d.params.push_back(colorParam("trackColor", "theme.secondary"));

            Shape rail;
            rail.id = "track";
            rail.kind = ShapeKind::Rect;
            rail.setField("x", "0");
            rail.setField("y", "0");
            rail.setField("w", "w");
            rail.setField("h", "h");
            rail.setField("fill", "trackColor");
            rail.setField("cornerRadius", "h / 2");
            d.shapes.push_back(rail);

            Shape fill;
            fill.id = "fill";
            fill.kind = ShapeKind::Rect;
            fill.setField("x", "0");
            fill.setField("y", "0");
            fill.setField("w", "w * base.display");   // the bar IS the base's smoothed value
            fill.setField("h", "h");
            fill.setField("fill", "accent");
            fill.setField("cornerRadius", "h / 2");
            fill.setAnimated("scaleY", true);
            fill.setField("pivotY", "self.h / 2");
            d.shapes.push_back(fill);

            Step pop;
            pop.tracks.push_back(track("scaleY", "1", "1.6", "140", "EaseOutBack"));
            Step settle;
            settle.tracks.push_back(track("scaleY", "", "1", "220", "EaseOutCubic"));
            d.shapes.back().reactions.push_back(reaction("complete", {pop, settle}));
            return d;
        }

        if (baseName == "Button")
        {
            d.designW = 132.0;
            d.designH = 36.0;
            d.params.push_back(colorParam("surface", "theme.secondary"));

            Shape body;
            body.id = "body";
            body.kind = ShapeKind::Rect;
            body.setField("x", "0");
            body.setField("y", "0");
            body.setField("w", "w");
            body.setField("h", "h");
            body.setField("fill", "mix(surface, accent, base.hover * 0.5)");   // hover cross-fade
            body.setField("cornerRadius", "6");
            body.setField("pivotX", "self.w / 2");
            body.setField("pivotY", "self.h / 2");
            body.setAnimated("scaleX", true);
            body.setAnimated("scaleY", true);
            d.shapes.push_back(body);

            Shape wash;
            wash.id = "wash";
            wash.kind = ShapeKind::Rect;
            wash.parent = "body";
            wash.setField("x", "0");
            wash.setField("y", "0");
            wash.setField("w", "self.h * 0");        // grows from the centre on press
            wash.setField("h", "body.h");
            wash.setField("fill", "fade(#ffffff, 0.18)");
            wash.setField("cornerRadius", "6");
            wash.setField("opacity", "0");
            wash.setAnimated("opacity", true);
            d.shapes.push_back(wash);

            Step press;
            press.tracks.push_back(track("scaleX", "", "0.96", "90", "EaseOutCubic"));
            press.tracks.push_back(track("scaleY", "", "0.96", "90", "EaseOutCubic"));
            press.tracks.push_back(track("wash.opacity", "0", "1", "90", "EaseOutCubic"));
            d.findShape("body")->reactions.push_back(reaction("pressDown", {press}));

            Step release;
            release.tracks.push_back(track("scaleX", "", "1", "180", "EaseOutBack"));
            release.tracks.push_back(track("scaleY", "", "1", "180", "EaseOutBack"));
            release.tracks.push_back(track("wash.opacity", "", "0", "180", "EaseOutCubic"));
            d.findShape("body")->reactions.push_back(reaction("release", {release}));
            d.findShape("body")->reactions.push_back(reaction("cancel", {release}));
            return d;
        }

        if (baseName == "Slider")
        {
            d.designW = 220.0;
            d.designH = 24.0;
            d.params.push_back(colorParam("trackColor", "theme.secondary"));

            Shape rail;
            rail.id = "rail";
            rail.kind = ShapeKind::Rect;
            rail.setField("x", "0");
            rail.setField("y", "(h - 4) / 2");
            rail.setField("w", "w");
            rail.setField("h", "4");
            rail.setField("fill", "trackColor");
            rail.setField("cornerRadius", "2");
            d.shapes.push_back(rail);

            Shape fill;
            fill.id = "fill";
            fill.kind = ShapeKind::Rect;
            fill.setField("x", "0");
            fill.setField("y", "(h - 4) / 2");
            fill.setField("w", "w * base.norm");
            fill.setField("h", "4");
            fill.setField("fill", "accent");
            fill.setField("cornerRadius", "2");
            d.shapes.push_back(fill);

            Shape thumb;
            thumb.id = "thumb";
            thumb.kind = ShapeKind::Circle;
            thumb.setField("w", "14");
            thumb.setField("h", "14");
            thumb.setField("x", "w * base.norm - self.w / 2");
            thumb.setField("y", "(h - self.h) / 2");
            thumb.setField("fill", "theme.foreground");
            thumb.setField("pivotX", "self.w / 2");
            thumb.setField("pivotY", "self.h / 2");
            thumb.setAnimated("scaleX", true);
            thumb.setAnimated("scaleY", true);
            d.shapes.push_back(thumb);

            Step grab;
            grab.tracks.push_back(track("scaleX", "", "1.35", "120", "EaseOutBack"));
            grab.tracks.push_back(track("scaleY", "", "1.35", "120", "EaseOutBack"));
            d.findShape("thumb")->reactions.push_back(reaction("dragStart", {grab}));

            Step let;
            let.tracks.push_back(track("scaleX", "", "1", "160", "EaseOutCubic"));
            let.tracks.push_back(track("scaleY", "", "1", "160", "EaseOutCubic"));
            d.findShape("thumb")->reactions.push_back(reaction("dragEnd", {let}));
            return d;
        }

        if (baseName == "Checkbox")
        {
            d.designW = 22.0;
            d.designH = 22.0;
            d.params.push_back(colorParam("surface", "theme.secondary"));

            Shape box;
            box.id = "box";
            box.kind = ShapeKind::Rect;
            box.setField("x", "0");
            box.setField("y", "0");
            box.setField("w", "minSide");
            box.setField("h", "minSide");
            box.setField("fill", "mix(surface, accent, base.checked)");
            box.setField("cornerRadius", "5");
            box.setField("pivotX", "self.w / 2");
            box.setField("pivotY", "self.h / 2");
            box.setAnimated("scaleX", true);
            box.setAnimated("scaleY", true);
            d.shapes.push_back(box);

            Shape tick;
            tick.id = "tick";
            tick.kind = ShapeKind::Path;
            tick.parent = "box";
            tick.setField("x", "0");
            tick.setField("y", "0");
            tick.setField("w", "box.w");
            tick.setField("h", "box.h");
            tick.setField("stroke", "theme.primaryForeground");
            tick.setField("strokeWidth", "2.2");
            tick.setField("opacity", "0");
            tick.setAnimated("opacity", true);
            tick.path.push_back({'M', {"self.w * 0.26", "self.h * 0.52"}});
            tick.path.push_back({'L', {"self.w * 0.44", "self.h * 0.70"}});
            tick.path.push_back({'L', {"self.w * 0.76", "self.h * 0.32"}});
            d.shapes.push_back(tick);

            Step check;
            check.tracks.push_back(track("scaleX", "0.86", "1", "180", "EaseOutBack"));
            check.tracks.push_back(track("scaleY", "0.86", "1", "180", "EaseOutBack"));
            check.tracks.push_back(track("tick.opacity", "", "base.checked", "140", "EaseOutCubic"));
            d.findShape("box")->reactions.push_back(reaction("checkedChanged", {check}));
            return d;
        }

        // VisualLoop — the default, and the example from the brief: a ring that fades in,
        // then spins forever, and fades out when the loop ends.
        d.designW = 160.0;
        d.designH = 160.0;
        d.params.push_back(numberParam("thickness", "4", 1, 24));

        Shape ring;
        ring.id = "ring";
        ring.kind = ShapeKind::Circle;
        ring.setField("w", "minSide * 0.7");
        ring.setField("h", "minSide * 0.7");
        ring.setField("x", "(w - self.w) / 2");
        ring.setField("y", "(h - self.h) / 2");
        ring.setField("stroke", "accent");
        ring.setField("strokeWidth", "thickness");
        ring.setField("opacity", "0");
        ring.setAnimated("opacity", true);
        ring.setAnimated("rotation", true);
        d.shapes.push_back(ring);

        Step fade;
        fade.tracks.push_back(track("opacity", "", "1", "300", "EaseOutCubic"));
        Step spin;
        spin.tracks.push_back(track("rotation", "0", "turns(1)", "1200", "Linear", -1));
        d.shapes.back().reactions.push_back(reaction("loopStart", {fade, spin}));

        Step out;
        out.tracks.push_back(track("opacity", "", "0", "300", "EaseInCubic"));
        d.shapes.back().reactions.push_back(reaction("loopEnd", {out}));
        return d;
    }
}
