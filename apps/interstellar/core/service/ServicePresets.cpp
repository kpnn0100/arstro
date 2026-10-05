/*
 *  interstellar_core — render presets (R-DLV-3).
 *
 *  A preset is a render's whole output spec — the flags `render` takes, less the timeline, the path and
 *  the range, which belong to one render — saved by name. Three are built in (YouTube 1080p, ProRes HQ
 *  master, Review H.264); the user's live beside the engine settings (`render-presets`, one per line,
 *  `name<TAB>flags`), so every project has them. `render --preset <name>` lays the preset's flags under
 *  the ones given (a flag given wins). A preset's size is a FRAME to fit in: the project's own aspect,
 *  inside it, never above the project — a 1080p preset on a 4:3 project is 1440×1080, not refused.
 */
#include "ServiceInternal.h"
#include "Project.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace interstellar
{
    using CK = Command::Kind;
    using EK = Event::Kind;

    namespace
    {
        const std::vector<std::pair<std::string, std::string>> &builtInPresets()
        {
            static const std::vector<std::pair<std::string, std::string>> k = {
                {"YouTube 1080p", "--format h264 --res 1920x1080 --quality 18 --speed slow"},
                {"ProRes HQ master", "--format prores --profile hq"},
                {"Review H.264", "--format h264 --res 1280x720 --quality 26 --speed fast"},
            };
            return k;
        }

        // "--a 1 --b x" → {{a,1},{b,x}}; the flags a preset may hold
        bool parseFlags(const std::string &text, std::vector<std::pair<std::string, std::string>> &out, std::string &bad)
        {
            static const std::vector<std::string> allowed = {"format", "profile", "res", "fps", "quality", "speed", "bits", "encoder", "output", "peak", "container"};
            std::istringstream in(text);
            std::string k, v;
            out.clear();
            while (in >> k)
            {
                if (k.size() < 3 || k.compare(0, 2, "--") != 0 || !(in >> v)) { bad = k; return false; }
                k = k.substr(2);
                if (std::find(allowed.begin(), allowed.end(), k) == allowed.end()) { bad = "--" + k; return false; }
                out.emplace_back(k, v);
            }
            return true;
        }

        std::string specWords(const std::vector<std::pair<std::string, std::string>> &flags)
        {
            auto get = [&](const char *k) {
                for (const auto &kv : flags) if (kv.first == k) return kv.second;
                return std::string();
            };
            const std::string f = get("format");
            std::string s = f == "h264" ? "H.264" : f == "h265" ? "H.265" : f == "prores" ? "ProRes" : f == "dnxhr" ? "DNxHR" : f == "png-seq" ? "PNG sequence" : f;
            if (!get("profile").empty()) s += " " + get("profile");
            if (!get("quality").empty()) s += " \xC2\xB7 q" + get("quality");
            if (!get("speed").empty()) s += " \xC2\xB7 " + get("speed");
            if (!get("bits").empty()) s += " \xC2\xB7 " + get("bits") + "-bit";
            s += " \xC2\xB7 " + (get("res").empty() ? std::string("project size") : "fits " + get("res"));
            if (!get("fps").empty()) s += " \xC2\xB7 " + get("fps") + " fps";
            if (!get("output").empty()) s += " \xC2\xB7 " + get("output");
            return s;
        }
    }

    std::string InterstellarService::presetsPath() const
    {
        if (mHost.settingsPath.empty()) return std::string();
        return (fs::path(mHost.settingsPath).parent_path() / "render-presets").string();
    }

    std::vector<std::pair<std::string, std::string>> InterstellarService::userPresets() const
    {
        std::vector<std::pair<std::string, std::string>> out;
        std::ifstream f(presetsPath());
        std::string line;
        while (std::getline(f, line))
        {
            const auto tab = line.find('\t');
            if (tab == std::string::npos || tab == 0) continue;
            out.emplace_back(line.substr(0, tab), line.substr(tab + 1));
        }
        return out;
    }

    bool InterstellarService::presetFlags(const std::string &name, std::string &flags) const
    {
        for (const auto &p : builtInPresets()) if (p.first == name) { flags = p.second; return true; }
        for (const auto &p : userPresets()) if (p.first == name) { flags = p.second; return true; }
        return false;
    }

    void InterstellarService::fillPresetModel(AppModel &m) const
    {
        m.renderPresets.clear();
        auto add = [&](const std::string &name, const std::string &flags, bool builtIn) {
            std::vector<std::pair<std::string, std::string>> kv;
            std::string bad;
            parseFlags(flags, kv, bad);
            m.renderPresets.push_back({name, builtIn, flags, specWords(kv)});
        };
        for (const auto &p : builtInPresets()) add(p.first, p.second, true);
        for (const auto &p : userPresets()) add(p.first, p.second, false);
    }

    bool InterstellarService::renderWithPreset(const Command &c)
    {
        std::string flags;
        if (!presetFlags(c.flag("preset"), flags)) return fail("render: no render preset named \"" + c.flag("preset") + "\" (`render preset list`)");
        std::vector<std::pair<std::string, std::string>> kv;
        std::string bad;
        if (!parseFlags(flags, kv, bad)) return fail("render: the preset \"" + c.flag("preset") + "\" has a flag render does not take: " + bad);
        // the preset's flags first, then each flag given REPLACES its value (or joins)
        Command merged = c;
        merged.flags.clear();
        for (const auto &f : kv)
        {
            std::string v = f.second;
            if (f.first == "res")
            {
                // a frame to fit in: the project's aspect, inside it, never above the project
                int fw = 0, fh = 0;
                const int PW = mProject->width, PH = mProject->height;
                if (std::sscanf(v.c_str(), "%dx%d", &fw, &fh) == 2 && fw > 0 && fh > 0 && PW > 0 && PH > 0)
                {
                    const double s = std::min({1.0, (double)fw / PW, (double)fh / PH});
                    if (s >= 1.0) continue;   // the project fits already: its own size
                    // the render's own arithmetic: the long edge, the aspect kept
                    int ow = std::max(1, (int)std::lround(PW * s)), oh = std::max(1, (int)std::lround(PH * s));
                    ow -= ow % 2;
                    oh -= oh % 2;
                    v = std::to_string(ow) + "x" + std::to_string(oh);
                }
            }
            merged.flags.emplace_back(f.first, v);
        }
        for (const auto &f : c.flags)
        {
            if (f.first == "preset") continue;
            auto it = std::find_if(merged.flags.begin(), merged.flags.end(), [&](const auto &x) { return x.first == f.first; });
            if (it != merged.flags.end()) it->second = f.second;   // a flag given wins
            else merged.flags.push_back(f);
        }
        return renderCommand(merged);
    }

    bool InterstellarService::presetCommand(const Command &c)
    {
        if (c.kind == CK::RenderPresetList)
        {
            AppModel m;
            fillPresetModel(m);
            std::ostringstream o;
            for (const auto &p : m.renderPresets) o << p.name << (p.builtIn ? "  (built in)" : "") << "  " << p.flags << "\n";
            mOutput = o.str();
            return true;
        }
        const std::string name = c.arg(0);
        const std::string verb = specFor(c.kind)->verb;
        if (name.empty() || name.find('\t') != std::string::npos || name.find('\n') != std::string::npos || name.size() > 64)
            return fail(verb + ": a preset's name is one line of up to 64 characters");
        for (const auto &p : builtInPresets())
            if (p.first == name) return fail(verb + ": \"" + name + "\" is built in — save under another name");
        if (presetsPath().empty()) return fail(verb + ": this session keeps no settings, so no presets");
        auto presets = userPresets();
        if (c.kind == CK::RenderPresetDelete)
        {
            const auto before = presets.size();
            presets.erase(std::remove_if(presets.begin(), presets.end(), [&](const auto &p) { return p.first == name; }), presets.end());
            if (presets.size() == before) return fail(verb + ": no preset named \"" + name + "\"");
        }
        else
        {
            // the spec: every render flag given, none of a render's own (timeline, path, range)
            std::string flags;
            for (const auto &f : c.flags) flags += (flags.empty() ? "" : " ") + std::string("--") + f.first + " " + f.second;
            std::vector<std::pair<std::string, std::string>> kv;
            std::string bad;
            if (!parseFlags(flags, kv, bad)) return fail(verb + ": " + bad + " is not part of a render's spec");
            bool hasFormat = false;
            for (const auto &f : kv) hasFormat = hasFormat || f.first == "format";
            if (!hasFormat) return fail(verb + ": a preset names its --format");
            bool replaced = false;
            for (auto &p : presets) if (p.first == name) { p.second = flags; replaced = true; }
            if (!replaced) presets.emplace_back(name, flags);
        }
        std::ofstream f(presetsPath(), std::ios::trunc);
        for (const auto &p : presets) f << p.first << '\t' << p.second << '\n';
        if (!f) return fail(verb + ": cannot write " + presetsPath());
        refreshModel();
        return true;
    }
}
}
