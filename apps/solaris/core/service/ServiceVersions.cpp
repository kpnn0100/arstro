// SolarisService — versions of a song (R-VER; docs/discussion.md Round 15): the grammar over model/Versions. A version
// is made, opened, frozen, thawed, rebased, deleted and reverted here; an edit on an open version is recorded by
// `dispatch` once the command has landed (`recordVersion`), so no other command knows versions exist.
#include "Format.h"
#include "SolarisService.h"
#include "Versions.h"
#include <functional>

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    bool SolarisService::versionCommand(const Command &c, std::string &err)
    {
        if (!requireOpen(err)) return false;
        Project &p = mProject;
        auto find = [&](const std::string &ref) -> const Version * {
            const Version *v = p.versionOf(ref);
            if (!v)
            {
                std::string text;
                resolveVersion(p, ref, text, err); // names the versions there are
                if (ref == "main") err = "main is the song itself, not a version of it";
            }
            return v;
        };
        switch (c.kind)
        {
        case K::VersionNew:
        {
            const std::string name = c.arg(0);
            if (name.empty() || name == "main") { err = name.empty() ? "a version needs a name" : "`main` is the song itself — call the version something else"; return false; }
            if (p.versionOf(name)) { err = "there is already a version `" + name + "`"; return false; }
            const std::string id = newVersion(p, name, c.flag("base"), err);
            if (id.empty()) return false;
            if (c.has("open") && !openVersion(p, id, err)) return false;
            mOutput = id + "\n";
            changed("version.created", id);
            return true;
        }
        case K::VersionList:
        {
            // the tree under main: each version under its base, its facts beside it, the open one marked
            std::string out = std::string("main") + (p.version.empty() ? "  ← open" : "") + "\n";
            std::function<void(const std::string &, const std::string &)> under = [&](const std::string &base, const std::string &indent) {
                for (const auto &v : p.versions)
                {
                    if (v.base != base) continue;
                    const VersionFacts f = versionFacts(p, v.id);
                    out += indent + v.id + " " + quoteIfNeeded(v.name) + " · " + std::to_string(f.deltas) + (f.deltas == 1 ? " change" : " changes");
                    if (f.own) out += ", " + std::to_string(f.own) + " of its own";
                    if (v.frozen) out += " · frozen";
                    if (f.dangling) out += " · " + std::to_string(f.dangling) + " dangling";
                    if (!f.problem.empty()) out += " · does not stand: " + f.problem;
                    if (p.version == v.id) out += "  ← open";
                    out += "\n";
                    under(v.id, indent + "  ");
                }
            };
            under("", "  ");
            mOutput = out;
            return true;
        }
        case K::VersionOpen:
        {
            if (c.arg(0) != "main" && !find(c.arg(0))) return false;
            if (!openVersion(p, c.arg(0), err)) return false;
            mOutput = (p.version.empty() ? std::string("main") : p.version) + "\n";
            changed("version.opened", p.version.empty() ? "main" : p.version);
            return true;
        }
        case K::VersionDiff:
        {
            const Version *v = find(c.arg(0));
            if (!v) return false;
            const auto lines = versionLines(p, v->id);
            mOutput.clear();
            for (const auto &l : lines) mOutput += l + "\n";
            if (lines.empty()) mOutput = "no changes — " + v->name + " is its base as it is\n";
            return true;
        }
        case K::VersionFreeze: case K::VersionThaw:
        {
            const Version *v = find(c.arg(0));
            if (!v) return false;
            const std::string id = v->id;
            const bool freeze = c.kind == K::VersionFreeze;
            if (!(freeze ? freezeVersion(p, id, err) : thawVersion(p, id, err))) return false;
            changed(freeze ? "version.frozen" : "version.thawed", id);
            return true;
        }
        case K::VersionRebase:
        {
            const Version *v = find(c.arg(0));
            if (!v) return false;
            const std::string id = v->id;
            std::vector<std::string> report;
            if (!rebaseVersion(p, id, c.has("dry-run"), report, err)) return false;
            mOutput.clear();
            for (const auto &r : report) mOutput += r + "\n";
            if (report.empty()) mOutput = "nothing to reconcile\n";
            if (!c.has("dry-run")) changed("version.rebased", id);
            return true;
        }
        case K::VersionDelete:
        {
            const Version *v = find(c.arg(0));
            if (!v) return false;
            const std::string id = v->id;
            if (!deleteVersion(p, id, err)) return false;
            changed("version.deleted", id);
            return true;
        }
        case K::Revert:
        {
            // an address `<node>.<field>` (its key in the song's file), or a node alone: every change to it
            const std::string a = c.arg(0);
            const auto dot = a.find('.');
            const std::string node = a.substr(0, dot), field = dot == std::string::npos ? std::string() : a.substr(dot + 1);
            if (!revertVersion(p, node, field, err)) return false;
            changed("version.reverted", node);
            return true;
        }
        default:
            return false;
        }
    }
}
}
