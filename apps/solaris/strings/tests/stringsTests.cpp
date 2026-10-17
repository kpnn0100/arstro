/*
 *  R-ACCESS-2 — the window's words in the user's language (docs/discussion.md Round 21, part 1): every table compiled in
 *  parses; a table that drops or adds a placeholder, repeats a key, has the wrong number of plural forms or is not one
 *  is refused naming its line; English is the keys; Vietnamese from its table, a context and a plural by its rule, a
 *  string it lacks in English; the pseudo-language accented, a third longer, in brackets, its placeholders kept; `auto`
 *  resolved from the OS's language; the words returned stay where they are.
 */
#ifdef NDEBUG
#undef NDEBUG // a test asserts in every build
#endif
#include "Embedded.h"
#include "Strings.h"
#include <cassert>
#include <cstdio>
#include <string>

using namespace arstro::solaris_strings;

namespace
{
    int passed = 0;
    void pass(const char *what)
    {
        std::printf("[PASS] %s\n", what);
        ++passed;
    }
    bool contains(const std::string &s, const std::string &n) { return s.find(n) != std::string::npos; }
    size_t points(const std::string &s)
    {
        size_t n = 0;
        for (char c : s) n += (c & 0xC0) != 0x80;
        return n;
    }
    /** A table's refusal: its reason, which must name the line. */
    std::string refused(const std::string &text)
    {
        Table t;
        std::string err;
        assert(!parseTable(text, t, err));
        return err;
    }
    const char *kHead = "@language xx\n@name Test\n@plural one-other\n";
}

int main()
{
    // ── what is compiled in ──────────────────────────────────────────────────────────────────────────────────────────
    {
        assert(kEmbeddedCount >= 1);
        for (int i = 0; i < kEmbeddedCount; ++i)
        {
            Table t;
            std::string err;
            const bool ok = parseTable(std::string((const char *)kEmbedded[i].data, kEmbedded[i].size), t, err);
            if (!ok) std::printf("%s: %s\n", kEmbedded[i].file, err.c_str());
            assert(ok);
        }
        const auto &ls = languages();
        assert(ls.size() == 3 && ls[0].code == "en" && ls[0].name == "English");
        assert(ls[1].code == "vi" && ls[1].name == "Ti\xE1\xBA\xBFng Vi\xE1\xBB\x87t" && ls[1].plural == "other");
        assert(ls[2].code == "en-XA" && ls.back().plural == "one-other");
        assert(known("vi") && known("en-XA") && !known("fr") && !known("auto"));
        pass("every table compiled in parses; the languages are English, Vietnamese (in itself) and en-XA");
    }

    // ── a table that is not one is refused, naming its line ──────────────────────────────────────────────────────────
    {
        Table t;
        std::string err;
        assert(parseTable(std::string(kHead) + "# a comment\n\n\"Play\" = \"Spielen\";\n\"menu|Open\" = \"Offnen\";\n"
                                               "\"{1} clip\" \"{1} clips\" = \"{1} Clip\" \"{1} Clips\";\n\"Say \\\"hi\\\"\\\\\" = \"x\\ny\";\n",
                          t, err));
        assert(t.language.code == "xx" && t.language.name == "Test" && t.entries.size() == 4);
        assert(t.entries.at("menu|Open")[0] == "Offnen" && t.entries.at(std::string("{1} clip") + kPluralJoin + "{1} clips").size() == 2);
        assert(t.entries.at("Say \"hi\"\\")[0] == "x\ny");
        assert(contains(refused(std::string(kHead) + "\"Play\" = \"A\";\n\"Play\" = \"B\";\n"), "line 5: `Play` twice"));
        assert(contains(refused(std::string(kHead) + "\"{1} of {2}\" = \"{1} von\";\n"), "line 4: `{1} von` must keep the placeholders of `{1} of {2}`"));
        assert(contains(refused(std::string(kHead) + "\"Play\" = \"{1} Spielen\";\n"), "line 4:"));
        assert(contains(refused(std::string(kHead) + "\"menu|{1} open\" = \"offen\";\n"), "must keep the placeholders"));
        assert(contains(refused(std::string(kHead) + "\"{1} clip\" \"{1} clips\" = \"{1} Clip\";\n"), "line 4: a plural has 2 form(s) under @plural one-other"));
        assert(contains(refused("@language xx\n@plural other\n\"{1} clip\" \"{1} clips\" = \"{1} a\" \"{1} b\";\n"), "a plural has 1 form(s)"));
        assert(contains(refused(std::string(kHead) + "Play = \"Spielen\";\n"), "line 4: a string in quotes expected"));
        assert(contains(refused(std::string(kHead) + "\"Play\" = \"Spielen\"\n"), "line 4: `;` ends a line"));
        assert(contains(refused(std::string(kHead) + "\"Play\" \"Spielen\";\n"), "line 4: `=` expected"));
        assert(contains(refused(std::string(kHead) + "\"Play\" = \"Spie;\n"), "a string never closed"));
        assert(contains(refused("\"Play\" = \"Spielen\";\n"), "line 1: a string before @language and @plural"));
        assert(contains(refused("@language xx\n@plural many\n"), "line 2: no plural rule `many`"));
        assert(contains(refused("@langauge xx\n"), "line 1: no header `@langauge`"));
        assert(contains(refused("@plural other\n"), "no @language"));
        pass("a table that drops or adds a placeholder, repeats a key, has the wrong plural forms or is not one is refused, by line");
    }

    // ── English is the keys ──────────────────────────────────────────────────────────────────────────────────────────
    {
        setLanguage("en");
        assert(language() == "en" && tr("Settings") == "Settings" && trc("port", "in") == "in");
        assert(trn(1, "{1} bar", "{1} bars") == "{1} bar" && trn(0, "{1} bar", "{1} bars") == "{1} bars");
        assert(fillIn(trn(3, "{1} bar", "{1} bars"), "3") == "3 bars");
        assert(fillIn("{2} of {1}", std::string("a"), std::string("b")) == "b of a" && fillIn("{1} and {3}", "x") == "x and {3}");
        pass("English is the keys: a context and a count pick the English; {1} {2} in any order");
    }

    // ── Vietnamese from its table ────────────────────────────────────────────────────────────────────────────────────
    {
        setLanguage("vi");
        assert(language() == "vi");
        assert(tr("Settings") == "C\xC3\xA0i \xC4\x91\xE1\xBA\xB7t");                         // Cài đặt
        assert(trc("port", "in") == "v\xC3\xA0o");                                               // vào
        assert(tr("in") == "in");                                                               // another key: its context is part of it
        assert(tr("Nothing like this is in the table") == "Nothing like this is in the table"); // a string it lacks: English
        const std::string one = trn(1, "{1} bar", "{1} bars"), many = trn(7, "{1} bar", "{1} bars");
        assert(one == many && fillIn(many, "7") == "7 \xC3\xB4 nh\xE1\xBB\x8Bp");              // one form for every count: 7 ô nhịp
        assert(trn(1, "{1} take", "{1} takes") == "{1} take" && trn(2, "{1} take", "{1} takes") == "{1} takes"); // a plural it lacks: English's
        const std::string *p = &tr("Settings");
        for (int i = 0; i < 1000; ++i) tr("x" + std::to_string(i));
        assert(&tr("Settings") == p && *p == "C\xC3\xA0i \xC4\x91\xE1\xBA\xB7t");
        pass("Vietnamese from its table — a context, one form for every count; what it lacks in English; the words stay put");
    }

    // ── the pseudo-language ──────────────────────────────────────────────────────────────────────────────────────────
    {
        setLanguage("en-XA");
        const std::string s = tr("Settings");
        assert(s.front() == '[' && s.back() == ']' && contains(s, "\xC5\xA0\xC3\xA9\xC5\xA3\xC5\xA3\xC3\xAE\xC3\xB1\xC4\x9D\xC5\xA1")); // Šéţţîñĝš
        assert(points(s) * 3 >= points("Settings") * 4);
        const std::string longer = "Choose the folder your samples are in, and Solaris lists every sound it finds there";
        assert(points(pseudo(longer)) * 3 >= points(longer) * 4); // a third longer, not just its brackets
        const std::string f = tr("{1} of {2}");
        assert(contains(f, "{1}") && contains(f, "{2}") && fillIn(f, "3", "9").find("3") != std::string::npos);
        assert(trn(1, "{1} clip", "{1} clips") != trn(2, "{1} clip", "{1} clips") && trn(2, "{1} clip", "{1} clips").front() == '[');
        assert(trc("menu", "Open") == pseudo("Open"));
        assert(pseudo("") == "[ \xC2\xB7]");
        pass("en-XA: every string accented, a third longer, in brackets — its placeholders kept");
    }

    // ── which language: the setting and the OS ───────────────────────────────────────────────────────────────────────
    {
        assert(resolve("auto", "vi_VN.UTF-8") == "vi" && resolve("auto", "vi") == "vi" && resolve("auto", "en_US.UTF-8") == "en");
        assert(resolve("auto", "fr_FR.UTF-8") == "en" && resolve("auto", "C") == "en" && resolve("auto", "") == "en");
        assert(resolve("auto", "sr_RS@latin") == "en" && resolve("vi", "fr_FR") == "vi" && resolve("en-XA", "vi_VN") == "en-XA");
        assert(resolve("xx", "vi_VN") == "en");
        setLanguage("fr");
        assert(language() == "en" && tr("Settings") == "Settings");
        pass("auto is the OS's language when Solaris has it, else English; an unknown language is English");
    }

    // ── upper case, for a section header in any of them ─────────────────────────────────────────────────────────────
    {
        // "Thiết bị âm thanh đầu ra · ơ ư" → "THIẾT BỊ ÂM THANH ĐẦU RA · Ơ Ư"
        assert(upper("Thi\xE1\xBA\xBFt b\xE1\xBB\x8B \xC3\xA2m thanh \xC4\x91\xE1\xBA\xA7u ra \xC2\xB7 \xC6\xA1 \xC6\xB0") ==
               "THI\xE1\xBA\xBET B\xE1\xBB\x8A \xC3\x82M THANH \xC4\x90\xE1\xBA\xA6U RA \xC2\xB7 \xC6\xA0 \xC6\xAF");
        assert(upper(pseudo("Settings")) == "[\xC5\xA0\xC3\x89\xC5\xA2\xC5\xA2\xC3\x8E\xC3\x91\xC4\x9C\xC5\xA0 \xC2\xB7\xC2\xB7\xC2\xB7]"); // ŠÉŢŢÎÑĜŠ
        assert(upper("Audio 48 kHz") == "AUDIO 48 KHZ" && upper("") == "" && upper("\xE2\x80\xA6 \xC3\xB7") == "\xE2\x80\xA6 \xC3\xB7"); // … ÷ kept
        pass("upper case for the letters the languages use: Vietnamese's, the pseudo-language's, ASCII; the rest kept");
    }

    std::printf("solaris_strings_tests: %d passed, 0 failed\n", passed);
    return 0;
}
