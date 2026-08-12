#include "Gene.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace genesis
{
    namespace gene
    {
        namespace
        {
            constexpr double kPi = 3.14159265358979323846;
            constexpr double kTau = 6.28318530717958647692;

            struct FnInfo
            {
                int minArgs, maxArgs;
                const char *cpp;   // C++ form; "" = handled specially by the emitter
            };

            const std::map<std::string, FnInfo> &functions()
            {
                static const std::map<std::string, FnInfo> fns = {
                    {"min",   {2, 2, "std::min"}},
                    {"max",   {2, 2, "std::max"}},
                    {"abs",   {1, 1, "std::fabs"}},
                    {"sqrt",  {1, 1, "std::sqrt"}},
                    {"floor", {1, 1, "std::floor"}},
                    {"ceil",  {1, 1, "std::ceil"}},
                    {"round", {1, 1, "std::round"}},
                    {"sin",   {1, 1, "std::sin"}},
                    {"cos",   {1, 1, "std::cos"}},
                    {"tan",   {1, 1, "std::tan"}},
                    {"exp",   {1, 1, "std::exp"}},
                    {"log",   {1, 1, "std::log"}},
                    {"pow",   {2, 2, "std::pow"}},
                    {"mod",   {2, 2, "std::fmod"}},
                    {"atan2", {2, 2, "std::atan2"}},
                    {"sign",  {1, 1, ""}},
                    {"clamp", {3, 3, ""}},
                    {"lerp",  {3, 3, ""}},
                    {"deg",   {1, 1, ""}},
                    {"rad",   {1, 1, ""}},
                    {"turns", {1, 1, ""}},
                    {"pct",   {1, 1, ""}},
                    {"rgba",  {4, 4, ""}},
                    {"rgb",   {3, 3, ""}},
                    {"fade",  {2, 2, ""}},   // colour with alpha multiplied
                    {"mix",   {3, 3, ""}},   // blend two colours
                };
                return fns;
            }

            bool isIdentStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
            bool isIdentChar(char c) { return isIdentStart(c) || (c >= '0' && c <= '9'); }
            bool isDigit(char c) { return c >= '0' && c <= '9'; }

            int hexVal(char c)
            {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            }

            struct Parser
            {
                const std::string &src;
                size_t i = 0;
                std::string err;

                explicit Parser(const std::string &s) : src(s) {}

                bool fail(const std::string &m)
                {
                    if (err.empty())
                        err = m + " (at offset " + std::to_string(i) + ")";
                    return false;
                }
                void ws() { while (i < src.size() && (src[i] == ' ' || src[i] == '\t' || src[i] == '\n' || src[i] == '\r')) ++i; }
                bool eat(const char *tok)
                {
                    ws();
                    size_t n = 0;
                    while (tok[n]) ++n;
                    if (src.compare(i, n, tok) != 0) return false;
                    // Don't let "<" swallow the "<" of "<=" — callers try the longer token first.
                    i += n;
                    return true;
                }
                char peek() { ws(); return i < src.size() ? src[i] : '\0'; }

                NodePtr expr() { return ternary(); }

                NodePtr ternary()
                {
                    NodePtr c = logicalOr();
                    if (!c) return nullptr;
                    ws();
                    if (i < src.size() && src[i] == '?')
                    {
                        ++i;
                        NodePtr a = expr();
                        if (!a) return nullptr;
                        ws();
                        if (i >= src.size() || src[i] != ':') { fail("expected ':' in a ?: expression"); return nullptr; }
                        ++i;
                        NodePtr b = expr();
                        if (!b) return nullptr;
                        auto n = std::make_shared<Node>();
                        n->kind = NodeKind::Ternary;
                        n->args = {c, a, b};
                        return n;
                    }
                    return c;
                }
                NodePtr binaryNode(const std::string &op, NodePtr l, NodePtr r)
                {
                    auto n = std::make_shared<Node>();
                    n->kind = NodeKind::Binary;
                    n->op = op;
                    n->args = {std::move(l), std::move(r)};
                    return n;
                }
                NodePtr logicalOr()
                {
                    NodePtr l = logicalAnd();
                    while (l && eat("||"))
                    {
                        NodePtr r = logicalAnd();
                        if (!r) return nullptr;
                        l = binaryNode("||", l, r);
                    }
                    return l;
                }
                NodePtr logicalAnd()
                {
                    NodePtr l = comparison();
                    while (l && eat("&&"))
                    {
                        NodePtr r = comparison();
                        if (!r) return nullptr;
                        l = binaryNode("&&", l, r);
                    }
                    return l;
                }
                NodePtr comparison()
                {
                    NodePtr l = additive();
                    while (l)
                    {
                        ws();
                        std::string op;
                        if (eat("<=")) op = "<=";
                        else if (eat(">=")) op = ">=";
                        else if (eat("==")) op = "==";
                        else if (eat("!=")) op = "!=";
                        else if (i < src.size() && src[i] == '<') { ++i; op = "<"; }
                        else if (i < src.size() && src[i] == '>') { ++i; op = ">"; }
                        else break;
                        NodePtr r = additive();
                        if (!r) return nullptr;
                        l = binaryNode(op, l, r);
                    }
                    return l;
                }
                NodePtr additive()
                {
                    NodePtr l = multiplicative();
                    while (l)
                    {
                        ws();
                        if (i < src.size() && (src[i] == '+' || src[i] == '-'))
                        {
                            std::string op(1, src[i++]);
                            NodePtr r = multiplicative();
                            if (!r) return nullptr;
                            l = binaryNode(op, l, r);
                        }
                        else break;
                    }
                    return l;
                }
                NodePtr multiplicative()
                {
                    NodePtr l = unary();
                    while (l)
                    {
                        ws();
                        if (i < src.size() && (src[i] == '*' || src[i] == '/' || src[i] == '%'))
                        {
                            std::string op(1, src[i++]);
                            NodePtr r = unary();
                            if (!r) return nullptr;
                            l = binaryNode(op, l, r);
                        }
                        else break;
                    }
                    return l;
                }
                NodePtr unary()
                {
                    ws();
                    if (i < src.size() && (src[i] == '-' || src[i] == '!'))
                    {
                        std::string op(1, src[i++]);
                        NodePtr v = unary();
                        if (!v) return nullptr;
                        auto n = std::make_shared<Node>();
                        n->kind = NodeKind::Unary;
                        n->op = op;
                        n->args = {v};
                        return n;
                    }
                    return postfix();
                }
                NodePtr postfix()
                {
                    NodePtr p = primary();
                    while (p)
                    {
                        ws();
                        if (i < src.size() && src[i] == '.')
                        {
                            if (p->kind != NodeKind::Ident)
                            {
                                fail("'.' may only follow a name");
                                return nullptr;
                            }
                            ++i;
                            std::string field;
                            ws();
                            if (i >= src.size() || !isIdentStart(src[i])) { fail("expected a field name after '.'"); return nullptr; }
                            while (i < src.size() && isIdentChar(src[i])) field += src[i++];
                            auto n = std::make_shared<Node>();
                            n->kind = NodeKind::Member;
                            n->name = p->name;
                            n->field = field;
                            p = n;
                            continue;
                        }
                        break;
                    }
                    return p;
                }
                NodePtr primary()
                {
                    ws();
                    if (i >= src.size()) { fail("unexpected end of expression"); return nullptr; }
                    char c = src[i];

                    if (c == '(')
                    {
                        ++i;
                        NodePtr v = expr();
                        if (!v) return nullptr;
                        ws();
                        if (i >= src.size() || src[i] != ')') { fail("expected ')'"); return nullptr; }
                        ++i;
                        return v;
                    }
                    if (c == '#')
                    {
                        ++i;
                        std::string hex;
                        while (i < src.size() && hexVal(src[i]) >= 0) hex += src[i++];
                        if (hex.size() != 6 && hex.size() != 8 && hex.size() != 3)
                        {
                            fail("a colour literal is #rgb, #rrggbb, or #rrggbbaa");
                            return nullptr;
                        }
                        auto n = std::make_shared<Node>();
                        n->kind = NodeKind::ColorLit;
                        auto ch = [&](int idx) { return (double)hexVal(hex[(size_t)idx]); };
                        if (hex.size() == 3)
                        {
                            n->r = ch(0) * 17.0 / 255.0;
                            n->g = ch(1) * 17.0 / 255.0;
                            n->b = ch(2) * 17.0 / 255.0;
                            n->a = 1.0;
                        }
                        else
                        {
                            n->r = (ch(0) * 16 + ch(1)) / 255.0;
                            n->g = (ch(2) * 16 + ch(3)) / 255.0;
                            n->b = (ch(4) * 16 + ch(5)) / 255.0;
                            n->a = hex.size() == 8 ? (ch(6) * 16 + ch(7)) / 255.0 : 1.0;
                        }
                        return n;
                    }
                    if (isDigit(c) || (c == '.' && i + 1 < src.size() && isDigit(src[i + 1])))
                    {
                        const char *start = src.c_str() + i;
                        char *end = nullptr;
                        double d = std::strtod(start, &end);
                        if (end == start) { fail("bad number"); return nullptr; }
                        i += (size_t)(end - start);
                        auto n = std::make_shared<Node>();
                        n->kind = NodeKind::Number;
                        n->number = d;
                        return n;
                    }
                    if (isIdentStart(c))
                    {
                        std::string name;
                        while (i < src.size() && isIdentChar(src[i])) name += src[i++];

                        if (name == "raw")
                        {
                            ws();
                            if (i < src.size() && src[i] == '{')
                            {
                                ++i;
                                std::string body;
                                int depth = 1;
                                while (i < src.size() && depth > 0)
                                {
                                    if (src[i] == '{') ++depth;
                                    else if (src[i] == '}') { if (--depth == 0) break; }
                                    body += src[i++];
                                }
                                if (depth != 0) { fail("unterminated raw{ }"); return nullptr; }
                                ++i;   // closing brace
                                const size_t b0 = body.find_first_not_of(" \t\n\r");
                                const size_t b1 = body.find_last_not_of(" \t\n\r");
                                body = b0 == std::string::npos ? std::string() : body.substr(b0, b1 - b0 + 1);
                                auto n = std::make_shared<Node>();
                                n->kind = NodeKind::Raw;
                                n->name = body;
                                return n;
                            }
                        }
                        ws();
                        if (i < src.size() && src[i] == '(')
                        {
                            ++i;
                            auto n = std::make_shared<Node>();
                            n->kind = NodeKind::Call;
                            n->name = name;
                            ws();
                            if (i < src.size() && src[i] == ')') { ++i; }
                            else
                            {
                                while (true)
                                {
                                    NodePtr a = expr();
                                    if (!a) return nullptr;
                                    n->args.push_back(a);
                                    ws();
                                    if (i < src.size() && src[i] == ',') { ++i; continue; }
                                    if (i < src.size() && src[i] == ')') { ++i; break; }
                                    fail("expected ',' or ')' in a call");
                                    return nullptr;
                                }
                            }
                            auto found = functions().find(name);
                            if (found == functions().end()) { fail("unknown function '" + name + "'"); return nullptr; }
                            const int argc = (int)n->args.size();
                            if (argc < found->second.minArgs || argc > found->second.maxArgs)
                            {
                                fail("'" + name + "' takes " + std::to_string(found->second.minArgs) + " argument(s), got " + std::to_string(argc));
                                return nullptr;
                            }
                            return n;
                        }
                        auto n = std::make_shared<Node>();
                        n->kind = NodeKind::Ident;
                        n->name = name;
                        return n;
                    }
                    fail(std::string("unexpected character '") + c + "'");
                    return nullptr;
                }
            };

            bool evalNode(const NodePtr &n, const Scope &sc, Value &out, std::string *err);

            bool evalNumber(const NodePtr &n, const Scope &sc, double &out, std::string *err)
            {
                Value v;
                if (!evalNode(n, sc, v, err)) return false;
                if (v.isColor)
                {
                    if (err && err->empty()) *err = "expected a number, got a colour";
                    return false;
                }
                out = v.num;
                return true;
            }
            bool evalColor(const NodePtr &n, const Scope &sc, Value &out, std::string *err)
            {
                if (!evalNode(n, sc, out, err)) return false;
                if (!out.isColor)
                {
                    if (err && err->empty()) *err = "expected a colour, got a number";
                    return false;
                }
                return true;
            }

            bool evalNode(const NodePtr &n, const Scope &sc, Value &out, std::string *err)
            {
                if (!n)
                {
                    if (err && err->empty()) *err = "empty expression";
                    return false;
                }
                switch (n->kind)
                {
                case NodeKind::Number: out = Value::number(n->number); return true;
                case NodeKind::ColorLit: out = Value::color(n->r, n->g, n->b, n->a); return true;
                case NodeKind::Raw:
                    if (err && err->empty()) *err = "raw{ } cannot be previewed (it is C++, emitted verbatim)";
                    return false;
                case NodeKind::Ident:
                {
                    if (n->name == "w") { out = Value::number(sc.w); return true; }
                    if (n->name == "h") { out = Value::number(sc.h); return true; }
                    if (n->name == "minSide") { out = Value::number(sc.w < sc.h ? sc.w : sc.h); return true; }
                    if (n->name == "maxSide") { out = Value::number(sc.w > sc.h ? sc.w : sc.h); return true; }
                    if (n->name == "aspect") { out = Value::number(sc.h != 0.0 ? sc.w / sc.h : 0.0); return true; }
                    if (n->name == "PI") { out = Value::number(kPi); return true; }
                    if (n->name == "TAU") { out = Value::number(kTau); return true; }
                    if (sc.lookupIdent && sc.lookupIdent(n->name, out)) return true;
                    if (err && err->empty()) *err = "unknown name '" + n->name + "'";
                    return false;
                }
                case NodeKind::Member:
                {
                    if (sc.lookupMember && sc.lookupMember(n->name, n->field, out)) return true;
                    if (err && err->empty()) *err = "unknown field '" + n->name + "." + n->field + "'";
                    return false;
                }
                case NodeKind::Unary:
                {
                    double v;
                    if (!evalNumber(n->args[0], sc, v, err)) return false;
                    out = Value::number(n->op == "-" ? -v : (v == 0.0 ? 1.0 : 0.0));
                    return true;
                }
                case NodeKind::Ternary:
                {
                    double c;
                    if (!evalNumber(n->args[0], sc, c, err)) return false;
                    return evalNode(c != 0.0 ? n->args[1] : n->args[2], sc, out, err);
                }
                case NodeKind::Binary:
                {
                    double l, r;
                    if (!evalNumber(n->args[0], sc, l, err)) return false;
                    if (!evalNumber(n->args[1], sc, r, err)) return false;
                    const std::string &o = n->op;
                    double v = 0.0;
                    if (o == "+") v = l + r;
                    else if (o == "-") v = l - r;
                    else if (o == "*") v = l * r;
                    else if (o == "/") v = (r == 0.0) ? 0.0 : l / r;      // division by zero yields 0, never NaN
                    else if (o == "%") v = (r == 0.0) ? 0.0 : std::fmod(l, r);
                    else if (o == "<") v = l < r;
                    else if (o == "<=") v = l <= r;
                    else if (o == ">") v = l > r;
                    else if (o == ">=") v = l >= r;
                    else if (o == "==") v = l == r;
                    else if (o == "!=") v = l != r;
                    else if (o == "&&") v = (l != 0.0 && r != 0.0);
                    else if (o == "||") v = (l != 0.0 || r != 0.0);
                    out = Value::number(v);
                    return true;
                }
                case NodeKind::Call:
                {
                    const std::string &f = n->name;
                    if (f == "rgba" || f == "rgb")
                    {
                        double c[4] = {0, 0, 0, 255};
                        for (size_t k = 0; k < n->args.size(); ++k)
                            if (!evalNumber(n->args[k], sc, c[k], err)) return false;
                        out = Value::color(c[0] / 255.0, c[1] / 255.0, c[2] / 255.0,
                                           f == "rgba" ? c[3] / 255.0 : 1.0);
                        return true;
                    }
                    if (f == "fade")
                    {
                        Value col;
                        double t;
                        if (!evalColor(n->args[0], sc, col, err)) return false;
                        if (!evalNumber(n->args[1], sc, t, err)) return false;
                        out = Value::color(col.r, col.g, col.b, col.a * t);
                        return true;
                    }
                    if (f == "mix")
                    {
                        Value a, b;
                        double t;
                        if (!evalColor(n->args[0], sc, a, err)) return false;
                        if (!evalColor(n->args[1], sc, b, err)) return false;
                        if (!evalNumber(n->args[2], sc, t, err)) return false;
                        out = Value::color(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t,
                                           a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
                        return true;
                    }
                    double v[3] = {0, 0, 0};
                    for (size_t k = 0; k < n->args.size() && k < 3; ++k)
                        if (!evalNumber(n->args[k], sc, v[k], err)) return false;
                    double res = 0.0;
                    if (f == "min") res = v[0] < v[1] ? v[0] : v[1];
                    else if (f == "max") res = v[0] > v[1] ? v[0] : v[1];
                    else if (f == "abs") res = std::fabs(v[0]);
                    else if (f == "sqrt") res = v[0] <= 0.0 ? 0.0 : std::sqrt(v[0]);
                    else if (f == "floor") res = std::floor(v[0]);
                    else if (f == "ceil") res = std::ceil(v[0]);
                    else if (f == "round") res = std::round(v[0]);
                    else if (f == "sin") res = std::sin(v[0]);
                    else if (f == "cos") res = std::cos(v[0]);
                    else if (f == "tan") res = std::tan(v[0]);
                    else if (f == "exp") res = std::exp(v[0]);
                    else if (f == "log") res = v[0] <= 0.0 ? 0.0 : std::log(v[0]);
                    else if (f == "pow") res = std::pow(v[0], v[1]);
                    else if (f == "mod") res = v[1] == 0.0 ? 0.0 : std::fmod(v[0], v[1]);
                    else if (f == "atan2") res = std::atan2(v[0], v[1]);
                    else if (f == "sign") res = v[0] > 0.0 ? 1.0 : (v[0] < 0.0 ? -1.0 : 0.0);
                    else if (f == "clamp") res = v[0] < v[1] ? v[1] : (v[0] > v[2] ? v[2] : v[0]);
                    else if (f == "lerp") res = v[0] + (v[1] - v[0]) * v[2];
                    else if (f == "deg") res = v[0] * 180.0 / kPi;
                    else if (f == "rad") res = v[0] * kPi / 180.0;
                    else if (f == "turns") res = v[0] * kTau;
                    else if (f == "pct") res = v[0] / 100.0;
                    else
                    {
                        if (err && err->empty()) *err = "unknown function '" + f + "'";
                        return false;
                    }
                    out = Value::number(res);
                    return true;
                }
                }
                return false;
            }
        }

        NodePtr parse(const std::string &source, std::string *error)
        {
            Parser p(source);
            NodePtr n = p.expr();
            if (n)
            {
                p.ws();
                if (p.i != source.size())
                {
                    p.fail("unexpected trailing text");
                    n = nullptr;
                }
            }
            if (!n)
            {
                if (error) *error = p.err.empty() ? "invalid expression" : p.err;
                return nullptr;
            }
            if (error) error->clear();
            return n;
        }

        bool isConstant(const NodePtr &root)
        {
            return root && (root->kind == NodeKind::Number || root->kind == NodeKind::ColorLit);
        }

        NodePtr fold(const NodePtr &root)
        {
            if (!root) return root;
            for (auto &a : root->args)
                a = fold(a);
            if (root->kind == NodeKind::Number || root->kind == NodeKind::ColorLit ||
                root->kind == NodeKind::Ident || root->kind == NodeKind::Member || root->kind == NodeKind::Raw)
                return root;
            for (const auto &a : root->args)
                if (!isConstant(a))
                    return root;
            Scope empty;
            Value v;
            if (!evalNode(root, empty, v, nullptr))
                return root;   // not foldable (e.g. a type error) — leave it for the real evaluator
            auto n = std::make_shared<Node>();
            if (v.isColor)
            {
                n->kind = NodeKind::ColorLit;
                n->r = v.r; n->g = v.g; n->b = v.b; n->a = v.a;
            }
            else
            {
                n->kind = NodeKind::Number;
                n->number = v.num;
            }
            return n;
        }

        bool evaluate(const NodePtr &root, const Scope &scope, Value &out, std::string *error)
        {
            std::string local;
            const bool ok = evalNode(root, scope, out, error ? error : &local);
            if (ok && error) error->clear();
            return ok;
        }

        void collectMembers(const NodePtr &root, std::vector<std::pair<std::string, std::string>> &out)
        {
            if (!root) return;
            if (root->kind == NodeKind::Member)
                out.emplace_back(root->name, root->field);
            for (const auto &a : root->args)
                collectMembers(a, out);
        }

        void collectIdents(const NodePtr &root, std::vector<std::string> &out)
        {
            if (!root) return;
            if (root->kind == NodeKind::Ident)
                out.push_back(root->name);
            for (const auto &a : root->args)
                collectIdents(a, out);
        }

        bool containsRaw(const NodePtr &root)
        {
            if (!root) return false;
            if (root->kind == NodeKind::Raw) return true;
            for (const auto &a : root->args)
                if (containsRaw(a)) return true;
            return false;
        }

        const std::vector<std::string> &functionNames()
        {
            static std::vector<std::string> names = [] {
                std::vector<std::string> v;
                for (const auto &kv : functions()) v.push_back(kv.first);
                return v;
            }();
            return names;
        }

        const std::vector<std::string> &builtinIdents()
        {
            static const std::vector<std::string> v = {"w", "h", "minSide", "maxSide", "aspect", "PI", "TAU"};
            return v;
        }

        namespace
        {
            /** The SHORTEST literal that reads back as exactly `d`. The generated file is
             *  read by people, so 0.7 must not appear as 0.69999999999999996; and it must
             *  still be bit-exact, because the Verifier diffs preview against compiled. */
            std::string num(double d)
            {
                if (d == (long long)d && std::fabs(d) < 1e15)
                    return std::to_string((long long)d) + ".0";
                char buf[48];
                for (int prec = 6; prec <= 17; ++prec)
                {
                    std::snprintf(buf, sizeof buf, "%.*g", prec, d);
                    if (std::strtod(buf, nullptr) == d)
                        break;
                }
                std::string out = buf;
                if (out.find('.') == std::string::npos && out.find('e') == std::string::npos &&
                    out.find("inf") == std::string::npos && out.find("nan") == std::string::npos)
                    out += ".0";
                return out;
            }

            /** C++ (and Gene) operator precedence; higher binds tighter. Used to emit the
             *  minimum parentheses a reader needs, instead of wrapping every operand. */
            int precedenceOf(const NodePtr &n)
            {
                switch (n->kind)
                {
                case NodeKind::Ternary: return 1;
                case NodeKind::Binary:
                {
                    const std::string &o = n->op;
                    if (o == "||") return 2;
                    if (o == "&&") return 3;
                    if (o == "==" || o == "!=") return 4;
                    if (o == "<" || o == "<=" || o == ">" || o == ">=") return 5;
                    if (o == "+" || o == "-") return 6;
                    return 7;   // * / %
                }
                case NodeKind::Unary: return 8;
                default: return 100;   // literals, names, calls: never need wrapping
                }
            }


            std::string emitNode(const NodePtr &n, const CppNames &names, std::string *err);
            /** Emit `n` as an operand of something with precedence `parentPrec`, adding
             *  parentheses only when C++ would otherwise regroup it. */
            /** The precedence of the TEXT this node emits (not of the operator itself): the
             *  forms that already wrap themselves count as atoms. */
            int emittedPrecedence(const NodePtr &n)
            {
                if (n->kind == NodeKind::Binary)
                {
                    const std::string &o = n->op;
                    if (o == "<" || o == "<=" || o == ">" || o == ">=" || o == "==" || o == "!=" ||
                        o == "&&" || o == "||")
                        return 100;   // emitted as "(a < b ? 1.0 : 0.0)"
                    if ((o == "/" || o == "%") &&
                        !(n->args[1]->kind == NodeKind::Number && n->args[1]->number != 0.0))
                        return 100;   // emitted as "(d == 0.0 ? 0.0 : a / d)"
                }
                return precedenceOf(n);
            }

            std::string operand(const NodePtr &n, int parentPrec, const CppNames &names, std::string *err)
            {
                const std::string s = emitNode(n, names, err);
                return emittedPrecedence(n) < parentPrec ? "(" + s + ")" : s;
            }

            std::string colorCtor(double r, double g, double b, double a)
            {
                return "artboard::Color{" + num(r) + ", " + num(g) + ", " + num(b) + ", " + num(a) + "}";
            }
        }

        std::string emitCpp(const NodePtr &root, const CppNames &names, std::string *error)
        {
            std::string err;
            std::string out = emitNode(root, names, &err);
            if (!err.empty())
            {
                if (error) *error = err;
                return std::string();
            }
            if (error) error->clear();
            return out;
        }

        namespace
        {
            std::string emitNode(const NodePtr &n, const CppNames &names, std::string *err)
            {
                if (!n) { if (err->empty()) *err = "empty expression"; return ""; }
                switch (n->kind)
                {
                case NodeKind::Number: return num(n->number);
                case NodeKind::ColorLit: return colorCtor(n->r, n->g, n->b, n->a);
                case NodeKind::Raw: return "(" + n->name + ")";
                case NodeKind::Ident:
                {
                    if (n->name == "w") return "w";
                    if (n->name == "h") return "h";
                    if (n->name == "minSide") return "std::min(w, h)";
                    if (n->name == "maxSide") return "std::max(w, h)";
                    if (n->name == "aspect") return "(h != 0.0 ? w / h : 0.0)";
                    if (n->name == "PI") return "3.14159265358979324";
                    if (n->name == "TAU") return "6.28318530717958648";
                    if (names.ident)
                    {
                        std::string s = names.ident(n->name);
                        if (!s.empty()) return s;
                    }
                    if (err->empty()) *err = "unknown name '" + n->name + "'";
                    return "";
                }
                case NodeKind::Member:
                {
                    if (names.member)
                    {
                        std::string s = names.member(n->name, n->field);
                        if (!s.empty()) return s;
                    }
                    if (err->empty()) *err = "unknown field '" + n->name + "." + n->field + "'";
                    return "";
                }
                case NodeKind::Unary:
                    return n->op + operand(n->args[0], 8, names, err);
                case NodeKind::Ternary:
                    return operand(n->args[0], 9, names, err) + " != 0.0 ? " +
                           operand(n->args[1], 2, names, err) + " : " + operand(n->args[2], 2, names, err);
                case NodeKind::Binary:
                {
                    const std::string &o = n->op;
                    const int prec = precedenceOf(n);
                    // Division and modulo are defined as 0 on a zero divisor so the preview can
                    // never produce a NaN. The guard is only emitted when the divisor could
                    // actually be zero — a non-zero literal divisor needs no run-time test, and
                    // the guard would only make the generated line harder to read.
                    if (o == "/" || o == "%")
                    {
                        const NodePtr &rhs = n->args[1];
                        const bool needGuard = !(rhs->kind == NodeKind::Number && rhs->number != 0.0);
                        const std::string l = operand(n->args[0], prec, names, err);
                        const std::string r = operand(rhs, prec + 1, names, err);
                        if (!needGuard)
                            return o == "/" ? l + " / " + r : "std::fmod(" + l + ", " + r + ")";
                        const std::string rp = emitNode(rhs, names, err);
                        return "((" + rp + ") == 0.0 ? 0.0 : " +
                               (o == "/" ? l + " / " + r : "std::fmod(" + l + ", " + r + ")") + ")";
                    }
                    if (o == "&&" || o == "||")
                        return "((" + operand(n->args[0], 9, names, err) + " != 0.0) " + o + " (" +
                               operand(n->args[1], 9, names, err) + " != 0.0) ? 1.0 : 0.0)";
                    if (o == "<" || o == "<=" || o == ">" || o == ">=" || o == "==" || o == "!=")
                        return "(" + operand(n->args[0], prec, names, err) + " " + o + " " +
                               operand(n->args[1], prec + 1, names, err) + " ? 1.0 : 0.0)";
                    return operand(n->args[0], prec, names, err) + " " + o + " " +
                           operand(n->args[1], prec + 1, names, err);
                }
                case NodeKind::Call:
                {
                    const std::string &f = n->name;
                    std::vector<std::string> a;
                    a.reserve(n->args.size());
                    for (const auto &arg : n->args)
                        a.push_back(emitNode(arg, names, err));
                    if (f == "rgb")
                        return "artboard::Color{(" + a[0] + ") / 255.0, (" + a[1] + ") / 255.0, (" + a[2] + ") / 255.0, 1.0}";
                    if (f == "rgba")
                        return "artboard::Color{(" + a[0] + ") / 255.0, (" + a[1] + ") / 255.0, (" + a[2] + ") / 255.0, (" + a[3] + ") / 255.0}";
                    if (f == "fade")
                        return "genesisFade(" + a[0] + ", " + a[1] + ")";
                    if (f == "mix")
                        return "genesisMix(" + a[0] + ", " + a[1] + ", " + a[2] + ")";
                    if (f == "clamp")
                        return "genesisClamp(" + a[0] + ", " + a[1] + ", " + a[2] + ")";
                    if (f == "lerp")
                        return "((" + a[0] + ") + ((" + a[1] + ") - (" + a[0] + ")) * (" + a[2] + "))";
                    if (f == "sign")
                        return "genesisSign(" + a[0] + ")";
                    if (f == "deg") return "((" + a[0] + ") * 180.0 / 3.14159265358979324)";
                    if (f == "rad") return "((" + a[0] + ") * 3.14159265358979324 / 180.0)";
                    if (f == "turns") return "((" + a[0] + ") * 6.28318530717958648)";
                    if (f == "pct") return "((" + a[0] + ") / 100.0)";
                    if (f == "sqrt") return "genesisSqrt(" + a[0] + ")";
                    if (f == "log") return "genesisLog(" + a[0] + ")";
                    if (f == "mod") return "(((" + a[1] + ") == 0.0) ? 0.0 : std::fmod((" + a[0] + "), (" + a[1] + ")))";
                    auto found = functions().find(f);
                    if (found == functions().end() || !*found->second.cpp)
                    {
                        if (err->empty()) *err = "unknown function '" + f + "'";
                        return "";
                    }
                    std::string out = std::string(found->second.cpp) + "(";
                    for (size_t k = 0; k < a.size(); ++k)
                        out += (k ? ", " : "") + a[k];
                    return out + ")";
                }
                }
                return "";
            }
        }
    }
}
