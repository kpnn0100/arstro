/*
 *  gene_tests — the shared binding language, and in particular the two things Interstellar's
 *  promotion added: DOTTED PATHS of any depth, and a time scope that this library does not
 *  itself know how to resolve.
 *
 *  The path test exists because the pre-promotion parser did not merely lack deep paths — it
 *  ACCEPTED them and got them wrong. `a.b.c` built a two-level Member and then overwrote its
 *  field, yielding `a.c`: a silently wrong answer, which is the one failure mode worse than
 *  an error. Plain assert(), matching the neighbouring suites.
 */
// D-43 (cosmo's, and it bit this suite on its first run): CMAKE_BUILD_TYPE=Release puts
// -DNDEBUG in the flags and <cassert> then defines assert() to ((void)0) — so every test
// printed [PASS] while checking nothing, including one that was genuinely failing. <cassert>
// is the one standard header specified to be re-includable and to re-read NDEBUG each time,
// which is what makes this work rather than a trick.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>

#include "gene/Gene.h"
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace arstro::gene;

namespace
{
    /** A scope that answers dotted paths out of a map and nothing else — the shape
     *  Interstellar's evaluator has. */
    Scope mapScope(const std::map<std::string, double> &vals, double t = 0.0)
    {
        Scope sc;
        sc.w = 100; sc.h = 50;
        // Captured BY VALUE, not by reference: `vals` is a parameter and every caller passes a
        // temporary, so a reference capture dangles the moment mapScope returns. It did, and the
        // suite only said so once NDEBUG stopped disabling the assertions.
        sc.lookupIdent = [vals, t](const std::string &n, Value &out) {
            if (n == "t") { out = Value::number(t); return true; }
            auto it = vals.find(n);
            if (it == vals.end()) return false;
            out = Value::number(it->second);
            return true;
        };
        sc.lookupPath = [vals](const std::vector<std::string> &segs, Value &out) {
            std::string key;
            for (size_t i = 0; i < segs.size(); ++i) { if (i) key += '.'; key += segs[i]; }
            auto it = vals.find(key);
            if (it == vals.end()) return false;
            out = Value::number(it->second);
            return true;
        };
        return sc;
    }

    double evalOrDie(const std::string &src, const Scope &sc)
    {
        std::string err;
        auto ast = parse(src, &err);
        assert(ast && err.empty());
        Value v;
        const bool ok = evaluate(ast, sc, v, &err);
        if (!ok) std::fprintf(stderr, "evaluate failed: %s\n", err.c_str());
        assert(ok);
        assert(!v.isColor);
        return v.num;
    }

    void test_two_segment_members_still_work()
    {
        // Genesis's own shape: exactly two segments, through lookupMember. Unchanged by the
        // promotion, which is the whole claim of "a path is a superset".
        Scope sc;
        sc.lookupMember = [](const std::string &o, const std::string &f, Value &out) {
            if (o == "ring" && f == "w") { out = Value::number(40.0); return true; }
            return false;
        };
        assert(evalOrDie("ring.w * 2", sc) == 80.0);
        std::printf("[PASS] two-segment members still resolve through lookupMember\n");
    }

    void test_three_segments_are_a_path_not_a_truncation()
    {
        const std::map<std::string, double> vals{
            {"gr1.basic.exposure", 0.8}, {"gr1.exposure", 99.0}, {"gr1.basic", 77.0}};
        Scope sc = mapScope(vals);
        // The pre-promotion parser answered 99 here, by overwriting the field.
        assert(evalOrDie("gr1.basic.exposure", sc) == 0.8);
        std::printf("[PASS] gr1.basic.exposure resolves to itself, not to gr1.exposure\n");
    }

    void test_five_segments()
    {
        const std::map<std::string, double> vals{{"s1.mask.0.adjust.exposure", -0.25}};
        Scope sc = mapScope(vals);
        assert(evalOrDie("s1.mask.0.adjust.exposure", sc) == -0.25);
        std::printf("[PASS] a five-segment path resolves\n");
    }

    void test_the_user_example_clamps()
    {
        // interstellar R-BIND-1's own example, which is the reason this library moved.
        const char *expr = "clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)";
        {
            Scope sc = mapScope({{"gr1.basic.exposure", 0.8}});
            const double v = evalOrDie(expr, sc);
            assert(v > 0.8999 && v < 0.9001);
        }
        {   // clamps high
            Scope sc = mapScope({{"gr1.basic.exposure", 4.0}});
            assert(evalOrDie(expr, sc) == 1.0);
        }
        {   // clamps low
            Scope sc = mapScope({{"gr1.basic.exposure", -9.0}});
            assert(evalOrDie(expr, sc) == 0.0);
        }
        std::printf("[PASS] clamp(gr1.basic.exposure / 2 + 0.5, 0, 1) at three exposures\n");
    }

    void test_unknown_path_fails_naming_the_whole_path()
    {
        // It must not resolve a PREFIX and it must not say "unknown field 'gr1.basic'" — the
        // message is what a user fixes the typo from.
        Scope sc = mapScope({{"gr1.basic.exposure", 1.0}});
        std::string err;
        auto ast = parse("gr1.basic.exposer", &err);
        assert(ast && err.empty());
        Value v;
        assert(!evaluate(ast, sc, v, &err));
        assert(err.find("gr1.basic.exposer") != std::string::npos);
        std::printf("[PASS] an unknown path fails naming the whole path: %s\n", err.c_str());
    }

    void test_collect_paths_finds_every_reference()
    {
        std::string err;
        auto ast = parse("gr1.basic.exposure + ring.w * clp_a.geom.scale", &err);
        assert(ast);
        std::vector<std::string> out;
        collectPaths(ast, out);
        assert(out.size() == 3);
        bool deep = false, two = false;
        for (const auto &p : out)
        {
            if (p == "gr1.basic.exposure") deep = true;
            if (p == "ring.w") two = true;
        }
        // One call must find BOTH shapes, or a dependency graph silently misses its own edges.
        assert(deep && two);
        std::printf("[PASS] collectPaths finds deep paths AND two-segment members\n");
    }

    void test_two_segments_fall_through_to_the_path_resolver()
    {
        // Interstellar's addresses are uniformly dotted: `ac_push.value` has two segments and
        // `gr1.basic.exposure` has three, and one resolver must answer both.
        Scope sc = mapScope({{"ac_push.value", 0.5}});   // lookupMember deliberately unset
        assert(evalOrDie("1 + ac_push.value * 0.08", sc) == 1.04);
        std::printf("[PASS] a two-segment reference falls through to lookupPath\n");
    }

    void test_time_scope_reaches_the_host()
    {
        // `t` is listed as a builtin for completion but is NOT resolved in evaluate(): the
        // host supplies the clock, so this library keeps no notion of time.
        Scope sc = mapScope({{"ac.value", 0.5}}, /*t=*/3.0);
        assert(evalOrDie("t * 2 + ac.value", sc) == 6.5);
        const auto &ids = builtinIdents();
        bool hasT = false, hasFrame = false;
        for (const auto &i : ids) { if (i == "t") hasT = true; if (i == "frame") hasFrame = true; }
        assert(hasT && hasFrame);
        std::printf("[PASS] the time scope resolves through lookupIdent and is listed for completion\n");
    }

    void test_fold_leaves_a_path_alone()
    {
        std::string err;
        auto ast = fold(parse("2 * 3 + gr1.basic.exposure", &err));
        assert(ast);
        assert(!isConstant(ast));         // a path is never constant
        Scope sc = mapScope({{"gr1.basic.exposure", 1.0}});
        Value v;
        assert(evaluate(ast, sc, v, &err));
        assert(v.num == 7.0);             // the constant half still folded
        std::printf("[PASS] fold() folds around a path without folding it away\n");
    }
}

int main()
{
    test_two_segment_members_still_work();
    test_three_segments_are_a_path_not_a_truncation();
    test_five_segments();
    test_the_user_example_clamps();
    test_unknown_path_fails_naming_the_whole_path();
    test_collect_paths_finds_every_reference();
    test_two_segments_fall_through_to_the_path_resolver();
    test_time_scope_reaches_the_host();
    test_fold_leaves_a_path_alone();
    std::printf("\nall gene tests passed\n");
    return 0;
}
