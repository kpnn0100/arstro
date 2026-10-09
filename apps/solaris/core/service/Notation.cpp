#include "Notation.h"
#include "Format.h"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace arstro
{
namespace solaris
{
    namespace
    {
        int letterClass(char c)
        {
            switch (std::toupper((unsigned char)c))
            {
            case 'C': return 0;
            case 'D': return 2;
            case 'E': return 4;
            case 'F': return 5;
            case 'G': return 7;
            case 'A': return 9;
            case 'B': return 11;
            default: return -1;
            }
        }
        // the root of a name: a letter and its accidentals; `i` is left after them
        bool root(const std::string &s, size_t &i, int &pc)
        {
            if (s.empty() || letterClass(s[0]) < 0) return false;
            pc = letterClass(s[0]);
            i = 1;
            while (i < s.size() && (s[i] == '#' || s[i] == 'b'))
                pc += s[i++] == '#' ? 1 : -1;
            return true;
        }
        bool number(const std::string &s, double &out) { return parseNumber(s, out) && std::isfinite(out); }
    }

    bool parsePitchName(const std::string &text, int &midi, std::string &err)
    {
        size_t i = 0;
        int pc = 0;
        if (!root(text, i, pc) || i >= text.size()) { err = "`" + text + "` is not a pitch (a number 0–127, or a name: C4 = 60, F#3, Bb2)"; return false; }
        const std::string oct = text.substr(i);
        const bool neg = oct[0] == '-';
        const std::string digits = neg ? oct.substr(1) : oct;
        if (digits.empty() || digits.size() > 2 || digits.find_first_not_of("0123456789") != std::string::npos)
        { err = "`" + text + "` is not a pitch (a number 0–127, or a name: C4 = 60, F#3, Bb2)"; return false; }
        const int octave = (neg ? -1 : 1) * std::atoi(digits.c_str());
        const int m = (octave + 1) * 12 + pc;
        if (m < 0 || m > 127) { err = "`" + text + "` is " + std::to_string(m) + ", outside MIDI's 0–127 (c-1 … G9)"; return false; }
        midi = m;
        return true;
    }

    std::string pitchName(int midi)
    {
        static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        const int m = std::clamp(midi, 0, 127);
        return std::string(names[m % 12]) + std::to_string(m / 12 - 1);
    }

    std::string padKey(const std::string &name)
    {
        std::string k;
        for (char c : name)
            if (c != ' ' && c != '-' && c != '_') k += (char)std::tolower((unsigned char)c);
        return k;
    }

    std::string padText(const std::string &name)
    {
        std::string k;
        for (char c : name) k += c == ' ' ? '-' : (char)std::tolower((unsigned char)c);
        return k;
    }

    const std::vector<ChordQuality> &chordQualities()
    {
        static const std::vector<ChordQuality> q = {
            {{"", "maj", "M"}, {0, 4, 7}, "major"},
            {{"m", "min", "-"}, {0, 3, 7}, "minor"},
            {{"5"}, {0, 7}, "power (root and fifth)"},
            {{"dim"}, {0, 3, 6}, "diminished"},
            {{"aug", "+"}, {0, 4, 8}, "augmented"},
            {{"sus2"}, {0, 2, 7}, "suspended second"},
            {{"sus4", "sus"}, {0, 5, 7}, "suspended fourth"},
            {{"6"}, {0, 4, 7, 9}, "major sixth"},
            {{"m6"}, {0, 3, 7, 9}, "minor sixth"},
            {{"7"}, {0, 4, 7, 10}, "dominant seventh"},
            {{"maj7", "M7"}, {0, 4, 7, 11}, "major seventh"},
            {{"m7", "min7", "-7"}, {0, 3, 7, 10}, "minor seventh"},
            {{"mmaj7"}, {0, 3, 7, 11}, "minor-major seventh"},
            {{"dim7"}, {0, 3, 6, 9}, "diminished seventh"},
            {{"m7b5"}, {0, 3, 6, 10}, "half-diminished"},
            {{"7sus4"}, {0, 5, 7, 10}, "dominant seventh, suspended fourth"},
            {{"add9"}, {0, 4, 7, 14}, "major, added ninth"},
            {{"madd9"}, {0, 3, 7, 14}, "minor, added ninth"},
            {{"9"}, {0, 4, 7, 10, 14}, "dominant ninth"},
            {{"maj9"}, {0, 4, 7, 11, 14}, "major ninth"},
            {{"m9"}, {0, 3, 7, 10, 14}, "minor ninth"},
        };
        return q;
    }

    bool chordPitches(const std::string &chord, int octave, int inversion, std::vector<int> &out, std::string &err)
    {
        size_t i = 0;
        int pc = 0;
        auto vocabulary = [] {
            std::string v;
            for (const auto &q : chordQualities()) v += (v.empty() ? "" : ", ") + (q.names[0].empty() ? std::string("(major)") : q.names[0]);
            return v;
        };
        if (!root(chord, i, pc)) { err = "`" + chord + "` is not a chord: a root A–G (#/b), then one of " + vocabulary(); return false; }
        const std::string quality = chord.substr(i);
        const ChordQuality *found = nullptr;
        for (const auto &q : chordQualities())
            if (std::find(q.names.begin(), q.names.end(), quality) != q.names.end()) found = &q;
        if (!found) { err = "unknown chord `" + chord + "`: after the root comes one of " + vocabulary(); return false; }
        const int n = (int)found->intervals.size();
        if (inversion < 0 || inversion >= n)
        { err = "--inversion for " + chord + " is 0 to " + std::to_string(n - 1) + " (it has " + std::to_string(n) + " notes)"; return false; }
        out.clear();
        const int base = (octave + 1) * 12 + pc;
        for (int k : found->intervals) out.push_back(base + k);
        for (int k = 0; k < inversion; ++k) out[(size_t)k] += 12;
        std::sort(out.begin(), out.end());
        if (out.front() < 0 || out.back() > 127)
        { err = chord + " at --octave " + std::to_string(octave) + " reaches " + std::to_string(out.front() < 0 ? out.front() : out.back()) + ", outside 0–127"; return false; }
        return true;
    }

    bool parseNoteToken(const std::string &token, NoteToken &out, std::string &err)
    {
        out = NoteToken();
        const auto atSign = token.find('@');
        if (atSign == std::string::npos || atSign == 0)
        { err = "`" + token + "` is not a note: <pitch>@<beat>[:<length>[:<vel>]], e.g. C4@0:0.5:90"; return false; }
        out.pitch = token.substr(0, atSign);
        std::vector<std::string> parts;
        std::string rest = token.substr(atSign + 1), cur;
        for (char c : rest)
        {
            if (c == ':') { parts.push_back(cur); cur.clear(); }
            else cur += c;
        }
        parts.push_back(cur);
        if (parts.size() > 3) { err = "`" + token + "` has too many `:` — <pitch>@<beat>[:<length>[:<vel>]]"; return false; }
        if (!number(parts[0], out.at) || out.at < 0) { err = "`" + token + "`: the beat `" + parts[0] + "` is not a number ≥ 0"; return false; }
        if (parts.size() > 1 && !parts[1].empty())
        {
            if (!number(parts[1], out.length) || !(out.length > 0)) { err = "`" + token + "`: the length `" + parts[1] + "` must be more than 0 beats"; return false; }
            out.hasLength = true;
        }
        if (parts.size() > 2 && !parts[2].empty())
        {
            double v = 0;
            if (!number(parts[2], v) || v != std::floor(v) || v < 1 || v > 127) { err = "`" + token + "`: the velocity `" + parts[2] + "` must be a whole number 1–127"; return false; }
            out.vel = (int)v;
            out.hasVel = true;
        }
        return true;
    }

    bool parseSteps(const std::string &text, std::vector<int> &steps, std::string &err)
    {
        steps.clear();
        for (char c : text)
        {
            if (c == ' ' || c == '\t' || c == '|') continue;
            if (c == 'x') steps.push_back(1);
            else if (c == 'X') steps.push_back(2);
            else if (c == '.' || c == '-' || c == '_') steps.push_back(0);
            else
            {
                err = "`" + std::string(1, c) + "` in the steps `" + text + "`: x = a note, X = an accent, . or - = a rest";
                return false;
            }
        }
        if (steps.empty()) { err = "no steps given: x = a note, X = an accent, . = a rest (e.g. \"x...x...x...x...\")"; return false; }
        return true;
    }
}
}
