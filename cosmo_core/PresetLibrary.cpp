#include "PresetLibrary.h"
#include <algorithm>
#include <filesystem>
#include <functional>

namespace arstro
{
namespace cosmo
{
    std::vector<PresetNode> PresetLibrary::scan(const std::string &dir)
    {
        std::vector<PresetNode> out;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return out;

        std::function<std::vector<PresetNode>(const std::string &, const std::string &)> walk =
            [&](const std::string &absDir, const std::string &relPrefix) -> std::vector<PresetNode> {
            std::vector<PresetNode> nodes;
            std::error_code walkEc;
            for (const auto &e : std::filesystem::directory_iterator(absDir, walkEc))
            {
                if (e.is_directory(walkEc))
                {
                    PresetNode n;
                    n.folder = true;
                    n.name = e.path().filename().string();
                    n.relPath = relPrefix.empty() ? n.name : relPrefix + "/" + n.name;
                    n.kids = walk(e.path().string(), n.relPath);
                    nodes.push_back(std::move(n));
                }
                else if (e.path().extension() == ".apf")
                {
                    PresetNode n;
                    n.folder = false;
                    n.name = e.path().stem().string();
                    n.relPath = relPrefix.empty() ? n.name : relPrefix + "/" + n.name;
                    nodes.push_back(std::move(n));
                }
            }
            std::sort(nodes.begin(), nodes.end(), [](const PresetNode &a, const PresetNode &b) {
                if (a.folder != b.folder) return a.folder;  // folders before presets
                return a.name < b.name;
            });
            return nodes;
        };

        return walk(dir, "");
    }

    std::vector<std::string> PresetLibrary::flatNames(const std::string &dir)
    {
        std::vector<std::string> names;
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) return names;
        for (const auto &e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".apf")
                names.push_back(e.path().stem().string());
        std::sort(names.begin(), names.end());
        return names;
    }

    std::string PresetLibrary::categoryLabel(const std::string &key)
    {
        if (key == "basic") return "Basic (exposure, contrast, tone)";
        if (key == "color") return "Color (white balance, vibrance)";
        if (key == "presence") return "Presence (texture, clarity, dehaze)";
        if (key == "effects") return "Effects (grain)";
        if (key == "detail") return "Detail (sharpen, noise reduction)";
        if (key == "lens") return "Lens corrections";
        if (key == "curve") return "Tone curve";
        if (key == "mixer") return "Color mixer";
        if (key == "grade") return "Color grading";
        if (key == "transform") return "Crop and rotate";
        if (key == "masks") return "Masks (local adjustments)";
        return key;
    }
}
}
