/*
 *  Arstro cosmo_core — Event: the ONE way the service says what just changed (R-SVC-3).
 *
 *  An Event is not a log line that happens to be structured — under R-SVC-5 it IS the log
 *  line. `formatEvent()` produces the text the CLI streams with `--watch`, the journal
 *  records, the control socket sends, and the host writes to cosmo_v2.log. That collapses
 *  what used to be three separate pieces of P0 work (log levels, categories, UI logging)
 *  into one: emitting an event is logging it.
 *
 *      [evt] project.opening name=japan18 entries=18
 *      [evt] load.progress done=6 total=18 name=DSCF5411.RAF
 *      [evt] frame.ready slot=3 1600x1067 ms=41.2
 *      [evt] load.finished entries=18 decoded=18 failed=0 peak=5 budget=6
 *
 *  Kept small on purpose: `a`/`b` carry the two integers nearly every event needs
 *  (done/total, slot/index), `ms` the one duration, and `text` the name or the key=value
 *  tail. A wide variant type would be more precise and less usable from four front ends.
 *
 *  Log lines are an interface. Once a DR- entry or an `expect` assertion quotes one, its
 *  shape is fixed — the same discipline the existing log already has.
 */
#pragma once
#include <string>

namespace arstro
{
namespace cosmo
{
    struct Event
    {
        enum class Kind
        {
            Info,
            Error,            // text = why; also lands in AppModel::lastError
            ScreenChanged,    // text = screen name
            ProjectOpening,   // text = project name, b = entry count
            ProjectOpened,    // text = path, b = images
            ProjectClosed,
            ProjectSaved,     // text = path
            LoadProgress,     // a = done, b = total, text = entry name
            EntryDecoded,     // a = index, b = slot, text = name
            EntryFailed,      // a = index, text = path
            LoadFinished,     // a = decoded, b = total
            SelectionChanged, // a = node, b = slot
            ParamsChanged,    // text = the fields that changed
            HistoryChanged,   // a = current, b = nodes, text = label
            SettingsChanged,  // text = key=value tail
            FrameReady,       // a = slot, b = width, ms = render time
            ExportProgress,   // a = done, b = total, text = name
            ExportFinished,   // a = written, b = failures
            CommandRejected   // text = the line, plus why
        };

        Kind kind = Kind::Info;
        std::string text;
        int a = 0;
        int b = 0;
        double ms = 0.0;
    };

    /** Stable dotted name — `load.progress`, `frame.ready`. Used as the log category too. */
    const char *eventName(Event::Kind k);

    /** The canonical one-line form: `[evt] <name> <fields>`. Deterministic, so a test can
     *  assert on it and a script's `expect` can match it. */
    std::string formatEvent(const Event &e);
}
}
