#include "Command.h"
#include <cstdlib>
#include <sstream>

namespace arstro
{
namespace shell
{
    const std::vector<CommandSpec> &commandSpecs()
    {
        static const std::vector<CommandSpec> specs = {
            {CommandKind::AppLaunch, "app launch", ArgShape::Id, "<app-id>", "Bring the app to the front, starting it if it is not running."},
            {CommandKind::AppClose, "app close", ArgShape::Number, "<task>", "End a task and remove it from recents."},
            {CommandKind::AppsRescan, "apps rescan", ArgShape::None, "", "Read the installed bundles again."},
            {CommandKind::NavHome, "nav home", ArgShape::None, "", "Go to the home screen."},
            {CommandKind::NavBack, "nav back", ArgShape::None, "", "Back: closes the shade, leaves recents, or asks the front app."},
            {CommandKind::NavRecents, "nav recents", ArgShape::None, "", "Show the recent apps."},
            {CommandKind::TaskFocus, "task focus", ArgShape::Number, "<task>", "Bring a running task to the front."},
            {CommandKind::TaskConnected, "task connected", ArgShape::Number, "<task>", "Host: the task's process attached to the app socket."},
            {CommandKind::TaskExited, "task exited", ArgShape::Number, "<task>", "Host: the task's process is gone."},
            {CommandKind::TaskBackUnhandled, "task back-unhandled", ArgShape::Number, "<task>", "Host: the app did not use Back, so the shell goes home."},
            {CommandKind::ShadeOpen, "shade open", ArgShape::None, "", "Pull the quick-settings and notification shade down."},
            {CommandKind::ShadeClose, "shade close", ArgShape::None, "", "Push the shade up."},
            {CommandKind::ShadeToggle, "shade toggle", ArgShape::None, "", "Open the shade if closed, close it if open."},
            {CommandKind::Set, "set", ArgShape::Fields, "<key>=<value> ...", "Change settings. Keys and ranges: see settings."},
            {CommandKind::NotifyPost, "notify post", ArgShape::Fields, "title=<text> [body=<text>] [app=<app-id>]", "Post a notification."},
            {CommandKind::NotifyDismiss, "notify dismiss", ArgShape::Number, "<id>", "Remove one notification."},
            {CommandKind::NotifyClear, "notify clear", ArgShape::None, "", "Remove every notification."},
            {CommandKind::Touch, "touch", ArgShape::Touch, "down|move|up|cancel <x> <y> <time-ms>", "One touch sample in dp. Drives the system gestures."},
            {CommandKind::Screen, "screen", ArgShape::Size, "<width> <height>", "The screen size in dp."},
        };
        return specs;
    }

    const std::vector<EventSpec> &eventSpecs()
    {
        static const std::vector<EventSpec> specs = {
            {"nav.changed", "state=home|app|recents task=<n>", "The front of the screen changed."},
            {"task.spawn", "task=<n> app=<app-id>", "The host must start this app's process."},
            {"task.state", "task=<n> state=starting|running|paused", "A task's lifecycle state changed."},
            {"task.back", "task=<n>", "The host must ask this app to handle Back."},
            {"task.kill", "task=<n>", "The host must end this task's process."},
            {"task.closed", "task=<n> app=<app-id>", "A task left the recents list."},
            {"shade.changed", "open=true|false", "The shade opened or closed."},
            {"gesture.begin", "kind=home|back|shade-open|shade-close", "A touch was claimed by the system."},
            {"gesture.end", "kind=<kind> result=cancelled|home|recents|back|shade-opened|shade-closed", "The system gesture finished."},
            {"setting.changed", "key=<key> value=<value>", "A setting has a new value."},
            {"notify.posted", "id=<n> app=<app-id> title=<text>", "A notification arrived."},
            {"notify.dismissed", "id=<n>", "A notification was removed."},
            {"apps.changed", "count=<n>", "The list of installed apps was read again."},
            {"screen.changed", "w=<dp> h=<dp>", "The screen size changed."},
            {"error", "message=<text>", "A command was rejected. Also kept in lastError."},
        };
        return specs;
    }

    const std::vector<SettingSpec> &settingSpecs()
    {
        static const std::vector<SettingSpec> specs = {
            {"brightness", "number", "1", 0.05, 1.0, "Screen brightness."},
            {"volume", "number", "0.6", 0.0, 1.0, "Media volume."},
            {"wifi", "bool", "true", 0, 0, "Wi-Fi radio."},
            {"bluetooth", "bool", "false", 0, 0, "Bluetooth radio."},
            {"dnd", "bool", "false", 0, 0, "Do not disturb: notifications arrive silently."},
            {"reduced-motion", "bool", "false", 0, 0, "Replace movement with short fades."},
            {"clock24", "bool", "true", 0, 0, "24-hour clock."},
            {"scale", "number", "1", 0.75, 2.0, "Size of everything on screen, on top of the display's own density."},
            {"accent", "color", "#4F7EF7", 0, 0, "The one accent colour, #RRGGBB."},
            {"device-name", "text", "Arstro", 0, 0, "The name of this device."},
        };
        return specs;
    }

    std::vector<std::string> splitWords(const std::string &line)
    {
        std::vector<std::string> words;
        std::string cur;
        bool inQuote = false, any = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            const char c = line[i];
            if (inQuote)
            {
                if (c == '\\' && i + 1 < line.size()) cur += line[++i];
                else if (c == '"') inQuote = false;
                else cur += c;
            }
            else if (c == '"') { inQuote = true; any = true; }
            else if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                if (any) { words.push_back(cur); cur.clear(); any = false; }
            }
            else { cur += c; any = true; }
        }
        if (any) words.push_back(cur);
        return words;
    }

    std::string quoteValue(const std::string &v)
    {
        bool need = v.empty();
        for (char c : v)
            if (c == ' ' || c == '"' || c == '\t' || c == '=') need = true;
        if (!need) return v;
        std::string out = "\"";
        for (char c : v)
        {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out + "\"";
    }

    static bool toNumber(const std::string &s, double &out)
    {
        if (s.empty()) return false;
        char *end = nullptr;
        out = std::strtod(s.c_str(), &end);
        return end && *end == '\0';
    }

    bool parseCommand(const std::string &line, Command &out, std::string &error)
    {
        const std::vector<std::string> w = splitWords(line);
        if (w.empty()) { error = "empty command"; return false; }
        // Longest name first, so "task back-unhandled" is not read as an unknown "task".
        const CommandSpec *spec = nullptr;
        size_t used = 0;
        for (const CommandSpec &s : commandSpecs())
        {
            const std::vector<std::string> n = splitWords(s.name);
            if (n.size() > w.size() || n.size() <= used) continue;
            bool same = true;
            for (size_t i = 0; i < n.size(); ++i) same = same && n[i] == w[i];
            if (same) { spec = &s; used = n.size(); }
        }
        if (!spec) { error = "unknown command: " + line; return false; }
        out = Command{};
        out.kind = spec->kind;
        const std::vector<std::string> a(w.begin() + used, w.end());
        const std::string usage = std::string("usage: ") + spec->name + " " + spec->args;
        switch (spec->shape)
        {
        case ArgShape::None:
            if (!a.empty()) { error = "unexpected argument '" + a[0] + "' — " + usage; return false; }
            break;
        case ArgShape::Id:
            if (a.size() != 1) { error = usage; return false; }
            out.id = a[0];
            break;
        case ArgShape::Number:
        {
            double n;
            if (a.size() != 1 || !toNumber(a[0], n)) { error = usage; return false; }
            out.number = (int)n;
            break;
        }
        case ArgShape::Fields:
            if (a.empty()) { error = usage; return false; }
            for (const std::string &f : a)
            {
                const size_t eq = f.find('=');
                if (eq == std::string::npos || eq == 0) { error = "expected key=value, got '" + f + "'"; return false; }
                out.fields.emplace_back(f.substr(0, eq), f.substr(eq + 1));
            }
            break;
        case ArgShape::Touch:
        {
            if (a.size() != 4) { error = usage; return false; }
            if (a[0] == "down") out.touch.kind = TouchSample::Kind::Down;
            else if (a[0] == "move") out.touch.kind = TouchSample::Kind::Move;
            else if (a[0] == "up") out.touch.kind = TouchSample::Kind::Up;
            else if (a[0] == "cancel") out.touch.kind = TouchSample::Kind::Cancel;
            else { error = "unknown touch phase '" + a[0] + "' — " + usage; return false; }
            if (!toNumber(a[1], out.touch.x) || !toNumber(a[2], out.touch.y) || !toNumber(a[3], out.touch.tMs))
            { error = usage; return false; }
            break;
        }
        case ArgShape::Size:
            if (a.size() != 2 || !toNumber(a[0], out.w) || !toNumber(a[1], out.h)) { error = usage; return false; }
            break;
        }
        return true;
    }

    static std::string num(double v)
    {
        std::ostringstream o;
        o << v;
        return o.str();
    }

    std::string formatCommand(const Command &c)
    {
        for (const CommandSpec &s : commandSpecs())
        {
            if (s.kind != c.kind) continue;
            std::string out = s.name;
            switch (s.shape)
            {
            case ArgShape::None: break;
            case ArgShape::Id: out += " " + quoteValue(c.id); break;
            case ArgShape::Number: out += " " + std::to_string(c.number); break;
            case ArgShape::Fields:
                for (const auto &f : c.fields) out += " " + f.first + "=" + quoteValue(f.second);
                break;
            case ArgShape::Touch:
            {
                static const char *k[] = {"down", "move", "up", "cancel"};
                out += std::string(" ") + k[(int)c.touch.kind] + " " + num(c.touch.x) + " " + num(c.touch.y) + " " + num(c.touch.tMs);
                break;
            }
            case ArgShape::Size: out += " " + num(c.w) + " " + num(c.h); break;
            }
            return out;
        }
        return "";
    }

    std::string formatEvent(const Event &e)
    {
        std::string out = e.name;
        for (const auto &f : e.fields) out += " " + f.first + "=" + quoteValue(f.second);
        return out;
    }
}
}
