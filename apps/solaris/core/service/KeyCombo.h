/*
 *  solaris_core — KeyCombo: a key and its modifiers, as text (R-SET-4).
 *
 *  The one table of key names and codes. The service reads `settings set key.<action>=…` with it and
 *  keeps every combo in its canonical spelling (`Ctrl+Shift+Z`); a front end spells the key it was
 *  pressed with (`comboText`) and looks that up in the model's `settings.keys` — so every front end
 *  resolves a key the same way, with no copy of the table. The codes are the window's (`KeyEvent::
 *  keyCode`): a letter or digit is its upper-case ASCII; every other key is its Windows virtual-key
 *  number, which the host maps to from the key pressed (`app/KeyInput.h`).
 */
#pragma once
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct KeyNameCode
    {
        int code;
        const char *name;
    };

    /** The keys a combo may name, besides A–Z, 0–9 and F1–F12. */
    inline const std::vector<KeyNameCode> &namedKeys()
    {
        static const std::vector<KeyNameCode> k = {
            {8, "Backspace"}, {9, "Tab"}, {13, "Enter"}, {27, "Escape"}, {32, "Space"},
            {33, "PageUp"}, {34, "PageDown"}, {35, "End"}, {36, "Home"},
            {37, "Left"}, {38, "Up"}, {39, "Right"}, {40, "Down"}, {45, "Insert"}, {46, "Delete"},
            {186, "Semicolon"}, {187, "Equal"}, {188, "Comma"}, {189, "Minus"}, {190, "Period"}, {191, "Slash"},
            {192, "Backquote"}, {219, "BracketLeft"}, {220, "Backslash"}, {221, "BracketRight"}, {222, "Quote"}};
        return k;
    }
    constexpr int kKeyF1 = 112; // F1 … F12 = 112 … 123

    /** A key's name ("Z", "7", "F5", "Space"), or "" for a code no combo can name. */
    inline std::string keyNameOf(int code)
    {
        if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) return std::string(1, (char)code);
        if (code >= kKeyF1 && code < kKeyF1 + 12) return "F" + std::to_string(code - kKeyF1 + 1);
        for (const auto &k : namedKeys())
            if (k.code == code) return k.name;
        return std::string();
    }

    inline std::string keyLower(std::string s)
    {
        for (auto &c : s) c = (char)std::tolower((unsigned char)c);
        return s;
    }

    /** A key's code from its name, any case, or 0. A few spellings people type are taken too. */
    inline int keyCodeOf(const std::string &name)
    {
        const std::string n = keyLower(name);
        if (n.size() == 1 && ((n[0] >= 'a' && n[0] <= 'z') || (n[0] >= '0' && n[0] <= '9'))) return std::toupper((unsigned char)n[0]);
        if (n.size() >= 2 && n.size() <= 3 && n[0] == 'f' && std::isdigit((unsigned char)n[1]))
        {
            const int f = std::atoi(n.c_str() + 1);
            if (f >= 1 && f <= 12 && std::to_string(f) == n.substr(1)) return kKeyF1 + f - 1;
        }
        for (const auto &k : namedKeys())
            if (keyLower(k.name) == n) return k.code;
        static const KeyNameCode kAlso[] = {{13, "return"}, {27, "esc"}, {46, "del"}, {33, "pgup"}, {34, "pgdn"},
                                            {188, ","}, {190, "."}, {189, "-"}, {187, "="}, {191, "/"}, {186, ";"},
                                            {192, "`"}, {219, "["}, {220, "\\"}, {221, "]"}, {222, "'"}};
        for (const auto &k : kAlso)
            if (n == k.name) return k.code;
        return 0;
    }

    /** The canonical spelling of a key pressed with its modifiers — `Ctrl+Alt+Shift+<key>` — or "" when the
        key is not one a combo can name. */
    inline std::string comboText(int code, bool ctrl, bool alt, bool shift)
    {
        const std::string k = keyNameOf(code);
        if (k.empty()) return k;
        return std::string(ctrl ? "Ctrl+" : "") + (alt ? "Alt+" : "") + (shift ? "Shift+" : "") + k;
    }

    inline std::string keyTrim(const std::string &s)
    {
        const auto a = s.find_first_not_of(" \t");
        if (a == std::string::npos) return std::string();
        return s.substr(a, s.find_last_not_of(" \t") - a + 1);
    }

    /** One combo, any case, modifiers in any order ("shift+ctrl+z") → its canonical spelling. */
    inline bool parseCombo(const std::string &text, std::string &canonical, std::string &err)
    {
        bool ctrl = false, alt = false, shift = false;
        std::string t = keyTrim(text);
        int code = 0;
        size_t at = 0;
        while (true)
        {
            const size_t plus = t.find('+', at);
            // the last part is the key; "Ctrl++" is not a key (it is Shift+Equal), and a lone "+" is not one
            const std::string part = keyTrim(plus == std::string::npos ? t.substr(at) : t.substr(at, plus - at));
            const std::string low = keyLower(part);
            if (plus != std::string::npos)
            {
                if (low == "ctrl" || low == "control") ctrl = true;
                else if (low == "alt") alt = true;
                else if (low == "shift") shift = true;
                else { err = "`" + t + "`: `" + part + "` is not Ctrl, Alt or Shift"; return false; }
                at = plus + 1;
                continue;
            }
            if (low == "ctrl" || low == "control" || low == "alt" || low == "shift" || part.empty())
            { err = "`" + t + "` names no key — a combo ends in one (Ctrl+S, Shift+Up, F5)"; return false; }
            code = keyCodeOf(part);
            if (!code)
            {
                err = "`" + t + "`: no key `" + part + "` (keys: A–Z, 0–9, F1–F12";
                for (const auto &k : namedKeys()) err += std::string(", ") + k.name;
                err += ")";
                return false;
            }
            break;
        }
        canonical = comboText(code, ctrl, alt, shift);
        return true;
    }

    /** A list of combos — "Ctrl+Shift+Z, Ctrl+Y" — or `none` (no key at all) → canonical, each once. */
    inline bool parseCombos(const std::string &text, std::vector<std::string> &out, std::string &err)
    {
        out.clear();
        const std::string t = keyTrim(text);
        if (keyLower(t) == "none") return true;
        size_t at = 0;
        while (true)
        {
            const size_t comma = t.find(',', at);
            const std::string part = keyTrim(t.substr(at, comma == std::string::npos ? std::string::npos : comma - at));
            if (part.empty()) { err = "`" + t + "`: an empty combo (keys are a comma list: `Ctrl+Shift+Z, Ctrl+Y`, or `none`)"; return false; }
            std::string c;
            if (!parseCombo(part, c, err)) return false;
            if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
            if (comma == std::string::npos) break;
            at = comma + 1;
        }
        return true;
    }

    /** The list as the settings file and `settings print` write it: "Ctrl+Shift+Z, Ctrl+Y", or "none". */
    inline std::string combosText(const std::vector<std::string> &combos)
    {
        if (combos.empty()) return "none";
        std::string s;
        for (const auto &c : combos) s += (s.empty() ? "" : ", ") + c;
        return s;
    }
}
}
