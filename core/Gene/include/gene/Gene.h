/*
 *  Arstro — "Gene", the shared binding language.
 *
 *  PROMOTED out of `genesis_core` on 2026-09-11 so Interstellar could bind one parameter to
 *  a calculation over others without a second expression language existing in the repo
 *  (interstellar R-BIND-2). It is ALIASED, never copied: `apps/genesis/core/gene/Gene.h` is a
 *  four-line shim declaring `genesis::gene` as an alias of `arstro::gene`, so Genesis's 156
 *  call sites are unchanged and there is exactly one parser. This repo has already paid once
 *  for a forked file (genesis's palette copy, which drifted); `arstrobench` aliasing cosmo's
 *  token namespaces is the pattern that costs nothing.
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

namespace arstro
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
            Raw,        // raw{ ... } verbatim C++ passthrough
            /** A DOTTED PATH of three or more segments — `gr1.basic.exposure`,
             *  `s1.mask.0.adjust.exposure`. `Member` is the two-segment special case and is
             *  what Genesis emits, so Genesis is untouched by this: a path is a SUPERSET.
             *  Segments live in `segments`; `name`/`field` are unused. */
            Path
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
            std::vector<std::string> segments;   // Path: every dotted segment, in order
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
            /** Resolve a dotted path of ANY depth, segments in order. Interstellar's parameter
             *  addresses are three and more segments deep (`gr1.basic.exposure`), which the
             *  two-level `lookupMember` cannot express. When this is unset, a `Path` node fails
             *  to evaluate and names the whole path — it never silently resolves to a prefix,
             *  which is what the old two-level parser did. */
            std::function<bool(const std::vector<std::string> &, Value &)> lookupPath;
            /** A two-segment `Member` tries `lookupMember` first and then falls through to
             *  `lookupPath` with two segments, so a consumer whose addresses are uniformly
             *  dotted registers ONE resolver and `gr1.opacity` works as well as
             *  `gr1.basic.exposure`. */
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
            /** A dotted path of any depth -> C++ expression. Unset in Genesis, which never
             *  parses one; an unset resolver makes emitCpp fail rather than guess. */
            std::function<std::string(const std::vector<std::string> &)> path;
        };
        std::string emitCpp(const NodePtr &root, const CppNames &names, std::string *error = nullptr);

        /** Collect every `object.field` reference in the tree (for dependency/cycle analysis). */
        void collectMembers(const NodePtr &root, std::vector<std::pair<std::string, std::string>> &out);
        /** Every dotted reference in the tree as one joined string — two-segment `Member`s and
         *  deeper `Path`s alike — which is what a dependency graph over named parameters needs.
         *  One call, so a caller cannot collect the paths and forget the members. */
        void collectPaths(const NodePtr &root, std::vector<std::string> &out);
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
