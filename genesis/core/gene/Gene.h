/*
 *  Genesis — "Gene", the binding language.
 *
 *  Every numeric and colour field of an authored shape is an EXPRESSION, not a number.
 *  Typing `40` is an expression too; typing `min(w, h) * 0.5` is where responsiveness
 *  comes from. That single rule is the whole language design.
 *
 *  Gene is parsed ONCE into an AST and then consumed three ways:
 *      fold()      — constant-fold the tree (both consumers benefit)
 *      Evaluator   — the live preview's interpreter
 *      emitCpp()   — the exported C++ expression
 *  The interpreter and the emitter must agree; `Verifier` proves it by diffing the
 *  op streams of the previewed and the compiled component.
 *
 *  Two value types: `double` and `Color`. Booleans are doubles (0/1). Arithmetic on a
 *  colour is an error, caught at parse/analysis time where possible and at evaluation
 *  time otherwise. There are no statements, no user functions, and no loops — control
 *  flow lives in reactions, and anything past that is a `raw{ }` C++ escape.
 */
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace artboard { struct Color; }

namespace genesis
{
    namespace gene
    {
        /** A Gene value: a number or a colour. */
        struct Value
        {
            double num = 0.0;
            double r = 0, g = 0, b = 0, a = 1;   // colour channels, 0..1
            bool isColor = false;

            static Value number(double d) { Value v; v.num = d; return v; }
            static Value color(double r_, double g_, double b_, double a_)
            {
                Value v; v.isColor = true; v.r = r_; v.g = g_; v.b = b_; v.a = a_; return v;
            }
        };

        enum class NodeKind
        {
            Number,     // literal
            ColorLit,   // #rrggbb / #rrggbbaa literal
            Ident,      // w, h, minSide, a param name
            Member,     // self.w, ring.h, theme.primary, base.value
            Unary,      // - !
            Binary,     // + - * / % < <= > >= == != && ||
            Ternary,    // c ? a : b
            Call,       // min(...), rgba(...), fade(...)
            Raw         // raw{ ... } verbatim C++ passthrough
        };

        struct Node;
        using NodePtr = std::shared_ptr<Node>;

        struct Node
        {
            NodeKind kind = NodeKind::Number;
            double number = 0.0;
            double r = 0, g = 0, b = 0, a = 1;   // ColorLit
            std::string name;                    // Ident / Member(object) / Call / Raw text
            std::string field;                   // Member(field)
            std::string op;                      // Unary / Binary operator
            std::vector<NodePtr> args;           // Call args / operands / ternary [c,a,b]
        };

        /** Everything an expression can read. Supplied by the preview runtime; the emitter
         *  turns the same names into C++ instead of reading them. */
        struct Scope
        {
            /** Component size. */
            double w = 0, h = 0;
            /** Resolve `name` (a bare identifier: a param, or a built-in). */
            std::function<bool(const std::string &, Value &)> lookupIdent;
            /** Resolve `object.field` (self.x, ring.w, theme.primary, base.value). */
            std::function<bool(const std::string &, const std::string &, Value &)> lookupMember;
        };

        /** Parse `source`. Returns null and fills `error` on failure. */
        NodePtr parse(const std::string &source, std::string *error = nullptr);

        /** Fold constant subtrees in place; returns the (possibly replaced) root. */
        NodePtr fold(const NodePtr &root);

        /** Evaluate against `scope`. Returns false and fills `error` on an unknown name,
         *  a type error, or a `raw{}` node (which only the emitter can honor). */
        bool evaluate(const NodePtr &root, const Scope &scope, Value &out, std::string *error = nullptr);

        /** Emit the equivalent C++ expression. `resolver` maps a bare identifier or an
         *  `object.field` pair to the C++ text that reads it in the generated class. */
        struct CppNames
        {
            /** ident -> C++ expression, e.g. "thickness" -> "mThickness". */
            std::function<std::string(const std::string &)> ident;
            /** object.field -> C++ expression, e.g. ("ring","w") -> "mRing->width.value()". */
            std::function<std::string(const std::string &, const std::string &)> member;
        };
        std::string emitCpp(const NodePtr &root, const CppNames &names, std::string *error = nullptr);

        /** Collect every `object.field` reference in the tree (for dependency/cycle analysis). */
        void collectMembers(const NodePtr &root, std::vector<std::pair<std::string, std::string>> &out);
        /** Collect every bare identifier in the tree. */
        void collectIdents(const NodePtr &root, std::vector<std::string> &out);
        /** True if the tree contains a raw{} escape (unpreviewable, but exportable). */
        bool containsRaw(const NodePtr &root);
        /** True once fold() has reduced the whole tree to a literal. */
        bool isConstant(const NodePtr &root);

        /** The names a Gene expression may call, for the editor's completion + validation. */
        const std::vector<std::string> &functionNames();
        /** The built-in identifiers (w, h, minSide, ...). */
        const std::vector<std::string> &builtinIdents();
    }
}
