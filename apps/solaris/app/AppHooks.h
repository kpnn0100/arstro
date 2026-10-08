/*
 *  solaris_ui — AppHooks: the ONLY seam between the front end and the service (R-G-4, R-SVC-1).
 *
 *  The App draws from `model()` and nothing else, and reports intent as TEXT command lines through
 *  `dispatch` — the same lines `solaris-cc`, a script and an agent send (docs/API.md). So the GUI can
 *  do nothing a script cannot, and a second front end needs nothing from this one but the grammar.
 *
 *  The service is NOT linked into the app library. The host (`linux_main.cpp`) binds these to the
 *  real `SolarisService`; the shot harness and the UI tests bind them to a real one too (it is
 *  headless and cheap), so a shot is also a check that the two agree.
 */
#pragma once
#include "AppModel.h"
#include <functional>
#include <string>

namespace arstro
{
namespace solaris_ui
{
    struct AppHooks
    {
        std::function<const solaris::AppModel &()> model;
        std::function<bool(const std::string &commandLine, std::string &err)> dispatch;
    };
}
}
