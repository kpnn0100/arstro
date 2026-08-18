/*
 *  cosmo_v2 by arstro — levelled, categorised file logging + crash backtrace.
 *
 *  Host-layer facility. It does OS I/O (fopen, getenv, signal handlers, backtrace) and it
 *  talks to GLib, so it lives HERE and nowhere below: `cosmo_core`, `arstro_image` and the
 *  platform-free Artboard core contain no logging and no getenv at all. That is the seam
 *  rule this file exists to keep — everything below the host *answers* (returns a value,
 *  emits an `Event`); only the host writes a line. Env vars are therefore fine in this
 *  file and only in this file.
 *
 *  Lines go to a log file in the user config dir (ProjectStore::configDir()) and are teed
 *  to stderr:
 *
 *      2026-08-16 14:03:21.412 [INFO ] cosmo_v2 starting (0 args, log at …)
 *      2026-08-16 14:03:21.412 [DEBUG] [render] preview slot=0 1600x1067 gpu=0 12.4ms
 *
 *  An uncategorised line keeps *exactly* the pre-P0.4 shape — which is why the category
 *  column is omitted rather than left blank — so every existing LOGI call site prints
 *  what it printed before and none of them had to be edited.
 *
 *  Everything is reachable from a shell, because a bug report is filed by whoever can run
 *  one command and cannot rebuild (D-2: "the levels are decorative"; every level, the
 *  category filter, the file and the stderr echo were unreachable at runtime):
 *
 *      --log-level=debug|info|warn|error   --debug     COSMO_LOG_LEVEL
 *      --log-categories=ui,input,render,…              COSMO_LOG_CATEGORIES
 *      --log-file=<path> | -                           COSMO_LOG_FILE
 *      --log-stderr=0|1                                COSMO_LOG_STDERR
 *
 *  installGlibHandler() takes over g_log's *default handler*, not merely g_print and
 *  g_printerr, because g_warning/g_critical/g_message go through g_log and never touch
 *  those two — which is why no GTK, Cairo or GdkPixbuf diagnostic had ever reached the
 *  file (D-3).
 *
 *  installCrashHandler() catches the fatal signals and appends a backtrace before the
 *  process dies: execinfo on POSIX, CaptureStackBackTrace + dbghelp on Windows (D-4), so
 *  a field segfault leaves a diagnosable trail instead of vanishing.
 */
#pragma once
#include <string>

namespace arstro
{
namespace cosmo_v2
{
namespace log
{
    enum class Level { Debug, Info, Warn, Error };

    /** The filterable subsystems, and the whole vocabulary of COSMO_LOG_CATEGORIES.
     *
     *  `None` is not a subsystem: it is the uncategorised spine (startup, config, crash
     *  banners, anything a caller did not classify) and is never filtered out. Neither is
     *  a Warn or an Error, whatever its category — a filter narrows the chatter, and a
     *  warning you filtered away is precisely the line you needed (that is D-3 again, one
     *  layer up: it is not enough to route GTK's warnings into the file if a category
     *  typo can then drop them). */
    enum class Category { None, Ui, Input, Render, Load, Session, Export, Gpu };

    /** Open the log for append and write a session header. The file is the first of:
     *  `--log-file`, `COSMO_LOG_FILE`, `path`, then configDir()/cosmo_v2.log — the
     *  operator's word beats the program's own choice, because the operator is the one
     *  who cannot recompile. A second call is ignored; env vars are read here (or by
     *  configureFromArgs(), whichever runs first) so a run with no flags is still
     *  configurable. */
    void init(const std::string &path = "");

    /** Absolute path of the active log file ("" if there is none — init() never ran, the
     *  open failed, or the operator asked for stderr only with `--log-file=-`). */
    const std::string &path();

    /** Minimum level that is written. Default Info, so LOGD costs a load and a compare
     *  and nothing else. */
    void setLevel(Level l);
    Level level();

    /** Enable exactly these categories. Same syntax as COSMO_LOG_CATEGORIES: a
     *  comma/space separated list of category names, or `all`/`*`, or `none`/`off`. An
     *  unrecognised name is logged as a WARN rather than ignored — a silent typo here
     *  looks identical to "the subsystem logged nothing", which costs a session. */
    void setCategories(const std::string &list);
    void setCategoryEnabled(Category c, bool on);

    /** Would a line at this level and category be written? Call it before building an
     *  expensive message; LOGD/CLOGD already do. */
    bool enabled(Level l, Category c = Category::None);

    /** Also echo every line to stderr (default true). */
    void setStderrEcho(bool on);

    const char *categoryName(Category c);
    bool parseLevel(const std::string &s, Level &out);
    bool parseCategory(const std::string &s, Category &out);

    /** The category of a service Event, derived from the first segment of the dotted name
     *  `eventName()` returns (`load.progress` -> Load, `frame.ready` -> Render).
     *
     *  Derived, never tabulated twice: under R-SVC-5 the event *is* the log line, and the
     *  host logs it with a single `LOGI(formatEvent(e))`. If categories were assigned at
     *  that call site the two vocabularies would drift the moment a new Event::Kind
     *  landed; deriving them from the name means a new event either matches a known
     *  segment or is uncategorised (and therefore unfilterable), but is never quietly
     *  filed under the wrong subsystem. A segment that has no sensible category — `info`,
     *  `error`, `command` — maps to None on purpose: "why did nothing happen?" is
     *  answered by `command.rejected`, so it must not be silenceable. */
    Category categoryForEventName(const std::string &dottedName);

    /** Category-free form: kept for source compatibility, and it recovers the category
     *  from an `[evt] <name>` prefix so the host's one event call site is filterable
     *  without being edited. */
    void write(Level level, const std::string &msg);
    void write(Level level, Category cat, const std::string &msg);

    /** printf-style convenience. */
    void writef(Level level, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    void writef(Level level, Category cat, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 3, 4)))
#endif
        ;

    /** Apply every log flag present in argv. Non-destructive (argv is left alone, so
     *  gtk_init and the host's own path loop still see what they expect) and unknown
     *  args are skipped. Call it *before* init() so `--log-file` and `--log-level` shape
     *  the session header and every startup line too. */
    void configureFromArgs(int argc, char **argv);

    /** How many argv entries starting at `i` this facility owns — 0 when the arg is not
     *  ours. For a host loop that also collects file paths:
     *      if (int n = log::consumeArgs(argc, argv, i)) { i += n - 1; continue; }
     *  Without that, `--log-level=debug` is collected as a filename to open. */
    int consumeArgs(int argc, char **argv, int i);

    /** Route GLib through this log: g_log's default handler (so g_warning, g_critical and
     *  g_message from GTK/Cairo/GdkPixbuf finally land in the file — D-3) plus g_print
     *  and g_printerr. No-op where GLib is not compiled in. */
    void installGlibHandler();

    /** Install fatal-signal handlers that append a backtrace to the log, then re-raise the
     *  default handler so the OS still produces its normal core/exit. */
    void installCrashHandler();
}
}
}

// Terse call sites: LOGI("opened %s", path).
#define COSMO_LOG(level, ...) ::arstro::cosmo_v2::log::writef(level, __VA_ARGS__)
#define LOGD(...) COSMO_LOG(::arstro::cosmo_v2::log::Level::Debug, __VA_ARGS__)
#define LOGI(...) COSMO_LOG(::arstro::cosmo_v2::log::Level::Info, __VA_ARGS__)
#define LOGW(...) COSMO_LOG(::arstro::cosmo_v2::log::Level::Warn, __VA_ARGS__)
#define LOGE(...) COSMO_LOG(::arstro::cosmo_v2::log::Level::Error, __VA_ARGS__)

// Categorised call sites name the enumerator bare, so the common case stays as short as
// the uncategorised one: CLOGD(Input, "click x=%d y=%d consumed=%s", x, y, who).
#define COSMO_LOGC(level, cat, ...) \
    ::arstro::cosmo_v2::log::writef(level, ::arstro::cosmo_v2::log::Category::cat, __VA_ARGS__)
#define CLOGD(cat, ...) COSMO_LOGC(::arstro::cosmo_v2::log::Level::Debug, cat, __VA_ARGS__)
#define CLOGI(cat, ...) COSMO_LOGC(::arstro::cosmo_v2::log::Level::Info, cat, __VA_ARGS__)
#define CLOGW(cat, ...) COSMO_LOGC(::arstro::cosmo_v2::log::Level::Warn, cat, __VA_ARGS__)
#define CLOGE(cat, ...) COSMO_LOGC(::arstro::cosmo_v2::log::Level::Error, cat, __VA_ARGS__)
