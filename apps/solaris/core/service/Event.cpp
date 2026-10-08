#include "Event.h"
#include "Command.h"
#include "Format.h"

namespace arstro
{
namespace solaris
{
    using EK = Event::Kind;

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
             "A line was refused (R-SVC-3): unknown verb, flag, address or parameter; a refused edit."},
            {EK::ScreenChanged, "screen.changed", {"screen"}, "home | project."},
            {EK::ProjectOpened, "project.opened", {"path", "strips", "clips"}, "A song is open."},
            {EK::ProjectSaved, "project.saved", {"path"}, "The .slp was written."},
            {EK::ProjectClosed, "project.closed", {}, "Back to Home."},
            {EK::ProjectChanged, "project.changed", {"what", "node"},
             "A node was added, removed, moved or re-routed. what = e.g. strip.added, clip.moved, route.set."},
            {EK::ParamsChanged, "params.changed", {"address", "value"}, "An address was written; value is what was STORED (clamped)."},
            {EK::RenderFinished, "render.finished", {"out", "frames", "peak"}, "A render was written; peak is the master's, dBFS."},
            {EK::RenderFailed, "render.failed", {"why"}, "A render stopped with an error."},
            {EK::AuditReport, "audit.report", {"findings"}, "How many findings `audit` printed."},
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
