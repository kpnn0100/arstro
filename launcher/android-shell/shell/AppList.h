/*
 *  arstro-android-shell — AppList (M6.1/6.2): installed-app enumeration + launch.
 *
 *  Enumerates desktop apps via GIO `GAppInfo`/`GDesktopAppInfo` (covers flatpak/snap exports),
 *  exposing name + icon name for the launcher grid/drawer, and launching one with a Wayland
 *  activation token. A fake list (fixed names) is used for deterministic golden renders.
 *  Icon pixmaps (GtkIconTheme → registerImage → masked) are a later refinement; the launcher
 *  currently draws the M2.6 letter-in-tonal-circle fallback, which needs only the name.
 */
#pragma once
#include <gio/gio.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace arstro
{
namespace androidshell
{
    struct AppEntry
    {
        std::string name;
        std::string iconName;
        GAppInfo *info = nullptr;  // owned by the GList in AppList; not freed per-entry
    };

    class AppList
    {
    public:
        ~AppList()
        {
            for (GList *l = mRaw; l; l = l->next) g_object_unref(l->data);
            g_list_free(mRaw);
        }

        // Load the real installed apps (NoDisplay filtered out), sorted by name.
        void loadInstalled()
        {
            clear();
            mRaw = g_app_info_get_all();
            for (GList *l = mRaw; l; l = l->next)
            {
                GAppInfo *info = G_APP_INFO(l->data);
                if (!g_app_info_should_show(info)) continue;
                AppEntry e;
                e.name = g_app_info_get_display_name(info) ? g_app_info_get_display_name(info) : "";
                GIcon *icon = g_app_info_get_icon(info);
                if (icon) { char *s = g_icon_to_string(icon); if (s) { e.iconName = s; g_free(s); } }
                e.info = info;
                mApps.push_back(std::move(e));
            }
            std::sort(mApps.begin(), mApps.end(),
                      [](const AppEntry &a, const AppEntry &b) { return a.name < b.name; });
        }

        // A fixed list for golden renders (no system dependency).
        void loadFake()
        {
            clear();
            for (const char *n : {"Files", "Terminal", "Browser", "Settings", "Photos", "Music",
                                  "Calendar", "Mail", "Maps", "Camera", "Clock", "Notes",
                                  "Calculator", "Store", "Editor", "Weather"})
                mApps.push_back({n, "", nullptr});
        }

        void launch(size_t i) const
        {
            if (i >= mApps.size() || !mApps[i].info) return;
            GdkAppLaunchContext *ctx = nullptr;  // a real Wayland token needs a GdkDisplay context
            g_app_info_launch(mApps[i].info, nullptr, G_APP_LAUNCH_CONTEXT(ctx), nullptr);
        }

        // Filter by a search query (case-insensitive substring on the name).
        std::vector<size_t> search(const std::string &q) const
        {
            std::vector<size_t> out;
            std::string lq = lower(q);
            for (size_t i = 0; i < mApps.size(); ++i)
                if (lq.empty() || lower(mApps[i].name).find(lq) != std::string::npos) out.push_back(i);
            return out;
        }

        const std::vector<AppEntry> &apps() const { return mApps; }
        int count() const { return (int)mApps.size(); }

    private:
        void clear() { mApps.clear(); }
        static std::string lower(std::string s)
        {
            for (char &c : s) c = (char)std::tolower((unsigned char)c);
            return s;
        }
        std::vector<AppEntry> mApps;
        GList *mRaw = nullptr;
    };

} // namespace androidshell
} // namespace arstro
