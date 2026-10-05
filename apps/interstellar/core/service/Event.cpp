#include "Event.h"
#include "Command.h"

namespace arstro
{
namespace interstellar
{
    using EK = Event::Kind;

    // canonicalNumber is the project model's (Project.cpp): ONE spelling of a number for the
    // .isp, the event stream, the model dump and the API document.

    Event &Event::with(const std::string &key, const std::string &value)
    {
        fields.emplace_back(key, value);
        return *this;
    }
    Event &Event::with(const std::string &key, double value) { return with(key, canonicalNumber(value)); }
    Event &Event::with(const std::string &key, long long value) { return with(key, std::to_string(value)); }

    std::string Event::field(const std::string &key, const std::string &fallback) const
    {
        for (const auto &kv : fields)
            if (kv.first == key) return kv.second;
        return fallback;
    }

    const std::vector<EventSpec> &eventSpecs()
    {
        static const std::vector<EventSpec> specs = {
            {EK::Info, "info", {"text"}, "A note worth logging; carries no state."},
            {EK::Error, "error", {"why"}, "Something failed. Also set as AppModel.lastError."},
            {EK::CommandRejected, "command.rejected", {"line", "why"},
             "A line was refused (R-SVC-3): unknown verb, unknown flag, refused edit."},
            {EK::ScreenChanged, "screen.changed", {"screen"}, "Home / Loading / Edit."},
            {EK::ProjectOpened, "project.opened", {"path", "timelines", "rack"}, "A project is open."},
            {EK::ProjectSaved, "project.saved", {"path", "cmp"}, "The .isp and the rack's .cmp were written."},
            {EK::ProjectClosed, "project.closed", {}, "Back to Home."},
            {EK::RackLoaded, "rack.loaded", {"images", "failed"}, "The hosted Cosmo project finished decoding."},
            {EK::RackChanged, "rack.changed", {"what", "node"}, "A node was added, grouped, renamed, reframed."},
            {EK::ParamsChanged, "params.changed", {"address", "value", "target"},
             "An address was written. target = rack | version | clip | track | project."},
            {EK::SelectionChanged, "selection.changed", {"rack", "clip"}, "The Grade target or the selected clip changed."},
            {EK::TimelineOpened, "timeline.opened", {"timeline", "name"}, "The editor's current timeline changed."},
            {EK::TimelineChanged, "timeline.changed", {"timeline", "what"},
             "A version was created, pinned, unpinned, frozen, thawed or deleted."},
            {EK::TimelineRebased, "timeline.rebased", {"timeline", "pruned", "dangling", "pin"},
             "rebase ran: deltas pruned, the dangling ones named, the pin advanced (old→new)."},
            {EK::ArrangeChanged, "arrange.changed", {"timeline", "what", "node"}, "A cut operation landed."},
            {EK::PlayheadMoved, "playhead.moved", {"t", "frame"}, "The playhead moved by command."},
            {EK::PlaybackChanged, "playback.changed", {"playing"}, "Playback started or stopped."},
            {EK::FrameReady, "frame.ready", {"seq", "width", "height", "ms"}, "A monitor frame was composited."},
            {EK::RenderQueued, "render.queued", {"job", "timeline", "out"}, "A render was queued."},
            {EK::RenderProgress, "render.progress", {"job", "timeline", "done", "total"}, "Frames written so far."},
            {EK::RenderFinished, "render.finished", {"job", "timeline", "frames", "out"}, "A render completed."},
            {EK::RenderFailed, "render.failed", {"job", "timeline", "why"}, "A render stopped with an error or was cancelled."},
            {EK::LintReport, "lint.report", {"offline", "dangling", "refused"}, "Counts from `lint`; details follow as info."},
            {EK::HistoryChanged, "history.changed", {"did", "label", "canUndo", "canRedo"},
             "An edit was recorded, undone or redone (did = edit | undo | redo | cleared)."},
            {EK::SettingsChanged, "settings.changed", {"cpuPercent", "threads", "previewEdge", "useGpu", "uiScale", "hardwareVideo", "previewCache"},
             "Engine settings after a change, all keys."},
            {EK::PresetsChanged, "presets.changed", {"count"}, "The preset library was rescanned."},
            {EK::CacheChanged, "cache.changed", {"timeline", "frames", "total", "building"},
             "The preview cache of the current timeline: frames cached and current, of total (R-PLAY-1)."},
        };
        return specs;
    }

    const char *eventName(Event::Kind k)
    {
        for (const auto &s : eventSpecs())
            if (s.kind == k) return s.name.c_str();
        return "unknown";
    }

    std::string formatEvent(const Event &e)
    {
        std::string line = std::string("[evt] ") + eventName(e.kind);
        for (const auto &kv : e.fields) line += " " + kv.first + "=" + quoteToken(kv.second);
        return line;
    }
}
}
