/*
 *  cosmo_core by arstro — ProjectStore: the persisted "recent projects" index for
 *  the home screen (R-HOME-6). A cosmo project is a `.cmp` file, which is just a
 *  workspace catalog (EditSession::saveWorkspaceAs / readWorkspaceFile) referencing
 *  the original images on disk (R-HOME-2). This class does NOT own the .cmp format;
 *  it only remembers which projects were recently opened so the home grid can list
 *  them with a thumbnail, name, photo count, size and last-opened time.
 *
 *  Platform-free: plain std::filesystem + fstream + getenv, no UI/OS-toolkit calls.
 *  The index is a small tab-separated file in the user config dir; newest first.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    struct RecentEntry
    {
        std::string name;            // project display name (usually the .cmp stem)
        std::string path;            // absolute path to the .cmp file (identity key)
        std::string firstImagePath;  // first image in the project (for the thumbnail); "" if none
        int photoCount = 0;
        long long sizeBytes = 0;     // sum of referenced image file sizes (0 = unknown/empty)
        long long lastOpened = 0;    // unix epoch seconds
    };

    class ProjectStore
    {
    public:
        /** User config dir for cosmo_v2 — settings.txt, recent.tsv and cosmo_v2.log all live
         *  here. Created if missing. Resolved in this order (D-8; the reasoning, including
         *  why an existing legacy directory is adopted rather than migrated, is the long
         *  comment above `resolveConfigDir` in ProjectStore.cpp):
         *    1. `$XDG_CONFIG_HOME/cosmo_v2` — the explicit override every scripted run uses
         *       to sandbox itself, so it wins on every platform.
         *    2. Windows only: a pre-D-8 directory that already holds state, adopted as-is.
         *    3. Windows only: `%APPDATA%\cosmo_v2`, else `%USERPROFILE%\.config\cosmo_v2`.
         *    4. `$HOME/.config/cosmo_v2` — POSIX, and MSYS2, which sets HOME.
         *    5. `./.config/cosmo_v2` — last resort, when nothing is set. */
        static std::string configDir();
        /** Path of the recent-projects index file inside configDir(). */
        static std::string indexPath();

        /** Recent projects, newest first (drops entries whose .cmp no longer exists). */
        static std::vector<RecentEntry> recents();
        /** Insert/refresh `e` at the front (keyed by path), cap the list, and persist. */
        static void remember(RecentEntry e);
    };
}
}
