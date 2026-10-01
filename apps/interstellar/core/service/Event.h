/*
 *  interstellar_core — Event: the ONE way the service says what just changed (R-SVC-2).
 *
 *  `formatEvent()` output IS the log line — what `interstellar-cc --watch` streams, what the GUI
 *  host writes to its log, what an agent greps. Emitting an event is logging it.
 *
 *      [evt] rack.loaded images=6 failed=1
 *      [evt] params.changed address=s_day01.basic.exposure value=0.35 target=rack
 *      [evt] timeline.rebased timeline=tl_2 pruned=1 dangling=clp_3 pin=8f2c1ab→c0ffee1
 *      [evt] render.progress job=r1 timeline=social30 done=48 total=720
 *
 *  Fields are named key=value pairs rather than cosmo's anonymous `a`/`b` integers: an agent reads
 *  this stream through `docs/api.json`, and a field called `a` documents nothing. Each kind's fields
 *  are declared in `eventSpecs()`, which the API document is generated from (R-API-1).
 *
 *  Log lines are an interface. Once a DR- entry or a test quotes one, its shape is fixed.
 */
#pragma once
#include <string>
#include <utility>
#include <vector>

namespace arstro
{
namespace interstellar
{
    struct Event
    {
        enum class Kind
        {
            Info,
            Error,              // why — also lands in AppModel::lastError
            CommandRejected,    // line, why
            ScreenChanged,      // screen
            ProjectOpened,      // path, timelines, rack
            ProjectSaved,       // path, cmp
            ProjectClosed,
            RackLoaded,         // images, failed
            RackChanged,        // what, node
            ParamsChanged,      // address, value, target (rack | version | clip | track | project)
            SelectionChanged,   // rack, clip
            TimelineOpened,     // timeline, name
            TimelineChanged,    // timeline, what
            TimelineRebased,    // timeline, pruned, dangling, pin
            ArrangeChanged,     // timeline, what, node
            PlayheadMoved,      // t, frame
            PlaybackChanged,    // playing
            FrameReady,         // seq, width, height, ms
            RenderQueued,       // job, timeline, out
            RenderProgress,     // job, timeline, done, total
            RenderFinished,     // job, timeline, frames, out
            RenderFailed,       // job, timeline, why
            LintReport          // offline, dangling, refused
        };

        Kind kind = Kind::Info;
        std::vector<std::pair<std::string, std::string>> fields;

        Event() = default;
        explicit Event(Kind k) : kind(k) {}
        Event &with(const std::string &key, const std::string &value);
        Event &with(const std::string &key, double value);
        Event &with(const std::string &key, long long value);
        Event &with(const std::string &key, int value) { return with(key, (long long)value); }
        Event &with(const std::string &key, bool value) { return with(key, std::string(value ? "1" : "0")); }
        Event &with(const std::string &key, const char *value) { return with(key, std::string(value)); }
        std::string field(const std::string &key, const std::string &fallback = std::string()) const;
    };

    struct EventSpec
    {
        Event::Kind kind;
        std::string name;                 // "render.progress"
        std::vector<std::string> fields;  // declared order
        std::string summary;
    };

    const std::vector<EventSpec> &eventSpecs();
    const char *eventName(Event::Kind k);

    /** `[evt] <name> k=v …` — one line, no trailing newline. Values quoted when they need it. */
    std::string formatEvent(const Event &e);

    /** Shortest decimal that reads back to the same double, never `-0`. Defined ONCE, by the
     *  project model (Project.cpp), so the .isp, the log and the API spell a number identically. */
    std::string canonicalNumber(double v);
}
}
