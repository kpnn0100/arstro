#include "Formula.h"
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace arstro
{
namespace solaris
{
    using engine::ExprOp;

    namespace
    {
        struct FuncName { const char *name; ExprOp::Func fn; };
        const FuncName kFuncs[] = {
            {"sin", ExprOp::Sin}, {"cos", ExprOp::Cos}, {"tan", ExprOp::Tan}, {"abs", ExprOp::Abs}, {"sign", ExprOp::Sign},
            {"min", ExprOp::Min}, {"max", ExprOp::Max}, {"clamp", ExprOp::Clamp}, {"lerp", ExprOp::Lerp}, {"pow", ExprOp::PowF},
            {"exp", ExprOp::Exp}, {"log", ExprOp::Log}, {"sqrt", ExprOp::Sqrt}, {"floor", ExprOp::Floor}, {"ceil", ExprOp::Ceil},
            {"round", ExprOp::Round}, {"frac", ExprOp::Frac},
        };
        const char *kClock[] = {"beat", "bar", "bpm", "t"};

        bool isName(char c) { return std::isalnum((unsigned char)c) || c == '_' || c == '.'; }

        // Recursive descent straight to postfix; `depth` simulates the evaluator's stack.
        struct Parser
        {
            const std::string &s;
            size_t i = 0;
            ParsedFormula &out;
            std::string err;
            int depth = 0, maxDepth = 0;

            void skip() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
            bool fail(const std::string &why)
            {
                if (err.empty()) err = why + " (column " + std::to_string(i + 1) + ")";
                return false;
            }
            void push(const ExprOp &o)
            {
                out.expr.ops.push_back(o);
                switch (o.kind)
                {
                case ExprOp::Num: case ExprOp::Var: ++depth; break;
                case ExprOp::Neg: break;
                case ExprOp::Fn: depth -= engine::funcArity(o.fn) - 1; break;
                default: --depth; break;
                }
                maxDepth = std::max(maxDepth, depth);
            }
            bool expr()
            {
                if (!term()) return false;
                for (;;)
                {
                    skip();
                    if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                    {
                        const char op = s[i++];
                        if (!term()) return false;
                        ExprOp o;
                        o.kind = op == '+' ? ExprOp::Add : ExprOp::Sub;
                        push(o);
                    }
                    else return true;
                }
            }
            bool term()
            {
                if (!unary()) return false;
                for (;;)
                {
                    skip();
                    if (i < s.size() && (s[i] == '*' || s[i] == '/'))
                    {
                        const char op = s[i++];
                        if (!unary()) return false;
                        ExprOp o;
                        o.kind = op == '*' ? ExprOp::Mul : ExprOp::Div;
                        push(o);
                    }
                    else return true;
                }
            }
            bool unary()
            {
                skip();
                if (i < s.size() && s[i] == '-')
                {
                    ++i;
                    if (!unary()) return false;
                    ExprOp o;
                    o.kind = ExprOp::Neg;
                    push(o);
                    return true;
                }
                if (i < s.size() && s[i] == '+') { ++i; return unary(); }
                return power();
            }
            bool power()
            {
                if (!primary()) return false;
                skip();
                if (i < s.size() && s[i] == '^')
                {
                    ++i;
                    if (!unary()) return false; // right-associative: 2^3^2 = 2^9
                    ExprOp o;
                    o.kind = ExprOp::Pow;
                    push(o);
                }
                return true;
            }
            bool primary()
            {
                skip();
                if (i >= s.size()) return fail("the formula ends where a value was expected");
                if (s[i] == '(')
                {
                    ++i;
                    if (!expr()) return false;
                    skip();
                    if (i >= s.size() || s[i] != ')') return fail("a `(` is not closed");
                    ++i;
                    return true;
                }
                if (std::isdigit((unsigned char)s[i]) || s[i] == '.')
                {
                    char *end = nullptr;
                    const double v = std::strtod(s.c_str() + i, &end);
                    const size_t used = (size_t)(end - (s.c_str() + i));
                    if (used == 0) return fail("`" + s.substr(i, 1) + "` is not a number");
                    i += used;
                    ExprOp o;
                    o.kind = ExprOp::Num;
                    o.num = v;
                    push(o);
                    return true;
                }
                if (!(std::isalpha((unsigned char)s[i]) || s[i] == '_')) return fail("unexpected `" + s.substr(i, 1) + "`");
                const size_t a = i;
                while (i < s.size() && isName(s[i])) ++i;
                const std::string name = s.substr(a, i - a);
                skip();
                if (i < s.size() && s[i] == '(')
                {
                    const FuncName *f = nullptr;
                    for (const auto &fn : kFuncs)
                        if (name == fn.name) f = &fn;
                    if (!f) { i = a; return fail("unknown function `" + name + "`"); }
                    ++i;
                    int args = 0;
                    skip();
                    if (i < s.size() && s[i] == ')') ++i;
                    else
                        for (;;)
                        {
                            if (!expr()) return false;
                            ++args;
                            skip();
                            if (i < s.size() && s[i] == ',') { ++i; continue; }
                            if (i < s.size() && s[i] == ')') { ++i; break; }
                            return fail("`" + name + "(` is not closed");
                        }
                    const int want = engine::funcArity(f->fn);
                    if (args != want)
                    {
                        i = a;
                        return fail("`" + name + "` takes " + std::to_string(want) + (want == 1 ? " value" : " values") + ", not " + std::to_string(args));
                    }
                    ExprOp o;
                    o.kind = ExprOp::Fn;
                    o.fn = f->fn;
                    push(o);
                    return true;
                }
                ExprOp o;
                if (name == "pi") { o.kind = ExprOp::Num; o.num = M_PI; push(o); return true; }
                for (int k = 0; k < engine::kClockSlots; ++k)
                    if (name == kClock[k]) { o.kind = ExprOp::Var; o.index = k; push(o); return true; }
                // a symbol: an automation or an address — resolved by the caller
                int at = -1;
                for (size_t k = 0; k < out.names.size(); ++k)
                    if (out.names[k] == name) at = (int)k;
                if (at < 0) { at = (int)out.names.size(); out.names.push_back(name); }
                o.kind = ExprOp::Var;
                o.index = -(at + 1);
                push(o);
                return true;
            }
        };
    }

    bool isFormulaWord(const std::string &name)
    {
        if (name == "pi") return true;
        for (const char *c : kClock)
            if (name == c) return true;
        for (const auto &f : kFuncs)
            if (name == f.name) return true;
        return false;
    }

    bool parseFormula(const std::string &text, ParsedFormula &out, std::string &err)
    {
        out = ParsedFormula{};
        std::string body = text;
        size_t b = 0;
        while (b < body.size() && std::isspace((unsigned char)body[b])) ++b;
        if (b < body.size() && body[b] == '=') ++b;
        body = body.substr(b);
        Parser p{body, 0, out, {}};
        p.skip();
        if (p.i >= body.size()) { err = "an empty formula"; return false; }
        if (!p.expr()) { err = p.err; return false; }
        p.skip();
        if (p.i < body.size()) { p.fail("unexpected `" + body.substr(p.i, 1) + "`"); err = p.err; return false; }
        if (p.maxDepth > engine::Expr::kStack) { err = "the formula is nested too deeply"; return false; }
        return true;
    }

    void bindSymbols(ParsedFormula &f, const std::vector<int> &slots)
    {
        for (auto &o : f.expr.ops)
            if (o.kind == ExprOp::Var && o.index < 0) o.index = slots[(size_t)(-o.index - 1)];
    }
}
}
