#include "WidgetLog.h"
#include <cstdarg>
#include <cstdio>

namespace arstro
{
namespace cosmo_v2
{
    namespace
    {
        WidgetLogSink gSink = nullptr;
    }

    void setWidgetLogSink(WidgetLogSink sink) { gSink = sink; }
    bool widgetLogEnabled() { return gSink != nullptr; }

    void widgetLog(const char *fmt, ...)
    {
        if (!gSink) return;
        char buf[512];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        gSink(buf);
    }
}
}
