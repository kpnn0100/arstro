/*
 *  Arstro cosmo_core — the agent seam (R-ASSIST-1/2/6).
 *
 *  An agent is the user's OWN program — Claude Code, or any command they name — running beside
 *  cosmo and editing the open project through cosmo's MCP tools (R-ASSIST-3). Starting a process,
 *  its pipes and its wire protocol are the HOST's (`apps/cosmo/AssistProcess`): cosmo_core makes no
 *  OS call. So the service owns the conversation (state, messages, events — what every front end
 *  shows) and holds an IAssistant the host made, which it asks, without blocking, what the agent
 *  said since the last pump (R-SVC-6). A test injects a fake one and needs no agent at all.
 */
#pragma once
#include <string>
#include <vector>

namespace arstro
{
namespace cosmo
{
    /** What the service knows that the agent needs: the user's choice of agent (settings) and the
     *  project it will work on. The host adds what only it knows (the control socket, cosmo-cc,
     *  the workspace with the bundled skill). */
    struct AssistLaunch
    {
        std::string agent;                      // the command; "claude" = Claude Code (settings `agent=`)
        std::string args;                       // appended to it (settings `agentArgs=`, R-ASSIST-5)
        std::vector<std::string> skillDirs;     // extra skill directories (settings `agentSkills=`)
        std::string project;                    // the open project's path, "" for none
        int photos = 0;
    };

    /** One thing the agent did, as the conversation shows it. */
    struct AssistOutput
    {
        enum class Kind
        {
            Ready,    // the agent is up; text = what it said about itself (model, version)
            Text,     // the agent answered; text = the answer
            Tool,     // the agent used a tool; text = "name: what" (one line)
            Done,     // the agent finished the turn; text = a summary ("12 s, $0.04") or ""
            Failed,   // something went wrong; text = why. The agent may still be running.
            Exited    // the process is gone; text = why ("" = it was asked to stop)
        };
        Kind kind = Kind::Text;
        std::string text;
    };

    class IAssistant
    {
    public:
        virtual ~IAssistant() = default;
        /** Start the agent. False + `err` naming the reason (not installed, no workspace). */
        virtual bool start(const AssistLaunch &launch, std::string &err) = 0;
        /** Hand it one message from the user. False + `err` if it cannot take one now. */
        virtual bool send(const std::string &text, std::string &err) = 0;
        /** End it, and anything it started. Idempotent. */
        virtual void stop() = 0;
        /** Everything it did since the last call. NEVER blocks (R-SVC-6). */
        virtual void poll(std::vector<AssistOutput> &out) = 0;
        /** Which agent this is, for the model ("Claude Code", "codex"). */
        virtual std::string name() const = 0;
    };
}
}
