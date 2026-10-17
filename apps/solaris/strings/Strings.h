/*
 *  solaris_strings — the window's words in the user's language (R-ACCESS-2; docs/discussion.md Round 21).
 *
 *  English is the key: `tr("Settings")` draws "Settings" in English and the table's line for it in another language —
 *  a string with no line falls back to its English. `trc("menu", "Open")` when one English word means two things (the
 *  key "menu|Open"); `trn(n, "{1} clip", "{1} clips")` when a count picks the form (the language's plural rule);
 *  `fillIn(tr("{1} of {2}"), a, b)` puts numbers and names in by position, so a language may reorder them.
 *
 *  A table is a text file per language (`lang/<code>.strings`), compiled INTO the binary — the app's words, like its
 *  fonts, are not a thing that may differ between machines:
 *
 *      @language vi
 *      @name Tiếng Việt
 *      @plural other                                  (one-other: English's; other: one form for every count)
 *      "Settings" = "Cài đặt";
 *      "menu|Open" = "Mở";
 *      "{1} clip" "{1} clips" = "{1} đoạn";            (a plural: its keys side by side, a form per the rule's count)
 *
 *  Only the window's own words go through it — never a command line, an event, the model, the service's lines, the
 *  registry's words or the user's data (Round 21). `en` is the keys themselves; `en-XA` is generated — every letter
 *  accented, a third longer, in brackets — so a string not from the table, or one that will not fit a longer
 *  language, stands out. No dependencies: the service links it to validate and list the languages, the window to draw.
 *  The current language is the UI thread's; the look-ups return references that stay valid for the process.
 */
#pragma once
#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace arstro
{
namespace solaris_strings
{
    struct Language
    {
        std::string code;   // "en", "vi", "en-XA"
        std::string name;   // in itself: "English", "Tiếng Việt"
        std::string plural; // "one-other" | "other"
    };

    struct Table
    {
        Language language;
        /** key (English; "ctx|English"; a plural's keys joined by kPluralJoin) → its forms (one; or the rule's count). */
        std::map<std::string, std::vector<std::string>, std::less<>> entries;
    };
    constexpr char kPluralJoin = '\x1F';

    /** A table's text parsed; false naming the line and what is wrong (a key twice, a placeholder dropped or added, the
     *  wrong number of plural forms, a line that is not one, a header missing). */
    bool parseTable(const std::string &text, Table &out, std::string &err);
    /** How many forms a plural rule has (0: no such rule). */
    int pluralForms(const std::string &rule);
    /** Which form a count takes under a rule. */
    int pluralIndex(const std::string &rule, long n);

    /** Every language Solaris has: English, each table compiled in, the pseudo-language — in that order. */
    const std::vector<Language> &languages();
    bool known(const std::string &code);
    /** What a setting names, resolved: "auto" → the OS's language ("vi_VN.UTF-8" → "vi") when Solaris has it, else
     *  "en"; a known code → itself; anything else → "en". */
    std::string resolve(const std::string &setting, const std::string &osLanguage);

    /** The language the window draws in now (a known code; another is English). */
    void setLanguage(const std::string &code);
    const std::string &language();

    const std::string &tr(std::string_view en);
    const std::string &trc(std::string_view context, std::string_view en);
    const std::string &trn(long n, std::string_view one, std::string_view other);
    /** {1} … {9} replaced by the arguments, in any order the template has them. */
    std::string fillIn(const std::string &tpl, std::initializer_list<std::string> args);
    template <typename... A> std::string fillIn(const std::string &tpl, const A &...args) { return fillIn(tpl, {std::string(args)...}); }

    /** The pseudo-language's form of an English string: "[Šéţţîñĝš ···]" — placeholders kept. */
    std::string pseudo(std::string_view en);

    /** Upper case for the letters the languages use — ASCII, Latin-1, Latin Extended-A, ơ ư, and Latin Extended
     *  Additional (Vietnamese's ạ … ỹ): an uppercase section header in any of them ("Thiết bị" → "THIẾT BỊ"). */
    std::string upper(std::string_view s);
}
}
