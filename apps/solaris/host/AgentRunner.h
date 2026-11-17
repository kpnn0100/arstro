/*
 *  solaris_host — AgentRunner: the user's own agent, run as a process beside Solaris (R-AGENT-3).
 *
 *  The conversation is the service's (R-AGENT-1); a process is an OS object and so the host's.
 *  This installs the runner: cosmo's `ProcessAssistant` (Claude Code over stream-JSON, any ACP
 *  agent, or any command), compiled in place and given SOLARIS's profile - its MCP server is
 *  `solaris-cc mcp --attach <the window's control socket>` (R-MCP-2), so the agent's hands are the
 *  song the user is looking at. Cosmo's headers stay inside the .cpp: nothing here names them, so
 *  the window's own include paths are untouched.
 */
#pragma once
#include <functional>
#include <string>

namespace arstro
{
namespace solaris
{
    class SolarisService;
}
namespace solaris_host
{
    /** Give `svc` its agent runner and its look at this machine's installed agents.
     *  `controlSocket` returns the path of this application's control socket, OPENING it if it
     *  is not open yet ("" when it cannot): the agent's tools need one, and a window started
     *  without `--control` has none until the first `assist start`. */
    void installAgent(solaris::SolarisService &svc, std::function<std::string()> controlSocket);
}
}
