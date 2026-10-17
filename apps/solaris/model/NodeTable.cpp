#include "NodeTable.h"
#include <algorithm>
#include <sstream>

namespace arstro
{
namespace solaris
{
namespace nodes
{
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
}
}
}
