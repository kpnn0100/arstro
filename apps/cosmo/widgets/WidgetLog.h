/*
 *  cosmo_v2 by arstro — WidgetLog: the widget layer's one line out.
 *
 *  §7 of the design workflow has said for a while that the UI logs nothing, so a user's "this
 *  control does something weird" is unanswerable from a log file. The obvious fix — include
 *  `Log.h` in a widget — does not work: `Log.h` is a HOST facility (OS I/O, GLib, and
 *  ProjectStore for the default path), and `cosmo_widget_tests` is deliberately device-free,
 *  linking neither GTK nor cosmo_core. Including it would drag both into the one test target
 *  that is fast because it has neither.
 *
 *  So the widget layer declares a sink and the host fills it in. Default is a null pointer:
 *  logging from a widget costs one load and a branch when nobody installed anything, which is
 *  the case in every test and in every release run without --debug.
 *
 *  Not a general logging system — deliberately one level (debug) and one category (ui). A
 *  widget that needs to shout has a real problem and should return a value or emit an Event
 *  (the seam rule in Log.h's own header); this is for tracing an interaction while chasing it.
 */
#pragma once

namespace arstro
{
namespace cosmo_v2
{
    /** Install the sink. The host passes a function that forwards to CLOGD(Ui, …). Passing
     *  nullptr turns widget tracing back off. */
    using WidgetLogSink = void (*)(const char *line);
    void setWidgetLogSink(WidgetLogSink sink);
    /** True when a sink is installed — check it before building an expensive message. */
    bool widgetLogEnabled();
    /** printf-style. No-op with no sink. */
    void widgetLog(const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 1, 2)))
#endif
        ;
}
}

/** Terse call site that also skips formatting when nothing is listening. */
#define WLOG(...)                                                     \
    do                                                                \
    {                                                                 \
        if (::arstro::cosmo_v2::widgetLogEnabled())                   \
            ::arstro::cosmo_v2::widgetLog(__VA_ARGS__);                \
    } while (0)
