#include "Event.h"
#include <sstream>

namespace arstro
{
namespace cosmo
{
    const char *eventName(Event::Kind k)
    {
        switch (k)
        {
            case Event::Kind::Info: return "info";
            case Event::Kind::Error: return "error";
            case Event::Kind::ScreenChanged: return "screen.changed";
            case Event::Kind::ProjectOpening: return "project.opening";
            case Event::Kind::ProjectOpened: return "project.opened";
            case Event::Kind::ProjectClosed: return "project.closed";
            case Event::Kind::ProjectSaved: return "project.saved";
            case Event::Kind::LoadProgress: return "load.progress";
            case Event::Kind::EntryStarted: return "entry.started";
            case Event::Kind::LoadStage: return "load.stage";
            case Event::Kind::EntryProgress: return "entry.progress";
            case Event::Kind::EntryDecoded: return "entry.decoded";
            case Event::Kind::EntryFailed: return "entry.failed";
            case Event::Kind::LoadFinished: return "load.finished";
            case Event::Kind::SelectionChanged: return "selection.changed";
            case Event::Kind::ParamsChanged: return "params.changed";
            case Event::Kind::HistoryChanged: return "history.changed";
            case Event::Kind::SettingsChanged: return "settings.changed";
            case Event::Kind::FrameReady: return "frame.ready";
            case Event::Kind::ExportProgress: return "export.progress";
            case Event::Kind::ExportFinished: return "export.finished";
            case Event::Kind::CommandRejected: return "command.rejected";
        }
        return "unknown";
    }

    std::string formatEvent(const Event &e)
    {
        // One line per event, fields named, no field printed that the event does not carry —
        // so a grep or an `expect` assertion can match a stable prefix. This IS the log line
        // (R-SVC-5), which is why the shape is fixed once quoted anywhere.
        std::ostringstream o;
        o << "[evt] " << eventName(e.kind);
        switch (e.kind)
        {
            case Event::Kind::ProjectOpening: o << " name=" << e.text << " entries=" << e.b; break;
            case Event::Kind::ProjectOpened: o << " path=" << e.text << " images=" << e.b; break;
            case Event::Kind::LoadProgress:
                o << " done=" << e.a << " total=" << e.b;
                if (!e.text.empty()) o << " name=" << e.text;
                break;
            case Event::Kind::EntryDecoded: o << " index=" << e.a << " slot=" << e.b << " name=" << e.text; break;
            case Event::Kind::EntryStarted: o << " index=" << e.a << " started=" << e.b << " name=" << e.text; break;
            case Event::Kind::LoadStage: o << " stage=" << e.text; break;
            case Event::Kind::EntryProgress:
                o << " index=" << e.a << " pct=" << (int)(e.ms + 0.5);
                if (!e.text.empty()) o << " stage=" << e.text;
                break;
            case Event::Kind::EntryFailed: o << " index=" << e.a << " path=" << e.text; break;
            case Event::Kind::LoadFinished: o << " decoded=" << e.a << " total=" << e.b; break;
            case Event::Kind::SelectionChanged: o << " node=" << e.a << " slot=" << e.b; break;
            case Event::Kind::HistoryChanged:
                o << " current=" << e.a << " nodes=" << e.b;
                if (!e.text.empty()) o << " label=" << e.text;
                break;
            case Event::Kind::FrameReady:
                o << " slot=" << e.a << " width=" << e.b;
                if (e.ms > 0) o << " ms=" << e.ms;
                break;
            case Event::Kind::ExportProgress: o << " done=" << e.a << " total=" << e.b << " name=" << e.text; break;
            case Event::Kind::ExportFinished: o << " written=" << e.a << " failures=" << e.b; break;
            default:
                if (!e.text.empty()) o << ' ' << e.text;
                break;
        }
        return o.str();
    }
}
}
