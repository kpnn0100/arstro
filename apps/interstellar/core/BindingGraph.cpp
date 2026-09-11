#include "BindingGraph.h"
#include "Project.h"
#include <algorithm>
#include <set>

namespace arstro
{
namespace interstellar
{
    const BindingGraph::Compiled *BindingGraph::forTarget(const std::string &target) const
    {
        for (const auto &b : mBindings)
            if (b.target == target) return &b;
        return nullptr;
    }

    void BindingGraph::markBroken(const std::string &target, const std::string &why)
    {
        for (auto &b : mBindings)
            if (b.target == target) { b.broken = true; b.brokenWhy = why; }
    }
    void BindingGraph::clearBroken()
    {
        for (auto &b : mBindings) { b.broken = false; b.brokenWhy.clear(); }
    }

    bool BindingGraph::rebuild(const Project &p, std::string &err)
    {
        mBindings.clear();
        mOrder.clear();
        for (const auto &b : p.bindings)
        {
            Compiled c;
            c.target = b.target;
            c.expr = b.expr;
            std::string perr;
            c.ast = gene::fold(gene::parse(b.expr, &perr));
            if (!c.ast) { err = "bind " + b.target + ": " + perr; return false; }
            gene::collectPaths(c.ast, c.deps);
            mBindings.push_back(std::move(c));
        }
        return topoSort(err);
    }

    bool BindingGraph::set(const Project &p, const std::string &target, const std::string &expr,
                           const std::function<bool(const std::string &)> &hasLink, std::string &err)
    {
        // One authority per parameter, refused rather than resolved by a precedence rule nobody
        // would remember (R-BIND-5).
        if (hasLink && hasLink(target))
        {
            err = "'" + target + "' already has an automation link — a parameter has ONE producer. "
                  "Read the shape from the expression instead (e.g. ac_x.value).";
            return false;
        }
        std::string perr;
        gene::NodePtr ast = gene::fold(gene::parse(expr, &perr));
        if (!ast) { err = "cannot parse: " + perr; return false; }

        Compiled c;
        c.target = target;
        c.expr = expr;
        c.ast = ast;
        gene::collectPaths(ast, c.deps);

        // Install into a COPY, sort it, and only keep it if the sort succeeded — so a refused
        // cycle leaves the graph exactly as it was.
        auto saved = mBindings;
        auto it = std::find_if(mBindings.begin(), mBindings.end(),
                               [&](const Compiled &b) { return b.target == target; });
        if (it == mBindings.end()) mBindings.push_back(std::move(c));
        else *it = std::move(c);

        if (!topoSort(err))
        {
            mBindings = std::move(saved);
            topoSort(perr);
            return false;
        }
        (void)p;
        return true;
    }

    bool BindingGraph::remove(const std::string &target)
    {
        const auto before = mBindings.size();
        mBindings.erase(std::remove_if(mBindings.begin(), mBindings.end(),
                                       [&](const Compiled &b) { return b.target == target; }),
                        mBindings.end());
        std::string err;
        topoSort(err);
        return mBindings.size() != before;
    }

    bool BindingGraph::topoSort(std::string &err)
    {
        mOrder.clear();
        // Edges: a binding depends on any address another binding targets. An address nobody
        // targets is an input, not a node.
        std::map<std::string, const Compiled *> byTarget;
        for (const auto &b : mBindings) byTarget[b.target] = &b;

        enum class Mark { None, Temp, Done };
        std::map<std::string, Mark> mark;
        std::vector<std::string> stack;

        std::function<bool(const Compiled *)> visit = [&](const Compiled *b) -> bool {
            auto &m = mark[b->target];
            if (m == Mark::Done) return true;
            if (m == Mark::Temp)
            {
                // Name the whole cycle: "refused" without the path is not actionable.
                std::string path;
                bool started = false;
                for (const auto &s : stack)
                {
                    if (s == b->target) started = true;
                    if (started) path += s + " -> ";
                }
                err = "cycle " + path + b->target;
                return false;
            }
            m = Mark::Temp;
            stack.push_back(b->target);
            for (const auto &d : b->deps)
            {
                auto it = byTarget.find(d);
                if (it == byTarget.end()) continue;    // an input, not a binding
                if (!visit(it->second)) return false;
            }
            stack.pop_back();
            m = Mark::Done;
            mOrder.push_back(b);
            return true;
        };

        for (const auto &b : mBindings)
            if (!visit(&b)) { mOrder.clear(); return false; }
        return true;
    }
}
}
