/*
 *  solaris_host — the one symbol cosmo's ControlChannel needs from cosmo's host, given to it here.
 *
 *  ControlChannel.cpp (compiled in place from apps/cosmo, see ControlServer.h) logs through cosmo's
 *  LOGI/LOGW macros, which expand to `cosmo_v2::log::writef`. Linking cosmo's Log.cpp would bring its
 *  ProjectStore and GLib with it; Solaris has no log file yet, so the lines go to stderr — the same
 *  place solaris's own startup messages go. Only `writef(Level, fmt, …)` is used by the channel; a
 *  link error names anything else the day the channel starts to want it.
 */
#include "../../cosmo/Log.h"
#include <cstdarg>
#include <cstdio>

namespace arstro
{
namespace cosmo_v2
{
namespace log
{
    void writef(Level level, const char *fmt, ...)
    {
        if (level == Level::Debug) return;
        std::fputs(level == Level::Info ? "solaris: " : "solaris: warning: ", stderr);
        va_list ap;
        va_start(ap, fmt);
        std::vfprintf(stderr, fmt, ap);
        va_end(ap);
        std::fputc('\n', stderr);
    }
}
}
}
