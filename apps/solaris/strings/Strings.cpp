#include "Strings.h"
#include "Embedded.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace arstro
{
namespace solaris_strings
{
    namespace
    {
        std::string trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && std::isspace((unsigned char)s[a])) ++a;
            while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
            return s.substr(a, b - a);
        }

        /** The {1} … {9} a string has. */
        std::set<char> placeholders(std::string_view s)
        {
            std::set<char> out;
            for (size_t i = 0; i + 2 < s.size(); ++i)
                if (s[i] == '{' && s[i + 1] >= '1' && s[i + 1] <= '9' && s[i + 2] == '}') out.insert(s[i + 1]);
            return out;
        }

        /** The quoted strings of one side of a line, from `at`; stops at '=' or ';'. False: not a quoted string. */
        bool quoted(const std::string &line, size_t &at, std::vector<std::string> &out, std::string &err)
        {
            while (true)
            {
                while (at < line.size() && std::isspace((unsigned char)line[at])) ++at;
                if (at >= line.size() || line[at] == '=' || line[at] == ';') return !out.empty() || (err = "a string in quotes expected", false);
                if (line[at] != '"') { err = "a string in quotes expected"; return false; }
                std::string s;
                ++at;
                bool closed = false;
                while (at < line.size())
                {
                    const char c = line[at++];
                    if (c == '"') { closed = true; break; }
                    if (c == '\\' && at < line.size())
                    {
                        const char e = line[at++];
                        s += e == 'n' ? '\n' : e == 't' ? '\t' : e; // \" \\ and the rest as themselves
                    }
                    else s += c;
                }
                if (!closed) { err = "a string never closed"; return false; }
                out.push_back(s);
            }
        }

        // ── the pseudo-language ─────────────────────────────────────────────────────────────────────────────────────
        const char *accented(char c)
        {
            switch (c)
            {
            case 'a': return "\xC3\xA5"; case 'c': return "\xC3\xA7"; case 'd': return "\xC4\x8F"; case 'e': return "\xC3\xA9";
            case 'g': return "\xC4\x9D"; case 'h': return "\xC4\xA5"; case 'i': return "\xC3\xAE"; case 'j': return "\xC4\xB5";
            case 'k': return "\xC4\xB7"; case 'l': return "\xC4\xBC"; case 'n': return "\xC3\xB1"; case 'o': return "\xC3\xB6";
            case 'r': return "\xC5\x95"; case 's': return "\xC5\xA1"; case 't': return "\xC5\xA3"; case 'u': return "\xC3\xBC";
            case 'w': return "\xC5\xB5"; case 'y': return "\xC3\xBD"; case 'z': return "\xC5\xBE";
            case 'A': return "\xC3\x85"; case 'C': return "\xC3\x87"; case 'D': return "\xC4\x8E"; case 'E': return "\xC3\x89";
            case 'G': return "\xC4\x9C"; case 'H': return "\xC4\xA4"; case 'I': return "\xC3\x8E"; case 'J': return "\xC4\xB4";
            case 'K': return "\xC4\xB6"; case 'L': return "\xC4\xBB"; case 'N': return "\xC3\x91"; case 'O': return "\xC3\x96";
            case 'R': return "\xC5\x94"; case 'S': return "\xC5\xA0"; case 'T': return "\xC5\xA2"; case 'U': return "\xC3\x9C";
            case 'W': return "\xC5\xB4"; case 'Y': return "\xC3\x9D"; case 'Z': return "\xC5\xBD";
            default: return nullptr;
            }
        }

        // ── what is compiled in, and the language drawn now ─────────────────────────────────────────────────────────
        struct State
        {
            std::vector<Language> languages;
            std::map<std::string, Table> tables;
            std::string current = "en";
            const Table *table = nullptr;
            std::map<std::string, std::map<std::string, std::string, std::less<>>> cache; // per language: key → words
            std::string key;                                                              // a look-up's key, reused
        };

        State load()
        {
            State s;
            s.languages.push_back({"en", "English", "one-other"});
            for (int i = 0; i < kEmbeddedCount; ++i)
            {
                Table t;
                std::string err;
                if (!parseTable(std::string((const char *)kEmbedded[i].data, kEmbedded[i].size), t, err)) continue; // its test fails instead
                s.languages.push_back(t.language);
                s.tables[t.language.code] = std::move(t);
            }
            s.languages.push_back({"en-XA", "Pseudo (en-XA)", "one-other"});
            return s;
        }

        State &state()
        {
            static State s = load();
            return s;
        }

        std::map<std::string, std::string, std::less<>> &cacheOf(State &s) { return s.cache[s.current]; }
    }

    int pluralForms(const std::string &rule) { return rule == "one-other" ? 2 : rule == "other" ? 1 : 0; }
    int pluralIndex(const std::string &rule, long n) { return rule == "one-other" ? (n == 1 ? 0 : 1) : 0; }

    bool parseTable(const std::string &text, Table &out, std::string &err)
    {
        out = Table();
        size_t start = 0;
        int lineNo = 0;
        auto fail = [&](const std::string &why) {
            err = "line " + std::to_string(lineNo) + ": " + why;
            return false;
        };
        while (start <= text.size())
        {
            size_t end = text.find('\n', start);
            if (end == std::string::npos) end = text.size();
            const std::string line = trim(text.substr(start, end - start));
            start = end + 1;
            ++lineNo;
            if (line.empty() || line[0] == '#') continue;
            if (line[0] == '@')
            {
                const size_t sp = line.find(' ');
                const std::string k = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
                const std::string v = sp == std::string::npos ? std::string() : trim(line.substr(sp));
                if (v.empty()) return fail("@" + k + " names nothing");
                if (k == "language") out.language.code = v;
                else if (k == "name") out.language.name = v;
                else if (k == "plural")
                {
                    if (!pluralForms(v)) return fail("no plural rule `" + v + "` (one-other, other)");
                    out.language.plural = v;
                }
                else return fail("no header `@" + k + "` (@language, @name, @plural)");
                continue;
            }
            if (out.language.code.empty() || out.language.plural.empty()) return fail("a string before @language and @plural");
            size_t at = 0;
            std::vector<std::string> keys, forms;
            std::string why;
            if (!quoted(line, at, keys, why)) return fail(why);
            if (at >= line.size() || line[at] != '=') return fail("`=` expected after the English");
            ++at;
            if (!quoted(line, at, forms, why)) return fail(why);
            if (at >= line.size() || line[at] != ';' || !trim(line.substr(at + 1)).empty()) return fail("`;` ends a line");
            if (keys.size() > 2) return fail("one English string, or a plural's two");
            const size_t want = keys.size() == 2 ? (size_t)pluralForms(out.language.plural) : 1;
            if (forms.size() != want)
                return fail(keys.size() == 2 ? "a plural has " + std::to_string(want) + " form(s) under @plural " + out.language.plural
                                             : "one translation for one English string");
            const std::string &en = keys.back();
            const size_t bar = keys.size() == 1 ? en.find('|') : std::string::npos;
            const std::set<char> need = placeholders(bar == std::string::npos ? std::string_view(en) : std::string_view(en).substr(bar + 1));
            for (const auto &f : forms)
                if (placeholders(f) != need) return fail("`" + f + "` must keep the placeholders of `" + en + "`");
            const std::string key = keys.size() == 2 ? keys[0] + kPluralJoin + keys[1] : keys[0];
            if (out.entries.count(key)) return fail("`" + en + "` twice");
            out.entries[key] = forms;
        }
        if (out.language.code.empty()) { err = "no @language"; return false; }
        if (out.language.plural.empty()) { err = "no @plural"; return false; }
        if (out.language.name.empty()) out.language.name = out.language.code;
        return true;
    }

    const std::vector<Language> &languages() { return state().languages; }

    bool known(const std::string &code)
    {
        for (const auto &l : languages())
            if (l.code == code) return true;
        return false;
    }

    std::string resolve(const std::string &setting, const std::string &osLanguage)
    {
        if (setting != "auto") return known(setting) ? setting : "en";
        std::string os;
        for (char c : osLanguage)
        {
            if (c == '.' || c == '@') break; // vi_VN.UTF-8, sr_RS@latin
            os += c == '_' ? '-' : c;
        }
        if (known(os)) return os;
        const std::string primary = os.substr(0, os.find('-'));
        return known(primary) ? primary : "en";
    }

    void setLanguage(const std::string &code)
    {
        State &s = state();
        s.current = known(code) ? code : "en";
        const auto t = s.tables.find(s.current);
        s.table = t == s.tables.end() ? nullptr : &t->second;
    }

    const std::string &language() { return state().current; }

    namespace
    {
        /** The words for a key (its English `en`; its table key `key`), cached for the language drawn now. */
        const std::string &lookUp(const std::string &key, std::string_view en, int form)
        {
            State &s = state();
            auto &c = cacheOf(s);
            if (const auto it = c.find(key); it != c.end()) return it->second;
            std::string words;
            if (s.current == "en-XA") words = pseudo(en);
            else if (s.table)
            {
                const std::string_view tableKey = form < 0 ? std::string_view(key) : std::string_view(key).substr(0, key.size() - 2);
                const auto e = s.table->entries.find(tableKey);
                words = e != s.table->entries.end() && (size_t)std::max(form, 0) < e->second.size() ? e->second[(size_t)std::max(form, 0)] : std::string(en);
            }
            else words = std::string(en);
            return c.emplace(key, std::move(words)).first->second;
        }
    }

    const std::string &tr(std::string_view en)
    {
        State &s = state();
        s.key.assign(en);
        return lookUp(s.key, en, -1);
    }

    const std::string &trc(std::string_view context, std::string_view en)
    {
        State &s = state();
        s.key.assign(context);
        s.key += '|';
        s.key.append(en);
        return lookUp(s.key, en, -1);
    }

    const std::string &trn(long n, std::string_view one, std::string_view other)
    {
        State &s = state();
        // the table's rule picks the table's form; English's picks the English the language falls back to
        const int english = pluralIndex("one-other", n);
        const int form = s.table ? pluralIndex(s.table->language.plural, n) : english;
        s.key.assign(one);
        s.key += kPluralJoin;
        s.key.append(other);
        s.key += '\x1E';
        s.key += (char)('0' + form + (s.table ? 0 : 5)); // a table's form, or English's (never the same cache line)
        if (s.table)
        {
            // a plural the table lacks falls back to the English for this count
            const std::string_view tableKey = std::string_view(s.key).substr(0, s.key.size() - 2);
            if (!s.table->entries.count(tableKey))
            {
                s.key.back() = (char)('5' + english);
                return lookUp(s.key, english == 0 ? one : other, -1);
            }
            return lookUp(s.key, english == 0 ? one : other, form);
        }
        return lookUp(s.key, english == 0 ? one : other, -1);
    }

    std::string fillIn(const std::string &tpl, std::initializer_list<std::string> args)
    {
        std::string out;
        out.reserve(tpl.size() + 16);
        const std::vector<std::string> a(args);
        for (size_t i = 0; i < tpl.size(); ++i)
        {
            if (tpl[i] == '{' && i + 2 < tpl.size() && tpl[i + 1] >= '1' && tpl[i + 1] <= '9' && tpl[i + 2] == '}' && (size_t)(tpl[i + 1] - '1') < a.size())
            {
                out += a[(size_t)(tpl[i + 1] - '1')];
                i += 2;
            }
            else out += tpl[i];
        }
        return out;
    }

    std::string upper(std::string_view s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size();)
        {
            // one character: its code point, and how many bytes it took
            const unsigned char c = (unsigned char)s[i];
            unsigned cp = c;
            size_t n = 1;
            if (c >= 0xF0 && i + 3 < s.size()) { cp = ((c & 0x07u) << 18) | (((unsigned char)s[i + 1] & 0x3Fu) << 12) | (((unsigned char)s[i + 2] & 0x3Fu) << 6) | ((unsigned char)s[i + 3] & 0x3Fu); n = 4; }
            else if (c >= 0xE0 && i + 2 < s.size()) { cp = ((c & 0x0Fu) << 12) | (((unsigned char)s[i + 1] & 0x3Fu) << 6) | ((unsigned char)s[i + 2] & 0x3Fu); n = 3; }
            else if (c >= 0xC0 && i + 1 < s.size()) { cp = ((c & 0x1Fu) << 6) | ((unsigned char)s[i + 1] & 0x3Fu); n = 2; }
            unsigned up = cp;
            if (cp >= 'a' && cp <= 'z') up = cp - 32;
            else if (cp >= 0xE0 && cp <= 0xFE && cp != 0xF7) up = cp - 0x20;                          // à … þ
            else if (cp == 0xFF) up = 0x178;                                                         // ÿ
            else if ((cp >= 0x101 && cp <= 0x137 && (cp & 1)) || (cp >= 0x14B && cp <= 0x177 && (cp & 1))) up = cp - 1; // ā … ķ, ŋ … ŷ
            else if ((cp >= 0x13A && cp <= 0x148 && !(cp & 1)) || (cp >= 0x17A && cp <= 0x17E && !(cp & 1))) up = cp - 1; // ĺ … ň, ź … ž
            else if (cp == 0x1A1 || cp == 0x1B0) up = cp - 1;                                        // ơ ư
            else if (((cp >= 0x1E01 && cp <= 0x1E95) || (cp >= 0x1EA1 && cp <= 0x1EFF)) && (cp & 1)) up = cp - 1; // ḁ …, ạ … ỹ
            if (up == cp) out.append(s.substr(i, n));
            else if (up < 0x80) out += (char)up;
            else if (up < 0x800) { out += (char)(0xC0 | (up >> 6)); out += (char)(0x80 | (up & 0x3F)); }
            else { out += (char)(0xE0 | (up >> 12)); out += (char)(0x80 | ((up >> 6) & 0x3F)); out += (char)(0x80 | (up & 0x3F)); }
            i += n;
        }
        return out;
    }

    std::string pseudo(std::string_view en)
    {
        std::string out = "[";
        size_t points = 0;
        for (size_t i = 0; i < en.size(); ++i)
        {
            const char c = en[i];
            if ((c & 0xC0) != 0x80) ++points; // a UTF-8 lead byte: one character
            if (c == '{' && i + 2 < en.size() && en[i + 1] >= '1' && en[i + 1] <= '9' && en[i + 2] == '}')
            {
                out.append(en.substr(i, 3)); // a placeholder kept
                i += 2;
                continue;
            }
            if (const char *a = accented(c)) out += a;
            else out += c;
        }
        out += ' ';
        for (size_t k = 0; k < std::max<size_t>(1, (points + 2) / 3); ++k) out += "\xC2\xB7"; // a third longer, in middle dots
        out += ']';
        return out;
    }
}
}
