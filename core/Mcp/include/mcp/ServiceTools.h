/*
 *  Arstro Mcp — ServiceTools: the three tools every Arstro service gets for free.
 *
 *  Every Arstro app with a service (interstellar, solaris, cosmo …) has the same shape at its
 *  top: one text line in, parsed by the service's own grammar; what the line printed and the
 *  events it caused out; `api --json` for the generated API document and `state print --json`
 *  for the model. So the MCP face of such a service is the same three tools, and an app only
 *  says how to run ONE line — nothing about the grammar is repeated here, which is the point
 *  (arstro.rule §1: the front end holds no behaviour).
 *
 *      <app>_api    the generated API document (`api --json`) — read it first
 *      <app>_run    command lines, in order, stopping at the first refusal
 *      <app>_state  the model (`state print --json [--stable]`)
 */
#pragma once
#include "mcp/Server.h"
#include <functional>
#include <string>

namespace arstro
{
namespace mcp
{
    struct TextService
    {
        std::string app;          // "interstellar" -> tools interstellar_api / _run / _state
        /** Run one grammar line to completion (dispatch, and pump until idle where the service
         *  works in the background). `printed` = what the line printed; false + `err` = refused. */
        std::function<bool(const std::string &line, std::string &printed, std::string &err)> runLine;
        /** The event lines formatted since the last call, then forgotten. May be empty. */
        std::function<std::string()> takeEvents;
        /** Extra words for `<app>_run`'s description: where the grammar's examples live. */
        std::string runHint;
    };

    void addTextServiceTools(Server &server, const TextService &svc);
}
}
