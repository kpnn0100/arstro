/*
 *  R-ACCESS-2 (docs/discussion.md Round 21) — the window's words, read out of its source: every English string the code
 *  hands the table — `tr("…")`, `trc("ctx", "…")`, `trn(n, "…", "…")`, `say("…")`, `sayn(n, "…", "…")`, `N_("…")` — as
 *  the table keys it ("ctx|…", a plural's two joined by \x1F). Comments are skipped (a doc's example is not a word the
 *  window says), adjacent literals joined, C escapes decoded — so the scan and `lang/<code>.strings` can be compared:
 *  the table cannot lack a word the code says, nor keep one it no longer does.
 */
#pragma once
#include <cctype>
#include <set>
#include <string>

namespace words_scan
{
    /** The source with its comments blanked (strings and characters kept as they are). */
    inline std::string uncommented(const std::string &s)
    {
        std::string out = s;
        for (size_t i = 0; i < s.size();)
        {
            if (s[i] == '"' || s[i] == '\'')
            {
                const char q = s[i++];
                while (i < s.size() && s[i] != q) i += s[i] == '\\' ? 2 : 1;
                ++i;
            }
            else if (s.compare(i, 2, "//") == 0)
                while (i < s.size() && s[i] != '\n') out[i++] = ' ';
            else if (s.compare(i, 2, "/*") == 0)
            {
                while (i < s.size() && s.compare(i, 2, "*/") != 0) { if (s[i] != '\n') out[i] = ' '; ++i; }
                if (i < s.size()) { out[i] = out[i + 1] = ' '; i += 2; }
            }
            else ++i;
        }
        return out;
    }

    inline void utf8(std::string &out, unsigned cp)
    {
        if (cp < 0x80) out += (char)cp;
        else if (cp < 0x800) { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
        else { out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
    }

    /** "…" "…" at s[i] (after spaces): their bytes, escapes decoded; false when no literal is there. */
    inline bool literals(const std::string &s, size_t &i, std::string &out)
    {
        out.clear();
        bool any = false;
        while (true)
        {
            while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
            if (i >= s.size() || s[i] != '"') return any;
            any = true;
            for (++i; i < s.size() && s[i] != '"'; ++i)
            {
                if (s[i] != '\\') { out += s[i]; continue; }
                const char e = s[++i];
                if (e == 'x')
                {
                    unsigned v = 0;
                    int n = 0;
                    while (n < 2 && i + 1 < s.size() && std::isxdigit((unsigned char)s[i + 1])) { v = v * 16 + (unsigned)std::stoi(std::string(1, s[++i]), nullptr, 16); ++n; }
                    out += (char)v;
                }
                else if (e == 'u') { utf8(out, (unsigned)std::stoul(s.substr(i + 1, 4), nullptr, 16)); i += 4; }
                else out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
            }
            ++i; // the closing quote
        }
    }

    /** Past one argument (to the comma or the parenthesis that ends it, at its own depth). */
    inline void skipArgument(const std::string &s, size_t &i)
    {
        int depth = 0;
        for (; i < s.size(); ++i)
        {
            const char c = s[i];
            if (c == '"' || c == '\'') { const char q = c; for (++i; i < s.size() && s[i] != q; ++i) if (s[i] == '\\') ++i; continue; }
            if (c == '(' || c == '[' || c == '{') ++depth;
            else if (c == ')' || c == ']' || c == '}') { if (depth == 0) return; --depth; }
            else if (c == ',' && depth == 0) return;
        }
    }

    inline bool comma(const std::string &s, size_t &i)
    {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i >= s.size() || s[i] != ',') return false;
        ++i;
        return true;
    }

    /** Every key a source hands the table. */
    inline void keysIn(const std::string &source, std::set<std::string> &keys)
    {
        const std::string s = uncommented(source);
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (i > 0 && (std::isalnum((unsigned char)s[i - 1]) || s[i - 1] == '_' || s[i - 1] == '.' || s[i - 1] == '>')) continue;
            size_t n = 0;
            while (i + n < s.size() && (std::isalnum((unsigned char)s[i + n]) || s[i + n] == '_')) ++n;
            const std::string name = s.substr(i, n);
            if (name != "tr" && name != "say" && name != "N_" && name != "trc" && name != "trn" && name != "sayn") continue;
            size_t j = i + n;
            while (j < s.size() && std::isspace((unsigned char)s[j])) ++j;
            if (j >= s.size() || s[j] != '(') continue;
            ++j;
            std::string a, b;
            if (name == "tr" || name == "say" || name == "N_")
            {
                if (literals(s, j, a)) keys.insert(a);
            }
            else if (name == "trc")
            {
                if (literals(s, j, a) && comma(s, j) && literals(s, j, b)) keys.insert(a + "|" + b);
            }
            else // trn, sayn: the count, then the two forms
            {
                skipArgument(s, j);
                if (comma(s, j) && literals(s, j, a) && comma(s, j) && literals(s, j, b)) keys.insert(a + '\x1F' + b);
            }
        }
    }
}
