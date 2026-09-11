/*
 *  interstellar_core — Event: the ONE way the service says what just changed (R-SVC-3).
 *
 *  An Event is not a log line that happens to be structured: under R-SVC-5 it IS the log line.
 *  `formatEvent()` produces the text the CLI streams with `--watch`, the journal records, the
 *  socket sends and the file log writes. There is no separate logging layer, so a thing that
 *  happened cannot be visible in one channel and invisible in another.
 *
 *      [evt] project.opened path=/tmp/cut.isp tracks=2 clips=3
 *      [evt] rack.event cosmo: params.changed exposure
 *      [evt] param.changed gr1.basic.exposure=0.200
 *      [evt] lint.finding al_ex at=4.000 gr1.basic.exposure steps by 0.5 on entry with fadeIn=0
 *      [evt] command.rejected set gr1.basic.exposer=1 — no such parameter ...
 *
 *  Line shapes are an interface. Once this header or an `expect` assertion quotes one, fields
 *  are appended at the end and never reordered or renamed.
 */
#pragma once
#include <string>

namespace arstro
{
namespace interstellar
{
    struct Event
    {
        enum class Kind
        {
            Info,
            Error,
            ProjectOpening, ProjectOpened, ProjectSaved, ProjectClosed,
            RackEvent,          // a re-published cosmo::Event; text = its formatted form
            RackPinned, RackChanged,
            TimelineChanged,    // text = what changed
            ParamChanged,       // text = address, ms = the resolved value
            AutomationChanged, BindingChanged,
            BindingBroken,      // ONCE per render, not per frame (R-BIND-8)
            LintFinding,
            PlayheadChanged,
            FrameReady,
            RenderProgress, RenderFinished,
            CommandRejected     // ALSO lands in AppModel::lastError
        };

        Kind kind = Kind::Info;
        std::string text;
        int a = 0, b = 0, c = 0;
        double ms = 0.0;
    };

    /** Stable dotted name — `param.changed`, `lint.finding`. The log category too. */
    const char *eventName(Event::Kind k);
    std::string formatEvent(const Event &e);
}
}
