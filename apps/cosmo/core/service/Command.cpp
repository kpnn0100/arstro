#include "Command.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace cosmo
{
    namespace
    {
        /** Split on whitespace, honouring "double quotes" so a preset or group name with a
         *  space survives. No escapes: every persisted format in cosmo is escape-free plain
         *  text and this stays consistent with them. */
        std::vector<std::string> tokenize(const std::string &line)
        {
            std::vector<std::string> out;
            std::string cur;
            bool inQuote = false, have = false;
            for (char c : line)
            {
                if (c == '"') { inQuote = !inQuote; have = true; continue; }
                if (!inQuote && (c == ' ' || c == '\t'))
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

        /** `--outdir X` / `--format=jpg` both land in `fields`; a bare `--flag` becomes
         *  `flag=1`, so a caller only ever reads `field()`. */
        void collectFlags(const std::vector<std::string> &t, size_t from, Command &c)
        {
            for (size_t i = from; i < t.size(); ++i)
            {
                std::string tok = t[i];
                if (tok.rfind("--", 0) == 0) tok = tok.substr(2);
                std::pair<std::string, std::string> kv;
                if (splitField(tok, kv)) { c.fields.push_back(kv); continue; }
                if (i + 1 < t.size() && t[i + 1].rfind("--", 0) != 0 && t[i + 1].find('=') == std::string::npos)
                {
                    c.fields.emplace_back(tok, t[i + 1]);
                    ++i;
                }
                else
                    c.fields.emplace_back(tok, "1");
            }
        }

        /** "120s" / "500ms" / "2000" -> milliseconds. */
        int parseDurationMs(const std::string &v)
        {
            if (v.size() > 2 && v.compare(v.size() - 2, 2, "ms") == 0) return std::atoi(v.c_str());
            if (!v.empty() && v.back() == 's') return std::atoi(v.c_str()) * 1000;
            return std::atoi(v.c_str());
        }
    }

    std::string Command::field(const std::string &key, const std::string &fallback) const
    {
        for (const auto &kv : fields)
            if (kv.first == key) return kv.second;
        return fallback;
    }

    const std::vector<std::string> &commandNames()
    {
        // R-SVC-9: the coverage test walks this, so a behaviour with no command fails a
        // build rather than being noticed later by a human reading the grammar.
        static const std::vector<std::string> names = {
            "project open", "project new", "project save", "project close", "import",
            "select", "select next", "select prev", "set", "bypass", "group new",
            "group ungroup", "group rename", "delete", "mask set", "mask delete",
            "undo", "redo", "preset apply",
            "preset save", "export",
            "settings set", "screen", "state print", "wait", "quit"};
        return names;
    }

    Command parseCommand(const std::string &line, std::string &err)
    {
        err.clear();
        Command c;
        const auto hash = line.find('#');
        const std::string body = hash == std::string::npos ? line : line.substr(0, hash);
        const std::vector<std::string> t = tokenize(body);
        if (t.empty()) return c;   // blank or comment: a no-op with no error, so scripts read naturally

        const std::string &v = t[0];
        const std::string sub = t.size() > 1 ? t[1] : std::string();
        auto need = [&](size_t n, const char *what) {
            if (t.size() >= n) return true;
            err = std::string("expected ") + what;
            return false;
        };

        if (v == "project")
        {
            if (!need(2, "project open|new|save|close")) return c;
            if (sub == "open" || sub == "new")
            {
                if (!need(3, "a project path")) return c;
                c.kind = sub == "open" ? Command::Kind::ProjectOpen : Command::Kind::ProjectNew;
                c.path = t[2];
            }
            else if (sub == "save")
            {
                c.kind = Command::Kind::ProjectSave;
                if (t.size() > 2) c.path = t[2];   // absent = save over the current path
            }
            else if (sub == "close") c.kind = Command::Kind::ProjectClose;
            else err = "unknown project subcommand: " + sub;
        }
        else if (v == "import")
        {
            if (!need(2, "at least one image path")) return c;
            c.kind = Command::Kind::Import;
            c.paths.assign(t.begin() + 1, t.end());
        }
        else if (v == "select")
        {
            if (!need(2, "a node id, or next|prev")) return c;
            if (sub == "next") c.kind = Command::Kind::SelectNext;
            else if (sub == "prev") c.kind = Command::Kind::SelectPrev;
            else { c.kind = Command::Kind::Select; c.index = std::atoi(sub.c_str()); }
        }
        else if (v == "set")
        {
            if (!need(2, "at least one key=value")) return c;
            c.kind = Command::Kind::Set;
            for (size_t i = 1; i < t.size(); ++i)
            {
                std::pair<std::string, std::string> kv;
                if (!splitField(t[i], kv)) { err = "not a key=value: " + t[i]; return Command{}; }
                c.fields.push_back(kv);
            }
        }
        else if (v == "bypass")
        {
            if (!need(2, "a node id")) return c;
            c.kind = Command::Kind::Bypass;
            c.index = std::atoi(sub.c_str());
            const std::string on = t.size() > 2 ? t[2] : "on";
            c.flag = (on == "on" || on == "1" || on == "true");
        }
        else if (v == "group")
        {
            if (!need(2, "group new|ungroup")) return c;
            if (sub == "new")
            {
                c.kind = Command::Kind::GroupNew;
                if (t.size() > 2) c.name = t[2];
            }
            else if (sub == "ungroup")
            {
                if (!need(3, "a node id")) return c;
                c.kind = Command::Kind::GroupUngroup;
                c.index = std::atoi(t[2].c_str());
            }
            else if (sub == "rename")
            {
                if (!need(3, "group rename [<node>] <name>")) return c;
                c.kind = Command::Kind::GroupRename;
                if (t.size() >= 4) { c.index = std::atoi(t[2].c_str()); c.name = t[3]; }
                else
                {
                    // Bare form = the selected group, mirroring `delete`. A script cannot know
                    // the id `group new` just allocated — it depends on how many nodes the
                    // project already had — so requiring one made the command unusable in the
                    // exact sequence people actually write: create a group, then name it.
                    c.index = -1;
                    c.name = t[2];
                }
            }
            else err = "unknown group subcommand: " + sub;
        }
        else if (v == "mask")
        {
            if (!need(3, "mask set <i> k=v… | mask delete <i>")) return c;
            if (sub == "set")
            {
                if (!need(4, "mask set <i> <key>=<value>")) return c;
                c.kind = Command::Kind::MaskSet;
                c.index = std::atoi(t[2].c_str());
                for (size_t i = 3; i < t.size(); ++i)
                {
                    std::pair<std::string, std::string> kv;
                    if (!splitField(t[i], kv)) { err = "not a key=value: " + t[i]; return Command{}; }
                    c.fields.push_back(kv);
                }
            }
            else if (sub == "delete")
            {
                c.kind = Command::Kind::MaskDelete;
                c.index = std::atoi(t[2].c_str());
            }
            else err = "unknown mask subcommand: " + sub;
        }
        else if (v == "delete")
        {
            // A bare `delete` means the current selection, which is what a keyboard shortcut
            // sends; an explicit node is what a script or a context menu sends.
            c.kind = Command::Kind::Delete;
            c.index = t.size() > 1 ? std::atoi(sub.c_str()) : -1;
        }
        else if (v == "undo") c.kind = Command::Kind::Undo;
        else if (v == "redo") c.kind = Command::Kind::Redo;
        else if (v == "preset")
        {
            if (!need(3, "preset apply|save <name>")) return c;
            if (sub == "apply") c.kind = Command::Kind::PresetApply;
            else if (sub == "save") c.kind = Command::Kind::PresetSave;
            else { err = "unknown preset subcommand: " + sub; return c; }
            c.name = t[2];
        }
        else if (v == "export")
        {
            c.kind = Command::Kind::Export;
            collectFlags(t, 1, c);
            c.path = c.field("outdir");
            if (c.path.empty()) { err = "export needs --outdir"; return Command{}; }
        }
        else if (v == "settings")
        {
            if (!need(3, "settings set key=value")) return c;
            if (sub != "set") { err = "unknown settings subcommand: " + sub; return c; }
            c.kind = Command::Kind::SettingsSet;
            for (size_t i = 2; i < t.size(); ++i)
            {
                std::pair<std::string, std::string> kv;
                if (!splitField(t[i], kv)) { err = "not a key=value: " + t[i]; return Command{}; }
                c.fields.push_back(kv);
            }
        }
        else if (v == "screen")
        {
            if (!need(2, "home|editor")) return c;
            c.kind = Command::Kind::Screen;
            c.name = sub;
        }
        else if (v == "state")
        {
            if (!need(2, "state print")) return c;
            if (sub != "print") { err = "unknown state subcommand: " + sub; return c; }
            c.kind = Command::Kind::StatePrint;
            // Two independent options, so they cannot share `flag`: --json picks the format,
            // --stable drops the fields that legitimately differ between two front ends
            // (R-SVC-9). Keeping --stable in `fields` means adding a third option later needs
            // no signature change. D-14: --stable used to parse and then be silently dropped,
            // so a socket dump could never be compared with a CLI one — which is the single
            // thing the option exists for.
            collectFlags(t, 2, c);
            c.flag = c.field("json") == "1";
        }
        else if (v == "wait")
        {
            if (!need(2, "a condition, e.g. load.finished")) return c;
            c.kind = Command::Kind::Wait;
            c.name = sub;
            Command tmp;
            collectFlags(t, 2, tmp);
            const std::string to = tmp.field("timeout");
            c.index = to.empty() ? 120000 : parseDurationMs(to);
        }
        else if (v == "quit" || v == "exit") c.kind = Command::Kind::Quit;
        else err = "unknown command: " + v;

        return c;
    }

    std::string formatCommand(const Command &c)
    {
        // Quote anything with a space, so format(parse(x)) round-trips through tokenize().
        auto q = [](const std::string &s) {
            return s.find(' ') == std::string::npos ? s : "\"" + s + "\"";
        };
        std::ostringstream o;
        switch (c.kind)
        {
            case Command::Kind::None: return "";
            case Command::Kind::ProjectOpen: o << "project open " << q(c.path); break;
            case Command::Kind::ProjectNew: o << "project new " << q(c.path); break;
            case Command::Kind::ProjectSave:
                o << "project save";
                if (!c.path.empty()) o << ' ' << q(c.path);
                break;
            case Command::Kind::ProjectClose: o << "project close"; break;
            case Command::Kind::Import:
                o << "import";
                for (const auto &p : c.paths) o << ' ' << q(p);
                break;
            case Command::Kind::Select: o << "select " << c.index; break;
            case Command::Kind::SelectNext: o << "select next"; break;
            case Command::Kind::SelectPrev: o << "select prev"; break;
            case Command::Kind::Set:
                o << "set";
                for (const auto &kv : c.fields) o << ' ' << kv.first << '=' << kv.second;
                break;
            case Command::Kind::Bypass: o << "bypass " << c.index << (c.flag ? " on" : " off"); break;
            case Command::Kind::GroupNew: o << "group new " << q(c.name); break;
            case Command::Kind::GroupUngroup: o << "group ungroup " << c.index; break;
            case Command::Kind::GroupRename:
                o << "group rename";
                if (c.index >= 0) o << ' ' << c.index;
                o << ' ' << q(c.name);
                break;
            case Command::Kind::MaskSet:
                o << "mask set " << c.index;
                for (const auto &kv : c.fields) o << ' ' << kv.first << '=' << kv.second;
                break;
            case Command::Kind::MaskDelete: o << "mask delete " << c.index; break;
            case Command::Kind::Delete:
                o << "delete";
                if (c.index >= 0) o << ' ' << c.index;
                break;
            case Command::Kind::Undo: o << "undo"; break;
            case Command::Kind::Redo: o << "redo"; break;
            case Command::Kind::PresetApply: o << "preset apply " << q(c.name); break;
            case Command::Kind::PresetSave: o << "preset save " << q(c.name); break;
            case Command::Kind::Export:
                o << "export";
                for (const auto &kv : c.fields) o << " --" << kv.first << ' ' << q(kv.second);
                break;
            case Command::Kind::SettingsSet:
                o << "settings set";
                for (const auto &kv : c.fields) o << ' ' << kv.first << '=' << kv.second;
                break;
            case Command::Kind::Screen: o << "screen " << c.name; break;
            case Command::Kind::StatePrint:
                o << "state print";
                if (c.flag) o << " --json";
                if (c.field("stable") == "1") o << " --stable";
                if (c.field("params") == "1") o << " --params";
                break;
            case Command::Kind::Wait: o << "wait " << c.name << " --timeout " << c.index << "ms"; break;
            case Command::Kind::Quit: o << "quit"; break;
        }
        return o.str();
    }
}
}
