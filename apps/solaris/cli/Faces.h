/*
 *  solaris-cc — the verbs that make Solaris a peer of its other faces (R-SVC-5, R-SVC-7).
 *
 *  `main.cpp` hands these whole argument lists over before it builds a service of its own:
 *
 *      attach <socket> …       drive a running window over its control socket (Attach.cpp) — no service
 *      ntwb serve|install|…    Solaris as an Arstro Remote app, its UI in a browser (NtwbMain.cpp)
 *
 *  Kept out of main.cpp so the CLI's own loop and these faces change without touching each other.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace solaris_cli
{
    /** `args` = everything after `attach`. Exit codes as solaris-cc: 0, 1 failed, 2 usage, 3 refused. */
    int attachMain(const std::vector<std::string> &args);
    /** `args` = everything after `ntwb`. */
    int ntwbMain(const std::vector<std::string> &args);
    /** The usage lines of these verbs, for solaris-cc's --help. */
    const char *facesUsage();
}
}
