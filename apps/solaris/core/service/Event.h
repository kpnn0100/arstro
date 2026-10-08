/*
 *  solaris_core — Event: the ONE way the service says what just changed (R-SVC-2).
 *
 *  `formatEvent()` output IS the log line — what `solaris-cc --watch` streams, what the GUI host
 *  logs, what an agent greps. Emitting an event is logging it; there is no second channel.
 *
 *      [evt] project.opened path=song.slp strips=6 clips=14
 *      [evt] params.changed address=dv_2.filter.cutoff value=1200.0
 *      [evt] render.finished out=mix.wav frames=1440000 peak=-1.2
 *
 *  Each kind's fields are declared in `eventSpecs()`, which the API document is generated from.
 *  Line shapes are an interface: once quoted by a test or a doc, add fields at the end only.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace solaris
{
    struct Event
    {
        enum class Kind
        {
            Info,
            Error,            // why — also lands in AppModel::lastError
            CommandRejected,  // line, why
            ScreenChanged,    // screen
            ProjectOpened,    // path, strips, clips
            ProjectSaved,     // path
            ProjectClosed,
            ProjectChanged,   // what, node — a node was added, removed, moved or re-routed
            ParamsChanged,    // address, value
            RenderFinished,   // out, frames, peak
            RenderFailed,     // why
            AuditReport,      // findings
            SettingsChanged,  // what
            DevicesChanged,   // count
            BrowseChanged,    // path, entries
            RecentsChanged    // count
        };

        Kind kind = Kind::Info;
        std::vector<std::pair<std::string, std::string>> fields;

        Event() = default;
        explicit Event(Kind k) : kind(k) {}
        Event &with(const std::string &key, const std::string &value);
        Event &with(const std::string &key, const char *value) { return with(key, std::string(value)); }
        Event &with(const std::string &key, double value);
        Event &with(const std::string &key, long long value);
        Event &with(const std::string &key, int value) { return with(key, (long long)value); }
        std::string field(const std::string &key, const std::string &fallback = std::string()) const;
    };

    struct EventSpec
    {
        Event::Kind kind;
        std::string name;                // "render.finished"
        std::vector<std::string> fields; // declared order
        std::string summary;
    };

    const std::vector<EventSpec> &eventSpecs();
    const char *eventName(Event::Kind k);
    /** `[evt] <name> k=v …` — one line, no trailing newline. */
    std::string formatEvent(const Event &e);
}
}
