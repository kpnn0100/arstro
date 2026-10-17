// SolarisService — composition at an agent's size (R-SVC-8): notes in bulk and by name, step rows,
// pattern edits, and the views that read a song back (`ls`, `show`, `pattern print`). An audit had an
// agent spend 110 commands on a 16-bar song, 64 of them single notes with every pitch computed by
// hand; these are the lines it was missing. Every edit here runs inside `dispatch`'s all-or-nothing,
// so a bad token anywhere in a `notes add` leaves the pattern as it was, and the whole line is ONE
// undo step.
#include "Compile.h"
#include "Format.h"
#include "Notation.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        bool sameTick(double a, double b) { return std::fabs(a - b) < 0.5 / kPpq; }
        void sortNotes(Pattern &pt)
        {
            std::stable_sort(pt.notes.begin(), pt.notes.end(), [](const Note &a, const Note &b) {
                return a.at != b.at ? a.at < b.at : a.pitch < b.pitch;
            });
        }
        // a note's place, as the notation writes it: "0", "1.5" — never "4.0"
        std::string beatText(double beats)
        {
            std::string s = canonicalBeats(beats);
            if (s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
            return s;
        }
        std::string q(const std::string &name) { return "\"" + name + "\""; }
        std::string countOf(size_t n, const char *one, const char *many) { return std::to_string(n) + " " + (n == 1 ? one : many); }
        double barBeats(const Project &p)
        {
            int num = 4, den = 4;
            std::sscanf(p.header.sig.c_str(), "%d/%d", &num, &den);
            return num > 0 && den > 0 ? num * 4.0 / den : 4.0;
        }
        bool beatsOf(const Command &c, const std::string &name, double fallback, double &out, std::string &err)
        {
            out = fallback;
            if (!c.has(name)) return true;
            if (!parseNumber(c.flag(name), out) || !std::isfinite(out) || out < 0)
            {
                err = "--" + name + " must be a number of beats ≥ 0, got `" + c.flag(name) + "`";
                return false;
            }
            out = toTick(out);
            return true;
        }
        bool velOf(const Command &c, int fallback, int &out, std::string &err)
        {
            out = fallback;
            if (!c.has("vel")) return true;
            double v = 0;
            if (!parseNumber(c.flag("vel"), v) || v != std::floor(v) || v < 1 || v > 127) { err = "--vel must be a whole number from 1 to 127"; return false; }
            out = (int)v;
            return true;
        }
        bool chanceOf(const Command &c, double &out, std::string &err)
        {
            if (!c.has("chance")) return true;
            if (!parseNumber(c.flag("chance"), out) || !(out >= 0 && out <= 1)) { err = "--chance is the odds a note sounds, 0 to 1"; return false; }
            return true;
        }
        // R-ROLL-11: a seeded stream — splitmix64, so the same seed on the same notes writes the same notes
        struct Rng
        {
            unsigned long long s;
            double next()
            {
                unsigned long long z = (s += 0x9E3779B97F4A7C15ULL);
                z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
                z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
                return (double)((z ^ (z >> 31)) >> 11) / 9007199254740992.0;
            }
        };
        // --seed, or a hash of the notes (so a second run on the changed notes is a new draw), short enough to read back
        bool seedOf(const Command &c, const Pattern &pt, unsigned long long &out, std::string &err)
        {
            if (c.has("seed"))
            {
                double v = 0;
                if (!parseNumber(c.flag("seed"), v) || v != std::floor(v) || v < 0 || v > 9e15) { err = "--seed is a whole number ≥ 0"; return false; }
                out = (unsigned long long)v;
                return true;
            }
            unsigned long long h = 1469598103934665603ULL;
            for (const auto &n : pt.notes)
                for (long long v : {(long long)n.pitch, (long long)std::llround(n.at * kPpq), (long long)std::llround(n.length * kPpq), (long long)n.vel})
                {
                    h ^= (unsigned long long)v;
                    h *= 1099511628211ULL;
                }
            out = h % 1000000;
            return true;
        }
        // every word of the positionals after the pattern: `notes add pt_1 "C4@0 E4@1" G4@2` is three notes
        std::vector<std::string> wordsAfterFirst(const Command &c)
        {
            std::vector<std::string> out;
            for (size_t i = 1; i < c.args.size(); ++i)
                for (const auto &w : tokenize(c.args[i])) out.push_back(w);
            return out;
        }
    }

    // ── pitches ─────────────────────────────────────────────────────────────────────────────────

    bool SolarisService::pitchOf(const std::string &text, const std::string &pattern, int &midi, std::string &err) const
    {
        double v = 0;
        if (parseNumber(text, v) && std::isfinite(v))
        {
            if (v != std::floor(v) || v < 0 || v > 127) { err = "a pitch number is a whole number 0–127, got " + text; return false; }
            midi = (int)v;
            return true;
        }
        std::string nameErr;
        if (parsePitchName(text, midi, nameErr)) return true;
        // a kit's pad: the instruments whose strips play this pattern — or, when none does yet, every kit
        // the registry has (a pattern written before its clip)
        std::vector<const DeviceType *> kits;
        for (const auto &c : mProject.clips)
        {
            if (c.pattern != pattern) continue;
            const Rack *rk = mProject.rack(c.track);
            const DeviceType *t = rk && !rk->devices.empty() ? DeviceRegistry::find(rk->devices.front().type) : nullptr;
            if (t && !t->noteNames.empty() && std::find(kits.begin(), kits.end(), t) == kits.end()) kits.push_back(t);
        }
        bool played = false;
        for (const auto &c : mProject.clips) played |= c.pattern == pattern;
        if (!played)
            for (const auto &t : DeviceRegistry::types())
                if (!t.noteNames.empty()) kits.push_back(&t);
        const std::string key = padKey(text);
        for (const DeviceType *t : kits)
            for (const auto &nn : t->noteNames)
                if (padKey(nn.second) == key) { midi = nn.first; return true; }
        if (nameErr.find("outside") != std::string::npos || kits.empty())
        {
            err = nameErr;
            if (kits.empty()) err += "; " + pattern + " plays through no kit, so a pad name does not apply";
            return false;
        }
        err = "`" + text + "` is not a pitch (60, C4, F#3) nor a pad of " + kits.front()->name + " (pads:";
        for (const auto &nn : kits.front()->noteNames) err += " " + padText(nn.second);
        err += ")";
        return false;
    }

    std::string SolarisService::pastEndWarning(const Pattern &pt) const
    {
        // notes that start at or after the end spill into the pattern's next repeat (Compile loops it at its
        // length) — said at once, and by `audit`
        size_t n = 0;
        double last = 0;
        for (const auto &x : pt.notes)
            if (x.at >= pt.length - 0.5 / kPpq) { ++n; last = std::max(last, x.at + x.length); }
        if (!n) return std::string();
        const double bar = barBeats(mProject);
        return "warning: " + countOf(n, "note of ", "notes of ") + pt.id + (n == 1 ? " starts" : " start") + " at or after its end (beat " + beatText(pt.length) +
               ") — they play over its next repeat; set " + pt.id + ".length=" + beatText(std::ceil(last / bar - 1e-9) * bar) + "\n";
    }

    std::string SolarisService::growToHold(Pattern &pt)
    {
        // R-ROLL-9: a pattern holds what is written in it — grown to the end of the bar that holds its last note's
        // end, in the same edit; a clip that played it whole (its length the pattern's) grows with it; a clip cut
        // shorter, or looping on past it, stays as it is
        double end = 0;
        for (const auto &n : pt.notes) end = std::max(end, n.at + n.length);
        if (end <= pt.length + 0.5 / kPpq) return std::string();
        const double bar = barBeats(mProject), old = pt.length, grown = std::ceil(end / bar - 1e-9) * bar;
        pt.length = toTick(grown);
        std::string clips;
        for (auto &c : mProject.clips)
            if (c.pattern == pt.id && std::fabs(c.length - old) < 0.5 / kPpq)
            {
                c.length = pt.length;
                clips += (clips.empty() ? "; " : ", ") + c.id;
            }
        changed("pattern.length", pt.id);
        return pt.id + " grew to " + beatText(pt.length) + " beats to hold its notes" + (clips.empty() ? "" : clips + " with it") + "\n";
    }

    // ── the edits ───────────────────────────────────────────────────────────────────────────────

    bool SolarisService::composeCommand(const Command &c, std::string &err)
    {
        Project &p = mProject;
        Pattern *pt = p.pattern(c.arg(0));
        if (!pt)
        {
            std::vector<std::string> ids;
            for (const auto &x : p.patterns) ids.push_back(x.id);
            err = "no pattern `" + c.arg(0) + "`";
            const auto near = nearest(c.arg(0), ids);
            if (!near.empty()) err += " (did you mean: " + near[0] + "?)";
            return false;
        }
        // the notes `words` name by pitch@beat (indexes into pt->notes) — refused whole, naming the first that is none
        auto named = [&](const std::vector<std::string> &words, std::vector<size_t> &which, const char *nothing) {
            for (size_t i = 0; i < words.size(); ++i)
            {
                NoteToken t;
                int pitch = 0;
                std::string why;
                if (!parseNoteToken(words[i], t, why) || !pitchOf(t.pitch, pt->id, pitch, why))
                { err = "note " + std::to_string(i + 1) + " `" + words[i] + "`: " + why + " — " + nothing; return false; }
                size_t k = pt->notes.size();
                for (size_t j = 0; j < pt->notes.size(); ++j)
                    if (pt->notes[j].pitch == pitch && sameTick(pt->notes[j].at, toTick(t.at))) k = j;
                if (k == pt->notes.size()) { err = "note " + std::to_string(i + 1) + " `" + words[i] + "`: " + pt->id + " has no note there — " + nothing; return false; }
                if (std::find(which.begin(), which.end(), k) == which.end()) which.push_back(k);
            }
            return true;
        };
        // R-ROLL-11: what a tool acts on — the notes --notes names (the roll's selection), else every one
        auto scope = [&](std::vector<size_t> &which) {
            if (!c.has("notes"))
            {
                for (size_t k = 0; k < pt->notes.size(); ++k) which.push_back(k);
                return true;
            }
            const auto words = tokenize(c.flag("notes"));
            if (words.empty()) { err = "--notes names notes: \"<pitch>@<beat> …\""; return false; }
            return named(words, which, "nothing changed");
        };
        const int mask = scaleMaskOf(*pt); // R-ROLL-11: 0 unless the pattern is locked to a scale
        switch (c.kind)
        {
        case K::NotesAdd:
        {
            double length = 0.25;
            int vel = 100;
            if (!beatsOf(c, "length", 0.25, length, err) || !velOf(c, 100, vel, err)) return false;
            if (!(length > 0)) { err = "--length must be more than 0 beats"; return false; }
            const auto words = wordsAfterFirst(c);
            if (words.empty()) { err = "notes add needs notes: <pitch>@<beat>[:<length>[:<vel>]] …"; return false; }
            std::vector<Note> add;
            for (size_t i = 0; i < words.size(); ++i)
            {
                NoteToken t;
                Note n;
                std::string why;
                if (!parseNoteToken(words[i], t, why) || !pitchOf(t.pitch, pt->id, n.pitch, why))
                {
                    err = "note " + std::to_string(i + 1) + " `" + words[i] + "`: " + why + " — nothing was added";
                    return false;
                }
                n.at = toTick(t.at);
                n.length = t.hasLength ? toTick(t.length) : length;
                if (!(n.length > 0)) { err = "note " + std::to_string(i + 1) + " `" + words[i] + "`: shorter than a tick"; return false; }
                n.vel = t.hasVel ? t.vel : vel;
                n.chance = t.hasChance ? t.chance : 1.0; // R-ROLL-11
                n.pitch = snapToScale(n.pitch, mask, 0); // … and on the scale, when the pattern has one
                add.push_back(n);
            }
            for (const auto &n : add)
            {
                pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(),
                                               [&](const Note &o) { return o.pitch == n.pitch && sameTick(o.at, n.at); }),
                                pt->notes.end()); // a note again replaces, as `note add` does
                pt->notes.push_back(n);
            }
            sortNotes(*pt);
            changed("notes.added", pt->id);
            mOutput = countOf(add.size(), "note", "notes") + "\n" + growToHold(*pt); // R-ROLL-9 (amends R-SVC-8's warning)
            return true;
        }
        case K::NotesMove:
        case K::NotesDelete:
        {
            // R-ROLL-8: a selection, as ONE edit — each note named by pitch@beat; refused whole, naming the first
            const bool move = c.kind == K::NotesMove;
            double by = 0;
            int semi = 0;
            if (move)
            {
                // --by may be negative: a selection moves left as well as right
                if (c.has("by") && (!parseNumber(c.flag("by"), by) || !std::isfinite(by) || std::fabs(by) > 1e6))
                { err = "--by is a number of beats (negative moves earlier), got `" + c.flag("by") + "`"; return false; }
                double s = 0;
                if (c.has("semi") && (!parseNumber(c.flag("semi"), s) || s != std::floor(s) || std::fabs(s) > 127))
                { err = "--semi is a whole number of semitones"; return false; }
                semi = (int)s;
            }
            const auto words = wordsAfterFirst(c);
            if (words.empty()) { err = std::string("notes ") + (move ? "move" : "delete") + " needs notes: <pitch>@<beat> …"; return false; }
            std::vector<size_t> which; // indexes into pt->notes
            if (!named(words, which, "nothing changed")) return false;
            if (!move)
            {
                std::sort(which.rbegin(), which.rend());
                for (size_t k : which) pt->notes.erase(pt->notes.begin() + (long)k);
                changed("notes.deleted", pt->id);
                mOutput = countOf(which.size(), "note", "notes") + " deleted\n";
                return true;
            }
            const double shift = toTick(by);
            std::vector<int> tos; // R-ROLL-11: in a scale-locked pattern each goes on to the scale's next pitch the way it moved
            for (size_t i = 0; i < which.size(); ++i)
            {
                const Note &n = pt->notes[which[i]];
                int to = n.pitch + semi;
                const double at = n.at + shift;
                const std::string name = std::to_string(n.pitch) + "@" + beatText(n.at);
                if (to < 0 || to > 127) { err = "note " + name + " would go to pitch " + std::to_string(to) + ", outside 0–127 — nothing moved"; return false; }
                if (semi) to = snapToScale(to, mask, semi > 0 ? 1 : -1);
                if (at < -0.5 / kPpq) { err = "note " + name + " would start before beat 0 — nothing moved"; return false; }
                for (size_t j = 0; j < pt->notes.size(); ++j)
                    if (std::find(which.begin(), which.end(), j) == which.end() && pt->notes[j].pitch == to && sameTick(pt->notes[j].at, at))
                    { err = "note " + name + " would land on " + std::to_string(to) + "@" + beatText(at) + ", which is not moved — nothing moved"; return false; }
                for (size_t h = 0; h < i; ++h)
                    if (tos[h] == to && sameTick(pt->notes[which[h]].at, pt->notes[which[i]].at))
                    { err = "note " + name + " would land on " + std::to_string(to) + "@" + beatText(at) + " with another moved there — nothing moved"; return false; }
                tos.push_back(to);
            }
            for (size_t i = 0; i < which.size(); ++i)
            {
                pt->notes[which[i]].pitch = tos[i];
                pt->notes[which[i]].at = std::max(0.0, pt->notes[which[i]].at + shift);
            }
            sortNotes(*pt);
            changed("notes.moved", pt->id);
            mOutput = countOf(which.size(), "note", "notes") + " moved\n" + growToHold(*pt);
            return true;
        }
        case K::NotesSet:
        {
            // R-ROLL-11: a selection's velocity, chance and/or length, ONE edit
            if (!c.has("vel") && !c.has("chance") && !c.has("length")) { err = "notes set needs --vel, --chance and/or --length"; return false; }
            int vel = 0;
            double chance = 1, length = 0;
            if (!velOf(c, 100, vel, err) || !chanceOf(c, chance, err) || !beatsOf(c, "length", 0.25, length, err)) return false;
            if (c.has("length") && !(length > 0)) { err = "--length must be more than 0 beats"; return false; }
            const auto words = wordsAfterFirst(c);
            if (words.empty()) { err = "notes set needs notes: <pitch>@<beat> …"; return false; }
            std::vector<size_t> which;
            if (!named(words, which, "nothing changed")) return false;
            for (size_t k : which)
            {
                Note &n = pt->notes[k];
                if (c.has("vel")) n.vel = vel;
                if (c.has("chance")) n.chance = chance;
                if (c.has("length")) n.length = length;
            }
            changed("notes.set", pt->id);
            mOutput = countOf(which.size(), "note", "notes") + " set\n" + growToHold(*pt);
            return true;
        }
        case K::PatternScale:
        {
            std::string text;
            for (size_t i = 1; i < c.args.size(); ++i) text += (text.empty() ? "" : " ") + c.args[i];
            if (text == "off")
            {
                pt->scale.clear();
                changed("pattern.scale", pt->id);
                mOutput = pt->id + ": no scale\n";
                return true;
            }
            std::string canon;
            int m = 0;
            if (!parseScale(text, canon, m, err)) return false;
            pt->scale = canon;
            int key = 0;
            parsePitchName(canon.substr(0, canon.find(' ')) + "4", key, err);
            std::string names;
            for (int i = 0; i < 12; ++i)
                if (m >> ((key + i) % 12) & 1)
                {
                    std::string nm = pitchName(key + i);
                    nm.resize(nm.find_first_of("-0123456789"));
                    names += " " + nm;
                }
            changed("pattern.scale", pt->id);
            mOutput = canon + ":" + names + "\n";
            return true;
        }
        case K::PatternQuantize:
        {
            double grid = 0.25, swing = 0, strength = 1;
            if (c.has("grid") && (!parseNumber(c.flag("grid"), grid) || !(grid >= 1.0 / 32 && grid <= 4)))
            { err = "--grid must be a step from 1/32 to 4 beats"; return false; }
            if (c.has("swing") && (!parseNumber(c.flag("swing"), swing) || !(swing >= 0 && swing <= 0.75)))
            { err = "--swing must be 0 to 0.75 of a step"; return false; }
            if (c.has("strength") && (!parseNumber(c.flag("strength"), strength) || !(strength >= 0 && strength <= 1)))
            { err = "--strength is how far each note goes, 0 to 1"; return false; }
            const Groove *groove = nullptr; // R-ROLL-11
            if (c.has("groove"))
            {
                if (c.has("grid") || c.has("swing")) { err = "a --groove carries its own grid and swing — give it without --grid and --swing"; return false; }
                groove = findGroove(c.flag("groove"));
                if (!groove)
                {
                    std::string names;
                    for (const auto &g : grooves()) names += (names.empty() ? "" : ", ") + g.name;
                    err = "no groove `" + c.flag("groove") + "` (" + names + ")";
                    return false;
                }
            }
            std::vector<size_t> which;
            if (!scope(which)) return false;
            for (size_t k : which)
            {
                Note &n = pt->notes[k];
                const double step = groove ? groove->step : grid;
                const long long s = std::llround(n.at / step);
                double target = s * step + ((s % 2) ? swing * grid : 0.0), accent = 1.0; // every second step pushed late
                if (groove)
                {
                    const long long cyc = (long long)groove->offset.size(), i = ((s % cyc) + cyc) % cyc;
                    target = s * step + groove->offset[(size_t)i] * step;
                    accent = groove->accent[(size_t)i];
                }
                n.at = toTick(std::max(0.0, n.at + strength * (target - n.at)));
                if (accent != 1.0) n.vel = std::clamp((int)std::lround(n.vel * (1.0 + strength * (accent - 1.0))), 1, 127);
            }
            // notes that landed on one another merge: the louder stays
            std::stable_sort(pt->notes.begin(), pt->notes.end(), [](const Note &a, const Note &b) {
                return a.at != b.at ? a.at < b.at : (a.pitch != b.pitch ? a.pitch < b.pitch : a.vel > b.vel);
            });
            pt->notes.erase(std::unique(pt->notes.begin(), pt->notes.end(), [](const Note &a, const Note &b) {
                                return a.pitch == b.pitch && sameTick(a.at, b.at);
                            }),
                            pt->notes.end());
            changed("pattern.quantized", pt->id);
            return true;
        }
        case K::PatternHumanise:
        {
            double timing = 0.02;
            int amount = 8;
            if (c.has("timing") || c.has("vel")) { timing = 0; amount = 0; } // naming one leaves the other alone
            if (c.has("timing") && (!parseNumber(c.flag("timing"), timing) || !(timing >= 0 && timing <= 1)))
            { err = "--timing is how far a start may move, 0 to 1 beat either way"; return false; }
            if (c.has("vel"))
            {
                double v = 0;
                if (!parseNumber(c.flag("vel"), v) || v != std::floor(v) || v < 0 || v > 127) { err = "--vel is how far a velocity may move, a whole number 0 to 127 either way"; return false; }
                amount = (int)v;
            }
            std::vector<size_t> which;
            unsigned long long seed = 0;
            if (!scope(which) || !seedOf(c, *pt, seed, err)) return false;
            Rng rng{seed};
            for (size_t k : which)
            {
                Note &n = pt->notes[k];
                // a triangular spread (two draws summed): most moves small, none past the amount
                const double at = toTick(std::max(0.0, n.at + timing * (rng.next() + rng.next() - 1.0)));
                const int dv = (int)std::lround(amount * (rng.next() + rng.next() - 1.0));
                bool clash = false;
                for (size_t j = 0; j < pt->notes.size(); ++j) clash |= j != k && pt->notes[j].pitch == n.pitch && sameTick(pt->notes[j].at, at);
                if (!clash) n.at = at; // onto another of its pitch: it keeps its time
                n.vel = std::clamp(n.vel + dv, 1, 127);
            }
            sortNotes(*pt);
            changed("pattern.humanised", pt->id);
            mOutput = countOf(which.size(), "note", "notes") + " humanised (seed " + std::to_string(seed) + ")\n" + growToHold(*pt);
            return true;
        }
        case K::PatternLegato:
        {
            std::vector<size_t> which;
            if (!scope(which)) return false;
            std::vector<double> starts;
            for (size_t k : which) starts.push_back(pt->notes[k].at);
            std::sort(starts.begin(), starts.end());
            for (size_t k : which)
            {
                Note &n = pt->notes[k];
                const auto next = std::upper_bound(starts.begin(), starts.end(), n.at + 0.5 / kPpq);
                const double end = next != starts.end() ? *next : pt->length; // a chord's notes end together; the last at the end
                if (end > n.at + 0.5 / kPpq) n.length = toTick(end - n.at);
            }
            changed("pattern.legato", pt->id);
            mOutput = countOf(which.size(), "note", "notes") + " legato\n";
            return true;
        }
        case K::PatternArpeggiate:
        {
            double rate = 0.25, gate = 1;
            int octaves = 1;
            const std::string mode = c.flag("mode", "up");
            if (c.has("rate") && (!parseNumber(c.flag("rate"), rate) || !(rate >= 1.0 / 32 - 1e-9 && rate <= 4)))
            { err = "--rate is a step from 1/32 to 4 beats"; return false; }
            if (c.has("gate") && (!parseNumber(c.flag("gate"), gate) || !(gate >= 0.05 && gate <= 1)))
            { err = "--gate is how much of a step a note lasts, 0.05 to 1"; return false; }
            if (c.has("octaves"))
            {
                double o = 0;
                if (!parseNumber(c.flag("octaves"), o) || o != std::floor(o) || o < 1 || o > 4) { err = "--octaves is 1 to 4"; return false; }
                octaves = (int)o;
            }
            if (mode != "up" && mode != "down" && mode != "updown" && mode != "random")
            { err = "--mode is up, down, updown or random, got `" + mode + "`"; return false; }
            std::vector<size_t> which;
            unsigned long long seed = 0;
            if (!scope(which) || !seedOf(c, *pt, seed, err)) return false;
            if (which.empty()) { err = pt->id + " has no notes to arpeggiate"; return false; }
            Rng rng{seed};
            std::vector<Note> src;
            double from = 1e300, to = 0;
            for (size_t k : which)
            {
                src.push_back(pt->notes[k]);
                from = std::min(from, pt->notes[k].at);
                to = std::max(to, pt->notes[k].at + pt->notes[k].length);
            }
            const double eps = 0.5 / kPpq;
            std::vector<Note> out;
            long long counter = 0; // the arpeggio's place: back to its first after a rest
            for (long long s = (long long)std::ceil(from / rate - 1e-9); s * rate < to - eps; ++s)
            {
                const double t = toTick(s * rate);
                std::vector<const Note *> held;
                for (const auto &n : src)
                    if (n.at <= t + eps && n.at + n.length > t + eps) held.push_back(&n);
                if (held.empty()) { counter = 0; continue; }
                std::sort(held.begin(), held.end(), [](const Note *a, const Note *b) { return a->pitch != b->pitch ? a->pitch < b->pitch : a->vel > b->vel; });
                held.erase(std::unique(held.begin(), held.end(), [](const Note *a, const Note *b) { return a->pitch == b->pitch; }), held.end());
                std::vector<std::pair<int, const Note *>> pool; // its pitches, then an octave up, …
                for (int o = 0; o < octaves; ++o)
                    for (const Note *h : held)
                        if (h->pitch + 12 * o <= 127) pool.emplace_back(h->pitch + 12 * o, h);
                const long long m = (long long)pool.size();
                long long i = counter % m;
                if (mode == "down") i = m - 1 - counter % m;
                else if (mode == "updown")
                {
                    const long long cyc = m > 1 ? 2 * m - 2 : 1, p = counter % cyc; // the turns not repeated
                    i = p < m ? p : 2 * m - 2 - p;
                }
                else if (mode == "random") i = std::min(m - 1, (long long)(rng.next() * (double)m));
                Note x;
                x.pitch = pool[(size_t)i].first;
                x.at = t;
                x.length = toTick(std::max(rate * gate, 1.0 / kPpq));
                x.vel = pool[(size_t)i].second->vel;
                x.chance = pool[(size_t)i].second->chance;
                out.push_back(x);
                ++counter;
            }
            std::sort(which.rbegin(), which.rend());
            for (size_t k : which) pt->notes.erase(pt->notes.begin() + (long)k);
            for (const auto &x : out)
            {
                pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(), [&](const Note &o) { return o.pitch == x.pitch && sameTick(o.at, x.at); }),
                                pt->notes.end()); // a step on another of its pitch replaces it
                pt->notes.push_back(x);
            }
            sortNotes(*pt);
            changed("pattern.arpeggiated", pt->id);
            mOutput = countOf(src.size(), "note", "notes") + " → " + countOf(out.size(), "step", "steps") + " (" + mode + ")\n" + growToHold(*pt);
            return true;
        }
        case K::PatternSteps:
        {
            if (!c.has("pitch")) { err = "pattern steps needs --pitch (a number, a name or a pad: kick)"; return false; }
            int pitch = 0, vel = 100;
            if (!pitchOf(c.flag("pitch"), pt->id, pitch, err) || !velOf(c, 100, vel, err)) return false;
            double step = 0.25, at = 0, length = 0;
            if (!beatsOf(c, "step", 0.25, step, err) || !beatsOf(c, "at", 0, at, err)) return false;
            if (!(step > 0)) { err = "--step must be more than 0 beats"; return false; }
            if (!beatsOf(c, "length", step, length, err)) return false;
            if (!(length > 0)) { err = "--length must be more than 0 beats"; return false; }
            std::string text;
            for (size_t i = 1; i < c.args.size(); ++i) text += c.args[i];
            std::vector<int> steps;
            if (!parseSteps(text, steps, err)) return false;
            const double end = at + steps.size() * step;
            // the row REPLACES that pitch's notes in its span: what is not an x there is a rest now
            pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(),
                                           [&](const Note &n) { return n.pitch == pitch && n.at >= at - 0.5 / kPpq && n.at < end - 0.5 / kPpq; }),
                            pt->notes.end());
            size_t hits = 0;
            for (size_t i = 0; i < steps.size(); ++i)
            {
                if (!steps[i]) continue;
                Note n;
                n.pitch = pitch;
                n.at = toTick(at + i * step);
                n.length = length;
                n.vel = steps[i] == 2 ? 127 : vel;
                pt->notes.push_back(n);
                ++hits;
            }
            sortNotes(*pt);
            changed("pattern.steps", pt->id);
            mOutput = countOf(hits, "note", "notes") + "\n" + growToHold(*pt); // R-ROLL-9
            return true;
        }
        case K::PatternDuplicate:
        {
            Pattern copy = *pt;
            copy.id = p.nextId("pt");
            copy.name = c.flag("name", pt->name + " copy");
            copy.remarks = Remarks();
            p.patterns.push_back(copy);
            changed("pattern.added", copy.id);
            mOutput = copy.id + "\n";
            return true;
        }
        case K::PatternClear:
        {
            int pitch = -1;
            if (c.has("pitch") && !pitchOf(c.flag("pitch"), pt->id, pitch, err)) return false;
            const size_t before = pt->notes.size();
            pt->notes.erase(std::remove_if(pt->notes.begin(), pt->notes.end(), [&](const Note &n) { return pitch < 0 || n.pitch == pitch; }),
                            pt->notes.end());
            changed("pattern.cleared", pt->id);
            mOutput = countOf(before - pt->notes.size(), "note", "notes") + " removed\n";
            return true;
        }
        case K::PatternDelete:
        {
            std::string users;
            for (const auto &cl : p.clips)
                if (cl.pattern == pt->id) users += (users.empty() ? "" : ", ") + cl.id;
            for (const auto &sl : p.slots) // R-LAUNCH-1: a slot plays it too
                if (sl.pattern == pt->id) users += (users.empty() ? "" : ", ") + sl.id;
            if (!users.empty()) { err = pt->id + " is played by " + users + " — delete those clips (or `clip unique` them) first"; return false; }
            const std::string id = pt->id;
            p.patterns.erase(std::remove_if(p.patterns.begin(), p.patterns.end(), [&](const Pattern &x) { return x.id == id; }), p.patterns.end());
            changed("pattern.deleted", id);
            return true;
        }
        case K::PatternTranspose:
        {
            double semi = 0;
            if (!c.has("semi")) { err = "pattern transpose needs --semi <n> (negative = down)"; return false; }
            if (!parseNumber(c.flag("semi"), semi) || semi != std::floor(semi) || std::fabs(semi) > 127)
            { err = "--semi must be a whole number of semitones, got `" + c.flag("semi") + "`"; return false; }
            for (const auto &n : pt->notes)
                if (n.pitch + semi < 0 || n.pitch + semi > 127)
                {
                    err = pitchName(n.pitch) + " (" + std::to_string(n.pitch) + ") at beat " + beatText(n.at) + " would be " +
                          std::to_string(n.pitch + (int)semi) + ", outside 0–127 — nothing moved";
                    return false;
                }
            for (auto &n : pt->notes) n.pitch += (int)semi;
            sortNotes(*pt);
            changed("pattern.transposed", pt->id);
            return true;
        }
        default:
            return false;
        }
    }

    // ── reading back ────────────────────────────────────────────────────────────────────────────

    bool SolarisService::readCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        switch (c.kind)
        {
        case K::Ls:
            mOutput = lsText();
            return true;
        case K::Show:
            return showText(c.arg(0), mOutput, err);
        case K::PatternPrint:
        {
            const Pattern *pt = mProject.pattern(c.arg(0));
            if (!pt) { err = "no pattern `" + c.arg(0) + "`"; return false; }
            // a kit names its keys: print a pad by name where the kit that plays it has one
            const DeviceType *kit = nullptr;
            std::string users;
            for (const auto &cl : mProject.clips)
                if (cl.pattern == pt->id)
                {
                    users += (users.empty() ? "" : ", ") + cl.id;
                    const Rack *rk = mProject.rack(cl.track);
                    const DeviceType *t = rk && !rk->devices.empty() ? DeviceRegistry::find(rk->devices.front().type) : nullptr;
                    if (!kit && t && !t->noteNames.empty()) kit = t;
                }
            std::ostringstream o;
            o << "# " << pt->id << " " << q(pt->name) << " · " << beatText(pt->length) << " beats · " << countOf(pt->notes.size(), "note", "notes")
              << " · " << (users.empty() ? std::string("no clip plays it") : "played by " + users) << (pt->scale.empty() ? "" : " · " + pt->scale) << "\n";
            for (const auto &n : pt->notes)
            {
                std::string name = pitchName(n.pitch);
                if (kit)
                    for (const auto &nn : kit->noteNames)
                        if (nn.first == n.pitch) name = padText(nn.second);
                o << name << "@" << beatText(n.at) << ":" << beatText(n.length) << ":" << n.vel << (n.chance < 1 ? ":" + canonicalNumber(n.chance) : std::string())
                  << "  # " << n.pitch
                  << (name != pitchName(n.pitch) ? " " + pitchName(n.pitch) : std::string()) << "\n";
            }
            mOutput = o.str();
            return true;
        }
        default:
            return false;
        }
    }

    std::string SolarisService::lsText() const
    {
        const Project &p = mProject;
        std::ostringstream o;
        double end = 0;
        for (const auto &c : p.clips) end = std::max(end, c.at + clipLengthBeats(p, c));
        o << "song " << q(p.header.name) << " · " << canonicalNumber(p.header.bpm) << " bpm · " << p.header.sig << " · "
          << beatText(end) << " beats" << (mModel.dirty ? " · unsaved" : "") << "\n";
        auto rackText = [&](const std::string &track) {
            std::string r;
            if (const Rack *rk = p.rack(track))
                for (const auto &d : rk->devices) r += (r.empty() ? "" : ", ") + d.type + " " + d.id + (d.bypass ? " (bypassed)" : "");
            return r.empty() ? r : " [" + r + "]";
        };
        for (const Mixer *m : p.mixersInOrder())
        {
            o << "mixer " << m->id << " " << q(m->name) << "\n";
            for (const Strip *s : p.stripsInOrder())
            {
                if (p.mixerOf(*s) != m) continue;
                o << "  " << s->id << " " << q(s->name) << " " << s->kind << rackText(s->id) << " → " << (s->out.empty() ? "master" : s->out);
                if (s->gain != 0) o << " · gain " << canonicalNumber(s->gain);
                if (s->pan != 0) o << " · pan " << canonicalNumber(s->pan);
                if (s->mute) o << " · muted";
                if (s->solo) o << " · solo";
                for (const auto &sd : p.sends)
                    if (sd.from == s->id)
                        o << " · " << sd.id << " → " << sd.to << " " << canonicalNumber(sd.gain) << " dB" << (sd.pre ? " pre" : "")
                          << (sd.sidechain ? " key" : "");
                o << "\n";
            }
        }
        o << "master" << rackText("master") << " → ";
        for (size_t i = 0; i < p.header.masterOut.size(); ++i) o << (i ? ", " : "") << p.header.masterOut[i];
        o << (p.header.masterOut.empty() ? "nothing" : "") << "\n";
        std::vector<const Lane *> lanes;
        for (const auto &l : p.lanes) lanes.push_back(&l);
        std::stable_sort(lanes.begin(), lanes.end(), [](const Lane *a, const Lane *b) { return a->order < b->order; });
        auto clipLine = [&](const Clip &c) {
            o << "  " << c.id << " @" << beatText(c.at) << " len " << beatText(clipLengthBeats(p, c)) << " ";
            if (c.isAudio()) o << q(c.src);
            else o << c.pattern;
            o << " via " << c.track << "\n";
        };
        std::string lastGroup;
        for (const Lane *l : lanes)
        {
            if (!l->group.empty() && l->group != lastGroup) // R-ARR-3: a group, before its first lane
                if (const LaneGroup *g = p.group(l->group)) o << "group " << g->id << " " << q(g->name) << (g->folded ? " · folded" : "") << "\n";
            lastGroup = l->group;
            o << "lane " << l->id << " " << q(laneTitle(p, *l)) << (l->track.empty() ? std::string() : " · track of " + l->track)
              << (l->group.empty() ? std::string() : " · in " + l->group) << "\n";
            for (const auto &c : p.clips)
                if (c.lane == l->id) clipLine(c);
        }
        bool loose = false;
        for (const auto &c : p.clips)
            if (!p.lane(c.lane))
            {
                if (!loose) o << "no lane\n";
                loose = true;
                clipLine(c);
            }
        for (const auto &pt : p.patterns)
        {
            size_t clips = 0;
            for (const auto &c : p.clips) clips += c.pattern == pt.id ? 1 : 0;
            o << "pattern " << pt.id << " " << q(pt.name) << " " << beatText(pt.length) << " beats · " << countOf(pt.notes.size(), "note", "notes")
              << " · " << countOf(clips, "clip", "clips") << "\n";
        }
        {
            std::vector<const Marker *> mks; // R-ARR-1: by time, each to its section's end
            for (const auto &mk : p.markers) mks.push_back(&mk);
            std::stable_sort(mks.begin(), mks.end(), [](const Marker *a, const Marker *b) { return a->at < b->at; });
            for (const Marker *mk : mks)
            {
                const double end = p.sectionEnd(*mk, songEndBeats());
                o << "marker " << mk->id << " " << q(mk->name) << " @" << beatText(mk->at) << (end > mk->at ? " .. " + beatText(end) : std::string())
                  << (mk->length > 0 ? " (a section)" : "") << "\n";
            }
        }
        for (const auto &a : p.automations)
        {
            std::string drives;
            for (const auto &address : readersOf(a.id)) drives += (drives.empty() ? "" : ", ") + address;
            o << "automation " << a.id << " " << q(a.name) << " " << countOf(a.points.size(), "point", "points")
              << (drives.empty() ? " · drives nothing" : " → " + drives) << "\n";
        }
        return o.str();
    }

    bool SolarisService::showText(const std::string &id, std::string &out, std::string &err) const
    {
        const Project &p = mProject;
        std::ostringstream o;
        auto field = [&](const std::string &name, const std::string &value) { o << "  " << name << " " << value << "\n"; };
        if (id == "project")
        {
            o << "project " << q(p.header.name) << "\n";
            field("bpm", canonicalNumber(p.header.bpm));
            field("sig", p.header.sig);
            field("sampleRate", std::to_string(p.header.sampleRate));
            if (p.header.masterGain != 0) field("masterGain", canonicalNumber(p.header.masterGain) + " dB");
            if (!mPath.empty()) field("path", mPath);
            if (mModel.dirty) field("unsaved", "true");
        }
        else if (const Strip *s = p.strip(id))
        {
            o << s->id << " strip " << q(s->name) << "\n";
            field("kind", s->kind);
            if (const Mixer *m = p.mixerOf(*s)) field("mixer", m->id + " " + q(m->name));
            field("out", s->out.empty() ? "master" : s->out);
            if (s->gain != 0) field("gain", canonicalNumber(s->gain) + " dB");
            if (s->pan != 0) field("pan", canonicalNumber(s->pan));
            if (s->mute) field("mute", "true");
            if (s->solo) field("solo", "true");
            if (s->colour >= 0) field("colour", std::to_string(s->colour));
            if (const Rack *rk = p.rack(s->id))
            {
                std::string d;
                for (const auto &x : rk->devices) d += (d.empty() ? "" : ", ") + x.id + " " + x.type;
                if (!d.empty()) field("devices", d);
            }
            for (const auto &sd : p.sends)
                if (sd.from == s->id) field("send", sd.id + " → " + sd.to + " " + canonicalNumber(sd.gain) + " dB" + (sd.pre ? " pre" : "") + (sd.sidechain ? " key" : ""));
            std::string clips;
            for (const auto &c : p.clips)
                if (c.track == s->id) clips += (clips.empty() ? "" : ", ") + c.id;
            if (!clips.empty()) field("clips", clips);
        }
        else if (const Clip *c = p.clip(id))
        {
            o << c->id << " " << (c->isAudio() ? "audio" : "note") << " clip " << q(c->name) << "\n";
            field("at", beatText(c->at));
            field("length", beatText(clipLengthBeats(p, *c)) + (c->length > 0 ? "" : " (its " + std::string(c->isAudio() ? "file's" : "pattern's") + ")"));
            if (c->isAudio()) { field("src", c->src); field("in", canonicalSeconds(c->in) + " s"); field("out", canonicalSeconds(c->out) + " s"); }
            else field("pattern", c->pattern);
            field("strip", c->track);
            field("lane", c->lane.empty() ? "(none)" : c->lane);
            if (c->gain != 0) field("gain", canonicalNumber(c->gain) + " dB");
            if (c->fadeIn != 0) field("fadeIn", beatText(c->fadeIn));
            if (c->fadeOut != 0) field("fadeOut", beatText(c->fadeOut));
            if (c->loop) field("loop", "true");
        }
        else if (const Pattern *pt = p.pattern(id))
        {
            o << pt->id << " pattern " << q(pt->name) << "\n";
            field("length", beatText(pt->length) + " beats");
            field("notes", std::to_string(pt->notes.size()) + " (`pattern print " + pt->id + "`)");
            std::string clips;
            for (const auto &c : p.clips)
                if (c.pattern == pt->id) clips += (clips.empty() ? "" : ", ") + c.id;
            field("clips", clips.empty() ? "none" : clips);
            const std::string warn = pastEndWarning(*pt);
            if (!warn.empty()) o << "  " << warn;
        }
        else if (const Lane *l = p.lane(id))
        {
            o << l->id << " lane " << q(laneTitle(p, *l)) << "\n";
            if (!l->track.empty()) field("track of", l->track + (l->name.empty() ? " (its name shown)" : std::string()));
            if (l->colour >= 0) field("colour", std::to_string(l->colour));
            std::string clips;
            for (const auto &c : p.clips)
                if (c.lane == l->id) clips += (clips.empty() ? "" : ", ") + c.id;
            field("clips", clips.empty() ? "none" : clips);
        }
        else if (const Mixer *m = p.mixer(id))
        {
            o << m->id << " mixer " << q(m->name) << "\n";
            field("order", std::to_string(m->order));
            std::string strips;
            for (const Strip *s : p.stripsInOrder())
                if (p.mixerOf(*s) == m) strips += (strips.empty() ? "" : ", ") + s->id;
            field("strips", strips.empty() ? "none" : strips);
        }
        else if (const Automation *a = p.automation(id))
        {
            o << a->id << " automation " << q(a->name) << "\n";
            field("range", canonicalNumber(a->min) + " … " + canonicalNumber(a->max) + (a->unit.empty() ? "" : " " + a->unit));
            if (!a->from.empty()) field("from", a->from);
            for (const auto &pt : a->points) field("point", "@" + beatText(pt.at) + " " + canonicalNumber(pt.value) + " " + pt.shape);
        }
        else if (const Port *po = p.port(id))
        {
            o << po->id << " port " << q(po->name) << "\n";
            field("dir", po->dir);
            field("channels", std::to_string(po->channels));
        }
        else
        {
            for (const auto &sd : p.sends)
                if (sd.id == id)
                {
                    o << sd.id << " send\n";
                    field("from", sd.from);
                    field("to", sd.to);
                    field("gain", canonicalNumber(sd.gain) + " dB");
                    if (sd.pre) field("pre", "true");
                    if (sd.sidechain) field("sidechain", "true");
                }
            for (const auto &rk : p.racks)
                for (const auto &d : rk.devices)
                {
                    if (d.id != id) continue;
                    const DeviceType *t = DeviceRegistry::find(d.type);
                    o << d.id << " " << d.type << (t ? " (" + t->label + ")" : std::string(" (unknown to this build)")) << " on " << rk.track << "\n";
                    if (d.bypass) field("bypass", "true");
                    if (!d.sample.empty()) field("sample", d.sample);
                    size_t shown = 0;
                    if (t)
                        for (const auto &spec : t->params)
                        {
                            // its non-default parameters, with the unit and what the default was
                            const std::string address = d.id + "." + spec.name;
                            const Binding *b = p.binding(address);
                            double v = spec.def;
                            bool stored = false;
                            for (const auto &kv : d.params)
                                if (kv.first == spec.name) stored = parseParam(spec, kv.second, v);
                            if (!b && (!stored || paramText(spec, v) == paramText(spec, spec.def))) continue;
                            const std::string unit = spec.unit.empty() || spec.isChoice() ? std::string() : " " + spec.unit;
                            std::string line = paramText(spec, v) + unit + "  (default " + paramText(spec, spec.def) + ")";
                            if (b) line = b->formula + "  (a formula; its own value " + paramText(spec, v) + unit + ")";
                            field(spec.name, line);
                            ++shown;
                        }
                    if (!shown && t) o << "  (every parameter at its default — `api` lists them)\n";
                }
            if (o.str().empty())
            {
                std::vector<std::string> ids = p.allIds();
                ids.push_back("project");
                err = "no node `" + id + "`";
                const auto near = nearest(id, ids);
                if (!near.empty()) err += " (did you mean: " + near[0] + "?)";
                return false;
            }
        }
        out = o.str();
        return true;
    }
}
}
