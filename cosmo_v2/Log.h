/*
 *  cosmo_v2 by arstro — file logging + crash backtrace.
 *
 *  App-layer facility (does OS I/O — fopen, signal handlers, backtrace — so it
 *  lives here, NOT in the platform-free Artboard core). Writes timestamped lines
 *  to a log file in the user config dir (see ProjectStore::configDir()) and tees
 *  them to stderr. installCrashHandler() catches fatal signals (SIGSEGV/SIGABRT/
 *  SIGBUS/SIGFPE/SIGILL) and dumps a backtrace to the log before the process dies,
 *  so a field segfault leaves a diagnosable trail instead of vanishing.
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

    /** Open `path` for append and write a session header. If `path` is empty the
     *  default is ProjectStore::configDir()/cosmo_v2.log. Safe to call once at
     *  startup; a second call is ignored. */
    void init(const std::string &path = "");

    /** Absolute path of the active log file ("" if init() was never called). */
    const std::string &path();

    /** Also echo every line to stderr (default true). */
    void setStderrEcho(bool on);

    void write(Level level, const std::string &msg);
    /** printf-style convenience. */
    void writef(Level level, const char *fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    /** Install fatal-signal handlers that append a backtrace to the log, then
     *  re-raise the default handler so the OS still produces its normal core/exit. */
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
