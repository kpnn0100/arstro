/*
 *  cosmo test suites — make a FAILING assert exit, not hang (D-10, R-TEST-1).
 *
 *  On MinGW/msvcrt, `abort()` from a failed `assert()` takes the Windows
 *  "terminated in an unusual way" path: a dialog or a silent stall rather than a process
 *  exit. And the assert text goes to a buffered stderr that is never flushed, so a
 *  redirected run prints nothing. The result is that a RED suite is indistinguishable from
 *  a slow one — and these suites legitimately take a minute, so "it must be stuck" is not a
 *  usable heuristic. Two sessions lost five minutes each to that before it was diagnosed.
 *
 *  That matters more than it looks: every verification claim in this project's history rests
 *  on "the suite is green". A suite that cannot report red is not evidence.
 *
 *  Call `testMainInit()` first in `main()`. No-op on POSIX, where abort() already exits
 *  non-zero and stderr reaches the pipe.
 *
 *  This header is included by more than one suite, so it owes them a clean namespace
 *  (R-TEST-2): everything <windows.h> leaks is undone below.
 */
#pragma once
#include <csignal>
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// <windows.h> still defines the 16-bit memory-model keywords as object-like macros:
// `near` and `far` expand to nothing and `small` to `char`. A suite that includes this
// header for one Win32 call does not expect a header to redefine its vocabulary, and the
// diagnostic when it does is unreadable — `bool near(double a, double b)` in
// widgets/tests/widgetTests.cpp reported "expected unqualified-id before 'double'" (D-40).
// A header that breaks its includer is worse than no header, so undo the leak here.
#undef near
#undef far
#undef small
#endif

namespace arstro
{
namespace cosmo
{
#ifdef _WIN32
    /** Leave the process the moment a test aborts. `_Exit` skips atexit handlers and the CRT's
     *  own end-of-run reporting, which is the part that stalls; the streams are unbuffered by
     *  the time this can fire, and the flush covers a caller that changed that. */
    inline void testAbortHandler(int)
    {
        std::fputs("[abort] assertion failed — exiting 3\n", stderr);
        std::fflush(nullptr);
        std::_Exit(3);
    }
#endif

    inline void testMainInit()
    {
        // Unbuffered on every platform: an assert's message must reach a redirected stderr
        // before abort() takes the process down, and a crash mid-suite must not swallow the
        // last lines that say how far it got.
        setvbuf(stderr, nullptr, _IONBF, 0);
        setvbuf(stdout, nullptr, _IONBF, 0);

#ifdef _WIN32
        // No error dialogs and no WER handoff: a CI or agent run has nobody to click OK, and
        // waiting for a click is the hang.
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

        // SetErrorMode covers the fault dialog but not abort()'s own termination path, and the
        // obvious lever for that — `_set_abort_behavior` — cannot be used here: msvcrt declares
        // it unconditionally in <stdlib.h> but libmsvcrt.a exports no such symbol, so calling it
        // fails at LINK time on the MSYS2 MINGW64 toolchain (`undefined reference to
        // __imp__set_abort_behavior`). It exists only in the UCRT import library. Handling
        // SIGABRT instead works on both CRTs and needs no guard: abort() raises it, and a
        // handler that does not return never reaches the CRT's reporting path at all. The
        // assert text is already on stderr by then — `_assert` prints before it aborts.
        std::signal(SIGABRT, testAbortHandler);
#endif
    }
}
}
