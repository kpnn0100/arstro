#include "ShellService.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace shell
{
    ShellService::ShellService()
    {
        for (const SettingSpec &s : settingSpecs()) mModel.settings[s.key] = s.def;
    }

    void ShellService::emit(const std::string &name, Fields fields)
    {
        Event e{name, std::move(fields)};
        for (const Sink &s : mSinks) s(e);
    }

    bool ShellService::reject(const std::string &why)
    {
        // A rejection is observable twice over: on the stream for whoever is listening, and in
        // the model for a front end that looks afterwards (arstro.rule §5).
        mModel.lastError = why;
        changed();
        emit("error", {{"message", why}});
        return false;
    }

    Task *ShellService::task(int id)
    {
        for (Task &t : mModel.tasks)
            if (t.id == id) return &t;
        return nullptr;
    }

    void ShellService::syncTaskStates()
    {
        // One rule for every path that can change who is in front: a task is running only while
        // it is the focused task AND the screen is showing it. Recents shows snapshots, so the
        // app behind a recents card is paused too — it must not burn a Pi's CPU drawing frames
        // nobody composites at full size.
        for (Task &t : mModel.tasks)
        {
            TaskState want = TaskState::Starting;
            if (t.connected)
                want = (mModel.nav == NavState::App && t.id == mModel.focusedTask) ? TaskState::Running
                                                                                     : TaskState::Paused;
            if (want != t.state)
            {
                t.state = want;
                emit("task.state", {{"task", std::to_string(t.id)}, {"state", taskStateName(want)}});
            }
        }
    }

    void ShellService::setNav(NavState nav, int focused)
    {
        if (nav == NavState::App && !task(focused)) nav = NavState::Home;
        const bool same = nav == mModel.nav && focused == mModel.focusedTask;
        mModel.nav = nav;
        mModel.focusedTask = focused;
        if (nav == NavState::App)
        {
            // Most recently used first: the focused task moves to the front of recents.
            auto it = std::find_if(mModel.tasks.begin(), mModel.tasks.end(),
                                   [&](const Task &t) { return t.id == focused; });
            if (it != mModel.tasks.end()) std::rotate(mModel.tasks.begin(), it, it + 1);
        }
        syncTaskStates();
        if (!same)
        {
            changed();
            emit("nav.changed", {{"state", navName(nav)}, {"task", std::to_string(focused)}});
        }
    }

    void ShellService::setShade(bool open)
    {
        if (mModel.shadeOpen == open) return;
        mModel.shadeOpen = open;
        changed();
        emit("shade.changed", {{"open", open ? "true" : "false"}});
    }

    void ShellService::back()
    {
        if (mModel.shadeOpen) { setShade(false); return; }
        if (mModel.nav == NavState::Recents)
        {
            setNav(task(mModel.focusedTask) ? NavState::App : NavState::Home, mModel.focusedTask);
            return;
        }
        if (mModel.nav == NavState::App)
        {
            const Task *t = task(mModel.focusedTask);
            // An app that has not attached yet cannot answer, so Back cannot wait on it.
            if (t && t->connected) emit("task.back", {{"task", std::to_string(t->id)}});
            else setNav(NavState::Home, mModel.focusedTask);
        }
    }

    bool ShellService::applySetting(const std::string &key, const std::string &value, std::string &error)
    {
        for (const SettingSpec &s : settingSpecs())
        {
            if (key != s.key) continue;
            std::string v = value;
            const std::string type = s.type;
            if (type == "bool")
            {
                if (v == "on" || v == "1") v = "true";
                if (v == "off" || v == "0") v = "false";
                if (v != "true" && v != "false") { error = key + " must be true or false, got '" + value + "'"; return false; }
            }
            else if (type == "number")
            {
                char *end = nullptr;
                const double n = std::strtod(v.c_str(), &end);
                if (v.empty() || !end || *end) { error = key + " must be a number, got '" + value + "'"; return false; }
                if (n < s.min || n > s.max)
                {
                    std::ostringstream o;
                    o << key << " must be between " << s.min << " and " << s.max << ", got " << value;
                    error = o.str();
                    return false;
                }
                std::ostringstream o;
                o << n;
                v = o.str();
            }
            else if (type == "color")
            {
                bool ok = v.size() == 7 && v[0] == '#';
                for (size_t i = 1; ok && i < v.size(); ++i) ok = std::isxdigit((unsigned char)v[i]) != 0;
                if (!ok) { error = key + " must be #RRGGBB, got '" + value + "'"; return false; }
            }
            if (mModel.settings[key] != v)
            {
                mModel.settings[key] = v;
                changed();
                emit("setting.changed", {{"key", key}, {"value", v}});
            }
            return true;
        }
        // An unknown key is an error naming the key — never a quiet success (cosmo D-59).
        error = "unknown setting '" + key + "'";
        return false;
    }

    void ShellService::applyGesture(GestureKind kind, GestureResult result)
    {
        switch (result)
        {
        case GestureResult::Home: setNav(NavState::Home, mModel.focusedTask); break;
        case GestureResult::Recents: setNav(NavState::Recents, mModel.focusedTask); break;
        case GestureResult::Back: back(); break;
        case GestureResult::ShadeOpened: setShade(true); break;
        case GestureResult::ShadeClosed: setShade(false); break;
        case GestureResult::Cancelled: break;
        }
        (void)kind;
    }

    bool ShellService::run(const std::string &line)
    {
        Command c;
        std::string error;
        if (!parseCommand(line, c, error)) return reject(error);
        return dispatch(c);
    }

    bool ShellService::dispatch(const Command &c)
    {
        // A touch sample is not a user-level command; it must not clear the error a previous
        // command left for whoever comes to read it.
        if (c.kind != CommandKind::Touch && !mModel.lastError.empty())
        {
            mModel.lastError.clear();
            changed();
        }
        switch (c.kind)
        {
        case CommandKind::AppLaunch:
        {
            const AppInfo *app = mModel.app(c.id);
            if (!app) return reject("no such app '" + c.id + "'");
            setShade(false);
            for (const Task &t : mModel.tasks)
                if (t.appId == c.id) { setNav(NavState::App, t.id); return true; }
            Task t;
            t.id = mNextTask++;
            t.appId = c.id;
            mModel.tasks.insert(mModel.tasks.begin(), t);
            changed();
            emit("task.spawn", {{"task", std::to_string(t.id)}, {"app", c.id}});
            setNav(NavState::App, t.id);
            return true;
        }
        case CommandKind::AppClose:
        case CommandKind::TaskExited:
        {
            Task *t = task(c.number);
            if (!t) return reject("no such task " + std::to_string(c.number));
            const std::string app = t->appId;
            const int id = t->id;
            if (c.kind == CommandKind::AppClose) emit("task.kill", {{"task", std::to_string(id)}});
            mModel.tasks.erase(mModel.tasks.begin() + (t - mModel.tasks.data()));
            changed();
            emit("task.closed", {{"task", std::to_string(id)}, {"app", app}});
            if (mModel.focusedTask == id)
            {
                // Closing the front card in recents keeps the user in recents while cards remain.
                const int next = mModel.tasks.empty() ? 0 : mModel.tasks.front().id;
                if (mModel.nav == NavState::Recents && next) setNav(NavState::Recents, next);
                else setNav(NavState::Home, 0);
            }
            return true;
        }
        case CommandKind::AppsRescan:
            if (mScanner)
            {
                mModel.apps = mScanner();
                std::sort(mModel.apps.begin(), mModel.apps.end(),
                          [](const AppInfo &a, const AppInfo &b) { return a.name < b.name; });
                changed();
            }
            emit("apps.changed", {{"count", std::to_string(mModel.apps.size())}});
            return true;
        case CommandKind::NavHome:
            setShade(false);
            setNav(NavState::Home, mModel.focusedTask);
            return true;
        case CommandKind::NavBack:
            back();
            return true;
        case CommandKind::NavRecents:
            setShade(false);
            setNav(NavState::Recents, mModel.focusedTask);
            return true;
        case CommandKind::TaskFocus:
            if (!task(c.number)) return reject("no such task " + std::to_string(c.number));
            setShade(false);
            setNav(NavState::App, c.number);
            return true;
        case CommandKind::TaskConnected:
        {
            Task *t = task(c.number);
            if (!t) return reject("no such task " + std::to_string(c.number));
            t->connected = true;
            changed();
            syncTaskStates();
            return true;
        }
        case CommandKind::TaskBackUnhandled:
            if (!task(c.number)) return reject("no such task " + std::to_string(c.number));
            if (mModel.nav == NavState::App && mModel.focusedTask == c.number)
                setNav(NavState::Home, c.number);
            return true;
        case CommandKind::ShadeOpen: setShade(true); return true;
        case CommandKind::ShadeClose: setShade(false); return true;
        case CommandKind::ShadeToggle: setShade(!mModel.shadeOpen); return true;
        case CommandKind::Set:
        {
            // All or nothing: validate every pair before applying any, so a typo in the second
            // key cannot leave the first one half-applied behind a "rejected".
            std::string error;
            for (const auto &f : c.fields)
            {
                bool known = false;
                for (const SettingSpec &s : settingSpecs()) known = known || f.first == s.key;
                if (!known) return reject("unknown setting '" + f.first + "'");
            }
            const std::map<std::string, std::string> before = mModel.settings;
            std::vector<Sink> held;
            held.swap(mSinks);  // hold events until the whole line is known to be valid
            bool ok = true;
            for (const auto &f : c.fields) ok = ok && applySetting(f.first, f.second, error);
            mSinks.swap(held);
            if (!ok) { mModel.settings = before; return reject(error); }
            for (const auto &kv : mModel.settings)
                if (before.at(kv.first) != kv.second)
                    emit("setting.changed", {{"key", kv.first}, {"value", kv.second}});
            return true;
        }
        case CommandKind::NotifyPost:
        {
            Notification n;
            for (const auto &f : c.fields)
            {
                if (f.first == "title") n.title = f.second;
                else if (f.first == "body") n.body = f.second;
                else if (f.first == "app") n.app = f.second;
                else return reject("unknown notification field '" + f.first + "'");
            }
            if (n.title.empty()) return reject("a notification needs a title");
            n.id = mNextNotification++;
            mModel.notifications.insert(mModel.notifications.begin(), n);
            changed();
            emit("notify.posted", {{"id", std::to_string(n.id)}, {"app", n.app}, {"title", n.title}});
            return true;
        }
        case CommandKind::NotifyDismiss:
        {
            auto it = std::find_if(mModel.notifications.begin(), mModel.notifications.end(),
                                   [&](const Notification &n) { return n.id == c.number; });
            if (it == mModel.notifications.end()) return reject("no such notification " + std::to_string(c.number));
            mModel.notifications.erase(it);
            changed();
            emit("notify.dismissed", {{"id", std::to_string(c.number)}});
            return true;
        }
        case CommandKind::NotifyClear:
        {
            const std::vector<Notification> gone = mModel.notifications;
            mModel.notifications.clear();
            changed();
            for (const Notification &n : gone) emit("notify.dismissed", {{"id", std::to_string(n.id)}});
            return true;
        }
        case CommandKind::Touch:
        {
            const GestureKind before = mNavigator.gesture().kind;
            mLastTouch = mNavigator.feed(c.touch, mModel.nav, mModel.shadeOpen, mModel.screenW, mModel.screenH);
            if (mLastTouch.claimedNow)
                emit("gesture.begin", {{"kind", gestureName(mNavigator.gesture().kind)}});
            if (mLastTouch.ended)
            {
                // Clear the live gesture BEFORE applying its result, so the nav.changed a view
                // hears already describes a model with no finger on it.
                mModel.gesture = GestureModel{};
                changed();
                emit("gesture.end", {{"kind", gestureName(before)}, {"result", resultName(mLastTouch.result)}});
                applyGesture(before, mLastTouch.result);
            }
            else if (mNavigator.active())
            {
                mModel.gesture = mNavigator.gesture();
                changed();
            }
            return true;
        }
        case CommandKind::Screen:
            if (c.w < 200 || c.h < 200) return reject("screen must be at least 200 by 200");
            mModel.screenW = c.w;
            mModel.screenH = c.h;
            changed();
            emit("screen.changed", {{"w", std::to_string((int)c.w)}, {"h", std::to_string((int)c.h)}});
            return true;
        }
        return reject("unhandled command");
    }

    std::string ShellService::settingsText() const
    {
        std::string out;
        for (const SettingSpec &s : settingSpecs())
            out += std::string(s.key) + "=" + quoteValue(mModel.settings.at(s.key)) + "\n";
        return out;
    }

    std::string ShellService::dump() const
    {
        std::ostringstream o;
        o << "screen " << mModel.screenW << " " << mModel.screenH << "\n";
        o << "nav " << navName(mModel.nav) << " task=" << mModel.focusedTask << "\n";
        o << "shade " << (mModel.shadeOpen ? "open" : "closed") << "\n";
        for (const AppInfo &a : mModel.apps)
            o << "app " << a.id << " name=" << quoteValue(a.name) << " version=" << quoteValue(a.version)
              << (a.system ? " system" : "") << "\n";
        for (const Task &t : mModel.tasks)
            o << "task " << t.id << " app=" << t.appId << " state=" << taskStateName(t.state) << "\n";
        for (const Notification &n : mModel.notifications)
            o << "notification " << n.id << " app=" << quoteValue(n.app) << " title=" << quoteValue(n.title)
              << " body=" << quoteValue(n.body) << "\n";
        for (const SettingSpec &s : settingSpecs())
            o << "setting " << s.key << "=" << quoteValue(mModel.settings.at(s.key)) << "\n";
        if (!mModel.lastError.empty()) o << "error " << quoteValue(mModel.lastError) << "\n";
        return o.str();
    }

    // ── the API document ─────────────────────────────────────────────────────────────────────

    static std::string jsonStr(const std::string &s)
    {
        std::string o = "\"";
        for (char c : s)
        {
            if (c == '"' || c == '\\') o += '\\';
            if (c == '\n') { o += "\\n"; continue; }
            o += c;
        }
        return o + "\"";
    }

    std::string apiJson()
    {
        std::ostringstream o;
        o << "{\n  \"app\": \"arstro-shell\",\n  \"commands\": [\n";
        const auto &cs = commandSpecs();
        for (size_t i = 0; i < cs.size(); ++i)
            o << "    {\"name\": " << jsonStr(cs[i].name) << ", \"args\": " << jsonStr(cs[i].args)
              << ", \"help\": " << jsonStr(cs[i].help) << "}" << (i + 1 < cs.size() ? "," : "") << "\n";
        o << "  ],\n  \"events\": [\n";
        const auto &es = eventSpecs();
        for (size_t i = 0; i < es.size(); ++i)
            o << "    {\"name\": " << jsonStr(es[i].name) << ", \"fields\": " << jsonStr(es[i].fields)
              << ", \"help\": " << jsonStr(es[i].help) << "}" << (i + 1 < es.size() ? "," : "") << "\n";
        o << "  ],\n  \"settings\": [\n";
        const auto &ss = settingSpecs();
        for (size_t i = 0; i < ss.size(); ++i)
        {
            o << "    {\"key\": " << jsonStr(ss[i].key) << ", \"type\": " << jsonStr(ss[i].type)
              << ", \"default\": " << jsonStr(ss[i].def);
            if (std::string(ss[i].type) == "number") o << ", \"min\": " << ss[i].min << ", \"max\": " << ss[i].max;
            o << ", \"help\": " << jsonStr(ss[i].help) << "}" << (i + 1 < ss.size() ? "," : "") << "\n";
        }
        o << "  ],\n  \"gestures\": {\"unit\": \"dp\", \"bottomZone\": " << gesture::kBottomZone
          << ", \"sideZone\": " << gesture::kSideZone << ", \"topZone\": " << gesture::kTopZone
          << ", \"shadeCloseZone\": " << gesture::kShadeCloseZone << ", \"slop\": " << gesture::kSlop
          << ", \"backCommit\": " << gesture::kBackCommit << ", \"minTravel\": " << gesture::kMinTravel
          << ", \"flingSpeed\": " << gesture::kFlingSpeed << ", \"recentsFraction\": " << gesture::kRecentsFraction
          << ", \"homeFraction\": " << gesture::kHomeFraction << "},\n";
        o << "  \"dump\": [\"screen\", \"nav\", \"shade\", \"app\", \"task\", \"notification\", \"setting\", \"error\"],\n";
        o << "  \"excludedFromDump\": [\"revision\", \"gesture\"]\n}\n";
        return o.str();
    }

    std::string apiMarkdown()
    {
        std::ostringstream o;
        o << "# arstro-shell API\n\nGenerated by `arstro-shell-cc api`. Do not edit: a test regenerates and compares it.\n\n";
        o << "Lengths are dp. One command per line; a value with a space is written `key=\"two words\"`.\n\n";
        o << "## Commands\n\n| Command | Arguments | What it does |\n|---|---|---|\n";
        for (const CommandSpec &c : commandSpecs())
            o << "| `" << c.name << "` | " << (c.args[0] ? "`" + std::string(c.args) + "`" : "") << " | " << c.help << " |\n";
        o << "\n## Events\n\n| Event | Fields | When |\n|---|---|---|\n";
        for (const EventSpec &e : eventSpecs())
            o << "| `" << e.name << "` | `" << e.fields << "` | " << e.help << " |\n";
        o << "\n## Settings\n\n| Key | Type | Default | Range | Meaning |\n|---|---|---|---|---|\n";
        for (const SettingSpec &s : settingSpecs())
        {
            o << "| `" << s.key << "` | " << s.type << " | `" << s.def << "` | ";
            if (std::string(s.type) == "number") o << s.min << " to " << s.max;
            o << " | " << s.help << " |\n";
        }
        o << "\n## System gestures\n\n| Threshold | Value |\n|---|---|\n"
          << "| Bottom edge zone (Home, Recents) | " << gesture::kBottomZone << " dp |\n"
          << "| Side edge zone (Back, inside an app) | " << gesture::kSideZone << " dp |\n"
          << "| Top edge zone (shade) | " << gesture::kTopZone << " dp |\n"
          << "| Zone that closes an open shade | bottom " << gesture::kShadeCloseZone << " dp |\n"
          << "| Travel before the system claims the touch | " << gesture::kSlop << " dp |\n"
          << "| Sideways travel that means Back | " << gesture::kBackCommit << " dp |\n"
          << "| Upward speed that means Home | " << gesture::kFlingSpeed << " dp/s |\n"
          << "| Height that means Recents when released slowly | " << gesture::kRecentsFraction << " of the screen |\n"
          << "| Height that means Home at any speed | " << gesture::kHomeFraction << " of the screen |\n";
        o << "\n## State dump\n\n`arstro-shell-cc state` prints one line per fact: `screen`, `nav`, `shade`, `app`, `task`, "
             "`notification`, `setting`, `error`. The revision counter and a gesture in flight are left out, so two dumps "
             "of the same state are identical.\n";
        return o.str();
    }
}
}
