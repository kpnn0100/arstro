/*
 *  solaris_ui — the window's words (R-ACCESS-2; docs/discussion.md Round 21, part 2).
 *
 *  A widget that draws its own text draws `tr("Play")` (`trc`, `trn`, `fill` — solaris_strings) at paint, so every
 *  paint pass is in the language drawn then. Text a widget STORES to show later — a menu's labels, a dialog's lines,
 *  a toast, a combo's choices — is kept as words, not as their translation: `say("Undo {1}", label)` (a count:
 *  `sayn(n, "{1} file", "{1} files", n)`; pieces said and data may be joined: `say("Copy Clip") + hint`) — and
 *  `shown(stored)` turns it into the words of the language drawn now.
 *  Anything not said that way is DATA and is shown as it is — a strip's name, a preset's, the service's own line — so a
 *  song named "Settings" is never translated. The App calls `shown` for each pass (its `relabel`), so a language
 *  switch dissolves stored words with drawn ones.
 */
#pragma once
#include "Strings.h"
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace arstro
{
namespace solaris_ui
{
    using solaris_strings::fillIn;
    using solaris_strings::tr;
    using solaris_strings::trc;
    using solaris_strings::trn;

    namespace words_detail
    {
        constexpr char kSay = '\x02', kSayN = '\x03', kSep = '\x1F', kEnd = '\x04';
        template <typename A> std::string arg(const A &a)
        {
            if constexpr (std::is_arithmetic_v<A>) return std::to_string(a);
            else return std::string(a);
        }
        inline void add(std::string &) {}
        template <typename A, typename... R> void add(std::string &s, const A &a, const R &...rest)
        {
            s += kSep;
            s += arg(a);
            add(s, rest...);
        }
    }

    /** An English literal kept to be translated where it is drawn (`tr(kLabels[r])`) — gettext's N_: it does nothing but
     *  mark the string, so the source scan lists it among the window's words. */
    constexpr const char *N_(const char *en) { return en; }

    /** English words to show later in the language drawn then; {1} … the arguments, which are data. */
    template <typename... A> std::string say(const char *en, const A &...args)
    {
        std::string s(1, words_detail::kSay);
        s += en;
        words_detail::add(s, args...);
        return s + words_detail::kEnd;
    }

    /** A count's words to show later: the form by the count in the language drawn then. */
    template <typename... A> std::string sayn(long n, const char *one, const char *other, const A &...args)
    {
        std::string s(1, words_detail::kSayN);
        s += std::to_string(n);
        words_detail::add(s, std::string(one), std::string(other), args...);
        return s + words_detail::kEnd;
    }

    namespace words_detail
    {
        /** {1} … {9} of a template from parts[from + k]. */
        inline std::string put(const std::string &tpl, const std::vector<std::string> &parts, size_t from)
        {
            std::string out;
            for (size_t i = 0; i < tpl.size(); ++i)
            {
                if (tpl[i] == '{' && i + 2 < tpl.size() && tpl[i + 2] == '}' && tpl[i + 1] >= '1' && tpl[i + 1] <= '9' &&
                    from + (size_t)(tpl[i + 1] - '1') < parts.size())
                {
                    out += parts[from + (size_t)(tpl[i + 1] - '1')];
                    i += 2;
                }
                else out += tpl[i];
            }
            return out;
        }
    }

    /** Stored words in the language drawn now — each said piece translated, the data around them as it is; or, with
     *  `inEnglish`, in English whatever is drawn (an identity that must not change with the language: a node's key). */
    inline std::string shown(const std::string &stored, bool inEnglish = false)
    {
        using namespace words_detail;
        if (stored.find(kSay) == std::string::npos && stored.find(kSayN) == std::string::npos) return stored;
        std::string out;
        size_t at = 0;
        while (at < stored.size())
        {
            const char c = stored[at];
            if (c != kSay && c != kSayN) { out += c; ++at; continue; }
            const size_t end = stored.find(kEnd, at);
            const std::string piece = stored.substr(at + 1, end == std::string::npos ? std::string::npos : end - at - 1);
            at = end == std::string::npos ? stored.size() : end + 1;
            std::vector<std::string> parts;
            for (size_t p = 0;;)
            {
                const size_t sep = piece.find(kSep, p);
                parts.push_back(piece.substr(p, sep == std::string::npos ? std::string::npos : sep - p));
                if (sep == std::string::npos) break;
                p = sep + 1;
            }
            if (c == kSay) out += put(inEnglish ? parts[0] : tr(parts[0]), parts, 1); // the English, its arguments
            else if (parts.size() >= 3)                                                // n, one, other, then the arguments
            {
                const long n = std::atol(parts[0].c_str());
                out += put(inEnglish ? (n == 1 ? parts[1] : parts[2]) : trn(n, parts[1], parts[2]), parts, 3);
            }
        }
        return out;
    }

    inline std::string english(const std::string &stored) { return shown(stored, true); }
}
}
