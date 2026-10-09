#include "Command.h"
#include <algorithm>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    std::string Command::arg(size_t i, const std::string &fallback) const { return i < args.size() ? args[i] : fallback; }

    bool Command::has(const std::string &name) const
    {
        for (const auto &kv : flags)
            if (kv.first == name) return true;
        return false;
    }

    std::string Command::flag(const std::string &name, const std::string &fallback) const
    {
        for (const auto &kv : flags)
            if (kv.first == name) return kv.second;
        return fallback;
    }

    const std::vector<CommandSpec> &commandSpecs()
    {
        // The ONE grammar table (R-API-1). Order is the order the API document prints.
        static const std::vector<CommandSpec> specs = {
            {K::ProjectNew, "project new", "<path.slp>", 1, 1, {"bpm=<n>", "sig=<n/d>", "rate=<hz>", "name=<text>"},
             "Create a song and open it: Mixer 1 \"Sources\", Mixer 2 \"Buses\" with the bus \"Main\" → master, "
             "and the output port \"Main\" (R-MIX-3).", "R-HOME-1"},
            {K::ProjectOpen, "project open", "<path.slp>", 1, 1, {}, "Open a song.", "R-HOME-1"},
            {K::ProjectSave, "project save", "[path.slp]", 0, 1, {}, "Write the .slp (a path saves it there).", "R-FMT-2"},
            {K::ProjectClose, "project close", "", 0, 0, {}, "Close the song and return Home.", "R-UI-1"},

            {K::Set, "set", "<address>=<value> …", 1, -1, {},
             "Write addresses: project.bpm, <strip>.gain, <clip>.at, <device>.<param> (any DSP registry "
             "parameter, in its unit), … — `api` lists every one. An unknown address or parameter is refused. "
             "A value starting with `=` is a FORMULA that drives a number (R-AUTO); a plain number clears it.",
             "R-SVC-3", true},
            {K::Get, "get", "<address>", 1, 1, {}, "Print an address's stored value.", "R-SVC-1"},

            {K::MixerAdd, "mixer add", "[name]", 0, 1, {}, "Add a mixer page after the last one.", "R-MIX-3"},
            {K::MixerDelete, "mixer delete", "<mx>", 1, 1, {}, "Delete an empty mixer. Refused while strips live on it.", "R-MIX-3"},
            {K::MixerMove, "mixer move", "<mx>", 1, 1, {"to=<index>"},
             "Move a mixer to position <index> (0 = first). Refused if it would make any route point backward.", "R-MIX-4"},

            {K::StripAdd, "strip add", "", 0, 0,
             {"kind=<audio|instrument|bus>", "name=<text>", "mixer=<mx>", "instrument=<synth|drums>", "out=<target>"},
             "Add a strip. Default mixer: the first for audio/instrument, the second for a bus; default output: "
             "the first bus on a later mixer (\"Main\"), else master. An instrument strip gets its instrument "
             "(default synth) as its first device.", "R-MIX-1"},
            {K::StripDelete, "strip delete", "<ch>", 1, 1, {"with-clips"},
             "Delete a strip and its rack. Refused while clips play through it or strips route to it, unless "
             "--with-clips (its clips go too).", "R-MIX-1"},
            {K::StripMove, "strip move", "<ch>", 1, 1, {"mixer=<mx>", "order=<n>"},
             "Move a strip to another mixer and/or position. Refused if a route would point backward.", "R-MIX-3"},
            {K::Route, "route", "<ch>", 1, 1, {"to=<ch|master|port>"},
             "Set a strip's main output: a strip on a LATER mixer, master, or an output port.", "R-MIX-4"},

            {K::SendAdd, "send add", "<ch>", 1, 1, {"to=<ch|master|port>", "gain=<dB>", "pre"},
             "Add a send (post-fader unless --pre). Same forward-only rule as `route`.", "R-MIX-5"},
            {K::SendDelete, "send delete", "<sd>", 1, 1, {}, "Remove a send.", "R-MIX-5"},

            {K::DeviceAdd, "device add", "<ch|master>", 1, 1, {"type=<registry type>", "at=<index>"},
             "Insert a DSP registry device into a rack (default: at the end). An instrument goes only first on an "
             "instrument strip.", "R-FX-5"},
            {K::DeviceRemove, "device remove", "<dv>", 1, 1, {}, "Remove a device from its rack. An instrument strip keeps its instrument.", "R-FX-5"},
            {K::DeviceMove, "device move", "<dv>", 1, 1, {"to=<index>"}, "Move a device within its rack.", "R-FX-5"},

            {K::LaneAdd, "lane add", "[name]", 0, 1, {}, "Add a timeline lane at the bottom.", "R-LANE-1"},
            {K::LaneDelete, "lane delete", "<ln>", 1, 1, {"with-clips"},
             "Delete a lane. Refused while clips are drawn on it, unless --with-clips.", "R-LANE-1"},

            {K::ClipAdd, "clip add", "", 0, 0,
             {"src=<file>", "strip=<ch>", "instrument=<type>", "pattern=<pt>", "lane=<ln>", "at=<beats>", "length=<beats>", "in=<s>", "out=<s>"},
             "Place a clip. With --src: an audio clip; a file the song has not used gets its own new strip on the "
             "first mixer (R-MIX-2), a used one reuses its strip, and with no --lane it gets a new lane. With "
             "--strip <instrument>: a note clip of --pattern (a new empty pattern if none). With --instrument "
             "<type>: the same on a NEW instrument strip of that type — what dropping an instrument does (R-BROWSE-3).", "R-MIX-2"},
            {K::ClipMove, "clip move", "<ac>", 1, 1, {"at=<beats>", "lane=<ln>", "strip=<ch>"},
             "Move a clip in time, to another lane (where it is drawn), and/or another strip (what it sounds through).", "R-LANE-2"},
            {K::ClipDuplicate, "clip duplicate", "<ac>", 1, 1, {"at=<beats>"},
             "A copy right after it (or at --at). A note clip's copy plays the SAME pattern — linked.", "R-CLIP-3"},
            {K::ClipUnique, "clip unique", "<ac>", 1, 1, {}, "Give a note clip its own copy of its pattern.", "R-CLIP-3"},
            {K::ClipDelete, "clip delete", "<ac>", 1, 1, {}, "Remove a clip.", "R-LANE-1"},

            {K::PatternNew, "pattern new", "", 0, 0, {"name=<text>", "length=<beats>"}, "Create an empty pattern.", "R-CLIP-2"},
            {K::NoteAdd, "note add", "<pt>", 1, 1, {"pitch=<0-127>", "at=<beats>", "length=<beats>", "vel=<1-127>"},
             "Add a note to a pattern — every clip of it changes.", "R-CLIP-2"},
            {K::NoteDelete, "note delete", "<pt>", 1, 1, {"pitch=<0-127>", "at=<beats>"}, "Remove the note at that pitch and time.", "R-CLIP-2"},

            {K::AutoCreate, "auto create", "<address>", 1, 1, {},
             "Automate a number: a new automation named \"<owner> · <parameter>\", ranged as it, holding its value from "
             "beat 0 to the song's end, shown on the timeline — and the address bound to `=au_n`. Prints the id.", "R-AUTO-5"},
            {K::AutoAdd, "auto add", "", 0, 0, {"name=<text>", "min=<v>", "max=<v>", "unit=<text>"},
             "An automation from nothing (default range 0…1). It moves nothing until a formula reads it. Prints the id.", "R-AUTO-4"},
            {K::AutoDelete, "auto delete", "<au>", 1, 1, {"unbind"},
             "Delete an automation. Refused while a formula reads it, unless --unbind (those bindings are cleared).", "R-AUTO-4"},
            {K::AutoPointAdd, "auto point add", "<au>", 1, 1, {"at=<beats>", "value=<v>", "shape=<linear|hold|smooth>"},
             "Add a point (one at the same beat is replaced). Its shape governs the segment after it.", "R-AUTO-6"},
            {K::AutoPointMove, "auto point move", "<au>", 1, 1, {"at=<beats>", "to=<beats>", "value=<v>"},
             "Move the point at --at to another beat and/or value.", "R-AUTO-6"},
            {K::AutoPointDelete, "auto point delete", "<au>", 1, 1, {"at=<beats>"}, "Remove the point at --at.", "R-AUTO-6"},
            {K::AutoPointShape, "auto point shape", "<au>", 1, 1, {"at=<beats>", "shape=<linear|hold|smooth>"},
             "Set how the curve leaves the point at --at.", "R-AUTO-6"},
            {K::BindClear, "bind clear", "<address>", 1, 1, {},
             "Clear an address's formula: it plays its own stored value again. (`set <address>=<number>` clears and sets.)", "R-AUTO-1"},
            {K::Eval, "eval", "<address>", 1, 1, {"at=<beats>", "explain"},
             "The value an address plays at a beat (default 0); --explain shows its formula and every name it reads.", "R-AUTO-8"},

            {K::Render, "render", "", 0, 0, {"out=<file.wav>", "from=<beats>", "to=<beats>", "stems=<ch,…>", "ports", "bits=<24|32f>"},
             "Render offline: the master to --out; with --stems, each named strip's post-fader output to "
             "<out>.<ch>.wav; with --ports, each output port to <out>.<port>.wav. The tail runs until −90 dBFS "
             "or 10 s.", "R-RENDER-2"},

            {K::MatrixPrint, "matrix print", "", 0, 0, {"json"}, "Every route and send at once: rows = strips, columns = destinations.", "R-MIX-9"},
            {K::Audit, "audit", "", 0, 0, {}, "The mix report: unused strips, clips on muted strips, unreachable strips, single-input buses, offline media, unknown devices, clipping.", "R-MIX-10"},
            {K::StatePrint, "state print", "", 0, 0, {"json", "stable"}, "The whole AppModel; --stable omits what changes with time.", "R-SVC-2"},
            {K::Api, "api", "", 0, 0, {"json", "md"}, "This document: every command, event, model field and device parameter.", "R-API-1"},

            {K::SettingsSet, "settings set", "<key>=<value> …", 1, -1, {},
             "The MACHINE's settings (never a song's): sampleRate (new songs, and the clock device's rate), "
             "bufferSize (frames), output / input (device ids from `devices list`; empty = the system default), "
             "port.<name>=<device>:<channel> (where a song's port plays on this machine). Saved at once.",
             "R-SET-1", true},
            {K::SettingsPrint, "settings print", "", 0, 0, {"json"}, "The machine's settings.", "R-SET-1"},
            {K::FolderAdd, "folder add", "<path>", 1, 1, {}, "Add a sample folder to the browser's quick-access list.", "R-SET-1"},
            {K::FolderRemove, "folder remove", "<path>", 1, 1, {}, "Remove a sample folder from the list (the folder itself is untouched).", "R-SET-1"},
            {K::FolderMove, "folder move", "<path>", 1, 1, {"to=<index>"}, "Reorder the sample folders.", "R-SET-1"},
            {K::DevicesList, "devices list", "", 0, 0, {}, "List this machine's audio devices (ids for `settings set output=…`).", "R-DEV-1"},
            {K::Browse, "browse", "<folder>", 1, 1, {}, "List a folder for the browser: sub-folders, audio files, songs.", "R-BROWSE-1"},
            {K::RecentsRemove, "recents remove", "<path>", 1, 1, {}, "Take a song off Home's recent list (the file is untouched).", "R-HOME-1"},

            {K::TransportPlay, "transport play", "", 0, 0, {"from=<beats>"},
             "Play on the clock device (settings output), from --from or where the transport stands. Edits while "
             "playing are heard: a gain, a pan, a mute, a solo, a device parameter at once; anything structural by a "
             "new engine swapped in at the same position.", "R-PLAY-1"},
            {K::TransportStop, "transport stop", "", 0, 0, {}, "Stop; the transport stays where it was heard.", "R-TIME-4"},
            {K::TransportSeek, "transport seek", "<beats>", 1, 1, {}, "Move the transport (playing or not).", "R-TIME-4"},
            {K::TransportLoop, "transport loop", "<from|off> [to]", 1, 2, {}, "Loop between two beats while playing; `off` ends it.", "R-TIME-4"},
            {K::Wait, "wait", "<seconds>", 1, 1, {}, "Let time pass (playback goes on, the model's transport and meters update) — "
             "for scripts that listen.", "R-PLAY-3"},
        };
        return specs;
    }

    const CommandSpec *specFor(Command::Kind k)
    {
        for (const auto &s : commandSpecs())
            if (s.kind == k) return &s;
        return nullptr;
    }

    namespace
    {
        std::string flagName(const std::string &f)
        {
            const auto eq = f.find('=');
            return eq == std::string::npos ? f : f.substr(0, eq);
        }
        bool flagValued(const std::string &f) { return f.find('=') != std::string::npos; }

        int editDistance(const std::string &a, const std::string &b)
        {
            std::vector<int> prev(b.size() + 1), cur(b.size() + 1);
            for (size_t j = 0; j <= b.size(); ++j) prev[j] = (int)j;
            for (size_t i = 1; i <= a.size(); ++i)
            {
                cur[0] = (int)i;
                for (size_t j = 1; j <= b.size(); ++j)
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
                std::swap(prev, cur);
            }
            return prev[b.size()];
        }
        std::string joinWords(const std::vector<std::string> &v, const char *sep)
        {
            std::string s;
            for (size_t i = 0; i < v.size(); ++i) s += (i ? sep : "") + v[i];
            return s;
        }
    }

    std::vector<std::string> tokenize(const std::string &line)
    {
        std::vector<std::string> out;
        std::string cur;
        bool inQuote = false, have = false;
        for (char c : line)
        {
            if (c == '"') { inQuote = !inQuote; have = true; continue; }
            if (!inQuote && (c == ' ' || c == '\t' || c == '\r' || c == '\n'))
            {
                if (have) { out.push_back(cur); cur.clear(); have = false; }
                continue;
            }
            cur += c;
            have = true;
        }
        if (have) out.push_back(cur);
        return out;
    }

    std::string quoteToken(const std::string &s)
    {
        if (!s.empty() && s.find_first_of(" \t\";#") == std::string::npos) return s;
        return "\"" + s + "\"";
    }

    std::vector<std::string> nearest(const std::string &word, const std::vector<std::string> &pool)
    {
        std::vector<std::pair<int, std::string>> scored;
        for (const auto &p : pool)
        {
            const int d = editDistance(word, p);
            const bool prefix = !word.empty() && p.compare(0, word.size(), word) == 0;
            if (prefix || d <= std::max<int>(2, (int)p.size() / 3)) scored.emplace_back(prefix ? 0 : d, p);
        }
        std::stable_sort(scored.begin(), scored.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::vector<std::string> out;
        for (const auto &s : scored)
        {
            if (std::find(out.begin(), out.end(), s.second) == out.end()) out.push_back(s.second);
            if (out.size() == 3) break;
        }
        return out;
    }

    std::string usageOf(const CommandSpec &s)
    {
        std::string u = s.verb;
        if (!s.args.empty()) u += " " + s.args;
        for (const auto &f : s.flags)
        {
            const auto eq = f.find('=');
            u += eq == std::string::npos ? " [--" + f + "]" : " [--" + f.substr(0, eq) + " " + f.substr(eq + 1) + "]";
        }
        return u;
    }

    Command parseCommand(const std::string &line, std::string &err)
    {
        err.clear();
        Command c;
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#' || line[first] == ';') return c;
        const std::vector<std::string> t = tokenize(line);
        if (t.empty()) return c;

        // Longest verb match: `clip add` beats `clip`, `matrix print` beats `matrix`.
        const CommandSpec *spec = nullptr;
        size_t verbWords = 0;
        for (const auto &s : commandSpecs())
        {
            const std::vector<std::string> words = tokenize(s.verb);
            if (words.size() > t.size() || words.size() <= verbWords) continue;
            if (std::equal(words.begin(), words.end(), t.begin())) { spec = &s; verbWords = words.size(); }
        }
        if (!spec)
        {
            std::vector<std::string> verbs;
            for (const auto &s : commandSpecs()) verbs.push_back(s.verb);
            std::vector<std::string> tried = {t[0]};
            if (t.size() > 1) tried.push_back(t[0] + " " + t[1]);
            std::vector<std::string> near;
            for (const auto &w : tried)
                for (const auto &n : nearest(w, verbs))
                    if (std::find(near.begin(), near.end(), n) == near.end() && near.size() < 3) near.push_back(n);
            err = "unknown command: " + (t.size() > 1 ? t[0] + " " + t[1] : t[0]);
            if (!near.empty()) err += " (did you mean: " + joinWords(near, ", ") + "?)";
            return c;
        }

        std::vector<std::string> flagNames;
        for (const auto &f : spec->flags) flagNames.push_back(flagName(f));
        Command out;
        out.kind = spec->kind;
        for (size_t i = verbWords; i < t.size(); ++i)
        {
            const std::string &tok = t[i];
            if (tok.size() > 2 && tok.compare(0, 2, "--") == 0)
            {
                std::string name = tok.substr(2), value;
                bool inlineValue = false;
                const auto eq = name.find('=');
                if (eq != std::string::npos) { value = name.substr(eq + 1); name = name.substr(0, eq); inlineValue = true; }
                const auto it = std::find(flagNames.begin(), flagNames.end(), name);
                if (it == flagNames.end())
                {
                    err = "`" + spec->verb + "` does not take --" + name;
                    const auto near = nearest(name, flagNames);
                    if (!near.empty()) err += " (did you mean: --" + joinWords(near, ", --") + "?)";
                    else if (flagNames.empty()) err += " (it takes no flags)";
                    else err += " (it takes: --" + joinWords(flagNames, ", --") + ")";
                    return c;
                }
                const bool valued = flagValued(spec->flags[size_t(it - flagNames.begin())]);
                if (valued && !inlineValue)
                {
                    if (i + 1 >= t.size()) { err = "--" + name + " needs a value: " + usageOf(*spec); return c; }
                    value = t[++i];
                }
                else if (!valued)
                {
                    if (inlineValue) { err = "--" + name + " is a switch and takes no value"; return c; }
                    value = "1";
                }
                out.flags.emplace_back(name, value);
                continue;
            }
            if (spec->fieldArgs)
            {
                const auto eq = tok.find('=');
                if (eq == std::string::npos || eq == 0)
                {
                    err = "expected <address>=<value>, got `" + tok + "`";
                    if (!out.fields.empty() && !out.fields.back().second.empty() && out.fields.back().second[0] == '=')
                        err += " — a formula with spaces is quoted: " + out.fields.back().first + "=\"" + out.fields.back().second + " …\"";
                    return c;
                }
                out.fields.emplace_back(tok.substr(0, eq), tok.substr(eq + 1));
                out.args.push_back(tok);
                continue;
            }
            out.args.push_back(tok);
        }
        const int n = (int)out.args.size();
        if (n < spec->minArgs || (spec->maxArgs >= 0 && n > spec->maxArgs))
        {
            err = (n < spec->minArgs ? "too few arguments — usage: " : "too many arguments — usage: ") + usageOf(*spec);
            return c;
        }
        return out;
    }

    std::string formatCommand(const Command &c)
    {
        const CommandSpec *s = specFor(c.kind);
        if (!s) return std::string();
        std::string line = s->verb;
        if (s->fieldArgs)
            for (const auto &f : c.fields) line += " " + quoteToken(f.first + "=" + f.second);
        else
            for (const auto &a : c.args) line += " " + quoteToken(a);
        for (const auto &f : c.flags)
        {
            bool valued = false;
            for (const auto &sf : s->flags)
                if (flagName(sf) == f.first) valued = flagValued(sf);
            line += " --" + f.first;
            if (valued) line += " " + quoteToken(f.second);
        }
        return line;
    }
}
}
