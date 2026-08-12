/*
 *  Genesis — the recents list.
 *
 *  What the home screen shows and what "reopen the last thing" means. Stored as one small
 *  text file under the user's config dir, so it survives across sessions the way cosmo's
 *  settings do. Missing or unreadable is not an error: an empty list is a valid state the
 *  home screen already draws.
 */
#pragma once
#include <string>
#include <vector>

namespace genesis
{
namespace ui
{
    struct RecentEntry
    {
        std::string path;
        std::string name;    // the component's name, so a card can be labelled before loading
        std::string base;    // its base class
        long long modified = 0;  // seconds since the epoch, for "2h ago"
        bool exists = false;     // re-checked on load; a moved file is shown as missing
    };

    class Recents
    {
    public:
        static std::string path();
        /** Load, dropping entries whose file has vanished. */
        static std::vector<RecentEntry> load();
        static bool save(const std::vector<RecentEntry> &entries);
        /** Put `entry` at the front, de-duplicated by path, capped at kMax. */
        static void remember(const std::string &file, const std::string &name, const std::string &base);
        /** "just now" / "2h ago" / "3d ago" for a card's caption. */
        static std::string relativeAge(long long modified, long long nowSeconds);

        static constexpr size_t kMax = 12;
    };
}
}
