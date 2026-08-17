/*
 *  cosmo test suites — make a FAILING assert exit, not hang (D-10).
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
 */
#pragma once
#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace arstro
{
namespace cosmo
{
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
#if defined(_MSC_VER) || defined(__MINGW32__)
        // Print the abort message, do NOT hand off to the fault reporter.
        _set_abort_behavior(_WRITE_ABORT_MSG, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#endif
    }
}
}
