// SolarisService — device presets (R-PRESET-1): a device's set values under a name, a text file per preset in the
// user's folder or the factory's (both named by the host; the factory's never written). Saving and deleting change the
// machine, not the song; loading is an edit of the song, ONE, each value checked as `set` checks it.
#include "Format.h"
#include "SolarisService.h"
#include "device/Device.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace arstro
{
namespace solaris
{
    using K = Command::Kind;

    namespace
    {
        constexpr const char *kExt = ".slpreset";

        std::string trim(const std::string &s)
        {
            const size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
            return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
        }

        bool nameOk(const std::string &name, std::string &err)
        {
            if (name.empty() || name.size() > 64 || name[0] == '.' || name.find_first_of("/\\\n\r\t") != std::string::npos)
            {
                err = "a preset's name is 1 … 64 characters, no slash, not starting with a dot — got `" + name + "`";
                return false;
            }
            return true;
        }

        /** A preset file: its type, and its values in order (`sample` among them for a sampler). */
        bool readPreset(const std::string &path, std::string &type, std::vector<std::pair<std::string, std::string>> &values, std::string &err)
        {
            std::ifstream f(path);
            if (!f) { err = "cannot read " + path; return false; }
            std::string line;
            bool magic = false;
            while (std::getline(f, line))
            {
                line = trim(line);
                if (line.empty() || line[0] == ';') continue;
                const size_t eq = line.find('=');
                if (eq == std::string::npos) { err = path + ": `" + line + "` is not key = value"; return false; }
                const std::string k = trim(line.substr(0, eq)), v = trim(line.substr(eq + 1));
                if (k == "arstro-preset") magic = true;
                else if (k == "type") type = v;
                else if (k == "name") continue; // the file's name is the preset's
                else values.emplace_back(k, v);
            }
            if (!magic || type.empty()) { err = path + " is not a preset (no `arstro-preset` and `type`)"; return false; }
            return true;
        }
    }

    std::string SolarisService::presetFile(const std::string &type, const std::string &name, bool factory) const
    {
        const std::string &dir = factory ? mHost.factoryPresetsPath : mHost.presetsPath;
        return dir.empty() ? std::string() : dir + "/" + type + "/" + name + kExt;
    }

    void SolarisService::scanPresets()
    {
        // R-PRESET-1: every preset of every registry type — the user's, then the factory's of a name the user has not
        mPresets.clear();
        for (const auto &t : DeviceRegistry::types())
            for (bool factory : {false, true})
            {
                const std::string &root = factory ? mHost.factoryPresetsPath : mHost.presetsPath;
                if (root.empty()) continue;
                std::error_code ec;
                std::vector<std::string> names;
                for (const auto &e : fs::directory_iterator(root + "/" + t.name, ec))
                {
                    const std::string fn = e.path().filename().string();
                    if (fn.size() > std::string(kExt).size() && fn.compare(fn.size() - std::string(kExt).size(), std::string::npos, kExt) == 0)
                        names.push_back(fn.substr(0, fn.size() - std::string(kExt).size()));
                }
                std::sort(names.begin(), names.end());
                for (const auto &n : names)
                {
                    bool shadowed = false;
                    for (const auto &x : mPresets) shadowed |= x.type == t.name && x.name == n;
                    if (!shadowed) mPresets.push_back(PresetModel{t.name, n, factory});
                }
            }
        mPresetsScanned = true;
    }

    bool SolarisService::applyPreset(const std::string &dv, const std::string &name, std::string &err)
    {
        Project &p = mProject;
        DeviceNode *node = nullptr;
        for (auto &r : p.racks)
            for (auto &d : r.devices)
                if (d.id == dv) node = &d;
        if (!node) { err = "no device `" + dv + "`"; return false; }
        const std::string type = node->type;
        std::string path = presetFile(type, name, false);
        std::error_code ec;
        if (path.empty() || !fs::exists(path, ec)) path = presetFile(type, name, true);
        if (path.empty() || !fs::exists(path, ec))
        {
            if (!mPresetsScanned) scanPresets();
            err = "no " + type + " preset `" + name + "`";
            std::string have;
            for (const auto &x : mPresets)
                if (x.type == type) have += " \"" + x.name + "\"";
            err += have.empty() ? " (there are none yet: `preset save " + dv + " <name>`)" : " — the " + type + "'s:" + have;
            return false;
        }
        std::string fileType;
        std::vector<std::pair<std::string, std::string>> values;
        if (!readPreset(path, fileType, values, err)) return false;
        if (fileType != type) { err = "`" + name + "` is a " + fileType + " preset — " + dv + " is a " + type; return false; }
        // every value back to the registry's default, then the preset's — each checked as `set` checks it
        node->params.clear();
        node->sample.clear();
        std::string stored;
        for (const auto &kv : values)
            if (!setAddress(dv + "." + kv.first, kv.second, stored, err)) { err = "preset `" + name + "`: " + err; return false; }
        changed("device.preset", dv);
        return true;
    }

    bool SolarisService::presetCommand(const Command &c, std::string &err)
    {
        if (!mPresetsScanned) scanPresets();
        if (c.kind == K::PresetList)
        {
            const std::string type = c.arg(0);
            if (!type.empty() && !DeviceRegistry::find(type)) { err = "`" + type + "` is not a device type"; return false; }
            std::ostringstream o;
            for (const auto &x : mPresets)
                if (type.empty() || x.type == type) o << x.type << ": " << x.name << (x.factory ? "  (factory)" : "") << "\n";
            mOutput = o.str();
            return true;
        }
        if (c.kind == K::PresetDelete)
        {
            const std::string type = c.arg(0), name = c.arg(1);
            if (!nameOk(name, err)) return false;
            const std::string path = presetFile(type, name, false);
            std::error_code ec;
            if (path.empty() || !fs::exists(path, ec))
            {
                const std::string fac = presetFile(type, name, true);
                err = !fac.empty() && fs::exists(fac, ec) ? "\"" + name + "\" is a factory preset — it cannot be deleted" : "no " + type + " preset `" + name + "` of yours";
                return false;
            }
            if (!fs::remove(path, ec)) { err = "cannot delete " + path; return false; }
            scanPresets();
            mOutput = "deleted " + type + " preset \"" + name + "\"\n";
            return true;
        }
        if (!requireOpen(err)) return false;
        const std::string dv = c.arg(0), name = c.arg(1);
        if (!nameOk(name, err)) return false;
        const DeviceNode *node = nullptr;
        for (const auto &r : mProject.racks)
            for (const auto &d : r.devices)
                if (d.id == dv) node = &d;
        if (!node) { err = "no device `" + dv + "`"; return false; }
        if (c.kind == K::PresetLoad) return applyPreset(dv, name, err);
        // K::PresetSave: the device's set values, as the .slp keeps them, and its sound
        const std::string path = presetFile(node->type, name, false);
        if (path.empty()) { err = "this build has no folder for presets"; return false; }
        std::error_code ec;
        if (fs::exists(path, ec) && !c.has("replace")) { err = "you have a " + node->type + " preset \"" + name + "\" — give --replace to write over it"; return false; }
        fs::create_directories(fs::path(path).parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) { err = "cannot write " + path; return false; }
        f << "arstro-preset = 1\ntype = " << node->type << "\nname = " << name << "\n";
        for (const auto &kv : node->params) f << kv.first << " = " << kv.second << "\n";
        if (!node->sample.empty()) f << "sample = " << resolvePath(node->sample) << "\n";
        if (!f) { err = "cannot write " + path; return false; }
        f.close();
        scanPresets();
        mOutput = "saved " + node->type + " preset \"" + name + "\"\n";
        return true;
    }
}
}
