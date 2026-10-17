#include "Merge.h"
#include "NodeTable.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>

namespace arstro
{
namespace solaris
{
    namespace
    {
        using namespace nodes;

        // a node's own content — its line, remarks and body; its children are nodes of their own
        std::string contentOf(const Node &n)
        {
            std::string s = nodeLine(n) + "\n";
            for (const auto &r : n.remarks) s += r + "\n";
            for (const auto &b : n.body) s += "  " + b + "\n";
            return s;
        }

        // a node as written, its children with it — what a conflict shows of each side
        std::string linesOf(const Table &t, const std::string &key)
        {
            const auto it = t.nodes.find(key);
            if (it == t.nodes.end()) return std::string();
            std::string out;
            writeNode(t, it->second, "", out);
            return out;
        }

        bool isId(const std::string &k)
        {
            const size_t u = k.rfind('_');
            if (u == std::string::npos || u == 0 || u + 1 >= k.size()) return false;
            for (size_t i = 0; i < u; ++i)
                if (!std::islower((unsigned char)k[i])) return false;
            for (size_t i = u + 1; i < k.size(); ++i)
                if (!std::isdigit((unsigned char)k[i])) return false;
            return true;
        }

        std::map<std::string, std::string> parentsOf(const Table &t)
        {
            std::map<std::string, std::string> p;
            for (const auto &kv : t.nodes)
                for (const auto &k : kv.second.kids) p[k] = kv.first;
            return p;
        }

        // what a node is, said: "strip Bass", "pattern Lead", "rack of ch_2"
        std::string whatOf(const std::string &k, const Table &t)
        {
            const auto it = t.nodes.find(k);
            if (it == t.nodes.end()) return k;
            const Node &n = it->second;
            static const std::map<std::string, std::string> noun = {
                {"atrack", "strip"}, {"amixer", "mixer"}, {"apattern", "pattern"}, {"aclip", "clip"}, {"alane", "lane"}, {"arack", "rack"},
                {"abind", "formula"}, {"aauto", "automation"}, {"asend", "send"}, {"aport", "port"}, {"amarker", "marker"},
                {"atempo", "tempo change"}, {"alanegroup", "lane group"}, {"ascene", "scene"}, {"aslot", "slot"}, {"avideo", "video"},
                {"aversion", "version"}, {"aeffect", "device"}};
            const auto nn = noun.find(n.tag);
            const std::string w = nn != noun.end() ? nn->second : (n.tag.size() > 1 && n.tag[0] == 'a' ? n.tag.substr(1) : n.tag);
            if (const std::string *nm = fieldOf(n.fields, "name"))
                if (const std::string s = unquoted(*nm); !s.empty()) return w + " " + s;
            if (k.rfind("rack:", 0) == 0) return w + " of " + k.substr(5);
            if (k.rfind("bind:", 0) == 0) return w + " on " + k.substr(5);
            return w + " " + k;
        }

        // ── a new id on both sides: mine renamed, every reference to it among my nodes with it ──────────────────────

        bool idChar(char c) { return std::isalnum((unsigned char)c) || c == '_'; }

        // every whole `from` in `v` (a value, a list, an address in a formula) → `to`
        std::string renameIn(const std::string &v, const std::string &from, const std::string &to)
        {
            std::string out;
            size_t i = 0;
            while (i < v.size())
            {
                const size_t at = v.find(from, i);
                if (at == std::string::npos) { out += v.substr(i); break; }
                const bool whole = (at == 0 || !idChar(v[at - 1])) && (at + from.size() >= v.size() || !idChar(v[at + from.size()]));
                out += v.substr(i, at - i);
                out += whole ? to : from;
                i = at + from.size();
            }
            return out;
        }

        bool namesAFile(const std::string &key) // a file's name or a text is never a reference
        {
            return key == "src" || key == "name" || key == "sample" || key == "frozen" || key == "note" || key == "definition";
        }

        void renameFields(Fields &f, const std::string &from, const std::string &to)
        {
            for (auto &x : f)
                if (!namesAFile(x.first)) x.second = renameIn(x.second, from, to);
        }

        std::string renameLine(const std::string &line, const std::string &from, const std::string &to)
        {
            const std::string t = trimmed(line);
            if (t.empty() || t[0] != '#') return line;
            Node n;
            splitLine(t, n.tag, n.fields, n.comment);
            renameFields(n.fields, from, to);
            return std::string(indentOf(line), ' ') + nodeLine(n);
        }

        void renameNode(Table &t, const std::string &from, const std::string &to)
        {
            std::map<std::string, std::string> rekey; // the keys that carry it: its own, a rack's, a formula's
            for (const auto &kv : t.nodes)
            {
                const std::string &k = kv.first;
                std::string nk = k == from ? to : k;
                if (k.rfind("rack:", 0) == 0 || k.rfind("bind:", 0) == 0) nk = k.substr(0, 5) + renameIn(k.substr(5), from, to);
                if (nk != k) rekey[k] = nk;
            }
            for (auto &kv : t.nodes)
            {
                renameFields(kv.second.fields, from, to); // its own `id`, the header's references too
                for (auto &b : kv.second.body) b = renameLine(b, from, to);
            }
            for (const auto &rk : rekey)
            {
                Node n = t.nodes[rk.first];
                n.key = rk.second;
                t.nodes.erase(rk.first);
                t.nodes[rk.second] = n;
            }
            auto fix = [&](std::vector<std::string> &v) {
                for (auto &k : v)
                    if (const auto it = rekey.find(k); it != rekey.end()) k = it->second;
            };
            fix(t.top);
            for (auto &kv : t.nodes) fix(kv.second.kids);
        }

        std::string nextFree(const std::string &id, std::set<std::string> &taken)
        {
            const std::string prefix = id.substr(0, id.rfind('_') + 1);
            int n = 0;
            for (const auto &k : taken)
                if (k.rfind(prefix, 0) == 0 && isId(k)) n = std::max(n, std::atoi(k.c_str() + prefix.size()));
            const std::string out = prefix + std::to_string(n + 1);
            taken.insert(out);
            return out;
        }

        // ── deciding ─────────────────────────────────────────────────────────────────────────────────────────────

        struct Side
        {
            bool has = false;
            std::string v;
        };
        enum class Pick { Same, Mine, Theirs, Conflict };

        Pick decide(const Side &b, const Side &m, const Side &t, std::string &how)
        {
            if (!b.has)
            {
                if (m.has && t.has)
                {
                    if (m.v == t.v) return Pick::Same;
                    how = "both added";
                    return Pick::Conflict;
                }
                return m.has ? Pick::Mine : t.has ? Pick::Theirs : Pick::Same;
            }
            if (m.has && t.has)
            {
                if (m.v == t.v) return Pick::Same;
                if (m.v == b.v) return Pick::Theirs;
                if (t.v == b.v) return Pick::Mine;
                how = "both changed";
                return Pick::Conflict;
            }
            if (m.has)
            {
                if (m.v == b.v) return Pick::Theirs; // removed there, untouched here
                how = "changed here, removed there";
                return Pick::Conflict;
            }
            if (t.has)
            {
                if (t.v == b.v) return Pick::Mine;   // removed here, untouched there
                how = "removed here, changed there";
                return Pick::Conflict;
            }
            return Pick::Same;
        }

        // an order three ways: the side that changed it (theirs when both did), the other side's own after their neighbours
        std::vector<std::string> mergeOrder(const std::vector<std::string> &b, const std::vector<std::string> &m, const std::vector<std::string> &t,
                                            const std::function<bool(const std::string &)> &present)
        {
            const bool mineLeads = t == b && m != b;
            const std::vector<std::string> &lead = mineLeads ? m : t, &other = mineLeads ? t : m;
            std::vector<std::string> out;
            for (const auto &k : lead)
                if (present(k)) out.push_back(k);
            for (size_t i = 0; i < other.size(); ++i)
            {
                const std::string &k = other[i];
                if (!present(k) || std::find(out.begin(), out.end(), k) != out.end()) continue;
                size_t at = 0;
                for (size_t j = i; j-- > 0;)
                    if (const auto it = std::find(out.begin(), out.end(), other[j]); it != out.end())
                    {
                        at = (size_t)(it - out.begin()) + 1;
                        break;
                    }
                out.insert(out.begin() + (long)at, k);
            }
            return out;
        }
    }

    MergeResult mergeSongs(const std::string &base, const std::string &mine, const std::string &theirs)
    {
        MergeResult r;
        const Table B = readTable(base), T = readTable(theirs);
        Table M = readTable(mine);
        // 1. the same id added on both sides as different nodes: mine is given the next free one
        std::set<std::string> taken;
        for (const Table *x : {&B, (const Table *)&M, &T})
            for (const auto &kv : x->nodes)
                if (isId(kv.first)) taken.insert(kv.first);
        std::vector<std::string> clashes;
        for (const auto &kv : M.nodes)
            if (isId(kv.first) && !B.nodes.count(kv.first) && T.nodes.count(kv.first) && linesOf(M, kv.first) != linesOf(T, kv.first))
                clashes.push_back(kv.first);
        for (const auto &id : clashes)
        {
            const std::string to = nextFree(id, taken);
            renameNode(M, id, to);
            r.renamed.push_back({id, to});
        }
        auto count = [&r](Pick p, const Side &m, const Side &t) {
            if (p == Pick::Theirs && (m.has != t.has || m.v != t.v)) ++r.fromTheirs;
            if (p == Pick::Mine && (m.has != t.has || m.v != t.v)) ++r.fromMine;
        };
        // 2. the header, field by field
        Table R;
        Node &hdr = R.nodes["project"];
        hdr.key = "project";
        {
            const Node &hb = B.nodes.at("project"), &hm = M.nodes.at("project"), &ht = T.nodes.at("project");
            auto keysOf = [](const Node &n) {
                std::vector<std::string> k;
                for (const auto &f : n.fields) k.push_back(f.first);
                return k;
            };
            auto side = [](const Node &n, const std::string &k) {
                Side s;
                if (const std::string *v = fieldOf(n.fields, k)) { s.has = true; s.v = *v; }
                return s;
            };
            const auto any = [](const std::string &) { return true; };
            for (const auto &k : mergeOrder(keysOf(hb), keysOf(hm), keysOf(ht), any))
            {
                const Side b = side(hb, k), m = side(hm, k), t = side(ht, k);
                std::string how;
                const Pick p = decide(b, m, t, how);
                count(p, m, t);
                const Side &take = p == Pick::Theirs ? t : m;
                if (take.has) hdr.fields.emplace_back(k, take.v);
                if (p == Pick::Conflict)
                    r.conflicts.push_back({"project:" + k, "the song's " + k, how, m.has ? k + " = " + m.v : std::string(), t.has ? k + " = " + t.v : std::string()});
            }
            hdr.remarks = hm.remarks; // whole comment lines in the header: kept as written here
        }
        // 3. the nodes
        std::set<std::string> keys, present;
        std::map<std::string, const Node *> chosen;
        for (const Table *x : {&B, (const Table *)&M, &T})
            for (const auto &kv : x->nodes)
                if (kv.first != "project") keys.insert(kv.first);
        std::set<std::string> conflicted;
        for (const auto &k : keys)
        {
            auto side = [&k](const Table &t) {
                Side s;
                if (const auto it = t.nodes.find(k); it != t.nodes.end()) { s.has = true; s.v = contentOf(it->second); }
                return s;
            };
            const Side b = side(B), m = side(M), t = side(T);
            std::string how;
            const Pick p = decide(b, m, t, how);
            count(p, m, t);
            const Table &from = p == Pick::Theirs ? T : M; // the same, mine, or a conflict kept as mine
            if (const auto it = from.nodes.find(k); it != from.nodes.end())
            {
                present.insert(k);
                chosen[k] = &it->second;
            }
            if (p == Pick::Conflict)
            {
                r.conflicts.push_back({k, whatOf(k, m.has ? M : T), how, linesOf(M, k), linesOf(T, k)});
                conflicted.insert(k);
            }
        }
        // a part kept whose whole is gone (a device changed here, its rack removed there): the whole stays, and is asked
        const auto pm = parentsOf(M), pt = parentsOf(T);
        for (const auto &k : std::vector<std::string>(present.begin(), present.end()))
        {
            auto parentOf = [&](const std::string &x) {
                if (const auto it = pm.find(x); it != pm.end()) return it->second;
                if (const auto it = pt.find(x); it != pt.end()) return it->second;
                return std::string();
            };
            for (std::string p = parentOf(k); !p.empty() && !present.count(p); p = parentOf(p))
            {
                const Table &src = M.nodes.count(p) ? M : T;
                present.insert(p);
                chosen[p] = &src.nodes.at(p);
                if (!conflicted.count(p))
                {
                    r.conflicts.push_back({p, whatOf(p, src), "changed here, removed there", linesOf(M, p), linesOf(T, p)});
                    conflicted.insert(p);
                }
            }
        }
        for (const auto &k : present) R.nodes[k] = *chosen[k];
        const auto isPresent = [&present](const std::string &k) { return k == "project" || present.count(k) > 0; };
        R.top = mergeOrder(B.top, M.top, T.top, isPresent);
        static const std::vector<std::string> none;
        auto kidsOf = [](const Table &t, const std::string &k) -> const std::vector<std::string> & {
            const auto it = t.nodes.find(k);
            return it == t.nodes.end() ? none : it->second.kids;
        };
        for (const auto &k : present) R.nodes[k].kids = mergeOrder(kidsOf(B, k), kidsOf(M, k), kidsOf(T, k), isPresent);
        r.text = writeTable(R);
        return r;
    }

    bool takeTheirs(std::string &text, const std::string &theirs, const std::string &node, std::string &err)
    {
        Table X = readTable(text);
        const Table T = readTable(theirs);
        if (node.rfind("project:", 0) == 0) // a header field
        {
            const std::string key = node.substr(8);
            Node &hx = X.nodes["project"];
            const std::string *tv = fieldOf(T.nodes.at("project").fields, key);
            const auto it = std::find_if(hx.fields.begin(), hx.fields.end(), [&key](const std::pair<std::string, std::string> &f) { return f.first == key; });
            if (tv && it != hx.fields.end()) it->second = *tv;
            else if (tv) hx.fields.emplace_back(key, *tv);
            else if (it != hx.fields.end()) hx.fields.erase(it);
            text = writeTable(X);
            return true;
        }
        if (!T.nodes.count(node)) // theirs has none: gone
        {
            eraseNode(X, node);
            text = writeTable(X);
            return true;
        }
        // theirs' node and its children, where mine had it — else after its neighbour in theirs
        const auto pt = parentsOf(T);
        const std::string parent = pt.count(node) ? pt.at(node) : std::string();
        if (!parent.empty() && !X.nodes.count(parent))
        {
            err = "its " + whatOf(parent, T) + " is not in the song — take that first";
            return false;
        }
        std::vector<std::string> &seqX = parent.empty() ? X.top : X.nodes[parent].kids;
        const std::vector<std::string> &seqT = parent.empty() ? T.top : T.nodes.at(parent).kids;
        long at = -1;
        if (const auto ix = std::find(seqX.begin(), seqX.end(), node); ix != seqX.end()) at = ix - seqX.begin();
        eraseNode(X, node);
        if (at < 0)
        {
            at = 0;
            const auto it = std::find(seqT.begin(), seqT.end(), node);
            for (auto j = it; j != seqT.begin();)
            {
                --j;
                if (const auto f = std::find(seqX.begin(), seqX.end(), *j); f != seqX.end())
                {
                    at = (f - seqX.begin()) + 1;
                    break;
                }
            }
        }
        at = std::min<long>(at, (long)seqX.size());
        seqX.insert(seqX.begin() + at, node);
        std::function<void(const std::string &)> copy = [&](const std::string &k) {
            X.nodes[k] = T.nodes.at(k);
            for (const auto &c : T.nodes.at(k).kids) copy(c);
        };
        copy(node);
        text = writeTable(X);
        return true;
    }
}
}
