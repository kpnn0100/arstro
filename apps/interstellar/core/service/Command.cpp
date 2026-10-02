#include "Command.h"
#include <algorithm>

namespace arstro
{
namespace interstellar
{
    using K = Command::Kind;

    std::string Command::arg(size_t i, const std::string &fallback) const
    {
        return i < args.size() ? args[i] : fallback;
    }

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
        // The ONE grammar table (R-API-1). Order is the order `api` prints, grouped as §8 is.
        static const std::vector<CommandSpec> specs = {
            {K::ProjectNew, "project new", "<path.isp>", 1, 1, {"fps=<n>", "res=<WxH>"},
             "Create a project and its rack (an empty .cmp beside it), and open it.", "R-SCOPE-2"},
            {K::ProjectOpen, "project open", "<path.isp>", 1, 1, {},
             "Open a project: its timelines, and the hosted Cosmo project its #rack names.", "R-SCOPE-2"},
            {K::ProjectSave, "project save", "[path.isp]", 0, 1, {},
             "Save the .isp and the rack's .cmp. A path saves the .isp there.", "R-RACK-2"},
            {K::ProjectClose, "project close", "", 0, 0, {}, "Close the project and return Home.", "R-UI-1"},

            {K::RackImport, "rack import", "<path.cmp>", 1, 1, {},
             "Point the rack at an existing Cosmo project — its groups and grades are the rack.", "R-RACK-1"},
            {K::RackAdd, "rack add", "<media…>", 1, -1, {"group=<node>"},
             "Add photos or videos to the rack. A video is graded on a reference frame "
             "(`clip.mp4#t=2.0` picks it).", "R-RACK-3"},
            {K::RackGroupNew, "rack group new", "[name]", 0, 1, {"nodes=<a,b,…>"},
             "Group rack nodes — the named ones, else the selection; a group's grade stacks onto every "
             "descendant. No name: one is made up, as Cosmo does.", "R-RACK-4"},
            {K::RackDuplicate, "rack duplicate", "<node>", 1, 1, {"name=<bind>"},
             "Duplicate a rack node as a variant: same media, its own grade.", "R-RACK-5"},
            {K::RackFrame, "rack frame", "<node>", 1, 1, {"at=<t>"},
             "Choose which frame of a video source Cosmo grades. Changes no parameter.", "R-RACK-3"},
            {K::RackRename, "rack rename", "<node> <bind>", 2, 2, {},
             "Change a rack node's bind name (what an address spells).", "R-RACK-6"},
            {K::RackSelect, "rack select", "<node>", 1, 1, {"add", "range"},
             "Make a rack node the Grade target and the selection. --add toggles it into the selection "
             "(Ctrl-click); --range selects from the last clicked node to it (Shift-click).", "R-RACK-8"},
            {K::RackRemove, "rack remove", "<node>", 1, 1, {},
             "Take a source out of the rack (Cosmo's delete). Refused while a clip uses it.", "R-RACK-8"},

            {K::Set, "set", "<address>=<value> …", 1, -1, {},
             "Write addresses. A rack address writes THROUGH to Cosmo on a root timeline and "
             "becomes this version's override on a derived one.", "R-RACK-2", true},
            {K::Get, "get", "<address>", 1, 1, {}, "Print an address's stored value.", "R-API-2"},
            {K::Eval, "eval", "<address>", 1, 1, {"timeline=<tl>", "explain"},
             "Print an address's RESOLVED value in a timeline; --explain names every layer.", "R-API-2"},
            {K::Revert, "revert", "<address>", 1, 1, {"timeline=<tl>"},
             "Drop this version's override at an address, so it inherits from its base again.", "R-VER-2"},

            {K::TimelineNew, "timeline new", "<name>", 1, 1, {"base=<tl>"},
             "Create a timeline; with --base it is a version of that one (stores deltas only).", "R-VER-1"},
            {K::TimelineList, "timeline list", "", 0, 0, {}, "List timelines as a version tree.", "R-VER-5"},
            {K::TimelineOpen, "timeline open", "<tl>", 1, 1, {}, "Make a timeline the editor's current one.", "R-UI-4"},
            {K::TimelinePin, "timeline pin", "<tl>", 1, 1, {"commit=<c>"},
             "Stop inheriting the base's colour: freeze it at a snapshot of the rack.", "R-VER-3"},
            {K::TimelineUnpin, "timeline unpin", "<tl>", 1, 1, {}, "Inherit the base's colour live again.", "R-VER-3"},
            {K::TimelineFreeze, "timeline freeze", "<tl>", 1, 1, {},
             "Stop inheriting the base's arrangement: materialise the resolved cut.", "R-VER-3"},
            {K::TimelineThaw, "timeline thaw", "<tl>", 1, 1, {},
             "Inherit the base's arrangement live again.", "R-VER-3"},
            {K::TimelineRebase, "timeline rebase", "<tl>", 1, 1, {"dry-run"},
             "Report and prune dangling deltas; advance a pin to the rack's present state.", "R-VER-4"},
            {K::TimelineDiff, "timeline diff", "<tl>", 1, 1, {},
             "What this version changes relative to its base.", "R-VER-2"},
            {K::TimelineDelete, "timeline delete", "<tl>", 1, 1, {},
             "Delete a timeline. Refused while another version is based on it.", "R-VER-1"},

            {K::TrackAdd, "track add", "", 0, 0, {"kind=<video|audio>", "name=<n>"},
             "Add a track to the current timeline.", "R-TL-1"},
            {K::ClipAdd, "clip add", "", 0, 0,
             {"track=<trk>", "src=<rackobj>", "in=<t>", "out=<t>", "at=<t>", "name=<n>"},
             "Place a span of a rack source on a track.", "R-TL-1"},
            {K::ClipTrim, "clip trim", "<clip>", 1, 1, {"in=<t>", "out=<t>"},
             "Set a clip's source in and/or out point (seconds into the SOURCE, like `<clip>.in`); "
             "trimming the head keeps the remaining frames where they were on the timeline.", "R-TL-3"},
            {K::ClipSplit, "clip split", "<clip>", 1, 1, {"at=<t>"}, "Cut a clip in two at a timeline time.", "R-TL-3"},
            {K::ClipMove, "clip move", "<clip>", 1, 1, {"at=<t>", "track=<trk>"},
             "Move a clip in time and/or to another track.", "R-TL-3"},
            {K::ClipDelete, "clip delete", "<clip>", 1, 1, {"ripple"},
             "Remove a clip; --ripple closes the gap.", "R-TL-3"},
            {K::ClipRoll, "clip roll", "<clip>", 1, 1, {"at=<t>"},
             "Move the cut between a clip and its right neighbour.", "R-TL-3"},
            {K::ClipSlip, "clip slip", "<clip>", 1, 1, {"by=<dt>"},
             "Shift a clip's source range without moving it on the timeline.", "R-TL-3"},
            {K::ClipSpeed, "clip speed", "<clip> <speed>", 2, 2, {}, "Set a clip's playback speed.", "R-TL-3"},
            {K::ClipSelect, "clip select", "[clip]", 0, 1, {}, "Select a clip (none clears).", "R-UI-3"},
            {K::TransitionAdd, "transition add", "", 0, 0, {"between=<a,b>", "kind=<dissolve|dip>", "dur=<s>"},
             "Dissolve between two adjacent clips; the outgoing clip is HELD through it.", "R-TL-4"},
            {K::MarkerAdd, "marker add", "<name>", 1, 1, {"at=<t>", "note=<text>"},
             "Drop a named marker on the current timeline.", "R-TL-1"},

            {K::FxAdd, "fx add", "", 0, 0,
             {"node=<rackobj>", "clip=<clip>", "type=<denoise|blend|freeze>", "radius=<n>",
              "strength=<0..1>", "shutter=<deg>", "at=<t>"},
             "Attach a temporal effect to a rack node (footage) or a clip (editorial).", "R-FX-2"},
            {K::FxDelete, "fx delete", "<fx>", 1, 1, {}, "Remove a temporal effect.", "R-FX-2"},

            {K::AudioTrackAdd, "audio track add", "", 0, 0, {"name=<n>"},
             "Add an audio track (suite schema `#atrack kind=audio`).", "R-AUD-2"},
            {K::AudioClipAdd, "audio clip add", "", 0, 0,
             {"track=<atrk>", "src=<file>", "at=<t>", "in=<t>", "out=<t>", "gain=<dB>", "fade=<s>"},
             "Place an audio file on an audio track.", "R-AUD-2"},

            {K::Undo, "undo", "", 0, 0, {},
             "Step back one edit — a grade, an override, a cut, a version change — across the rack and "
             "the project alike.", "R-EDIT-1"},
            {K::Redo, "redo", "", 0, 0, {}, "Step forward again after an undo.", "R-EDIT-1"},
            {K::GradeCopy, "grade copy", "<node>", 1, 1, {},
             "Copy a rack node's grade (its own params, masks excluded) to the clipboard.", "R-EDIT-2"},
            {K::GradePaste, "grade paste", "[node…]", 0, -1, {"all"},
             "Paste the copied grade onto rack nodes (or every source with --all). Root timeline only: "
             "it writes through to Cosmo.", "R-EDIT-2"},
            {K::RackUngroup, "rack ungroup", "<group>", 1, 1, {},
             "Dissolve a group; its members keep their own grades.", "R-RACK-4"},
            {K::SettingsSet, "settings set", "<key>=<value> …", 1, -1, {},
             "Engine settings: cpuPercent (25|50|75|100), threads (0=auto), previewEdge (px), useGpu "
             "(0|1), uiScale (%). Persisted; one CPU budget for the rack and the render path.", "R-SET-1", true},
            {K::PresetApply, "preset apply", "<name>", 1, 1, {"node=<bind>"},
             "Apply a library preset to a rack source (the Grade target by default). Root timeline only.",
             "R-EDIT-3"},
            {K::PresetSave, "preset save", "<name>", 1, 1, {"node=<bind>"},
             "Save a rack source's grade to the library as <name>.apf.", "R-EDIT-3"},
            {K::PresetImport, "preset import", "<path.apf>", 1, 1, {},
             "Copy an .apf (from Cosmo or anywhere) into the library.", "R-EDIT-3"},

            {K::Playhead, "playhead", "<t>|+<dt>|-<dt>|next-cut|prev-cut", 1, 1, {},
             "Move the playhead; snapped to a frame.", "R-TL-5"},
            {K::Play, "play", "", 0, 0, {}, "Start playback of the current timeline.", "R-UI-3"},
            {K::Pause, "pause", "", 0, 0, {}, "Stop playback.", "R-UI-3"},

            {K::Render, "render", "", 0, 0,
             {"timeline=<tl>", "out=<path>", "range=<a:b>", "format=<h264|prores|png-seq>"},
             "Queue a render of a NAMED timeline. There is no implicit current timeline.", "R-RENDER-1"},
            {K::RenderCancel, "render cancel", "<job>", 1, 1, {}, "Cancel a queued or running render.", "R-RENDER-4"},
            {K::ExportStill, "export-still", "", 0, 0, {"timeline=<tl>", "out=<p.png>", "at=<t>"},
             "Write one composited frame of a named timeline.", "R-RENDER-5"},

            {K::Capture, "capture", "", 0, 0, {"out=<p.png>", "source=<bind>"},
             "Save what the monitor shows, at full resolution: --source names a rack source (its "
             "reference frame, graded — Grade); without it, the current timeline at the playhead.", "R-UI-11"},
            {K::StatePrint, "state print", "", 0, 0, {"json", "stable"},
             "Print the AppModel; --stable omits machine-dependent fields.", "R-API-2"},
            {K::Api, "api", "", 0, 0, {"json", "md"}, "Print this document.", "R-API-1"},
            {K::Lint, "lint", "", 0, 0, {}, "Report offline media, dangling deltas and refused fields.", "R-RACK-7"},
            {K::Wait, "wait", "<rack.loaded|render.done|frame.ready>", 1, 1, {"timeout=<dur>"},
             "Block (pumping) until a condition holds.", "R-API-2"},
            {K::Quit, "quit", "", 0, 0, {}, "End a script or session.", "R-API-2"},
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
                    cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + (a[i - 1] != b[j - 1])});
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
            // A prefix match is a near miss however long the rest is (`time` → `timeline …`).
            const bool prefix = !word.empty() && p.compare(0, word.size(), word) == 0;
            if (prefix || d <= std::max<int>(2, (int)p.size() / 3)) scored.emplace_back(prefix ? 0 : d, p);
        }
        std::stable_sort(scored.begin(), scored.end(),
                         [](const auto &a, const auto &b) { return a.first < b.first; });
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

        // A comment is a WHOLE line starting with `#` or `;`. Not an inline one: `#` appears in a
        // frame selector (`clip.mp4#t=2.0`) and `;` in every curve value (`0,0;0.5,0.62;1,1`).
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos || line[first] == '#' || line[first] == ';') return c;

        const std::vector<std::string> t = tokenize(line);
        if (t.empty()) return c;

        // Longest verb match: `audio clip add` beats `audio`, `rack group new` beats `rack`.
        const CommandSpec *spec = nullptr;
        size_t verbWords = 0;
        for (const auto &s : commandSpecs())
        {
            const std::vector<std::string> words = tokenize(s.verb);
            if (words.size() > t.size() || words.size() <= verbWords) continue;
            if (std::equal(words.begin(), words.end(), t.begin()))
            {
                spec = &s;
                verbWords = words.size();
            }
        }
        if (!spec)
        {
            std::vector<std::string> verbs;
            for (const auto &s : commandSpecs()) verbs.push_back(s.verb);
            // Compare against as many words as the candidate has, so `timline pin` finds `timeline pin`.
            std::vector<std::string> tried = {t[0]};
            if (t.size() > 1) tried.push_back(t[0] + " " + t[1]);
            if (t.size() > 2) tried.push_back(t[0] + " " + t[1] + " " + t[2]);
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
            for (const auto &spec : s->flags)
                if (flagName(spec) == f.first) valued = flagValued(spec);
            line += " --" + f.first;
            if (valued) line += " " + quoteToken(f.second);
        }
        return line;
    }
}
}
