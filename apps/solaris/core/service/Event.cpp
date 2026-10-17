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
            {EK::ProjectSynced, "project.synced", {"how", "theirs", "mine", "conflicts", "renamed"},
             "The song's file changed under it and the song followed (R-COLLAB-1): how = reloaded (nothing unsaved here) | merged; "
             "theirs and mine = the changes each side brought; conflicts = the nodes asked about; renamed = new ids of mine renumbered."},
            {EK::ProjectClosed, "project.closed", {}, "Back to Home."},
            {EK::ProjectChanged, "project.changed", {"what", "node"},
             "A node was added, removed, moved or re-routed. what = e.g. strip.added, clip.moved, route.set. Every node a command makes is announced, one event each (R-SVC-8)."},
            {EK::ParamsChanged, "params.changed", {"address", "value"}, "An address was written; value is what was STORED, canonical (a value out of range is refused, never clamped — R-SVC-8)."},
            {EK::RenderFinished, "render.finished", {"out", "frames", "peak"}, "A render was written; peak is the master's, dBFS."},
            {EK::RenderFailed, "render.failed", {"why"}, "A render stopped with an error."},
            {EK::AuditReport, "audit.report", {"findings"}, "How many findings `audit` printed."},
            {EK::SettingsChanged, "settings.changed", {"what"}, "A machine setting changed and was saved: sampleRate, bufferSize, output, input, port.<name>, folders, key.<action> (R-SET-4) …"},
            {EK::DevicesChanged, "devices.changed", {"count"}, "`devices list` found this many devices."},
            {EK::BrowseChanged, "browse.changed", {"path", "entries"}, "The browser now lists this folder — or, a search (path `search:<text>` / `favourites`), what it found."},
            {EK::LibraryChanged, "library.changed", {"path", "tags"}, "R-PRESET-3: a file's tags (favourite among them) changed and were saved; tags comma-separated, '' none."},
            {EK::PluginsChanged, "plugins.changed", {"count", "problems"}, "R-VST-6: a scan found this many plugins; problems: bundles that could not be read."},
            {EK::PluginOffline, "plugin.offline", {"device", "why"}, "R-VST-6: a song's plugin is not playing — it never started, crashed or overran its budget; an effect passes dry, an instrument is silent (`device reload <dv>`). Said once."},
            {EK::SurfaceChanged, "surface.changed", {"protocol", "connected", "bank", "why"}, "R-CTRL-2: the control surface connected (1) or not (0, why), or moved to another bank of eight."},
            {EK::MapLearned, "map.learned", {"cc", "address"}, "R-CTRL-1: the MIDI control moved during a `map learn` is bound to the address (on its channel: `<ch>/<n>`)."},
            {EK::PluginEditor, "plugin.editor", {"device", "open"}, "R-VST-6: a plugin's own editor window opened (1) or closed (0) — by `device editor`, or its window's own close."},
            {EK::RecentsChanged, "recents.changed", {"count"}, "Home's recent songs changed."},
            {EK::TransportChanged, "transport.changed", {"playing", "position", "loop"},
             "Play, stop, seek or loop: playing 1/0, position in beats, loop `from-to` or `off`."},
            {EK::AuditionChanged, "audition.changed", {"file", "playing"},
             "A sample previewed from the browser started (playing 1) or ended — stopped, or to its end (playing 0)."},
            {EK::NotePreviewed, "note.previewed", {"strip", "pitch"},
             "A note was heard through a strip, outside the song (`note preview`, the piano roll's sound — R-ROLL-7)."},
            {EK::ProjectAutosaved, "project.autosaved", {"path"}, "The unsaved song was written beside it (`<song>.autosave`, R-SAFE-1)."},
            {EK::ProjectRecovery, "project.recovery", {"path", "age"},
             "The song just opened has an autosave newer than it (age in seconds) — `project recover` restores it, `project discard-recovery` drops it (R-SAFE-2)."},
            {EK::RecordFinished, "record.finished", {"clip", "notes", "seconds", "takes"},
             "A recording landed: its notes in this clip ('' — nothing was played) (R-MIDI-2); an audio take, how long it is (R-REC-1); "
             "how many takes a looped recording made (R-REC-2)."},
            {EK::StripArmed, "strip.armed", {"strip"}, "The MIDI keyboard plays this strip now ('' — none); the session's, not the song's (R-MIDI-2)."},
            {EK::MetersCleared, "meters.cleared", {}, "The clip lights and held peaks are out; the master's true peak and integrated loudness start again (R-METER-2)."},
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
