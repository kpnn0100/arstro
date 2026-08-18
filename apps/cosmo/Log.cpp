#include "Log.h"
#include "core/ProjectStore.h"

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <utility>
#include <vector>

#include <signal.h>
#ifdef _WIN32
#include <io.h>       // _write
#include <process.h>  // _getpid
#include <windows.h>  // CaptureStackBackTrace, LoadLibrary, SetUnhandledExceptionFilter
#else
#include <execinfo.h>
#include <unistd.h>
#endif

// ── Optional dependencies, resolved at compile time ──────────────────────────────────
// This file is compiled into every target built from the apps/cosmo/*.cpp glob — the GTK
// app, and now COSMO_APP_NOMAIN's headless harnesses too — so it must not decide for them
// that the whole app needs a toolkit. __has_include is a sound proxy for "may I link
// this": GTK3's include dirs and its libraries both arrive from the one imported
// PkgConfig::GTK3 target, so a target has both or neither, never the headers alone.
#if !defined(COSMO_LOG_NO_GLIB) && defined(__has_include)
#  if __has_include(<glib.h>)
#    include <glib.h>
#    define COSMO_LOG_HAVE_GLIB 1
#  endif
#endif

// dbghelp: header-optional AND link-optional. CMakeLists.txt does not link dbghelp and
// this file must not require it to, so every entry point is resolved with GetProcAddress
// — the same trick OmpPin.cpp uses for libgomp, and for the same reason: an optional
// diagnostic must never become a build dependency.
#if defined(_WIN32) && !defined(COSMO_LOG_NO_DBGHELP) && defined(__has_include)
#  if __has_include(<dbghelp.h>)
#    include <dbghelp.h>
#    define COSMO_LOG_HAVE_DBGHELP 1
#  endif
#endif

namespace arstro
{
namespace cosmo_v2
{
namespace log
{
    namespace
    {
        std::mutex gMutex;             // guards the file handles and gPath only
        FILE *gFile = nullptr;
        int gFd = -1;                  // raw fd of gFile, for the async-signal-safe crash path
        std::string gPath;
        bool gInit = false;            // "init() has run", NOT "a file was opened" — the two
                                       // differ when the open fails or `--log-file=-` asked
                                       // for stderr only, and a retry on every line is worse
                                       // than none

        // Atomics, not mutex-guarded state: enabled() is on the LOGD fast path and must not
        // contend with a thread that is mid-write. A stale read costs one misfiled line at
        // the instant the level changes, which is a price worth paying for a filter that is
        // free to consult.
        std::atomic<int> gLevel{static_cast<int>(Level::Info)};
        std::atomic<unsigned> gCats{~0u};    // bit per Category; everything on by default
        std::atomic<bool> gStderr{true};

        // Whether an operator (flag or env) has spoken. The environment is applied once, as
        // early as anything in this file is touched, and never overwrites a value someone
        // asked for explicitly — so `COSMO_LOG_LEVEL=warn cosmo --debug` is debug.
        bool gEnvApplied = false;
        bool gLevelSet = false;
        bool gCatsSet = false;
        bool gStderrSet = false;
        std::string gPathOverride;
        bool gPathOverrideSet = false;
        bool gNoFile = false;                // `--log-file=-`: stderr only, by request

        // Diagnostics about the logger's own configuration, queued until there is somewhere
        // to put them: a typo'd category is indistinguishable from "that subsystem was
        // silent", so it has to be said out loud, and it has to survive being said before
        // the file exists.
        std::vector<std::pair<Level, std::string>> gPending;

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

        unsigned catBit(Category c) { return 1u << static_cast<unsigned>(c); }

        std::string lower(const std::string &s)
        {
            std::string t = s;
            for (char &c : t) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            return t;
        }

        std::string trim(const std::string &s)
        {
            size_t a = 0, b = s.size();
            while (a < b && (s[a] == ' ' || s[a] == '\t')) ++a;
            while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t')) --b;
            return s.substr(a, b - a);
        }

        std::string chomp(const char *s)
        {
            std::string t = s ? s : "";
            while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
            return t;
        }

        const char *env(const char *name)
        {
            const char *v = std::getenv(name);
            return (v && *v) ? v : nullptr;
        }

        // "2026-07-07 14:03:21.412" into buf (must hold >= 24 bytes).
        void timestamp(char *buf, size_t n)
        {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            struct tm tmv;
#ifdef _WIN32
            localtime_s(&tmv, &ts.tv_sec);  // MSVC/MinGW-ucrt param order: (dest, source)
#else
            localtime_r(&ts.tv_sec, &tmv);
#endif
            char base[20];
            strftime(base, sizeof base, "%Y-%m-%d %H:%M:%S", &tmv);
            snprintf(buf, n, "%s.%03ld", base, ts.tv_nsec / 1000000);
        }

        // Signal-handler-safe: write() only, no malloc/printf.
        void safeWrite(int fd, const char *s)
        {
#ifdef _WIN32
            if (fd >= 0) { int r = _write(fd, s, (unsigned)std::strlen(s)); (void)r; }
#else
            if (fd >= 0) { ssize_t r = ::write(fd, s, std::strlen(s)); (void)r; }
#endif
        }

        // Crash-path output goes to both sinks unconditionally: the level and the category
        // filter are an operator's choice about noise, and a fatal signal is not noise.
        void crashWrite(const char *s) { safeWrite(gFd, s); safeWrite(2, s); }

        volatile sig_atomic_t gInCrash = 0;

        // The queued config diagnostics, and the one line that says how this session was
        // filtered — without which a log with a category filter on looks like a log with
        // nothing happening.
        void note(Level l, const std::string &msg)
        {
            bool queued = false;
            {
                std::lock_guard<std::mutex> lock(gMutex);
                if (!gInit) { gPending.emplace_back(l, msg); queued = true; }
            }
            if (!queued) write(l, msg);
        }

        void applyLevel(Level l) { gLevel.store(static_cast<int>(l)); gLevelSet = true; }

        // "level=info categories=all stderr=on" — how this session was filtered. It rides on
        // the session header rather than being a line of its own, because a header is
        // structural and a line is subject to the very level it is describing: at
        // `--log-level=warn` an INFO line saying "level=warn" would be the first thing
        // filtered out, leaving a nearly empty log with no explanation of why.
        std::string effectiveConfig()
        {
            std::string s = "level=";
            s += lower(trim(levelTag(static_cast<Level>(gLevel.load()))));
            s += " categories=";
            const unsigned m = gCats.load();
            if (m == ~0u) s += "all";
            else
            {
                std::string names;
                for (Category c : {Category::Ui, Category::Input, Category::Render, Category::Load,
                                   Category::Session, Category::Export, Category::Gpu})
                    if (m & catBit(c)) { if (!names.empty()) names += ','; names += categoryName(c); }
                s += names.empty() ? "none" : names;
            }
            s += gStderr.load() ? " stderr=on" : " stderr=off";
            return s;
        }

        // ── Environment ──────────────────────────────────────────────────────────────
        // Read once, at whichever entry point is touched first, and never over an explicit
        // choice. Env belongs in this file and only in this file: cosmo_core and
        // arstro_image do not call getenv, so a flag is the only way state reaches them.
        void applyEnv()
        {
            if (gEnvApplied) return;
            gEnvApplied = true;

            if (const char *v = env("COSMO_LOG_LEVEL"))
            {
                Level l;
                if (parseLevel(v, l)) { if (!gLevelSet) applyLevel(l); }
                else note(Level::Warn, std::string("log: COSMO_LOG_LEVEL='") + v +
                                       "' is not debug|info|warn|error — keeping info");
            }
            if (const char *v = env("COSMO_LOG_CATEGORIES"))
            {
                if (!gCatsSet) setCategories(v);
            }
            if (const char *v = env("COSMO_LOG_FILE"))
            {
                if (!gPathOverrideSet)
                {
                    gPathOverrideSet = true;
                    if (std::strcmp(v, "-") == 0) gNoFile = true; else gPathOverride = v;
                }
            }
            if (const char *v = env("COSMO_LOG_STDERR"))
            {
                const std::string s = lower(v);
                const bool off = (s == "0" || s == "off" || s == "no" || s == "false");
                if (!gStderrSet) { gStderr.store(!off); gStderrSet = true; }
            }
        }

        // ── Windows backtrace (D-4) ──────────────────────────────────────────────────
#if defined(_WIN32)
        // No snprintf on the crash path: the POSIX branch below holds the same discipline
        // (write() only), and a heap already corrupt enough to fault is not one to format
        // through.
        const char *hex64(unsigned long long v, char *buf /* >= 19 */)
        {
            static const char d[] = "0123456789abcdef";
            buf[0] = '0'; buf[1] = 'x';
            for (int i = 0; i < 16; ++i) buf[2 + i] = d[(v >> (60 - 4 * i)) & 0xFu];
            buf[18] = '\0';
            return buf;
        }
        const char *dec64(unsigned long long v, char *buf /* >= 21 */)
        {
            char tmp[21];
            int n = 0;
            do { tmp[n++] = char('0' + (v % 10)); v /= 10; } while (v);
            for (int i = 0; i < n; ++i) buf[i] = tmp[n - 1 - i];
            buf[n] = '\0';
            return buf;
        }

#if defined(COSMO_LOG_HAVE_DBGHELP)
        using SymInitializeFn        = BOOL (WINAPI *)(HANDLE, PCSTR, BOOL);
        using SymSetOptionsFn        = DWORD (WINAPI *)(DWORD);
        using SymFromAddrFn          = BOOL (WINAPI *)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
        using SymGetLineFromAddr64Fn = BOOL (WINAPI *)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);

        SymFromAddrFn gSymFromAddr = nullptr;
        SymGetLineFromAddr64Fn gSymGetLine = nullptr;
        bool gSymReady = false;

        // Called from installCrashHandler(), deliberately NOT from the handler:
        // LoadLibrary and SymInitialize take the loader lock and the heap, which is exactly
        // what an access violation has just walked over. Pay the cost at startup, where it
        // is allowed to fail, so the crash path only reads already-resolved pointers.
        // SYMOPT_DEFERRED_LOADS keeps that cost to a scan — the PDBs themselves are only
        // read if a crash actually asks for a name.
        void initSymbols()
        {
            HMODULE dll = LoadLibraryA("dbghelp.dll");
            if (!dll) return;   // no dbghelp on this box: addresses only, still useful
            auto init = reinterpret_cast<SymInitializeFn>(
                reinterpret_cast<void *>(GetProcAddress(dll, "SymInitialize")));
            auto opts = reinterpret_cast<SymSetOptionsFn>(
                reinterpret_cast<void *>(GetProcAddress(dll, "SymSetOptions")));
            gSymFromAddr = reinterpret_cast<SymFromAddrFn>(
                reinterpret_cast<void *>(GetProcAddress(dll, "SymFromAddr")));
            gSymGetLine = reinterpret_cast<SymGetLineFromAddr64Fn>(
                reinterpret_cast<void *>(GetProcAddress(dll, "SymGetLineFromAddr64")));
            if (!init) return;
            if (opts) opts(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
            gSymReady = init(GetCurrentProcess(), nullptr, TRUE) != FALSE;
        }
#else
        void initSymbols() {}
#endif

        // The Windows half of D-4: frames, symbolized where dbghelp can, raw addresses
        // where it cannot. Written from Linux and NEVER EXECUTED — see the note in
        // installCrashHandler().
        void windowsBacktrace()
        {
            void *frames[62];   // CaptureStackBackTrace's own cap on pre-Vista kernels;
                                // harmless everywhere later and deeper than any cosmo stack
            const USHORT n = CaptureStackBackTrace(0, 62, frames, nullptr);
            char num[21], addr[19];

#if defined(COSMO_LOG_HAVE_DBGHELP)
            // One stack buffer holding SYMBOL_INFO and its trailing name — SymFromAddr
            // writes the name past the struct, so the two must be contiguous and MaxNameLen
            // must describe the room actually there.
            const HANDLE proc = GetCurrentProcess();
            unsigned char symBuf[sizeof(SYMBOL_INFO) + 512];
#endif
            for (USHORT i = 0; i < n; ++i)
            {
                crashWrite("  #");
                crashWrite(dec64(i, num));
                crashWrite(" ");
                crashWrite(hex64(static_cast<unsigned long long>(
                                     reinterpret_cast<uintptr_t>(frames[i])), addr));
#if defined(COSMO_LOG_HAVE_DBGHELP)
                if (gSymReady && gSymFromAddr)
                {
                    std::memset(symBuf, 0, sizeof symBuf);
                    PSYMBOL_INFO sym = reinterpret_cast<PSYMBOL_INFO>(symBuf);
                    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
                    sym->MaxNameLen = 511;
                    DWORD64 disp = 0;
                    if (gSymFromAddr(proc, static_cast<DWORD64>(
                                         reinterpret_cast<uintptr_t>(frames[i])), &disp, sym))
                    {
                        crashWrite(" ");
                        crashWrite(sym->Name);
                        crashWrite("+");
                        crashWrite(hex64(static_cast<unsigned long long>(disp), addr));
                    }
                    if (gSymGetLine)
                    {
                        IMAGEHLP_LINE64 line;
                        std::memset(&line, 0, sizeof line);
                        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
                        DWORD lineDisp = 0;
                        if (gSymGetLine(proc, static_cast<DWORD64>(
                                            reinterpret_cast<uintptr_t>(frames[i])),
                                        &lineDisp, &line) && line.FileName)
                        {
                            crashWrite(" (");
                            crashWrite(line.FileName);
                            crashWrite(":");
                            crashWrite(dec64(line.LineNumber, num));
                            crashWrite(")");
                        }
                    }
                }
#endif
                crashWrite("\n");
            }
            if (n == 0) crashWrite("  (no frames captured)\n");
        }

        // strsignal() isn't available on Windows/MinGW; name only the signals
        // installCrashHandler() actually installs below.
        const char *signalName(int sig)
        {
            switch (sig)
            {
            case SIGSEGV: return "SIGSEGV";
            case SIGABRT: return "SIGABRT";
            case SIGFPE:  return "SIGFPE";
            case SIGILL:  return "SIGILL";
            default:      return nullptr;
            }
        }
#endif  // _WIN32

        void crashHandler(int sig)
        {
            if (gInCrash) _exit(128 + sig);  // re-entrancy guard
            gInCrash = 1;

#ifdef _WIN32
            const char *name = signalName(sig);
#else
            const char *name = strsignal(sig);
#endif
            crashWrite("\n==== cosmo_v2 FATAL SIGNAL ");
            crashWrite(name ? name : "signal");
            crashWrite(" — backtrace ====\n");

#ifdef _WIN32
            windowsBacktrace();
#else
            void *frames[64];
            const int n = backtrace(frames, 64);
            if (gFd >= 0) backtrace_symbols_fd(frames, n, gFd);
            backtrace_symbols_fd(frames, n, 2);
#endif
            crashWrite("==== end backtrace ====\n");

            // Re-raise with the default handler so the OS still cores/exits normally.
            signal(sig, SIG_DFL);
            raise(sig);
        }

#ifdef _WIN32
        // signal(SIGSEGV) on Windows only fires for what the CRT chooses to translate, and
        // the MinGW runtimes differ on whether an access violation is one of those. The
        // unhandled-exception filter is the path the OS always takes, so D-4 is answered
        // there as well — then CONTINUE_SEARCH, so WER/the debugger still get the crash
        // exactly as before. Same philosophy as re-raising SIG_DFL above: report, never
        // swallow.
        LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS *info)
        {
            if (!gInCrash)
            {
                gInCrash = 1;
                char addr[19];
                crashWrite("\n==== cosmo_v2 FATAL EXCEPTION ");
                crashWrite(hex64(info && info->ExceptionRecord
                                     ? info->ExceptionRecord->ExceptionCode : 0u, addr));
                crashWrite(" — backtrace ====\n");
                windowsBacktrace();
                crashWrite("==== end backtrace ====\n");
            }
            return EXCEPTION_CONTINUE_SEARCH;
        }
#endif

        // ── GLib (D-3) ───────────────────────────────────────────────────────────────
#if defined(COSMO_LOG_HAVE_GLIB)
        void glibLogHandler(const gchar *domain, GLogLevelFlags flags,
                            const gchar *message, gpointer)
        {
            // Mapped most-severe-first because the flags are a mask, not an ordinal.
            // G_LOG_LEVEL_INFO sits BELOW G_LOG_LEVEL_MESSAGE in GLib's own severity
            // order, so it maps to Debug: GTK's info chatter at our Info level would bury
            // the app's own lines, which is the failure mode that makes people stop
            // reading logs.
            //
            // Replacing the default handler also inherits its filtering job: G_MESSAGES_DEBUG
            // is honoured by g_log_default_handler, not by g_log itself, so from here on every
            // debug/info message GLib produces arrives whether or not that variable is set.
            // Mapping both onto our Debug level is what keeps them out of a normal log — and
            // is why `--log-level=debug` is noticeably chattier than it was.
            Level l;
            if (flags & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL)) l = Level::Error;
            else if (flags & G_LOG_LEVEL_WARNING)                   l = Level::Warn;
            else if (flags & G_LOG_LEVEL_MESSAGE)                   l = Level::Info;
            else                                                    l = Level::Debug;

            // The GLib domain (Gtk, Gdk, GdkPixbuf, cairo…) is kept in the text rather
            // than mapped onto a Category: the domains are GLib's vocabulary and the
            // categories are ours, and inventing a translation between them is the drift
            // categoryForEventName() exists to avoid. One bucket — Ui, the toolkit — and
            // a grep on the domain still works. Nothing is lost to a filter either way:
            // Warn and Error ignore categories entirely.
            std::string text;
            if (domain && *domain) { text = domain; text += ": "; }
            text += chomp(message);
            write(l, Category::Ui, text);

            // Deliberately no abort() here: g_logv applies G_LOG_FLAG_FATAL itself after
            // the handler returns, so a G_LOG_LEVEL_ERROR still dies — through SIGABRT,
            // which our crash handler turns into a backtrace.
        }

        void glibPrintHandler(const gchar *s) { write(Level::Info, chomp(s)); }
        void glibPrinterrHandler(const gchar *s) { write(Level::Error, chomp(s)); }
#endif
    }  // namespace

    const char *categoryName(Category c)
    {
        switch (c)
        {
        case Category::None:    return "";
        case Category::Ui:      return "ui";
        case Category::Input:   return "input";
        case Category::Render:  return "render";
        case Category::Load:    return "load";
        case Category::Session: return "session";
        case Category::Export:  return "export";
        case Category::Gpu:     return "gpu";
        }
        return "";
    }

    bool parseLevel(const std::string &s, Level &out)
    {
        const std::string t = lower(trim(s));
        if (t == "debug" || t == "d") { out = Level::Debug; return true; }
        if (t == "info"  || t == "i") { out = Level::Info;  return true; }
        if (t == "warn"  || t == "warning" || t == "w") { out = Level::Warn; return true; }
        if (t == "error" || t == "e") { out = Level::Error; return true; }
        return false;
    }

    bool parseCategory(const std::string &s, Category &out)
    {
        const std::string t = lower(trim(s));
        for (Category c : {Category::Ui, Category::Input, Category::Render, Category::Load,
                           Category::Session, Category::Export, Category::Gpu})
            if (t == categoryName(c)) { out = c; return true; }
        return false;
    }

    Category categoryForEventName(const std::string &dottedName)
    {
        // First segment only. `entry.decoded` is Load rather than a category of its own
        // because an entry only ever decodes during a load — the segments are grouped by
        // the subsystem that owns them, not spelled out one event at a time, so a new
        // Event::Kind under an existing segment is categorised the day it is added.
        const size_t dot = dottedName.find('.');
        const std::string head = lower(dot == std::string::npos ? dottedName
                                                                : dottedName.substr(0, dot));

        if (head == "load" || head == "entry") return Category::Load;
        if (head == "frame") return Category::Render;
        if (head == "export") return Category::Export;
        if (head == "screen") return Category::Ui;
        if (head == "project" || head == "session" || head == "settings" ||
            head == "selection" || head == "params" || head == "history")
            return Category::Session;

        // A name that already IS a category (a future `input.*` or `gpu.*` event) needs no
        // table entry — the two vocabularies meeting is the point.
        Category c;
        if (parseCategory(head, c)) return c;

        // `info`, `error`, `command`, and anything added later: uncategorised, therefore
        // never filtered. `command.rejected` is the answer to "why did nothing happen?",
        // so it must not be silenceable by a filter aimed at something else.
        return Category::None;
    }

    void setLevel(Level l)
    {
        applyEnv();
        applyLevel(l);
    }

    Level level() { applyEnv(); return static_cast<Level>(gLevel.load()); }

    void setCategoryEnabled(Category c, bool on)
    {
        applyEnv();
        unsigned m = gCats.load();
        if (on) m |= catBit(c); else m &= ~catBit(c);
        gCats.store(m);
        gCatsSet = true;
    }

    void setCategories(const std::string &list)
    {
        applyEnv();
        const std::string all = trim(lower(list));
        if (all.empty() || all == "all" || all == "*") { gCats.store(~0u); gCatsSet = true; return; }

        unsigned mask = catBit(Category::None);   // the spine is not a subsystem and is not
                                                  // opt-in; see Category::None in Log.h
        if (all != "none" && all != "off")
        {
            std::string bad;
            size_t i = 0;
            while (i <= list.size())
            {
                const size_t j = list.find_first_of(",; ", i);
                const std::string tok = trim(list.substr(i, (j == std::string::npos ? list.size() : j) - i));
                if (!tok.empty())
                {
                    Category c;
                    if (parseCategory(tok, c)) mask |= catBit(c);
                    else { if (!bad.empty()) bad += ' '; bad += tok; }
                }
                if (j == std::string::npos) break;
                i = j + 1;
            }
            if (!bad.empty())
                note(Level::Warn, "log: unknown category '" + bad +
                                  "' — known: ui input render load session export gpu");
        }
        gCats.store(mask);
        gCatsSet = true;
    }

    bool enabled(Level l, Category c)
    {
        if (static_cast<int>(l) < gLevel.load()) return false;
        // A category filter narrows chatter; it does not suppress problems. Both exceptions
        // are load-bearing: without them a `COSMO_LOG_CATEGORIES=load` run would hide the
        // GTK warnings D-3 was filed to make visible, and a typo would hide everything.
        if (l == Level::Warn || l == Level::Error) return true;
        if (c == Category::None) return true;
        return (gCats.load() & catBit(c)) != 0;
    }

    void setStderrEcho(bool on)
    {
        applyEnv();
        gStderr.store(on);
        gStderrSet = true;
    }

    void init(const std::string &path)
    {
        applyEnv();

        std::vector<std::pair<Level, std::string>> pending;
        std::string config;
        bool haveFile = false;
        {
            std::lock_guard<std::mutex> lock(gMutex);
            if (gInit) return;   // already initialised
            gInit = true;

            // Precedence: what the operator asked for (flag, then env — both applied into
            // gPathOverride), then what the program asked for, then the default. An
            // operator redirecting the log is the one who cannot recompile.
            if (!gNoFile)
            {
                gPath = gPathOverrideSet && !gPathOverride.empty()
                            ? gPathOverride
                            : (path.empty() ? (cosmo::ProjectStore::configDir() + "/cosmo_v2.log")
                                            : path);
                gFile = std::fopen(gPath.c_str(), "a");
                if (!gFile)
                {
                    // Say so on stderr: a redirect to an unwritable path otherwise looks
                    // exactly like an app that logged nothing.
                    std::fprintf(stderr, "cosmo_v2: cannot open log file %s — stderr only\n",
                                 gPath.c_str());
                    gPath.clear();
                }
            }

            config = effectiveConfig();
            haveFile = gFile != nullptr;
            if (gFile)
            {
                gFd = fileno(gFile);

                char ts[40];
                timestamp(ts, sizeof ts);
#ifdef _WIN32
                const long pid = (long)_getpid();
#else
                const long pid = (long)getpid();
#endif
                std::fprintf(gFile, "\n===== cosmo_v2 session start %s (pid %ld) — %s =====\n",
                             ts, pid, config.c_str());
                std::fflush(gFile);
            }
            pending.swap(gPending);
        }

        // Outside the lock: write() takes it. With no file there is no header to carry the
        // config, so say it as a line instead — a stderr-only session should still be able
        // to answer "what was filtered?".
        if (!haveFile) write(Level::Info, "log: " + config);
        for (const auto &p : pending) write(p.first, p.second);
    }

    const std::string &path() { return gPath; }

    void write(Level level, const std::string &msg)
    {
        // R-SVC-5: an Event IS the log line, and the host emits it with a single
        // LOGI(formatEvent(e)). Recovering the category from the "[evt] <name>" prefix is
        // what lets that one call site — and every other pre-existing LOGI — stay exactly
        // as written while still being filterable.
        Category cat = Category::None;
        static const char kEvt[] = "[evt] ";
        if (msg.compare(0, sizeof kEvt - 1, kEvt) == 0)
        {
            const size_t start = sizeof kEvt - 1;
            const size_t end = msg.find(' ', start);
            cat = categoryForEventName(msg.substr(start, end == std::string::npos
                                                            ? std::string::npos : end - start));
        }
        write(level, cat, msg);
    }

    void write(Level level, Category cat, const std::string &msg)
    {
        if (!enabled(level, cat)) return;

        char ts[40];
        timestamp(ts, sizeof ts);
        const char *ctag = (cat == Category::None) ? nullptr : categoryName(cat);

        std::lock_guard<std::mutex> lock(gMutex);
        if (gFile)
        {
            if (ctag) std::fprintf(gFile, "%s [%s] [%s] %s\n", ts, levelTag(level), ctag, msg.c_str());
            else      std::fprintf(gFile, "%s [%s] %s\n", ts, levelTag(level), msg.c_str());
            std::fflush(gFile);  // flush every line so a crash can't lose the tail
        }
        if (gStderr.load())
        {
            if (ctag) std::fprintf(stderr, "%s [%s] [%s] %s\n", ts, levelTag(level), ctag, msg.c_str());
            else      std::fprintf(stderr, "%s [%s] %s\n", ts, levelTag(level), msg.c_str());
        }
    }

    void writef(Level level, const char *fmt, ...)
    {
        // Level first, before the format: this is the check that makes LOGD free to leave
        // in hot code, and its absence is half of D-2. The category cannot be checked yet
        // — for an event line it is IN the message — so a filtered-out `[evt]` line still
        // pays one vsnprintf. That is the price of not editing 40 call sites.
        if (static_cast<int>(level) < gLevel.load()) return;

        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        write(level, buf);
    }

    void writef(Level level, Category cat, const char *fmt, ...)
    {
        if (!enabled(level, cat)) return;   // category known up front: no formatting at all

        char buf[1024];
        va_list ap;
        va_start(ap, fmt);
        std::vsnprintf(buf, sizeof buf, fmt, ap);
        va_end(ap);
        write(level, cat, buf);
    }

    int consumeArgs(int argc, char **argv, int i)
    {
        if (!argv || i <= 0 || i >= argc || !argv[i]) return 0;
        applyEnv();
        const std::string a = argv[i];

        // Both spellings, because a generated script writes `--log-level=debug` and a human
        // types `--log-level debug`, and the one that silently became a filename to open
        // would be the one nobody debugged.
        auto value = [&](const char *name, std::string &out) -> int {
            const std::string flag = std::string("--") + name;
            if (a == flag && i + 1 < argc) { out = argv[i + 1]; return 2; }
            if (a.compare(0, flag.size() + 1, flag + "=") == 0) { out = a.substr(flag.size() + 1); return 1; }
            return 0;
        };

        std::string v;
        if (a == "--debug") { setLevel(Level::Debug); return 1; }
        if (int n = value("log-level", v))
        {
            Level l;
            if (parseLevel(v, l)) setLevel(l);
            else note(Level::Warn, "log: --log-level=" + v + " is not debug|info|warn|error — keeping info");
            return n;
        }
        if (int n = value("log-categories", v)) { setCategories(v); return n; }
        if (int n = value("log-file", v))
        {
            gPathOverrideSet = true;
            if (v == "-") { gNoFile = true; gPathOverride.clear(); }
            else          { gNoFile = false; gPathOverride = v; }
            return n;
        }
        if (int n = value("log-stderr", v))
        {
            const std::string s = lower(trim(v));
            setStderrEcho(!(s == "0" || s == "off" || s == "no" || s == "false"));
            return n;
        }
        return 0;
    }

    void configureFromArgs(int argc, char **argv)
    {
        for (int i = 1; i < argc; )
        {
            const int n = consumeArgs(argc, argv, i);
            i += (n > 0 ? n : 1);
        }
    }

    void installGlibHandler()
    {
#if defined(COSMO_LOG_HAVE_GLIB)
        // g_log_set_default_handler, not just the two print handlers: g_warning,
        // g_critical and g_message go through g_log, which never consults them — which is
        // exactly why no GTK/Cairo/GdkPixbuf diagnostic had ever reached the file (D-3).
        //
        // The trap on the other side: a caller built with G_LOG_USE_STRUCTURED bypasses
        // handlers entirely and goes to the writer func. GTK3 does not, so this is the
        // right seam today; if a dependency ever does, g_log_set_writer_func is the answer
        // — but it may be called only once per process and aborts on a second call, so it
        // is not a free upgrade.
        g_log_set_default_handler(glibLogHandler, nullptr);
        g_set_print_handler(glibPrintHandler);
        g_set_printerr_handler(glibPrinterrHandler);
#endif
    }

    void installCrashHandler()
    {
#ifdef _WIN32
        // Resolve dbghelp now, while the process is healthy — see initSymbols().
        initSymbols();
        SetUnhandledExceptionFilter(unhandledExceptionFilter);
        // SIGBUS is a POSIX bus-error signal with no Windows equivalent.
        for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL})
#else
        for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE, SIGILL})
#endif
            signal(sig, crashHandler);
    }
}
}
}
