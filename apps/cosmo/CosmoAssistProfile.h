/*
 *  cosmo host — cosmo's AssistProfile (R-ASSIST-13): the values that make the shared agent module
 *  (ProcessAssistant, AssistConversation, AgentPanel) cosmo's agent. Everything R-ASSIST-2..5, -9
 *  and -10 say about "cosmo" - the `cosmo` MCP server run as `cosmo-cc mcp --attach <socket>`, the
 *  COSMO_* environment, the colour-grading skills, the photographer's wording, the allowed tools,
 *  the workspace `<config>/agent`, `cosmo-cc agent` for a local model - is stated here once.
 *
 *  The ONE file of the agent module that depends on cosmo_core (ProjectStore::configDir), and so
 *  the one file a sibling app does not compile: it writes its own profile instead.
 */
#pragma once
#include "AssistProfile.h"

namespace arstro
{
namespace cosmo_v2
{
    AssistProfile cosmoAssistProfile();
}
}
