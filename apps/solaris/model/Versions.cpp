#include "Versions.h"
#include "Format.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <sstream>

namespace arstro
{
namespace solaris
{
    namespace
    {
        // ── the song as nodes ───────────────────────────────────────────────────────────────────────────────────────
        // Read from canonical text, values kept exactly as written (quotes and all), so a node written back is the
        // node read.

        struct Node
        {
            std::string key, tag;
            Fields fields;                  // raw values; a bare token has an empty value
            std::string comment;            // an inline comment — kept, never versioned
            std::vector<std::string> remarks; // whole comment lines after it — kept, never versioned
            std::vector<std::string> body;  // its id-less lines, indented as written less its children's indent
            std::vector<std::string> kids;  // its child nodes (a rack's devices), in order
        };

        struct Table
        {
            std::vector<std::string> top;   // top-level keys in order; "project" (the header) first
            std::map<std::string, Node> nodes;
        };

        std::string trimmed(const std::string &s)
        {
            const auto a = s.find_first_not_of(" \t\r");
            if (a == std::string::npos) return std::string();
            return s.substr(a, s.find_last_not_of(" \t\r") - a + 1);
        }

        std::string unquoted(const std::string &v)
        {
            return (v.size() >= 2 && v.front() == '"' && v.back() == '"') ? v.substr(1, v.size() - 2) : v;
        }

        // `#tag k=v k="a b" bare  ; comment` → its tag, raw fields and comment
        void splitLine(const std::string &t, std::string &tag, Fields &fields, std::string &comment)
        {
            std::vector<std::string> toks;
            std::string cur;
            bool q = false, have = false;
            size_t i = 0;
            for (; i < t.size(); ++i)
            {
                const char c = t[i];
                if (c == '"') { q = !q; have = true; cur += c; continue; }
                if (!q && c == ';') break;
                if (!q && (c == ' ' || c == '\t'))
                {
                    if (have) { toks.push_back(cur); cur.clear(); have = false; }
                    continue;
                }
                cur += c;
                have = true;
            }
            if (have) toks.push_back(cur);
            if (i < t.size()) comment = trimmed(t.substr(i + 1));
            for (size_t k = 0; k < toks.size(); ++k)
            {
                if (k == 0 && toks[k][0] == '#') { tag = toks[k].substr(1); continue; }
                const auto eq = toks[k].find('=');
                if (eq == std::string::npos || eq == 0) fields.emplace_back(toks[k], std::string());
                else fields.emplace_back(toks[k].substr(0, eq), toks[k].substr(eq + 1));
            }
        }

        const std::string *fieldOf(const Fields &f, const std::string &k)
        {
            for (const auto &x : f)
                if (x.first == k) return &x.second;
            return nullptr;
        }

        // A node's name in a version: its id; a rack by its strip; a formula by its address; else the line itself.
        std::string keyOf(const std::string &tag, const Fields &f, const std::string &line)
        {
            if (const std::string *id = fieldOf(f, "id")) return unquoted(*id);
            if (tag == "arack") if (const std::string *t = fieldOf(f, "track")) return "rack:" + unquoted(*t);
            if (tag == "abind") if (const std::string *a = fieldOf(f, "address")) return "bind:" + unquoted(*a);
            return "raw:" + line;
        }

        std::string nodeLine(const Node &n)
        {
            std::string s = "#" + n.tag;
            for (const auto &f : n.fields) s += " " + f.first + (f.second.empty() ? std::string() : "=" + f.second);
            if (!n.comment.empty()) s += "  ; " + n.comment;
            return s;
        }

        std::string headerLine(const std::string &key, const std::string &value)
        {
            std::string k = key;
            if (k.size() < 11) k.append(11 - k.size(), ' ');
            else k += " ";
            return k + "= " + value + "\n";
        }

        size_t indentOf(const std::string &l)
        {
            size_t i = 0;
            while (i < l.size() && l[i] == ' ') ++i;
            return i;
        }

        Table readTable(const std::string &text)
        {
            Table t;
            Node hdr;
            hdr.key = "project";
            t.top.push_back("project");
            std::string top, kid;
            bool inHeader = true;
            std::istringstream in(text);
            std::string line;
            while (std::getline(in, line))
            {
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::string c = trimmed(line);
                if (c.empty()) continue;
                const size_t ind = indentOf(line);
                if (inHeader && ind == 0 && c[0] != '#')
                {
                    if (c[0] == ';') hdr.remarks.push_back(c);
                    else
                    {
                        const auto eq = c.find('=');
                        if (eq != std::string::npos) hdr.fields.emplace_back(trimmed(c.substr(0, eq)), trimmed(c.substr(eq + 1)));
                    }
                    continue;
                }
                inHeader = false;
                if (ind == 0 && c[0] == '#')
                {
                    Node n;
                    splitLine(c, n.tag, n.fields, n.comment);
                    n.key = keyOf(n.tag, n.fields, c);
                    while (t.nodes.count(n.key)) n.key += "'"; // an id-less raw node written twice: two nodes still
                    top = n.key;
                    kid.clear();
                    t.top.push_back(n.key);
                    t.nodes[n.key] = n;
                    continue;
                }
                if (top.empty()) continue;
                if (ind == 2 && c[0] == '#' && c.find(" id=") != std::string::npos)
                {
                    Node n;
                    splitLine(c, n.tag, n.fields, n.comment);
                    n.key = keyOf(n.tag, n.fields, c);
                    t.nodes[top].kids.push_back(n.key);
                    kid = n.key;
                    t.nodes[n.key] = n;
                    continue;
                }
                // a comment at a node's own indent is its remark; deeper lines are its body
                if (!kid.empty() && ind == 2 && c[0] == ';') t.nodes[kid].remarks.push_back(c);
                else if (!kid.empty() && ind >= 4) t.nodes[kid].body.push_back(line.substr(4));
                else if (ind == 0) t.nodes[top].remarks.push_back(c);
                else t.nodes[top].body.push_back(line.substr(2));
            }
            t.nodes["project"] = hdr;
            return t;
        }

        void writeNode(const Table &t, const Node &n, const std::string &indent, std::string &out)
        {
            out += indent + nodeLine(n) + "\n";
            for (const auto &r : n.remarks) out += indent + r + "\n";
            for (const auto &b : n.body) out += indent + "  " + b + "\n";
            for (const auto &k : n.kids)
            {
                const auto it = t.nodes.find(k);
                if (it != t.nodes.end()) writeNode(t, it->second, indent + "  ", out);
            }
        }

        std::string writeTable(const Table &t)
        {
            std::string out;
            const Node &h = t.nodes.at("project");
            for (const auto &f : h.fields) out += headerLine(f.first, f.second);
            for (const auto &r : h.remarks) out += r + "\n";
            for (const auto &k : t.top)
                if (k != "project") writeNode(t, t.nodes.at(k), "", out);
            return out;
        }

        void eraseNode(Table &t, const std::string &key)
        {
            const auto it = t.nodes.find(key);
            if (it == t.nodes.end()) return;
            const std::vector<std::string> kids = it->second.kids;
            t.nodes.erase(it);
            for (const auto &k : kids) eraseNode(t, k);
            t.top.erase(std::remove(t.top.begin(), t.top.end(), key), t.top.end());
            for (auto &n : t.nodes) n.second.kids.erase(std::remove(n.second.kids.begin(), n.second.kids.end(), key), n.second.kids.end());
        }

        // ── a version's lines ──────────────────────────────────────────────────────────────────────────────────────

        struct Delta
        {
            std::string op, node;
            Fields fields;                  // without `node`
            std::vector<std::string> body;  // #vbody's lines
            std::vector<std::string> raw;   // as stored: its line, then its body indented
        };

        struct Parsed
        {
            std::vector<Delta> deltas;
            std::string own;                // its own nodes, canonical text
            std::vector<std::string> ownLines;
            std::vector<std::string> snapshot;
        };

        bool isDelta(const std::string &tag)
        {
            return tag == "vset" || tag == "vunset" || tag == "vdrop" || tag == "vbody" || tag == "vorder";
        }

        Parsed readVersion(const Version &v)
        {
            Parsed p;
            enum { None, Body, Own, Snapshot } mode = None;
            for (const auto &line : v.lines)
            {
                const size_t ind = indentOf(line);
                if (mode == Snapshot) { p.snapshot.push_back(line.substr(std::min<size_t>(ind, 2))); continue; }
                const std::string c = trimmed(line);
                if (c.empty()) continue;
                if (ind == 0 && c[0] == '#')
                {
                    std::string tag, comment;
                    Fields f;
                    splitLine(c, tag, f, comment);
                    if (tag == "vsnapshot") { mode = Snapshot; continue; }
                    if (isDelta(tag))
                    {
                        Delta d;
                        d.op = tag;
                        for (const auto &x : f)
                        {
                            if (x.first == "node") d.node = unquoted(x.second);
                            else d.fields.push_back(x);
                        }
                        d.raw.push_back(c);
                        p.deltas.push_back(d);
                        mode = tag == "vbody" ? Body : None;
                        continue;
                    }
                    mode = Own;
                }
                if (mode == Body && ind >= 2)
                {
                    p.deltas.back().body.push_back(line.substr(2));
                    p.deltas.back().raw.push_back(line);
                }
                else if (mode == Own)
                {
                    p.own += line + "\n";
                    p.ownLines.push_back(line);
                }
            }
            return p;
        }

        std::vector<std::string> splitList(const std::string &s)
        {
            std::vector<std::string> out;
            std::string cur;
            for (char c : unquoted(s))
            {
                if (c == ',') { if (!cur.empty()) out.push_back(cur); cur.clear(); }
                else cur += c;
            }
            if (!cur.empty()) out.push_back(cur);
            return out;
        }

        // The base's song with a version's deltas on top.
        Table applyVersion(Table r, const Parsed &v, std::vector<std::string> *dangling)
        {
            auto miss = [&](const Delta &d) {
                if (dangling) dangling->push_back(d.raw.front());
            };
            // the base's nodes: dropped, their fields overridden, their bodies replaced
            for (const auto &d : v.deltas)
            {
                if (d.op == "vorder") continue;
                const auto it = r.nodes.find(d.node);
                if (it == r.nodes.end()) { miss(d); continue; }
                Node &n = it->second;
                if (d.op == "vdrop") eraseNode(r, d.node);
                else if (d.op == "vset")
                {
                    for (const auto &f : d.fields)
                    {
                        bool done = false;
                        for (auto &x : n.fields)
                            if (x.first == f.first) { x.second = f.second; done = true; }
                        if (!done) n.fields.push_back(f);
                    }
                }
                else if (d.op == "vunset")
                {
                    for (const auto &f : d.fields)
                        n.fields.erase(std::remove_if(n.fields.begin(), n.fields.end(), [&](const auto &x) { return x.first == f.first; }),
                                       n.fields.end());
                }
                else if (d.op == "vbody") n.body = d.body;
            }
            // its own nodes — a child (a device in a rack) placed by the order that lists it, the rest at the end
            std::set<std::string> listed;
            for (const auto &d : v.deltas)
                if (d.op == "vorder")
                    if (const std::string *c = fieldOf(d.fields, "children"))
                        for (const auto &k : splitList(*c)) listed.insert(k);
            const Table own = readTable(v.own);
            std::function<void(const std::string &)> take = [&](const std::string &k) {
                const auto existing = r.nodes.find(k);
                if (existing != r.nodes.end()) // the version's own wins where one of the base meets it (a rack both made)
                    for (const auto &kid : std::vector<std::string>(existing->second.kids)) eraseNode(r, kid);
                r.nodes[k] = own.nodes.at(k);
                for (const auto &kid : own.nodes.at(k).kids) take(kid);
            };
            for (const auto &k : own.top)
            {
                if (k == "project") continue;
                const bool there = r.nodes.count(k) > 0;
                take(k);
                if (!there && !listed.count(k)) r.top.push_back(k);
                if (listed.count(k)) r.top.erase(std::remove(r.top.begin(), r.top.end(), k), r.top.end());
            }
            // which children, in what order: a base child no order lists any more is not in this version
            std::set<std::string> ordered;
            for (const auto &d : v.deltas)
            {
                if (d.op != "vorder") continue;
                const auto it = r.nodes.find(d.node);
                if (it == r.nodes.end()) { miss(d); continue; }
                ordered.insert(d.node);
            }
            std::vector<std::string> unlisted;
            for (const auto &k : ordered)
                for (const auto &kid : r.nodes.at(k).kids)
                    if (!listed.count(kid)) unlisted.push_back(kid);
            for (const auto &kid : unlisted) eraseNode(r, kid);
            for (const auto &d : v.deltas)
            {
                if (d.op != "vorder" || !ordered.count(d.node)) continue;
                std::vector<std::string> kids;
                if (const std::string *c = fieldOf(d.fields, "children"))
                    for (const auto &k : splitList(*c))
                        if (r.nodes.count(k)) kids.push_back(k);
                r.nodes.at(d.node).kids = kids;
            }
            return r;
        }

        // What `e` changes of `b`: the deltas in `b`'s order, and the nodes of `e`'s own.
        void diffTables(const Table &b, const Table &e, std::vector<std::string> &out, std::vector<std::string> &mine)
        {
            auto name = [](const std::string &k) { return quoteIfNeeded(k); };
            std::function<void(const std::string &)> walk = [&](const std::string &k) {
                const Node &bn = b.nodes.at(k);
                const auto it = e.nodes.find(k);
                if (it == e.nodes.end() || it->second.tag != bn.tag) { out.push_back("#vdrop node=" + name(k)); return; }
                const Node &en = it->second;
                std::string set, unset;
                for (const auto &f : en.fields)
                {
                    const std::string *was = fieldOf(bn.fields, f.first);
                    if (!was || *was != f.second) set += " " + f.first + (f.second.empty() ? std::string() : "=" + f.second);
                }
                for (const auto &f : bn.fields)
                    if (!fieldOf(en.fields, f.first)) unset += " " + f.first;
                if (!set.empty()) out.push_back("#vset node=" + name(k) + set);
                if (!unset.empty()) out.push_back("#vunset node=" + name(k) + unset);
                if (bn.body != en.body)
                {
                    out.push_back("#vbody node=" + name(k));
                    for (const auto &l : en.body) out.push_back("  " + l);
                }
                if (bn.kids != en.kids)
                {
                    std::string list;
                    for (const auto &c : en.kids) list += (list.empty() ? "" : ",") + c;
                    out.push_back("#vorder node=" + name(k) + " children=" + (list.empty() ? std::string("\"\"") : list));
                }
                for (const auto &kid : bn.kids) walk(kid);
            };
            for (const auto &k : b.top) walk(k);
            auto ownNode = [&](const Node &n) {
                std::string text;
                writeNode(e, n, "", text);
                std::istringstream in(text);
                std::string l;
                while (std::getline(in, l)) mine.push_back(l);
            };
            for (const auto &k : e.top)
            {
                const Node &en = e.nodes.at(k);
                const auto bt = b.nodes.find(k);
                if (bt == b.nodes.end() || bt->second.tag != en.tag) { ownNode(en); continue; }
                std::function<void(const Node &)> kids = [&](const Node &n) {
                    for (const auto &kid : n.kids)
                    {
                        const auto kb = b.nodes.find(kid);
                        if (kb == b.nodes.end() || kb->second.tag != e.nodes.at(kid).tag) ownNode(e.nodes.at(kid));
                        else kids(e.nodes.at(kid));
                    }
                };
                kids(en);
            }
        }

        bool resolveText(const Project &p, const std::string &id, std::string &text, std::string &err,
                         std::vector<std::string> *dangling, size_t depth)
        {
            if (id.empty() || id == "main") { text = mainTextOf(p); return true; }
            const Version *v = p.versionOf(id);
            if (!v)
            {
                std::string names = "main";
                for (const auto &x : p.versions) names += ", " + x.name;
                err = "no version `" + id + "` — the song has " + names;
                return false;
            }
            if (depth > p.versions.size()) { err = "version " + v->name + "'s bases loop"; return false; }
            const Parsed pv = readVersion(*v);
            if (v->frozen)
            {
                text.clear();
                for (const auto &l : pv.snapshot) text += l + "\n";
                return true;
            }
            std::string base;
            if (!resolveText(p, v->base, base, err, nullptr, depth + 1)) return false;
            text = writeTable(applyVersion(readTable(base), pv, dangling));
            return true;
        }

        std::vector<std::string> linesOf(const Parsed &pv, bool keep(const Delta &))
        {
            std::vector<std::string> out;
            for (const auto &d : pv.deltas)
                if (keep(d)) out.insert(out.end(), d.raw.begin(), d.raw.end());
            out.insert(out.end(), pv.ownLines.begin(), pv.ownLines.end());
            return out;
        }

        std::vector<std::string> withoutBlanks(const std::string &text)
        {
            std::vector<std::string> out;
            std::istringstream in(text);
            std::string l;
            while (std::getline(in, l))
                if (!trimmed(l).empty()) out.push_back(l);
            return out;
        }

        // Its deltas and own nodes, then (frozen) its snapshot.
        void store(Version &v, const std::vector<std::string> &deltas, const std::vector<std::string> *snapshot)
        {
            v.lines = deltas;
            v.frozen = snapshot != nullptr;
            if (!snapshot) return;
            v.lines.push_back("#vsnapshot");
            for (const auto &l : *snapshot) v.lines.push_back("  " + l);
        }

        // The open version's nodes again, from its (changed) deltas.
        bool reopen(Project &p, std::string &err)
        {
            if (p.version.empty()) return true;
            const std::string id = p.version;
            return openVersion(p, id, err);
        }
    }

    std::string mainTextOf(const Project &p)
    {
        return p.version.empty() ? songText(p) : p.mainText;
    }

    bool resolveVersion(const Project &p, const std::string &id, std::string &text, std::string &err, std::vector<std::string> *dangling)
    {
        return resolveText(p, id, text, err, dangling, 0);
    }

    VersionFacts versionFacts(const Project &p, const std::string &id)
    {
        VersionFacts f;
        const Version *v = p.versionOf(id);
        if (!v) return f;
        const Parsed pv = readVersion(*v);
        f.deltas = (int)pv.deltas.size();
        f.own = (int)readTable(pv.own).top.size() - 1;
        std::string text, err;
        std::vector<std::string> dangling;
        if (!resolveVersion(p, id, text, err, &dangling)) { f.problem = err; return f; }
        f.dangling = (int)dangling.size();
        Project q;
        if (!parseProject(text, q, err)) f.problem = err;
        return f;
    }

    bool openVersion(Project &p, const std::string &id, std::string &err)
    {
        const std::string main = mainTextOf(p);
        std::string vid;
        if (id.empty() || id == "main")
        {
            if (p.version.empty()) return true;
        }
        else
        {
            const Version *v = p.versionOf(id);
            if (!v)
            {
                std::string text;
                return resolveVersion(p, id, text, err); // names the versions there are
            }
            vid = v->id;
        }
        std::string text;
        if (!resolveVersion(p, vid, text, err)) return false;
        Project q;
        if (!parseProject(text, q, err))
        {
            err = (vid.empty() ? std::string("main") : "version " + p.versionOf(vid)->name) + " does not stand as a song: " + err;
            return false;
        }
        q.versions = std::move(p.versions);
        q.version = vid;
        q.mainText = vid.empty() ? std::string() : main;
        p = std::move(q);
        return true;
    }

    void recordVersion(Project &p)
    {
        Version *v = p.version.empty() ? nullptr : p.versionOf(p.version);
        if (!v || v->frozen) return;
        std::string base, err;
        if (!resolveVersion(p, v->base, base, err)) return;
        const Table b = readTable(base);
        std::vector<std::string> lines, mine;
        diffTables(b, readTable(songText(p)), lines, mine);
        // a delta whose node the base does not have stays — dangling, until a rebase prunes it, never dropped unsaid
        for (const auto &d : readVersion(*v).deltas)
            if (!b.nodes.count(d.node)) lines.insert(lines.end(), d.raw.begin(), d.raw.end());
        lines.insert(lines.end(), mine.begin(), mine.end());
        store(*v, lines, nullptr);
    }

    std::string newVersion(Project &p, const std::string &name, const std::string &base, std::string &err)
    {
        std::string b;
        if (!base.empty() && base != "main")
        {
            const Version *bv = p.versionOf(base);
            if (!bv)
            {
                std::string text;
                resolveVersion(p, base, text, err);
                return std::string();
            }
            b = bv->id;
        }
        Version v;
        v.id = p.nextId("vr");
        v.name = name;
        v.base = b;
        p.versions.push_back(v);
        return v.id;
    }

    bool freezeVersion(Project &p, const std::string &id, std::string &err)
    {
        Version *v = p.versionOf(id);
        if (!v) { std::string t; return resolveVersion(p, id, t, err); }
        if (v->frozen) { err = "version " + v->name + " is already frozen"; return false; }
        std::string text;
        if (!resolveVersion(p, v->id, text, err)) return false;
        Project q;
        if (!parseProject(text, q, err)) { err = "version " + v->name + " does not stand as a song: " + err; return false; }
        const std::vector<std::string> snap = withoutBlanks(songText(q));
        store(*v, v->lines, &snap);
        return true;
    }

    bool thawVersion(Project &p, const std::string &id, std::string &err)
    {
        Version *v = p.versionOf(id);
        if (!v) { std::string t; return resolveVersion(p, id, t, err); }
        if (!v->frozen) { err = "version " + v->name + " is not frozen"; return false; }
        const Parsed pv = readVersion(*v);
        store(*v, linesOf(pv, [](const Delta &) { return true; }), nullptr);
        return v->id == p.version ? reopen(p, err) : true;
    }

    bool rebaseVersion(Project &p, const std::string &id, bool dryRun, std::vector<std::string> &report, std::string &err)
    {
        Version *v = p.versionOf(id);
        if (!v) { std::string t; return resolveVersion(p, id, t, err); }
        const Parsed pv = readVersion(*v);
        std::string base;
        if (!resolveVersion(p, v->base, base, err)) return false;
        const Table b = readTable(base);
        std::vector<std::string> dangling;
        const Table live = applyVersion(b, pv, &dangling);
        for (const auto &d : dangling) report.push_back("dangling: " + d);
        if (v->frozen)
        {
            Project q;
            if (!parseProject(writeTable(live), q, err)) { err = "version " + v->name + " does not stand on its base now: " + err; return false; }
            report.push_back("its snapshot advanced to " + (v->base.empty() ? std::string("main") : p.versionOf(v->base)->name) + " as it is now");
            if (dryRun) return true;
            const std::vector<std::string> snap = withoutBlanks(songText(q));
            std::vector<std::string> deltas;
            for (const auto &d : pv.deltas)
                if (b.nodes.count(d.node)) deltas.insert(deltas.end(), d.raw.begin(), d.raw.end());
            deltas.insert(deltas.end(), pv.ownLines.begin(), pv.ownLines.end());
            store(*v, deltas, &snap);
            return v->id == p.version ? reopen(p, err) : true;
        }
        if (dryRun) return true;
        std::vector<std::string> deltas;
        for (const auto &d : pv.deltas)
            if (b.nodes.count(d.node)) deltas.insert(deltas.end(), d.raw.begin(), d.raw.end());
        deltas.insert(deltas.end(), pv.ownLines.begin(), pv.ownLines.end());
        store(*v, deltas, nullptr);
        return true;
    }

    bool deleteVersion(Project &p, const std::string &id, std::string &err)
    {
        const Version *v = p.versionOf(id);
        if (!v) { std::string t; return resolveVersion(p, id, t, err); }
        std::string users;
        for (const auto &x : p.versions)
            if (x.base == v->id) users += (users.empty() ? "" : ", ") + x.name;
        if (!users.empty()) { err = "version " + v->name + " is the base of " + users + " — delete or rebase those first"; return false; }
        const std::string vid = v->id, base = v->base;
        if (p.version == vid && !openVersion(p, base.empty() ? "main" : base, err)) return false;
        p.versions.erase(std::remove_if(p.versions.begin(), p.versions.end(), [&](const Version &x) { return x.id == vid; }), p.versions.end());
        return true;
    }

    bool revertVersion(Project &p, const std::string &node, const std::string &field, std::string &err)
    {
        Version *v = p.version.empty() ? nullptr : p.versionOf(p.version);
        if (!v) { err = "main is the song itself — there is nothing for it to inherit"; return false; }
        if (v->frozen) { err = "version " + v->name + " is frozen — thaw it to change it"; return false; }
        const Parsed pv = readVersion(*v);
        const Table own = readTable(pv.own);
        if (own.nodes.count(node)) { err = node + " is version " + v->name + "'s own — delete it instead"; return false; }
        bool dropped = false;
        std::vector<std::string> lines;
        for (const auto &d : pv.deltas)
        {
            if (d.node != node) { lines.insert(lines.end(), d.raw.begin(), d.raw.end()); continue; }
            if (field.empty()) { dropped = true; continue; }
            if (d.op != "vset" && d.op != "vunset") { lines.insert(lines.end(), d.raw.begin(), d.raw.end()); continue; }
            std::string rest;
            for (const auto &f : d.fields)
            {
                if (f.first == field) { dropped = true; continue; }
                rest += " " + f.first + (f.second.empty() ? std::string() : "=" + f.second);
            }
            if (!rest.empty()) lines.push_back("#" + d.op + " node=" + quoteIfNeeded(node) + rest);
        }
        if (!dropped)
        {
            err = "version " + v->name + " has no override on `" + node + (field.empty() ? "" : "." + field) + "`";
            return false;
        }
        lines.insert(lines.end(), pv.ownLines.begin(), pv.ownLines.end());
        store(*v, lines, nullptr);
        return reopen(p, err);
    }

    std::vector<std::string> versionLines(const Project &p, const std::string &id)
    {
        const Version *v = p.versionOf(id);
        if (!v) return {};
        return linesOf(readVersion(*v), [](const Delta &) { return true; });
    }
}
}
