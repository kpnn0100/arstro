#include "Runtime.h"
#include "BaseCatalog.h"
#include "Theme.h"
#include <algorithm>
#include <functional>
#include <set>

namespace genesis
{
    namespace
    {
        constexpr double kResizeMs = artboard::motion::kDurationShort3;

        artboard::Easing easingFromName(const std::string &name)
        {
            // One table, indexed the same way BaseCatalog::easingNames() lists them, so the
            // editor's dropdown, the emitter's `Easing::X`, and this lookup cannot disagree.
            static const std::map<std::string, artboard::Easing> m = {
                {"Linear", artboard::Easing::Linear},
                {"EaseInQuad", artboard::Easing::EaseInQuad},
                {"EaseOutQuad", artboard::Easing::EaseOutQuad},
                {"EaseInOutQuad", artboard::Easing::EaseInOutQuad},
                {"EaseInCubic", artboard::Easing::EaseInCubic},
                {"EaseOutCubic", artboard::Easing::EaseOutCubic},
                {"EaseInOutCubic", artboard::Easing::EaseInOutCubic},
                {"EaseInQuart", artboard::Easing::EaseInQuart},
                {"EaseOutQuart", artboard::Easing::EaseOutQuart},
                {"EaseInOutQuart", artboard::Easing::EaseInOutQuart},
                {"EaseInSine", artboard::Easing::EaseInSine},
                {"EaseOutSine", artboard::Easing::EaseOutSine},
                {"EaseInOutSine", artboard::Easing::EaseInOutSine},
                {"EaseInExpo", artboard::Easing::EaseInExpo},
                {"EaseOutExpo", artboard::Easing::EaseOutExpo},
                {"EaseInOutExpo", artboard::Easing::EaseInOutExpo},
                {"EaseInBack", artboard::Easing::EaseInBack},
                {"EaseOutBack", artboard::Easing::EaseOutBack},
                {"EaseInOutBack", artboard::Easing::EaseInOutBack},
                {"EaseInElastic", artboard::Easing::EaseInElastic},
                {"EaseOutElastic", artboard::Easing::EaseOutElastic},
                {"EaseInOutElastic", artboard::Easing::EaseInOutElastic},
                {"EaseInBounce", artboard::Easing::EaseInBounce},
                {"EaseOutBounce", artboard::Easing::EaseOutBounce},
                {"EaseInOutBounce", artboard::Easing::EaseInOutBounce},
                {"Standard", artboard::Easing::Standard},
                {"StandardDecel", artboard::Easing::StandardDecel},
                {"StandardAccel", artboard::Easing::StandardAccel},
                {"EmphasizedDecel", artboard::Easing::EmphasizedDecel},
                {"EmphasizedAccel", artboard::Easing::EmphasizedAccel},
            };
            auto it = m.find(name);
            return it == m.end() ? artboard::Easing::Linear : it->second;
        }

        // ── base-class hosts ────────────────────────────────────────────────────────
        // The preview root really IS the authored base, so its behaviour is the real one.
        // Each host does nothing but forward the base's signal hooks into the Runtime.
        template <class Base>
        struct Host : Base
        {
            Runtime *rt = nullptr;
            void advance(double nowMs) override
            {
                if (rt) rt->hostAdvance(nowMs);
                Base::advance(nowMs);
            }
            void onHoverChanged(bool hovered) override
            {
                if (rt) rt->hostSignal(hovered ? "hoverEnter" : "hoverExit");
            }
            void onFocusChanged(bool focused) override
            {
                if (rt) rt->hostSignal(focused ? "focusGained" : "focusLost");
            }
        };

        struct LoopHost : Host<artboard::VisualLoop>
        {
            void onLoopStart() override { if (rt) rt->hostSignal("loopStart"); }
            void onCycle(int) override { if (rt) rt->hostSignal("cycle"); }
            void onLoopEnd() override { if (rt) rt->hostSignal("loopEnd"); }
        };
        struct ProgressHost : Host<artboard::ProgressIndicator>
        {
            void onValueChanged(double) override { if (rt) rt->hostSignal("valueChanged"); }
            void onComplete() override { if (rt) rt->hostSignal("complete"); }
            void onIndeterminate() override { if (rt) rt->hostSignal("indeterminate"); }
            void onDeterminate() override { if (rt) rt->hostSignal("determinate"); }
        };
        struct ButtonHost : Host<artboard::Button>
        {
            void onPressDown() override { if (rt) rt->hostSignal("pressDown"); }
            void onRelease() override { if (rt) rt->hostSignal("release"); }
            void onCancel() override { if (rt) rt->hostSignal("cancel"); }
            void onClicked() override { if (rt) rt->hostSignal("clicked"); }
        };
        struct SliderHost : Host<artboard::Slider>
        {
            void onDragStart() override { if (rt) rt->hostSignal("dragStart"); }
            void onValueChanged(double) override { if (rt) rt->hostSignal("valueChanged"); }
            void onDragEnd() override { if (rt) rt->hostSignal("dragEnd"); }
        };
        struct CheckboxHost : Host<artboard::Checkbox>
        {
            void onCheckedChanged(bool) override { if (rt) rt->hostSignal("checkedChanged"); }
        };

        std::shared_ptr<artboard::Segment> makeHost(const std::string &base, Runtime *rt)
        {
            if (base == "VisualLoop") { auto h = std::make_shared<LoopHost>(); h->rt = rt; return h; }
            if (base == "ProgressIndicator") { auto h = std::make_shared<ProgressHost>(); h->rt = rt; return h; }
            if (base == "Button") { auto h = std::make_shared<ButtonHost>(); h->rt = rt; return h; }
            if (base == "Slider") { auto h = std::make_shared<SliderHost>(); h->rt = rt; return h; }
            if (base == "Checkbox") { auto h = std::make_shared<CheckboxHost>(); h->rt = rt; return h; }
            return nullptr;
        }

        std::shared_ptr<artboard::Segment> makeShapeSegment(ShapeKind k)
        {
            switch (k)
            {
            case ShapeKind::Rect: return std::make_shared<artboard::RectangleSegment>();
            case ShapeKind::Circle: return std::make_shared<artboard::CircleSegment>();
            case ShapeKind::Path: return std::make_shared<artboard::PathSegment>();
            case ShapeKind::Label: return std::make_shared<artboard::LabelSegment>();
            }
            return std::make_shared<artboard::RectangleSegment>();
        }
    }

    Runtime::Runtime() = default;
    Runtime::~Runtime() = default;

    // ───────────────────────── build ─────────────────────────

    bool Runtime::build(const Document &doc, std::string *error)
    {
        for (const auto &d : doc.validate())
            if (d.isError())
            {
                if (error) *error = d.where + ": " + d.message;
                return false;
            }
        mDoc = doc;
        mNotes.clear();
        mNodes.clear();
        mOrder.clear();
        mReactions.clear();
        mLocals.clear();
        mAttached = false;
        mLayoutEveryFrame = false;
        mLastW = mLastH = -1.0;
        mSeq = 0;

        mRootOwner = makeHost(mDoc.base, this);
        if (!mRootOwner)
        {
            if (error) *error = "unknown base class '" + mDoc.base + "'";
            return false;
        }
        mRootSegment = mRootOwner.get();
        mRootSegment->width.set(mDoc.designW);
        mRootSegment->height.set(mDoc.designH);

        // Param values start at their authored defaults; the inspector overrides them live.
        for (const auto &p : mDoc.params)
        {
            gene::Scope sc;
            sc.lookupIdent = [](const std::string &, gene::Value &) { return false; };
            sc.lookupMember = [](const std::string &, const std::string &, gene::Value &) { return false; };
            if (p.type == ParamType::Text)
            {
                mParamTexts[p.name] = p.defaultExpr;
                continue;
            }
            gene::NodePtr n = gene::parse(p.defaultExpr, nullptr);
            gene::Value v;
            if (n && gene::evaluate(n, sc, v, nullptr))
            {
                if (v.isColor) mParamColors[p.name] = artboard::Color{v.r, v.g, v.b, v.a};
                else mParamNumbers[p.name] = v.num;
            }
            else
            {
                if (p.type == ParamType::Color) mParamColors[p.name] = artboard::Color{1, 1, 1, 1};
                else mParamNumbers[p.name] = 0.0;
            }
        }

        buildTree();

        // Evaluation order: a field's local must exist before another reads it. Cycles are
        // already rejected by Document::validate(), mirroring the emitter's analysis.
        {
            std::map<std::string, std::vector<std::string>> deps;
            std::map<std::string, std::pair<std::string, std::string>> split;
            std::vector<std::string> keys;
            for (const auto &s : mDoc.shapes)
                for (const auto *fd : fieldsFor(s.kind))
                {
                    const std::string key = s.id + "\x1f" + fd->name;
                    keys.push_back(key);
                    split[key] = {s.id, fd->name};
                    gene::NodePtr n = gene::parse(s.effectiveField(fd->name), nullptr);
                    if (!n) continue;
                    if (gene::containsRaw(n))
                        mNotes.push_back(s.id + "." + fd->name + ": raw{ } is exported but cannot be previewed");
                    std::vector<std::pair<std::string, std::string>> ms;
                    gene::collectMembers(n, ms);
                    for (const auto &m : ms)
                    {
                        if (m.first == "theme") continue;
                        if (m.first == "base") { mLayoutEveryFrame = true; continue; }
                        const std::string obj = m.first == "self" ? s.id : m.first;
                        deps[key].push_back(obj + "\x1f" + m.second);
                    }
                }
            for (const auto &s : mDoc.shapes)
                for (const auto &c : s.path)
                    for (const auto &a : c.args)
                    {
                        gene::NodePtr n = gene::parse(a, nullptr);
                        if (!n) continue;
                        std::vector<std::pair<std::string, std::string>> ms;
                        gene::collectMembers(n, ms);
                        for (const auto &m : ms)
                            if (m.first == "base") mLayoutEveryFrame = true;
                    }
            std::set<std::string> done;
            std::function<void(const std::string &)> visit = [&](const std::string &k) {
                if (!done.insert(k).second) return;
                auto it = deps.find(k);
                if (it != deps.end())
                    for (const auto &d : it->second)
                        if (split.count(d)) visit(d);
                mOrder.push_back(split[k]);
            };
            for (const auto &k : keys)
                visit(k);
        }

        for (const auto &r : mDoc.reactions)
            mReactions[r.signal] = ReactionState{};

        layout(0.0);
        applyStyles();
        if (error) error->clear();
        return true;
    }

    void Runtime::buildTree()
    {
        for (const auto &s : mDoc.shapes)
        {
            ShapeNode n;
            n.id = s.id;
            n.kind = s.kind;
            n.seg = makeShapeSegment(s.kind);
            for (const auto *fd : fieldsFor(s.kind))
            {
                if (fd->type == FieldType::Number && !*fd->segmentProperty)
                    n.styleProps.emplace(fd->name, artboard::Property{0.0});
                if (fd->type == FieldType::Color)
                    n.colors.emplace(fd->name, artboard::Color{});
            }
            for (const auto &a : s.animated)
                n.owned[a] = false;
            if (s.kind == ShapeKind::Label)
            {
                auto *label = static_cast<artboard::LabelSegment *>(n.seg.get());
                const bool isParam = s.text.size() > 2 && s.text.front() == '{' && s.text.back() == '}';
                if (isParam)
                {
                    const std::string pname = s.text.substr(1, s.text.size() - 2);
                    auto it = mParamTexts.find(pname);
                    label->text = it == mParamTexts.end() ? s.text : it->second;
                }
                else
                    label->text = s.text;
            }
            mNodes.push_back(std::move(n));
        }
        for (const auto &s : mDoc.shapes)
        {
            ShapeNode *self = node(s.id);
            if (!self) continue;
            if (s.parent.empty())
                mRootSegment->addChild(self->seg);
            else if (ShapeNode *p = node(s.parent))
                p->seg->addChild(self->seg);
        }
    }

    Runtime::ShapeNode *Runtime::node(const std::string &id)
    {
        for (auto &n : mNodes)
            if (n.id == id) return &n;
        return nullptr;
    }
    const Runtime::ShapeNode *Runtime::node(const std::string &id) const
    {
        for (const auto &n : mNodes)
            if (n.id == id) return &n;
        return nullptr;
    }

    artboard::Segment *Runtime::segmentFor(const std::string &shapeId) const
    {
        const ShapeNode *n = node(shapeId);
        return n ? n->seg.get() : nullptr;
    }

    std::string Runtime::shapeIdFor(const artboard::Segment *seg) const
    {
        for (const artboard::Segment *s = seg; s; s = nullptr)
            for (const auto &n : mNodes)
                if (n.seg.get() == s) return n.id;
        return std::string();
    }

    artboard::Property *Runtime::propertyFor(const std::string &shapeId, const std::string &field)
    {
        ShapeNode *n = node(shapeId);
        const FieldDef *fd = findField(field);
        if (!n || !fd) return nullptr;
        if (*fd->segmentProperty)
        {
            const std::string p = fd->segmentProperty;
            if (p == "x") return &n->seg->x;
            if (p == "y") return &n->seg->y;
            if (p == "width") return &n->seg->width;
            if (p == "height") return &n->seg->height;
            if (p == "opacity") return &n->seg->opacity;
            if (p == "rotation") return &n->seg->rotation;
            if (p == "scaleX") return &n->seg->scaleX;
            if (p == "scaleY") return &n->seg->scaleY;
            if (p == "pivotX") return &n->seg->pivotX;
            if (p == "pivotY") return &n->seg->pivotY;
            return nullptr;
        }
        auto it = n->styleProps.find(field);
        return it == n->styleProps.end() ? nullptr : &it->second;
    }

    // ───────────────────────── scopes ─────────────────────────

    bool Runtime::lookupTheme(const std::string &role, gene::Value &out) const
    {
        double r, g, b, a;
        if (!themeColor(role, r, g, b, a)) return false;
        out = gene::Value::color(r, g, b, a);
        return true;
    }

    bool Runtime::lookupIdent(const std::string &name, gene::Value &out) const
    {
        auto num = mParamNumbers.find(name);
        if (num != mParamNumbers.end()) { out = gene::Value::number(num->second); return true; }
        auto col = mParamColors.find(name);
        if (col != mParamColors.end())
        {
            out = gene::Value::color(col->second.r, col->second.g, col->second.b, col->second.a);
            return true;
        }
        return false;
    }

    bool Runtime::readBase(const std::string &name, double &out) const
    {
        const artboard::Segment *seg = mRootSegment;
        if (!seg) return false;
        if (name == "hover") { out = seg->hoverAmount(); return true; }
        if (name == "focused") { out = seg->hasFocus() ? 1.0 : 0.0; return true; }
        if (name == "enabled") { out = seg->enabled ? 1.0 : 0.0; return true; }
        if (name == "w") { out = seg->width.value(); return true; }
        if (name == "h") { out = seg->height.value(); return true; }

        if (const auto *l = dynamic_cast<const artboard::VisualLoop *>(seg))
        {
            if (name == "phase") { out = l->cyclePhase(); return true; }
            if (name == "cycle") { out = (double)l->cycleCount(); return true; }
            if (name == "elapsed") { out = l->elapsedMs(); return true; }
            if (name == "running") { out = l->running() ? 1.0 : 0.0; return true; }
        }
        if (const auto *p = dynamic_cast<const artboard::ProgressIndicator *>(seg))
        {
            if (name == "value") { out = p->value(); return true; }
            if (name == "display") { out = p->displayValue(); return true; }
            if (name == "phase") { out = p->phase(); return true; }
            if (name == "indeterminate") { out = p->indeterminate() ? 1.0 : 0.0; return true; }
        }
        if (const auto *s = dynamic_cast<const artboard::Slider *>(seg))
        {
            if (name == "value") { out = s->value(); return true; }
            if (name == "display") { out = s->displayValue(); return true; }
            if (name == "min") { out = s->minimum(); return true; }
            if (name == "max") { out = s->maximum(); return true; }
            if (name == "norm")
            {
                const double span = s->maximum() - s->minimum();
                out = span == 0.0 ? 0.0 : (s->displayValue() - s->minimum()) / span;
                out = out < 0.0 ? 0.0 : (out > 1.0 ? 1.0 : out);
                return true;
            }
        }
        if (const auto *c = dynamic_cast<const artboard::Checkbox *>(seg))
            if (name == "checked") { out = c->checked() ? 1.0 : 0.0; return true; }
        return false;
    }

    gene::Scope Runtime::layoutScope(const std::string &owner) const
    {
        gene::Scope sc;
        sc.w = mRootSegment ? mRootSegment->width.value() : 0.0;
        sc.h = mRootSegment ? mRootSegment->height.value() : 0.0;
        sc.lookupIdent = [this](const std::string &n, gene::Value &v) { return lookupIdent(n, v); };
        sc.lookupMember = [this, owner](const std::string &obj, const std::string &f, gene::Value &v) {
            if (obj == "theme") return lookupTheme(f, v);
            if (obj == "base")
            {
                double d;
                if (!readBase(f, d)) return false;
                v = gene::Value::number(d);
                return true;
            }
            const std::string shape = obj == "self" ? owner : obj;
            auto it = mLocals.find(shape + "." + f);
            if (it == mLocals.end()) return false;
            v = it->second;
            return true;
        };
        return sc;
    }

    gene::Scope Runtime::liveScope(const std::string &owner) const
    {
        gene::Scope sc = layoutScope(owner);
        sc.lookupMember = [this, owner](const std::string &obj, const std::string &f, gene::Value &v) {
            if (obj == "theme") return lookupTheme(f, v);
            if (obj == "base")
            {
                double d;
                if (!readBase(f, d)) return false;
                v = gene::Value::number(d);
                return true;
            }
            const std::string shape = obj == "self" ? owner : obj;
            const ShapeNode *n = node(shape);
            const FieldDef *fd = findField(f);
            if (!n || !fd) return false;
            if (fd->type == FieldType::Color)
            {
                auto it = n->colors.find(f);
                if (it == n->colors.end()) return false;
                v = gene::Value::color(it->second.r, it->second.g, it->second.b, it->second.a);
                return true;
            }
            artboard::Property *p = const_cast<Runtime *>(this)->propertyFor(shape, f);
            if (!p) return false;
            v = gene::Value::number(p->value());
            return true;
        };
        return sc;
    }

    double Runtime::evalNumber(const std::string &src, const gene::Scope &sc, double fallback) const
    {
        gene::NodePtr n = gene::parse(src, nullptr);
        if (!n) return fallback;
        gene::Value v;
        if (!gene::evaluate(n, sc, v, nullptr) || v.isColor) return fallback;
        return v.num;
    }

    bool Runtime::evalColor(const std::string &src, const gene::Scope &sc, artboard::Color &out) const
    {
        gene::NodePtr n = gene::parse(src, nullptr);
        if (!n) return false;
        gene::Value v;
        if (!gene::evaluate(n, sc, v, nullptr) || !v.isColor) return false;
        out = artboard::Color{v.r, v.g, v.b, v.a};
        return true;
    }

    // ───────────────────────── layout / styles ─────────────────────────

        // An edge (resize, a param change) always RETARGETS: easing toward the new value from
        // wherever the previous ease had got to. A per-frame refresh (ms == 0, used when a
        // binding tracks live base state) must not cut that ease short, so it yields while one
        // is in flight. `owned` outranks both: once a reaction has driven a field, motion owns
        // it, and layout stops asserting the bound value so a resize cannot stomp the result.
    void Runtime::bindProp(artboard::Property &p, double v, double ms, bool owned)
    {
        if (owned)
            return;
        if (ms > 0.0)
        {
            p.animateTo(v, ms, artboard::Easing::EaseOutCubic, mNowMs);
            return;
        }
        if (p.isAnimating())
            return;
        p.set(v);
    }

    void Runtime::layout(double transitionMs)
    {
        if (!mRootSegment) return;
        mLocals.clear();
        for (const auto &kv : mOrder)
        {
            const Shape *s = mDoc.findShape(kv.first);
            const FieldDef *fd = findField(kv.second);
            if (!s || !fd) continue;
            const std::string src = s->effectiveField(fd->name);
            const gene::Scope sc = layoutScope(s->id);
            if (fd->type == FieldType::Color)
            {
                if (src.empty()) continue;
                artboard::Color c;
                if (evalColor(src, sc, c))
                    mLocals[s->id + "." + fd->name] = gene::Value::color(c.r, c.g, c.b, c.a);
            }
            else
                mLocals[s->id + "." + fd->name] = gene::Value::number(evalNumber(src, sc, 0.0));
        }

        for (const auto &s : mDoc.shapes)
        {
            ShapeNode *n = node(s.id);
            if (!n) continue;
            for (const auto *fd : fieldsFor(s.kind))
            {
                auto it = mLocals.find(s.id + "." + fd->name);
                if (it == mLocals.end()) continue;
                if (fd->type == FieldType::Color)
                {
                    n->colors[fd->name] = artboard::Color{it->second.r, it->second.g, it->second.b, it->second.a};
                    continue;
                }
                auto ownIt = n->owned.find(fd->name);
                const bool owned = ownIt != n->owned.end() && ownIt->second;
                if (artboard::Property *p = propertyFor(s.id, fd->name))
                    bindProp(*p, it->second.num, transitionMs, owned);
            }
            if (s.kind == ShapeKind::Path)
            {
                auto *ps = static_cast<artboard::PathSegment *>(n->seg.get());
                ps->path.clear();
                const gene::Scope sc = layoutScope(s.id);
                for (const auto &c : s.path)
                {
                    std::vector<double> a;
                    a.reserve(c.args.size());
                    for (const auto &e : c.args)
                        a.push_back(evalNumber(e, sc, 0.0));
                    switch (c.op)
                    {
                    case 'M': if (a.size() >= 2) ps->path.moveTo(a[0], a[1]); break;
                    case 'L': if (a.size() >= 2) ps->path.lineTo(a[0], a[1]); break;
                    case 'Q': if (a.size() >= 4) ps->path.quadTo(a[0], a[1], a[2], a[3]); break;
                    case 'C': if (a.size() >= 6) ps->path.cubicTo(a[0], a[1], a[2], a[3], a[4], a[5]); break;
                    case 'Z': ps->path.close(); break;
                    default: break;
                    }
                }
            }
        }
    }

    void Runtime::applyStyles()
    {
        for (const auto &s : mDoc.shapes)
        {
            ShapeNode *n = node(s.id);
            if (!n) continue;
            const bool hasFill = !s.effectiveField("fill").empty();
            const bool hasStroke = !s.effectiveField("stroke").empty();
            if (s.kind == ShapeKind::Label)
            {
                auto *label = static_cast<artboard::LabelSegment *>(n->seg.get());
                if (hasFill) label->style.color = n->colors["fill"];
                label->style.sizePx = n->styleProps["fontSize"].value();
                label->style.letterSpacingPx = n->styleProps["letterSpacing"].value();
                continue;
            }
            artboard::Paint p;
            if (hasFill) { p.hasFill = true; p.fill = n->colors["fill"]; }
            if (hasStroke)
            {
                p.hasStroke = true;
                p.stroke = n->colors["stroke"];
                p.strokeWidth = n->styleProps["strokeWidth"].value();
            }
            if (s.kind == ShapeKind::Path)
                static_cast<artboard::PathSegment *>(n->seg.get())->path.paint = p;
            else if (s.kind == ShapeKind::Circle)
                static_cast<artboard::CircleSegment *>(n->seg.get())->style.paint = p;
            else
            {
                auto *rect = static_cast<artboard::RectangleSegment *>(n->seg.get());
                rect->style.paint = p;
                rect->style.cornerRadius = n->styleProps["cornerRadius"].value();
            }
        }
    }

    // ───────────────────────── reactions ─────────────────────────

    void Runtime::hostSignal(const std::string &signal)
    {
        for (const auto &r : mDoc.reactions)
            if (r.signal == signal)
            {
                startReaction(r);
                return;
            }
    }

    void Runtime::fire(const std::string &signal) { hostSignal(signal); }

    bool Runtime::isRunning(const std::string &signal) const
    {
        auto it = mReactions.find(signal);
        return it != mReactions.end() && it->second.running;
    }

    void Runtime::startReaction(const Reaction &r)
    {
        ReactionState &st = mReactions[r.signal];
        if (r.cancel == Cancel::IgnoreIfRunning && st.running)
            return;
        if (r.cancel == Cancel::Queue && st.running)
        {
            st.queued = true;
            return;
        }
        st.token = ++mSeq;
        st.running = true;
        playStep(r, 0);
    }

    void Runtime::playStep(const Reaction &r, size_t stepIndex)
    {
        if (stepIndex >= r.steps.size())
        {
            mReactions[r.signal].running = false;
            return;
        }
        const Step &step = r.steps[stepIndex];
        ReactionState &st = mReactions[r.signal];
        const int token = st.token;
        const std::string signal = r.signal;

        int finite = 0;
        for (const auto &t : step.tracks)
            if (t.repeat >= 0) ++finite;
        st.pending = finite;

        for (const auto &t : step.tracks)
        {
            const size_t dot = t.target.find('.');
            if (dot == std::string::npos) continue;
            const std::string owner = t.target.substr(0, dot);
            const std::string field = t.target.substr(dot + 1);
            artboard::Property *p = propertyFor(owner, field);
            if (!p) continue;
            if (ShapeNode *n = node(owner))
                n->owned[field] = true;   // motion owns this field from now on

            const gene::Scope sc = liveScope(owner);
            const double from = t.from.empty() ? p->value() : evalNumber(t.from, sc, p->value());
            const double to = evalNumber(t.to, sc, 0.0);
            const double dur = evalNumber(t.durationMs, sc, 200.0);
            const double delay = evalNumber(t.delayMs, sc, 0.0);
            const artboard::Tween spec(from, to, dur, delay, easingFromName(t.easing), t.repeat, t.yoyo);

            if (t.repeat < 0)
            {
                p->animate(spec, mNowMs);   // repeats forever: never completes, never chains
                continue;
            }
            p->animate(spec, mNowMs, [this, signal, token, stepIndex] {
                auto it = mReactions.find(signal);
                if (it == mReactions.end() || it->second.token != token)
                    return;   // a newer run of this reaction superseded us
                if (--it->second.pending > 0)
                    return;
                const Reaction *rr = nullptr;
                for (const auto &cand : mDoc.reactions)
                    if (cand.signal == signal) { rr = &cand; break; }
                if (!rr) return;
                if (stepIndex + 1 < rr->steps.size())
                {
                    playStep(*rr, stepIndex + 1);
                    return;
                }
                it->second.running = false;
                if (rr->cancel == Cancel::Queue && it->second.queued)
                {
                    it->second.queued = false;
                    startReaction(*rr);
                }
            });
        }
        if (finite == 0)
            st.running = false;   // every track repeats forever: the chain ends here
    }

    // ───────────────────────── frame ─────────────────────────

    void Runtime::setSize(double w, double h)
    {
        if (!mRootSegment) return;
        mRootSegment->width.set(w);
        mRootSegment->height.set(h);
    }

    void Runtime::hostAdvance(double nowMs)
    {
        mNowMs = nowMs;
        if (!mRootSegment) return;
        // The FIRST sizing snaps: a component being placed into a layout has not "changed
        // size", and easing in from the design size would look like a spurious entrance
        // animation. Every later change eases (R1).
        const bool firstSizing = mLastW < 0.0;
        const bool resized = mRootSegment->width.value() != mLastW || mRootSegment->height.value() != mLastH;
        if (resized)
        {
            mLastW = mRootSegment->width.value();
            mLastH = mRootSegment->height.value();
        }
        const double sizeMs = firstSizing ? 0.0 : kResizeMs;
        if (mLayoutEveryFrame)
            layout(resized ? sizeMs : 0.0);
        else if (resized)
            layout(sizeMs);
        for (auto &n : mNodes)
            for (auto &kv : n.styleProps)
                kv.second.update(nowMs);
        applyStyles();
        if (!mAttached)
        {
            mAttached = true;
            hostSignal("attach");
        }
        else if (resized)
            hostSignal("resize");
    }

    void Runtime::advance(double nowMs)
    {
        if (mRootSegment)
            mRootSegment->advance(nowMs);   // the host's override calls hostAdvance first
    }

    // ───────────────────────── driving the base ─────────────────────────

    void Runtime::loopStart()
    {
        if (auto *l = dynamic_cast<artboard::VisualLoop *>(mRootSegment)) l->start(mNowMs);
    }
    void Runtime::loopStop()
    {
        if (auto *l = dynamic_cast<artboard::VisualLoop *>(mRootSegment)) l->stop(mNowMs);
    }
    bool Runtime::loopRunning() const
    {
        const auto *l = dynamic_cast<const artboard::VisualLoop *>(mRootSegment);
        return l && l->running();
    }
    void Runtime::setProgress(double v)
    {
        if (auto *p = dynamic_cast<artboard::ProgressIndicator *>(mRootSegment)) p->setValue(v);
    }
    void Runtime::setIndeterminate(bool on)
    {
        if (auto *p = dynamic_cast<artboard::ProgressIndicator *>(mRootSegment)) p->setIndeterminate(on);
    }
    void Runtime::setPressed(bool on)
    {
        if (!mRootSegment) return;
        const artboard::Gesture g{on ? artboard::Gesture::Type::Down : artboard::Gesture::Type::Click,
                                  {mRootSegment->width.value() * 0.5, mRootSegment->height.value() * 0.5},
                                  {0, 0}, artboard::PointerButton::Left};
        mRootSegment->onGesture(g);
    }
    void Runtime::setSliderValue(double norm)
    {
        // Drive it the way a user does — a drag gesture — so the base's own signals fire
        // and the preview matches what the compiled component does under the same input.
        if (auto *s = dynamic_cast<artboard::Slider *>(mRootSegment))
        {
            const artboard::Gesture g{artboard::Gesture::Type::DragStart,
                                      {s->width.value() * norm, s->height.value() * 0.5},
                                      {0, 0}, artboard::PointerButton::Left};
            s->onGesture(g);
        }
    }
    void Runtime::setChecked(bool on)
    {
        if (auto *c = dynamic_cast<artboard::Checkbox *>(mRootSegment))
            if (c->checked() != on)
            {
                const artboard::Gesture g{artboard::Gesture::Type::Click,
                                          {c->width.value() * 0.5, c->height.value() * 0.5},
                                          {0, 0}, artboard::PointerButton::Left};
                c->onGesture(g);
            }
    }
    void Runtime::setHovered(bool on)
    {
        artboard::Segment::setHovered(on ? mRootSegment : nullptr);
    }

    // ───────────────────────── params ─────────────────────────

    void Runtime::setParamNumber(const std::string &name, double v)
    {
        mParamNumbers[name] = v;
        layout(kResizeMs);
        applyStyles();
    }
    void Runtime::setParamColor(const std::string &name, const artboard::Color &c)
    {
        mParamColors[name] = c;
        layout(0.0);
        applyStyles();
    }
    void Runtime::setParamText(const std::string &name, const std::string &s)
    {
        mParamTexts[name] = s;
        for (const auto &sh : mDoc.shapes)
            if (sh.kind == ShapeKind::Label && sh.text == "{" + name + "}")
                if (ShapeNode *n = node(sh.id))
                    static_cast<artboard::LabelSegment *>(n->seg.get())->text = s;
    }
    bool Runtime::paramNumber(const std::string &name, double &out) const
    {
        auto it = mParamNumbers.find(name);
        if (it == mParamNumbers.end()) return false;
        out = it->second;
        return true;
    }
    bool Runtime::paramColor(const std::string &name, artboard::Color &out) const
    {
        auto it = mParamColors.find(name);
        if (it == mParamColors.end()) return false;
        out = it->second;
        return true;
    }
}
