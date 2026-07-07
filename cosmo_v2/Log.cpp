#include "Log.h"
#include "../cosmo_core/ProjectStore.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

#include <execinfo.h>
#include <signal.h>
#include <unistd.h>

namespace arstro
{
namespace cosmo_v2
{
namespace log
{
    namespace
    {
        std::mutex gMutex;
        FILE *gFile = nullptr;
        int gFd = -1;          // raw fd of gFile, for the async-signal-safe crash path
        bool gStderr = true;
        std::string gPath;

        const char *levelTag(Level l)
        {
            switch (l)
            {
            case Level::Debug: return "DEBUG";
            case Level::Info:  return "INFO ";
            case Level::Warn:  return "WARN ";
            case Level::Error: return "ERROR";
            }
            return "?????";
        }

        // "2026-07-07 14:03:21.412" into buf (must hold >= 24 bytes).
        void timestamp(char *buf, size_t n)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            struct tm tmv;
            localtime_r(&ts.tv_sec, &tmv);
            char base[20];
            strftime(base, sizeof base, "%Y-%m-%d %H:%M:%S", &tmv);
            snprintf(buf, n, "%s.%03ld", base, ts.tv_nsec / 1000000);
        }

        // Signal-handler-safe: write() only, no malloc/printf.
        void safeWrite(int fd, const char *s)
        {
            if (fd >= 0) { ssize_t r = ::write(fd, s, std::strlen(s)); (void)r; }
        }

        volatile sig_atomic_t gInCrash = 0;

        void crashHandler(int sig)
        {
            if (gInCrash) _exit(128 + sig);  // re-entrancy guard
            gInCrash = 1;

            const char *name = strsignal(sig);
            const char *hdr = "\n==== cosmo_v2 FATAL SIGNAL ";
            safeWrite(gFd, hdr); safeWrite(2, hdr);
            safeWrite(gFd, name ? name : "signal"); safeWrite(2, name ? name : "signal");
            safeWrite(gFd, " — backtrace ====\n"); safeWrite(2, " — backtrace ====\n");

            void *frames[64];
            const int n = backtrace(frames, 64);
            if (gFd >= 0) backtrace_symbols_fd(frames, n, gFd);
            backtrace_symbols_fd(frames, n, 2);
            safeWrite(gFd, "==== end backtrace ====\n");

            // Re-raise with the default handler so the OS still cores/exits normally.
            signal(sig, SIG_DFL);
            raise(sig);
        }
    }

    void init(const std::string &path)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        if (gFile) return;  // already initialised

        gPath = path.empty() ? (cosmo::ProjectStore::configDir() + "/cosmo_v2.log") : path;
        gFile = std::fopen(gPath.c_str(), "a");
        if (!gFile)
        {
            gPath.clear();
            return;
        }
        gFd = fileno(gFile);

        char ts[40];
        timestamp(ts, sizeof ts);
        std::fprintf(gFile, "\n===== cosmo_v2 session start %s (pid %ld) =====\n", ts, (long)getpid());
        std::fflush(gFile);
    }

    const std::string &path() { return gPath; }

    void setStderrEcho(bool on)
    {
        std::lock_guard<std::mutex> lock(gMutex);
        gStderr = on;
    }

    void write(Level level, const std::string &msg)
    {
        char ts[40];
        timestamp(ts, sizeof ts);
        std::lock_guard<std::mutex> lock(gMutex);
        if (gFile)
        {
            std::fprintf(gFile, "%s [%s] %s\n", ts, levelTag(level), msg.c_str());
            std::fflush(gFile);  // flush every line so a crash can't lose the tail
        }
        if (gStderr)
            std::fprintf(stderr, "%s [%s] %s\n", ts, levelTag(level), msg.c_str());
    }

    void writef(Level level, const char *fmt, ...)
    {
        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        write(level, buf);
    }

    void installCrashHandler()
    {
        for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
            signal(sig, crashHandler);
    }
}
}
}
