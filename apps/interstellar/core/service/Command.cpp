#include "Command.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    namespace
    {
        /** Split on whitespace, honouring "double quotes" so a name or an expression with a
         *  space survives. No escapes, matching every other text format in the suite. */
        std::vector<std::string> tokenize(const std::string &line)
        {
            std::vector<std::string> out;
            std::string cur;
            bool q = false, have = false;
            for (char c : line)
            {
                if (c == '"') { q = !q; have = true; continue; }
                if (!q && (c == ' ' || c == '\t'))
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

        bool splitField(const std::string &tok, std::pair<std::string, std::string> &out)
        {
            const auto eq = tok.find('=');
            if (eq == std::string::npos || eq == 0) return false;
            out.first = tok.substr(0, eq);
            out.second = tok.substr(eq + 1);
            return true;
        }

        /** `--at 4.0` / `--at=4.0` both land in `fields`; a bare `--flag` becomes `flag=1`.
         *  Returns false and names the offender when a token is neither a flag nor a value,
         *  because a silently-swallowed argument is how a script builds on an edit that never
         *  happened (R-SVC-6). */
        bool collectFlags(const std::vector<std::string> &t, size_t from, Command &c, std::string &err)
        {
            for (size_t i = from; i < t.size(); ++i)
            {
                std::string tok = t[i];
                const bool dashed = tok.rfind("--", 0) == 0;
                if (dashed) tok = tok.substr(2);
                std::pair<std::string, std::string> kv;
                if (splitField(tok, kv)) { c.fields.push_back(kv); continue; }
                if (!dashed)
                {
                    err = "unexpected argument '" + t[i] + "' — expected --flag or key=value";
                    return false;
                }
                // The next token is this flag's VALUE unless it is another flag. Note it may
                // itself contain '=': `--points 0=0,1=1` is one value, and cosmo's parser
                // excludes such tokens (its flags never take one), which read `--points` as a
                // bare flag and then mis-parsed the breakpoints as a field. Bare `key=value`
                // arguments only reach `set` and `settings set`, which do not come through here.
                if (i + 1 < t.size() && t[i + 1].rfind("--", 0) != 0)
                {
                    c.fields.emplace_back(tok, t[i + 1]);
                    ++i;
                }
                else c.fields.emplace_back(tok, "1");
            }
            return true;
        }
    }

    std::string Command::field(const std::string &key, const std::string &fallback) const
    {
        for (const auto &kv : fields)
            if (kv.first == key) return kv.second;
        return fallback;
    }
    double Command::fieldNum(const std::string &key, double fallback) const
    {
        for (const auto &kv : fields)
            if (kv.first == key) return std::atof(kv.second.c_str());
        return fallback;
    }

    const std::vector<CommandSpec> &commandSpecs()
    {
        // Name, argument hint and one line of what it does — all three beside each other, so
        // `--help` and the API document cannot describe a command that does not exist and
        // cannot omit one that does (R-SVC-10).
        static const std::vector<CommandSpec> v = {
            {"project new",    "<path.isp> [--fps 24] [--res WxH]", "create a project"},
            {"project open",   "<path.isp>",                        "open one"},
            {"project save",   "[<path.isp>]",                      "save it"},
            {"project close",  "",                                  "close it"},
            {"rack import",    "<path.cmp> [--branch main] [--write-branch b]", "embed a Cosmo project as the rack"},
            {"rack new",       "[--name rack]",                     "create an empty rack"},
            {"rack pin",       "<commit>",                          "freeze the rack (read-only)"},
            {"rack unpin",     "",                                  "unfreeze it"},
            {"rack add",       "<media...>",                        "add sources to the rack"},
            {"rack group new", "\"<name>\"",                        "group the rack selection"},
            {"rack duplicate", "<node>",                            "a source VARIANT (R-RACK-3)"},
            {"rack rename",    "<node> \"<name>\"",                 "rename a rack node"},
            {"rack select",    "<node>",                            "select a rack node"},
            {"set",            "<address>=<value> ...",             "set any parameter, routed by owner"},
            {"get",            "<address>",                         "print its static value"},
            {"eval",           "<address> --at <t> [--explain]",    "print its RESOLVED value at t"},
            {"track add",      "--kind video|audio [--name v0] [--order n]", "add a track"},
            {"clip add",       "--track <t> --src rack:<node> --in <t> --out <t> --at <t> [--name n]", "add a clip"},
            {"clip trim",      "<clip> --in <t> | --out <t>",       "trim a clip"},
            {"clip split",     "<clip> --at <t>",                   "split at a time"},
            {"clip move",      "<clip> --at <t> [--track <t>]",     "move it"},
            {"clip delete",    "<clip> [--ripple]",                 "delete it"},
            {"clip roll",      "<clipA> <clipB> --by <dt>",         "roll the edit point"},
            {"clip slip",      "<clip> --by <dt>",                  "slip the source window"},
            {"transition add", "--between <a>,<b> --kind dissolve|dip --dur <t>", "add a transition"},
            {"marker add",     "--at <t> [--name n] [--note \"...\"]", "add a marker"},
            {"rename",         "<object> \"<name>\"",               "rename, rewriting every expression"},
            {"auto new",       "<name> --dur <t> --points 0=0,1=1 [--ease e] [--interp i]", "create a reusable SHAPE"},
            {"auto point",     "<autoclip> --at <t> --value <v> [--ease e]", "edit a breakpoint"},
            {"auto link",      "<autoclip> -> <address> --at <t> [--dur <t>] [--from v --to v] [--mode m] [--scope c] [--fade-in n]", "apply a shape to an address"},
            {"auto unlink",    "<autolink>",                        "remove a link"},
            {"auto lanes",     "[<object>]",                        "list the automated addresses"},
            {"bind",           "<address> = <expression>",          "drive a parameter by a calculation"},
            {"bind delete",    "<address>",                         "remove a binding"},
            {"bind list",      "",                                  "every binding, with its deps"},
            {"playhead",       "<t> | +<dt> | next-cut | prev-cut", "move the playhead"},
            {"play",           "",                                  "start playback"},
            {"pause",          "",                                  "stop it"},
            {"render",         "--out <path> [--range a:b] [--format png-seq] [--lint]", "render a master"},
            {"export-still",   "--out <path.png> [--at <t>]",       "one frame at full size"},
            {"lint",           "",                                  "report what a render would warn about"},
            {"settings set",   "proxyEdge=1280 cpuPercent=50",      "project settings"},
            {"state print",    "[--json] [--stable]",               "dump the model"},
            {"api",            "[--json]",                          "the generated API document"},
            {"wait",           "<condition> [--timeout 120s]",      "a front-end verb: pump until"},
            {"quit",           "",                                  "stop"}};
        return v;
    }

    const std::vector<std::string> &commandNames()
    {
        static const std::vector<std::string> v = [] {
            std::vector<std::string> n;
            for (const auto &s : commandSpecs()) n.push_back(s.name);
            return n;
        }();
        return v;
    }

    Command parseCommand(const std::string &line, std::string &err)
    {
        err.clear();
        Command c;
        // `#` starts a comment only at the beginning of a token, so an address or a colour
        // literal containing one survives.
        std::string body = line;
        {
            bool q = false;
            for (size_t i = 0; i < line.size(); ++i)
            {
                if (line[i] == '"') q = !q;
                else if (line[i] == '#' && !q && (i == 0 || line[i - 1] == ' ' || line[i - 1] == '\t'))
                { body = line.substr(0, i); break; }
            }
        }
        const std::vector<std::string> t = tokenize(body);
        if (t.empty()) return c;

        const std::string &v = t[0];
        const std::string sub = t.size() > 1 ? t[1] : std::string();
        auto need = [&](size_t n, const char *what) {
            if (t.size() >= n) return true;
            err = std::string("expected ") + what;
            return false;
        };

        if (v == "project")
        {
            if (!need(2, "project new|open|save|close")) return c;
            if (sub == "new" || sub == "open")
            {
                if (!need(3, "a project path")) return c;
                c.kind = sub == "new" ? Command::Kind::ProjectNew : Command::Kind::ProjectOpen;
                c.path = t[2];
                if (!collectFlags(t, 3, c, err)) { c.kind = Command::Kind::None; return c; }
            }
            else if (sub == "save")
            {
                c.kind = Command::Kind::ProjectSave;
                if (t.size() > 2) c.path = t[2];
            }
            else if (sub == "close") c.kind = Command::Kind::ProjectClose;
            else err = "unknown project subcommand: " + sub;
        }
        else if (v == "rack")
        {
            if (!need(2, "rack import|new|pin|unpin|add|group|duplicate|rename|select")) return c;
            if (sub == "import")
            {
                if (!need(3, "a .cmp path")) return c;
                c.kind = Command::Kind::RackImport;
                c.path = t[2];
                if (!collectFlags(t, 3, c, err)) { c.kind = Command::Kind::None; return c; }
            }
            else if (sub == "new") { c.kind = Command::Kind::RackNew; if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; } }
            else if (sub == "pin") { if (!need(3, "a commit")) return c; c.kind = Command::Kind::RackPin; c.name = t[2]; }
            else if (sub == "unpin") c.kind = Command::Kind::RackUnpin;
            else if (sub == "add")
            {
                if (!need(3, "one or more media paths")) return c;
                c.kind = Command::Kind::RackAdd;
                for (size_t i = 2; i < t.size(); ++i) c.paths.push_back(t[i]);
            }
            else if (sub == "group")
            {
                if (!need(4, "rack group new \"<name>\"")) return c;
                if (t[2] != "new") { err = "unknown rack group subcommand: " + t[2]; return c; }
                c.kind = Command::Kind::RackGroupNew;
                c.name = t[3];
            }
            else if (sub == "duplicate") { if (!need(3, "a rack node")) return c; c.kind = Command::Kind::RackDuplicate; c.name = t[2]; }
            else if (sub == "rename")
            {
                if (!need(4, "a node and a name")) return c;
                c.kind = Command::Kind::RackRename; c.name = t[2]; c.name2 = t[3];
            }
            else if (sub == "select") { if (!need(3, "a rack node")) return c; c.kind = Command::Kind::RackSelect; c.name = t[2]; }
            else err = "unknown rack subcommand: " + sub;
        }
        else if (v == "set")
        {
            if (!need(2, "one or more <address>=<value>")) return c;
            c.kind = Command::Kind::Set;
            for (size_t i = 1; i < t.size(); ++i)
            {
                std::pair<std::string, std::string> kv;
                if (!splitField(t[i], kv))
                {
                    // Refused, naming it. This is cosmo's D-59 made impossible: a `set` that
                    // accepts a token it does not understand and reports success is how an
                    // agent builds everything after it on a state that never changed.
                    err = "not an assignment: '" + t[i] + "' — expected <address>=<value>";
                    c.kind = Command::Kind::None;
                    return c;
                }
                c.fields.push_back(kv);
            }
        }
        else if (v == "get") { if (!need(2, "an address")) return c; c.kind = Command::Kind::Get; c.name = t[1]; }
        else if (v == "eval")
        {
            if (!need(2, "an address")) return c;
            c.kind = Command::Kind::Eval;
            c.name = t[1];
            if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; }
            c.ms = c.fieldNum("at", 0.0);
            c.flag = c.field("explain") == "1";
        }
        else if (v == "track")
        {
            if (!need(2, "track add")) return c;
            if (sub != "add") { err = "unknown track subcommand: " + sub; return c; }
            c.kind = Command::Kind::TrackAdd;
            if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "clip")
        {
            if (!need(2, "clip add|trim|split|move|delete|roll|slip")) return c;
            if (sub == "add")
            {
                c.kind = Command::Kind::ClipAdd;
                if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; }
            }
            else if (sub == "roll")
            {
                if (!need(4, "two clips")) return c;
                c.kind = Command::Kind::ClipRoll; c.name = t[2]; c.name2 = t[3];
                if (!collectFlags(t, 4, c, err)) { c.kind = Command::Kind::None; return c; }
            }
            else
            {
                if (!need(3, "a clip")) return c;
                if (sub == "trim") c.kind = Command::Kind::ClipTrim;
                else if (sub == "split") c.kind = Command::Kind::ClipSplit;
                else if (sub == "move") c.kind = Command::Kind::ClipMove;
                else if (sub == "delete") c.kind = Command::Kind::ClipDelete;
                else if (sub == "slip") c.kind = Command::Kind::ClipSlip;
                else { err = "unknown clip subcommand: " + sub; return c; }
                c.name = t[2];
                if (!collectFlags(t, 3, c, err)) { c.kind = Command::Kind::None; return c; }
            }
        }
        else if (v == "transition")
        {
            if (!need(2, "transition add")) return c;
            if (sub != "add") { err = "unknown transition subcommand: " + sub; return c; }
            c.kind = Command::Kind::TransitionAdd;
            if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "marker")
        {
            if (!need(2, "marker add")) return c;
            if (sub != "add") { err = "unknown marker subcommand: " + sub; return c; }
            c.kind = Command::Kind::MarkerAdd;
            if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "rename")
        {
            if (!need(3, "an object and a name")) return c;
            c.kind = Command::Kind::Rename; c.name = t[1]; c.name2 = t[2];
        }
        else if (v == "auto")
        {
            if (!need(2, "auto new|point|link|unlink|lanes")) return c;
            if (sub == "new")
            {
                if (!need(3, "a shape name")) return c;
                c.kind = Command::Kind::AutoNew; c.name = t[2];
                if (!collectFlags(t, 3, c, err)) { c.kind = Command::Kind::None; return c; }
            }
            else if (sub == "point")
            {
                if (!need(3, "an autoclip")) return c;
                c.kind = Command::Kind::AutoPointSet; c.name = t[2];
                if (!collectFlags(t, 3, c, err)) { c.kind = Command::Kind::None; return c; }
            }
            else if (sub == "link")
            {
                // `auto link ac_push -> gr1.basic.exposure --at 4` — the arrow is part of the
                // grammar because the direction is the whole point: a shape drives an address.
                if (!need(5, "<autoclip> -> <address>")) return c;
                if (t[3] != "->") { err = "expected '->' between the shape and the address"; return c; }
                c.kind = Command::Kind::AutoLinkAdd; c.name = t[2]; c.name2 = t[4];
                if (!collectFlags(t, 5, c, err)) { c.kind = Command::Kind::None; return c; }
            }
            else if (sub == "unlink") { if (!need(3, "an autolink")) return c; c.kind = Command::Kind::AutoUnlink; c.name = t[2]; }
            else if (sub == "lanes")
            {
                c.kind = Command::Kind::AutoLanes;
                if (t.size() > 2) c.name = t[2];
            }
            else err = "unknown auto subcommand: " + sub;
        }
        else if (v == "bind")
        {
            if (!need(2, "bind <address> = <expr> | bind list | bind delete <address>")) return c;
            if (sub == "list") { c.kind = Command::Kind::BindList; }
            else if (sub == "delete")
            {
                if (!need(3, "an address")) return c;
                c.kind = Command::Kind::BindDelete; c.name = t[2];
            }
            else
            {
                // Everything after the `=` is the expression, re-joined with single spaces: the
                // tokenizer has already dropped the quoting, and an expression's whitespace is
                // not significant.
                size_t eq = 0;
                for (size_t i = 1; i < t.size(); ++i)
                    if (t[i] == "=") { eq = i; break; }
                if (eq == 0 || eq + 1 >= t.size())
                {
                    err = "expected bind <address> = <expression>";
                    return c;
                }
                c.kind = Command::Kind::BindSet;
                c.name = t[1];
                for (size_t i = eq + 1; i < t.size(); ++i)
                {
                    if (i > eq + 1) c.text += ' ';
                    c.text += t[i];
                }
            }
        }
        else if (v == "playhead")
        {
            if (!need(2, "a time, +delta, next-cut or prev-cut")) return c;
            c.kind = Command::Kind::Playhead;
            c.name = t[1];
            c.ms = std::atof(t[1].c_str());
        }
        else if (v == "play") c.kind = Command::Kind::Play;
        else if (v == "pause") c.kind = Command::Kind::Pause;
        else if (v == "render")
        {
            c.kind = Command::Kind::Render;
            if (!collectFlags(t, 1, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "export-still")
        {
            c.kind = Command::Kind::ExportStill;
            if (!collectFlags(t, 1, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "lint") c.kind = Command::Kind::Lint;
        else if (v == "settings")
        {
            if (!need(3, "settings set key=value")) return c;
            if (sub != "set") { err = "unknown settings subcommand: " + sub; return c; }
            c.kind = Command::Kind::SettingsSet;
            for (size_t i = 2; i < t.size(); ++i)
            {
                std::pair<std::string, std::string> kv;
                if (!splitField(t[i], kv)) { err = "not an assignment: '" + t[i] + "'"; c.kind = Command::Kind::None; return c; }
                c.fields.push_back(kv);
            }
        }
        else if (v == "state")
        {
            if (!need(2, "state print")) return c;
            if (sub != "print") { err = "unknown state subcommand: " + sub; return c; }
            c.kind = Command::Kind::StatePrint;
            if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "api")
        {
            c.kind = Command::Kind::Api;
            if (!collectFlags(t, 1, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "wait")
        {
            if (!need(2, "a condition")) return c;
            c.kind = Command::Kind::Wait;
            c.name = t[1];
            if (!collectFlags(t, 2, c, err)) { c.kind = Command::Kind::None; return c; }
        }
        else if (v == "quit") c.kind = Command::Kind::Quit;
        else err = "unknown command: " + v;
        return c;
    }

    std::string formatCommand(const Command &c)
    {
        std::ostringstream o;
        auto fields = [&]() {
            for (const auto &kv : c.fields) o << " --" << kv.first << ' ' << kv.second;
        };
        auto bare = [&]() {
            for (const auto &kv : c.fields) o << ' ' << kv.first << '=' << kv.second;
        };
        auto q = [](const std::string &s) {
            return s.find(' ') == std::string::npos ? s : "\"" + s + "\"";
        };
        switch (c.kind)
        {
            case Command::Kind::None: return {};
            case Command::Kind::ProjectNew: o << "project new " << c.path; fields(); break;
            case Command::Kind::ProjectOpen: o << "project open " << c.path; break;
            case Command::Kind::ProjectSave: o << "project save"; if (!c.path.empty()) o << ' ' << c.path; break;
            case Command::Kind::ProjectClose: o << "project close"; break;
            case Command::Kind::RackImport: o << "rack import " << c.path; fields(); break;
            case Command::Kind::RackNew: o << "rack new"; fields(); break;
            case Command::Kind::RackPin: o << "rack pin " << c.name; break;
            case Command::Kind::RackUnpin: o << "rack unpin"; break;
            case Command::Kind::RackAdd:
                o << "rack add";
                for (const auto &p : c.paths) o << ' ' << p;
                break;
            case Command::Kind::RackGroupNew: o << "rack group new " << q(c.name); break;
            case Command::Kind::RackDuplicate: o << "rack duplicate " << c.name; break;
            case Command::Kind::RackRename: o << "rack rename " << c.name << ' ' << q(c.name2); break;
            case Command::Kind::RackSelect: o << "rack select " << c.name; break;
            case Command::Kind::Set: o << "set"; bare(); break;
            case Command::Kind::Get: o << "get " << c.name; break;
            case Command::Kind::Eval:
                o << "eval " << c.name << " --at " << c.ms;
                if (c.flag) o << " --explain";
                break;
            case Command::Kind::TrackAdd: o << "track add"; fields(); break;
            case Command::Kind::ClipAdd: o << "clip add"; fields(); break;
            case Command::Kind::ClipTrim: o << "clip trim " << c.name; fields(); break;
            case Command::Kind::ClipSplit: o << "clip split " << c.name; fields(); break;
            case Command::Kind::ClipMove: o << "clip move " << c.name; fields(); break;
            case Command::Kind::ClipDelete: o << "clip delete " << c.name; fields(); break;
            case Command::Kind::ClipRoll: o << "clip roll " << c.name << ' ' << c.name2; fields(); break;
            case Command::Kind::ClipSlip: o << "clip slip " << c.name; fields(); break;
            case Command::Kind::TransitionAdd: o << "transition add"; fields(); break;
            case Command::Kind::MarkerAdd: o << "marker add"; fields(); break;
            case Command::Kind::Rename: o << "rename " << c.name << ' ' << q(c.name2); break;
            case Command::Kind::AutoNew: o << "auto new " << c.name; fields(); break;
            case Command::Kind::AutoPointSet: o << "auto point " << c.name; fields(); break;
            case Command::Kind::AutoLinkAdd: o << "auto link " << c.name << " -> " << c.name2; fields(); break;
            case Command::Kind::AutoUnlink: o << "auto unlink " << c.name; break;
            case Command::Kind::AutoLanes: o << "auto lanes"; if (!c.name.empty()) o << ' ' << c.name; break;
            case Command::Kind::BindSet: o << "bind " << c.name << " = " << c.text; break;
            case Command::Kind::BindDelete: o << "bind delete " << c.name; break;
            case Command::Kind::BindList: o << "bind list"; break;
            case Command::Kind::Playhead: o << "playhead " << c.name; break;
            case Command::Kind::Play: o << "play"; break;
            case Command::Kind::Pause: o << "pause"; break;
            case Command::Kind::Render: o << "render"; fields(); break;
            case Command::Kind::ExportStill: o << "export-still"; fields(); break;
            case Command::Kind::Lint: o << "lint"; break;
            case Command::Kind::SettingsSet: o << "settings set"; bare(); break;
            case Command::Kind::StatePrint: o << "state print"; fields(); break;
            case Command::Kind::Api: o << "api"; fields(); break;
            case Command::Kind::Wait: o << "wait " << c.name; fields(); break;
            case Command::Kind::Quit: o << "quit"; break;
        }
        return o.str();
    }
}
}
