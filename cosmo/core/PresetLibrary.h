/*
 *  Arstro cosmo_core — PresetLibrary: UI-free filesystem scan of a preset
 *  directory (folders + .apf files), extracted from cosmo's PresetPanel/
 *  CosmoApp so any preset-browser widget can bind to the same tree/labels
 *  without duplicating the filesystem walk.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct PresetNode
    {
        bool folder = false;
        std::string name;     // display name (folder name, or preset name without ".apf")
        std::string relPath;  // path relative to the scanned root (folders joined with "/")
        std::vector<PresetNode> kids;  // folder only
    };

    class PresetLibrary
    {
    public:
        /** Recursively scan `dir` for subfolders and *.apf files. Each level is
         *  sorted folders-first, then alphabetically. Returns empty if `dir`
         *  doesn't exist. */
        static std::vector<PresetNode> scan(const std::string &dir);

        /** Alphabetically sorted *.apf stems directly inside `dir` (not recursive) --
         *  for a flat quick-apply menu. Empty if `dir` doesn't exist. */
        static std::vector<std::string> flatNames(const std::string &dir);

        /** Friendly label for a generic .apf category key (see EditParamsApf). */
        static std::string categoryLabel(const std::string &key);
    };
}
}
