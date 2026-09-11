#include "Event.h"
#include <cstdio>
#include <sstream>

namespace arstro
{
namespace interstellar
{
    const char *eventName(Event::Kind k)
    {
        switch (k)
        {
            case Event::Kind::Info: return "info";
            case Event::Kind::Error: return "error";
            case Event::Kind::ProjectOpening: return "project.opening";
            case Event::Kind::ProjectOpened: return "project.opened";
            case Event::Kind::ProjectSaved: return "project.saved";
            case Event::Kind::ProjectClosed: return "project.closed";
            case Event::Kind::RackEvent: return "rack.event";
            case Event::Kind::RackPinned: return "rack.pinned";
            case Event::Kind::RackChanged: return "rack.changed";
            case Event::Kind::TimelineChanged: return "timeline.changed";
            case Event::Kind::ParamChanged: return "param.changed";
            case Event::Kind::AutomationChanged: return "automation.changed";
            case Event::Kind::BindingChanged: return "binding.changed";
            case Event::Kind::BindingBroken: return "binding.broken";
            case Event::Kind::LintFinding: return "lint.finding";
            case Event::Kind::PlayheadChanged: return "playhead.changed";
            case Event::Kind::FrameReady: return "frame.ready";
            case Event::Kind::RenderProgress: return "render.progress";
            case Event::Kind::RenderFinished: return "render.finished";
            case Event::Kind::CommandRejected: return "command.rejected";
        }
        return "unknown";
    }

    std::string formatEvent(const Event &e)
    {
        std::ostringstream o;
        o << "[evt] " << eventName(e.kind);
        char num[32];
        auto t3 = [&](double v) { std::snprintf(num, sizeof num, "%.3f", v); return std::string(num); };
        switch (e.kind)
        {
            case Event::Kind::ProjectOpening: o << " path=" << e.text; break;
            case Event::Kind::ProjectOpened:
                o << " path=" << e.text << " tracks=" << e.a << " clips=" << e.b;
                break;
            case Event::Kind::ProjectSaved: o << " path=" << e.text; break;
            case Event::Kind::ParamChanged: o << ' ' << e.text << '=' << t3(e.ms); break;
            case Event::Kind::PlayheadChanged: o << " t=" << t3(e.ms) << " frame=" << e.a; break;
            case Event::Kind::FrameReady:
                o << " layers=" << e.a << ' ' << e.b << 'x' << e.c << " ms=" << t3(e.ms);
                break;
            case Event::Kind::LintFinding: o << ' ' << e.text << " at=" << t3(e.ms); break;
            case Event::Kind::RenderProgress: o << " done=" << e.a << " total=" << e.b; break;
            case Event::Kind::RenderFinished: o << " frames=" << e.a << " failures=" << e.b; break;
            case Event::Kind::TimelineChanged:
            case Event::Kind::AutomationChanged:
            case Event::Kind::BindingChanged:
            case Event::Kind::BindingBroken:
            case Event::Kind::RackEvent:
            case Event::Kind::RackChanged:
            case Event::Kind::RackPinned:
            case Event::Kind::CommandRejected:
            case Event::Kind::Info:
            case Event::Kind::Error:
                if (!e.text.empty()) o << ' ' << e.text;
                break;
            case Event::Kind::ProjectClosed: break;
        }
        return o.str();
    }
}
}
