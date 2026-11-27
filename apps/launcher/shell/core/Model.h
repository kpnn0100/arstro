/*
 *  arstro-shell — the observable model (R-CORE-1).
 *
 *  Plain data only: no pixels, no toolkit types, no file handles. Every front end (the SDL
 *  host, the CLI, a test) reads this and nothing else, so a behaviour that is not visible here
 *  is not observable at all (arstro.rule §5).
 *
 *  All lengths are LOGICAL units (dp): the host divides the display scale out at the input
 *  entry point, so a gesture threshold means the same finger travel on a 5" and a 10" panel.
 */
#pragma once
#include <map>
#include <string>
#include <vector>

namespace arstro
{
namespace shell
{
    /** An installed application, as its manifest describes it (R-PKG-1). */
    struct AppInfo
    {
        std::string id;       // reverse-dns, the stable key ("arstro.settings")
        std::string name;     // shown under the icon
        std::string version;
        std::string exec;     // absolute path of the executable
        std::string icon;     // absolute path of a PNG, or empty for the monogram tile
        std::string accent;   // "#RRGGBB" tint of the monogram tile / splash, or empty
        std::string dir;      // the bundle directory
        bool system = false;  // may send service commands (permission "system")
    };

    enum class TaskState { Starting, Running, Paused };

    /** One running application instance. `id` is never reused within a session. */
    struct Task
    {
        int id = 0;
        std::string appId;
        TaskState state = TaskState::Starting;
        bool connected = false;  // its process has said hello on the app socket
    };

    enum class NavState { Home, App, Recents };

    enum class GestureKind { None, Home, Back, ShadeOpen, ShadeClose };

    /** What letting go right now would do — the view morphs toward it while the finger is down. */
    enum class GestureResult { Cancelled, Home, Recents, Back, ShadeOpened, ShadeClosed };

    /** The system gesture in flight. Live values: excluded from the stable dump. */
    struct GestureModel
    {
        GestureKind kind = GestureKind::None;
        double dx = 0.0, dy = 0.0;      // finger travel since the press, dp
        double startX = 0.0, startY = 0.0;
        double progress = 0.0;          // 0..1, the gesture's own measure of completion
        int edge = 0;                   // Back: -1 from the left edge, +1 from the right
        GestureResult hint = GestureResult::Cancelled;
    };

    struct Notification
    {
        int id = 0;
        std::string app, title, body;
    };

    struct Model
    {
        double screenW = 1280.0, screenH = 800.0;  // dp
        std::vector<AppInfo> apps;                 // sorted by name
        std::vector<Task> tasks;                   // most recently used first
        NavState nav = NavState::Home;
        int focusedTask = 0;                       // 0 = none
        bool shadeOpen = false;
        GestureModel gesture;
        std::map<std::string, std::string> settings;  // every key in settingSpecs(), always present
        std::vector<Notification> notifications;       // newest first
        std::string lastError;                         // the last rejected command, "" when none
        unsigned revision = 0;                         // bumped on every change; excluded from the dump

        const AppInfo *app(const std::string &id) const
        {
            for (const AppInfo &a : apps)
                if (a.id == id) return &a;
            return nullptr;
        }
        const Task *task(int id) const
        {
            for (const Task &t : tasks)
                if (t.id == id) return &t;
            return nullptr;
        }
    };

    inline const char *navName(NavState n)
    {
        return n == NavState::Home ? "home" : n == NavState::App ? "app" : "recents";
    }
    inline const char *taskStateName(TaskState s)
    {
        return s == TaskState::Starting ? "starting" : s == TaskState::Running ? "running" : "paused";
    }
    inline const char *gestureName(GestureKind k)
    {
        switch (k)
        {
        case GestureKind::Home: return "home";
        case GestureKind::Back: return "back";
        case GestureKind::ShadeOpen: return "shade-open";
        case GestureKind::ShadeClose: return "shade-close";
        default: return "none";
        }
    }
    inline const char *resultName(GestureResult r)
    {
        switch (r)
        {
        case GestureResult::Home: return "home";
        case GestureResult::Recents: return "recents";
        case GestureResult::Back: return "back";
        case GestureResult::ShadeOpened: return "shade-opened";
        case GestureResult::ShadeClosed: return "shade-closed";
        default: return "cancelled";
        }
    }
}
}
