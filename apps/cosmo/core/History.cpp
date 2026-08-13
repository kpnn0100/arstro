#include "History.h"
#include <algorithm>
#include <cctype>
#include <numeric>

namespace arstro
{
namespace cosmo
{
    bool paramsEqual(const EditParams &a, const EditParams &b)
    {
        // The text serializer captures every field (scalars, curve, mixer, grade,
        // masks), so string equality is a reliable deep compare.
        return serializeParams(a) == serializeParams(b);
    }

    namespace
    {
        // Did any key/value differ between the same category of two documents?
        bool categoryChanged(const apf::Document &a, const apf::Document &b, const std::string &cat)
        {
            const apf::Category *ca = a.find(cat), *cb = b.find(cat);
            if (!ca && !cb) return false;
            if (!ca || !cb) return true;
            if (ca->values.size() != cb->values.size()) return true;
            for (size_t i = 0; i < ca->values.size(); ++i)
                if (ca->values[i] != cb->values[i]) return true;
            return false;
        }
        std::string titleCase(const std::string &k)
        {
            std::string s = k;
            if (!s.empty()) s[0] = (char)std::toupper((unsigned char)s[0]);
            return s;
        }
    }

    std::string describeChange(const EditParams &from, const EditParams &to)
    {
        // Reuse the preset category mapping so labels line up with the .apf categories.
        const auto cats = apfImageCategories();
        const apf::Document a = editParamsToApf(from, cats, "");
        const apf::Document b = editParamsToApf(to, cats, "");
        std::vector<std::string> changed;
        for (const auto &c : cats)
            if (categoryChanged(a, b, c)) changed.push_back(titleCase(c));
        if (changed.empty()) return "Edit";
        if (changed.size() == 1) return changed[0];
        return "Adjustments";
    }

    void History::init(const EditParams &p)
    {
        nodes.clear();
        HistoryNode root;
        root.params = p;
        root.parent = -1;
        root.label = "Open";
        root.seq = mNextSeq++;
        nodes.push_back(root);
        current = 0;
        mCanCoalesce = false;
        mLastEditMs = -1e30;
    }

    bool History::record(const EditParams &p, double nowMs)
    {
        if (current < 0) { init(p); return true; }
        if (paramsEqual(nodes[current].params, p)) return false;  // no real change (e.g. navigation)

        const bool coalesce = mCanCoalesce && (nowMs - mLastEditMs) < coalesceMs &&
                              nodes[current].kids.empty();  // only extend a fresh leaf
        if (coalesce)
        {
            nodes[current].params = p;  // keep the node's original label (whole-drag = one step)
        }
        else
        {
            HistoryNode n;
            n.params = p;
            n.parent = current;
            n.label = describeChange(nodes[current].params, p);
            n.seq = mNextSeq++;
            const int idx = (int)nodes.size();
            nodes.push_back(n);
            nodes[current].kids.push_back(idx);
            current = idx;
            mCanCoalesce = true;
        }
        mLastEditMs = nowMs;
        prune();
        return true;
    }

    const EditParams *History::undo()
    {
        if (!canUndo()) return nullptr;
        current = nodes[current].parent;
        mCanCoalesce = false;
        return &nodes[current].params;
    }

    const EditParams *History::redo()
    {
        if (!canRedo()) return nullptr;
        // prefer the most recently created child (the newest timeline)
        const auto &kids = nodes[current].kids;
        int best = kids.front();
        for (int k : kids) if (nodes[k].seq > nodes[best].seq) best = k;
        current = best;
        mCanCoalesce = false;
        return &nodes[current].params;
    }

    const EditParams *History::jumpTo(int node)
    {
        if (node < 0 || node >= (int)nodes.size()) return nullptr;
        current = node;
        mCanCoalesce = false;
        return &nodes[current].params;
    }

    void History::restore(std::vector<HistoryNode> loadedNodes, int cur, int steps, double coalMs)
    {
        nodes = std::move(loadedNodes);
        maxSteps = steps > 2 ? steps : 2;
        coalesceMs = coalMs < 0 ? 0 : coalMs;
        int maxSeq = -1;
        for (auto &n : nodes) { n.kids.clear(); if (n.seq > maxSeq) maxSeq = n.seq; }
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            const int p = nodes[i].parent;
            if (p >= 0 && p < (int)nodes.size()) nodes[p].kids.push_back(i);
        }
        current = (cur >= 0 && cur < (int)nodes.size()) ? cur
                                                        : (nodes.empty() ? -1 : (int)nodes.size() - 1);
        mNextSeq = maxSeq + 1;      // don't reuse a persisted seq -> redo preference stays correct
        mCanCoalesce = false;       // a loaded state never coalesces with the next edit
        mLastEditMs = -1e30;
    }

    void History::setLimits(int steps, double coalMs)
    {
        maxSteps = steps > 2 ? steps : 2;
        coalesceMs = coalMs < 0 ? 0 : coalMs;
        prune();
    }

    void History::prune()
    {
        if ((int)nodes.size() <= maxSteps) return;

        // Choose which nodes to keep, in priority order, up to maxSteps:
        //   1. the current node,
        //   2. its ancestors from NEAREST to farthest (drop the oldest first — a long
        //      linear session re-roots at the oldest surviving step),
        //   3. any remaining nodes by recency (newest branches survive over old ones).
        std::vector<char> keep(nodes.size(), 0);
        int kept = 0;
        auto add = [&](int n) { if (n >= 0 && !keep[n] && kept < maxSteps) { keep[n] = 1; ++kept; } };

        add(current);
        for (int n = nodes[current].parent; n >= 0; n = nodes[n].parent) add(n);

        std::vector<int> order(nodes.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&](int x, int y) { return nodes[x].seq > nodes[y].seq; });
        for (int n : order) add(n);

        // Rebuild keeping original order (so parents keep smaller indices than children).
        // A dropped ancestor is skipped: each kept node re-attaches to its NEAREST kept
        // ancestor, or becomes a root (-1) if none survive.
        std::vector<int> remap(nodes.size(), -1);
        std::vector<HistoryNode> out;
        out.reserve(kept);
        for (int i = 0; i < (int)nodes.size(); ++i)
            if (keep[i]) { remap[i] = (int)out.size(); out.push_back(nodes[i]); }
        for (int i = 0; i < (int)nodes.size(); ++i)
        {
            if (!keep[i]) continue;
            int par = nodes[i].parent;
            while (par >= 0 && !keep[par]) par = nodes[par].parent;  // nearest kept ancestor
            out[remap[i]].parent = par >= 0 ? remap[par] : -1;
            out[remap[i]].kids.clear();
        }
        for (int i = 0; i < (int)out.size(); ++i)
            if (out[i].parent >= 0) out[out[i].parent].kids.push_back(i);
        current = remap[current];
        nodes.swap(out);
    }
}
}
