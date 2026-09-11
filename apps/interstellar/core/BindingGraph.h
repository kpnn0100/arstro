/*
 *  interstellar_core — BindingGraph: a parameter driven by a calculation (R-BIND).
 *
 *      bind gr1.opacity = clamp(gr1.basic.exposure / 2 + 0.5, 0, 1)
 *
 *  The language is GENE — `core/Gene`, the one Genesis already had, promoted to a shared
 *  library and extended with dotted paths of any depth plus a time scope (R-BIND-2). It is
 *  aliased, never copied: this repo has already paid once for a forked file.
 *
 *  Three rules, all enforced at EDIT time rather than at render time, because a bad expression
 *  must be refused while the user is looking at it and not on frame 4 800 of 12 000:
 *
 *  * **the graph is a DAG.** A cycle is refused naming both ends. Gene's own `collectPaths`
 *    supplies the edges, so the dependency list is DERIVED and never stored (R-G-3).
 *  * **one authority per parameter.** A parameter that also has an automation link is refused,
 *    naming both. Where a user wants both, the expression reads the shape's output directly
 *    (`ac_push.value`), which writes the precedence down in the project (R-BIND-5).
 *  * **reads are of OWN values**, before the rack composes a node with its ancestors
 *    (R-BIND-6). `.eff` is reserved and deliberately absent.
 */
#pragma once
#include "gene/Gene.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace interstellar
{
    class Project;

    class BindingGraph
    {
    public:
        struct Compiled
        {
            std::string target, expr;
            gene::NodePtr ast;
            std::vector<std::string> deps;   // derived from the AST, never stored in the project
            bool broken = false;
            std::string brokenWhy;
        };

        /** Recompile every `#bind` in the project. False + `err` on the first bad one, which is
         *  what makes a hand-edited file fail loudly at open rather than mid-render. */
        bool rebuild(const Project &p, std::string &err);

        /** Compile and install one binding. Refuses: an unparseable expression, a cycle (naming
         *  both ends), and a target that already has an automation link. `hasLink` is supplied
         *  rather than queried so this class does not depend on Automation. */
        bool set(const Project &p, const std::string &target, const std::string &expr,
                 const std::function<bool(const std::string &)> &hasLink, std::string &err);
        bool remove(const std::string &target);

        /** Topological order, computed on change and not per frame. */
        const std::vector<const Compiled *> &evaluationOrder() const { return mOrder; }
        const std::vector<Compiled> &bindings() const { return mBindings; }
        const Compiled *forTarget(const std::string &target) const;

        /** Mark a binding broken — an unresolvable reference falls back to the static value and
         *  reports ONCE per render, not per frame (R-BIND-8). */
        void markBroken(const std::string &target, const std::string &why);
        void clearBroken();

    private:
        bool topoSort(std::string &err);
        std::vector<Compiled> mBindings;
        std::vector<const Compiled *> mOrder;
    };
}
}
